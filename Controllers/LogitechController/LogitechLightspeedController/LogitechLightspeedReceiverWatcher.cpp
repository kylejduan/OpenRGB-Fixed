/*---------------------------------------------------------*\
| LogitechLightspeedReceiverWatcher.cpp                     |
|                                                           |
|   Restores lighting after Lightspeed reconnects and       |
|   registers devices that were asleep during detection     |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include "LogitechLightspeedReceiverWatcher.h"
#include "LogManager.h"

namespace
{
constexpr unsigned char HIDPP_SHORT_REPORT          = 0x10;
constexpr unsigned char DJ_CONNECTION_NOTIFICATION  = 0x41;
constexpr unsigned char DJ_LINK_NOT_ESTABLISHED     = 0x40;
constexpr int           CONNECTION_REPORT_SIZE      = 7;
constexpr unsigned char RECEIVER_INDEX              = 0xFF;

std::chrono::milliseconds ContentionQuiet(const LightspeedWatcherTiming& timing, int episodes)
{
    const std::chrono::milliseconds cap   = std::max(timing.quiet, timing.poll_max);
    std::chrono::milliseconds       quiet = timing.quiet;

    for(int episode = 1; episode < episodes && quiet < cap; episode++)
    {
        quiet *= 2;
    }
    return(std::min(quiet, cap));
}
}

LogitechLightspeedReceiverWatcher::LogitechLightspeedReceiverWatcher(std::shared_ptr<hid_device> notifications_, LightspeedWatcherTiming timing_)
    : notifications(std::move(notifications_)), timing(std::move(timing_))
{
}

LogitechLightspeedReceiverWatcher::~LogitechLightspeedReceiverWatcher()
{
    Stop();
}

void LogitechLightspeedReceiverWatcher::SetCreateHook(CreateHook hook)
{
    create = std::move(hook);
}

void LogitechLightspeedReceiverWatcher::SetReceiverHooks(LightspeedReceiverHooks hooks)
{
    receiver = std::move(hooks);
}

void LogitechLightspeedReceiverWatcher::SetSharedReadLock(std::shared_ptr<std::mutex> lock)
{
    shared_reads = std::move(lock);
}

bool LogitechLightspeedReceiverWatcher::LinkTrusted() const
{
    return(!shared_reads);
}

void LogitechLightspeedReceiverWatcher::AddRegistered(uint8_t slot, LightspeedSlotHooks hooks)
{
    Slot& entry      = slots[slot];
    entry.registered = true;
    entry.hooks      = std::move(hooks);
}

void LogitechLightspeedReceiverWatcher::AddPending(uint8_t slot, bool linked)
{
    Slot& entry      = slots[slot];
    entry.registered = false;
    entry.linked     = linked;
}

void LogitechLightspeedReceiverWatcher::Start()
{
    if(worker.joinable())
    {
        return;
    }
    const clock::time_point now = clock::now();
    for(std::pair<const uint8_t, Slot>& entry : slots)
    {
        entry.second.interval     = timing.poll;
        entry.second.next_poll    = now + timing.poll;
        entry.second.next_repaint = now + timing.repaint;

        /*-------------------------------------------------*\
        | Detection failed on a linked device: no link      |
        | notification will follow, so retry on a schedule  |
        \*-------------------------------------------------*/
        if(!entry.second.registered && entry.second.linked)
        {
            for(std::chrono::milliseconds offset : timing.create_after_link)
            {
                Schedule(entry.second, now + offset);
            }
        }
        else if(!entry.second.registered && !LinkTrusted())
        {
            Schedule(entry.second, now + timing.create_retry);
        }
    }
    worker = std::thread(&LogitechLightspeedReceiverWatcher::Run, this);
}

void LogitechLightspeedReceiverWatcher::Stop()
{
    stop_requested = true;
    if(worker.joinable())
    {
        worker.join();
    }
    if(!stopped_reported && receiver.stopped)
    {
        stopped_reported = true;
        receiver.stopped();
    }
}

bool LogitechLightspeedReceiverWatcher::StopRequested() const
{
    return stop_requested.load();
}

void LogitechLightspeedReceiverWatcher::Run()
{
    LOG_INFO("[Lightspeed watcher] Watching %u receiver slot(s)%s", static_cast<unsigned int>(slots.size()), LinkTrusted() ? "" : " on the shared command handle");
    unsigned char   report[64];

    while(!stop_requested.load())
    {
        const int size = ReadNotification(report, sizeof(report));
        if(size < 0)
        {
            if(++failed_reads == 1)
            {
                LOG_WARNING("[Lightspeed watcher] Receiver notification read failed");
            }
            if(failed_reads >= timing.read_failures && !receiver_lost)
            {
                receiver_lost = true;
                LOG_WARNING("[Lightspeed watcher] Receiver stopped responding; waiting for it to return");
                if(receiver.lost)
                {
                    receiver.lost();
                }
            }
            std::this_thread::sleep_for(timing.read_timeout);
        }
        else
        {
            if(receiver_lost)
            {
                LOG_INFO("[Lightspeed watcher] Receiver notifications resumed");
                if(receiver.recovered)
                {
                    receiver.recovered();
                }
            }
            failed_reads  = 0;
            receiver_lost = false;
            if(size > 0)
            {
                OnReport(report, size, clock::now());
            }
        }

        /*-------------------------------------------------*\
        | A removed receiver invalidates every handle the   |
        | slots use, so queries and repaints would only     |
        | fail. The lost hook arranges the rescan.          |
        \*-------------------------------------------------*/
        if(receiver_lost)
        {
            continue;
        }

        for(std::pair<const uint8_t, Slot>& entry : slots)
        {
            if(stop_requested.load())
            {
                break;
            }
            Service(entry.first, entry.second, clock::now());
        }
    }
    LOG_INFO("[Lightspeed watcher] Stopped");
}

int LogitechLightspeedReceiverWatcher::ReadNotification(unsigned char* report, int size)
{
    if(LinkTrusted())
    {
        return(hid_read_timeout(notifications.get(), report, size, static_cast<int>(timing.read_timeout.count())));
    }

    /*-----------------------------------------------------*\
    | Never block on the shared handle while holding the    |
    | receiver lock: poll it and sleep outside the lock     |
    \*-----------------------------------------------------*/
    int result;
    {
        std::lock_guard<std::mutex> lock(*shared_reads);
        result = hid_read_timeout(notifications.get(), report, size, 0);
    }
    if(result == 0)
    {
        std::this_thread::sleep_for(timing.read_timeout);
    }
    return(result);
}

void LogitechLightspeedReceiverWatcher::OnReport(const unsigned char* report, int size, clock::time_point now)
{
    if(size < CONNECTION_REPORT_SIZE || report[0] != HIDPP_SHORT_REPORT || report[2] != DJ_CONNECTION_NOTIFICATION)
    {
        return;
    }
    const uint8_t index = report[1];
    const bool    up    = (report[4] & DJ_LINK_NOT_ESTABLISHED) == 0;

    if(index == 0 || index == RECEIVER_INDEX)
    {
        return;
    }

    std::map<uint8_t, Slot>::iterator found = slots.find(index);
    if(found == slots.end())
    {
        /*-------------------------------------------------*\
        | Enumeration missed this device, for example a     |
        | late boot announcement. Treat it as pending.      |
        \*-------------------------------------------------*/
        if(!up || !create)
        {
            return;
        }
        LOG_INFO("[Lightspeed watcher] Slot %u connected without an enumerated device", index);
        found = slots.emplace(index, Slot()).first;
        found->second.interval = timing.poll;
    }

    Slot& slot = found->second;
    LOG_DEBUG("[Lightspeed watcher] Slot %u link %s", index, up ? "up" : "down");

    if(slot.registered && !slot.monitored)
    {
        return;
    }
    slot.due.clear();
    slot.linked = up;
    if(!up)
    {
        /*-------------------------------------------------*\
        | On the shared handle the matching link-up report  |
        | may be consumed by a query; keep retrying slowly  |
        \*-------------------------------------------------*/
        if(!slot.registered && !LinkTrusted())
        {
            Schedule(slot, now + timing.create_retry);
        }
        return;
    }
    slot.interval       = timing.poll;
    slot.next_poll      = now + timing.poll;
    slot.next_repaint   = now + timing.repaint;
    slot.repaint_on_due = slot.registered; // A reconnect resets the device's LEDs.
    for(std::chrono::milliseconds offset : (slot.registered ? timing.after_link : timing.create_after_link))
    {
        Schedule(slot, now + offset);
    }
}

void LogitechLightspeedReceiverWatcher::Service(uint8_t index, Slot& slot, clock::time_point now)
{
    if((!slot.linked && LinkTrusted()) || (slot.registered && !slot.monitored))
    {
        return;
    }

    bool due_now = false;
    while(!slot.due.empty() && slot.due.front() <= now)
    {
        slot.due.erase(slot.due.begin());
        due_now = true;
    }

    if(!slot.registered)
    {
        if(!due_now || !create)
        {
            return;
        }
        std::optional<LightspeedSlotHooks> hooks = create(index);
        if(!hooks)
        {
            /*---------------------------------------------*\
            | Keep retrying slowly while the device stays   |
            | linked: no further link report may arrive     |
            \*---------------------------------------------*/
            if(slot.due.empty() && (slot.linked || !LinkTrusted()))
            {
                Schedule(slot, clock::now() + timing.create_retry);
            }
            return;
        }
        LOG_INFO("[Lightspeed watcher] Registered %s on slot %u after it connected", hooks->name.c_str(), index);
        const clock::time_point after = clock::now();
        slot.registered  = true;
        slot.hooks       = std::move(*hooks);
        slot.due.clear();
        slot.interval    = timing.poll;
        slot.next_poll   = after + timing.poll;
        slot.quiet_until = after + timing.quiet;
        Schedule(slot, slot.quiet_until);
        return;
    }

    /*-------------------------------------------------------------*\
    | A reconnect re-initialises the device's LEDs even when it      |
    | keeps our software control, so re-apply instead of checking    |
    \*-------------------------------------------------------------*/
    if(due_now && slot.repaint_on_due)
    {
        if(slot.due.empty())
        {
            slot.repaint_on_due = false;
        }
        Repaint(index, slot, "reconnected");
        return;
    }

    /*-------------------------------------------------------------*\
    | Check ownership first: a refresh reclaims control, which      |
    | would otherwise hide another application's takeover from the  |
    | contention backoff                                            |
    \*-------------------------------------------------------------*/
    if(due_now || now >= slot.next_poll)
    {
        CheckOwnership(index, slot);
        return;
    }

    /*-------------------------------------------------------------*\
    | Refresh host-painted colours. Device-side animations are left |
    | alone: re-applying one restarts it visibly. While another     |
    | application keeps taking control, only the backed-off         |
    | ownership recovery repaints.                                  |
    \*-------------------------------------------------------------*/
    const bool contended = slot.lost_episodes > 0 && now - slot.last_loss < timing.contention_reset;

    if(timing.repaint > std::chrono::milliseconds(0) && now >= slot.next_repaint && now >= slot.quiet_until && !contended)
    {
        slot.next_repaint = now + timing.repaint;

        if(!slot.hooks.static_colors || slot.hooks.static_colors())
        {
            /*-----------------------------------------------------*\
            | Never wake the RGB engine out of power save just to   |
            | refresh: the power-return check repaints instead      |
            \*-----------------------------------------------------*/
            const int power = slot.hooks.read_power ? slot.hooks.read_power(timing.reply_deadline_ms) : 1;

            if(power > 1)
            {
                slot.power_saving = true;
                return;
            }
            if(power < 0)
            {
                return;
            }

            Repaint(index, slot, "refreshing colours");
        }
    }
}

void LogitechLightspeedReceiverWatcher::Repaint(uint8_t index, Slot& slot, const char* reason)
{
    LOG_DEBUG("[Lightspeed watcher] %s on slot %u: %s", slot.hooks.name.c_str(), index, reason);
    slot.hooks.reapply();
    const clock::time_point after = clock::now();
    slot.quiet_until  = std::max(slot.quiet_until, after + timing.quiet); // Keep any contention backoff.
    // Keep the ownership deadline: frequent refreshes must not postpone it.
    slot.next_poll    = std::max(slot.next_poll, slot.quiet_until);
    slot.next_repaint = after + timing.repaint;
}

void LogitechLightspeedReceiverWatcher::CheckOwnership(uint8_t index, Slot& slot)
{
    const int               control = slot.hooks.read_control(timing.reply_deadline_ms);
    const clock::time_point after   = clock::now();

    if(control == -2)
    {
        slot.monitored = false;
        slot.due.clear();
        return;
    }
    if(control < 0)
    {
        // Back off, but never below a configured poll longer than the cap.
        slot.interval  = std::min(slot.interval * 2, std::max(timing.poll_max, timing.poll));
        slot.next_poll = after + slot.interval;
        LOG_DEBUG("[Lightspeed watcher] %s on slot %u did not answer", slot.hooks.name.c_str(), index);
        return;
    }

    slot.interval  = timing.poll;
    slot.next_poll = after + timing.poll;
    if((control & 3) == 3)
    {
        /*---------------------------------------------------------*\
        | Ownership is ours, but RGB power save resets what the     |
        | device shows. Repaint once it is back at full power.      |
        \*---------------------------------------------------------*/
        if(slot.hooks.read_power)
        {
            const int power = slot.hooks.read_power(timing.reply_deadline_ms);

            if(power > 1)
            {
                slot.power_saving = true;
            }
            else if(power == 1 && slot.power_saving)
            {
                slot.power_saving = false;
                Repaint(index, slot, "RGB power restored");
            }
        }
        return;
    }
    if(after < slot.quiet_until)
    {
        return; // The previous re-apply is still settling; its verification is scheduled.
    }

    if(slot.lost_episodes > 0 && after - slot.last_loss >= timing.contention_reset)
    {
        slot.lost_episodes = 0;
    }
    slot.lost_episodes++;
    slot.last_loss = after;

    /*-------------------------------------------------------------*\
    | Under persistent contention, log the first losses and then    |
    | only at powers of two so the log stays bounded                |
    \*-------------------------------------------------------------*/
    const int episodes = slot.lost_episodes;
    if(episodes <= 3 || (episodes & (episodes - 1)) == 0)
    {
        LOG_INFO("[Lightspeed watcher] %s on slot %u lost software lighting control (%02X); re-applying (loss %d)", slot.hooks.name.c_str(), index, control, episodes);
    }
    if(episodes == 3)
    {
        LOG_WARNING("[Lightspeed watcher] %s keeps losing lighting control; another application may be controlling it. Backing off.", slot.hooks.name.c_str());
    }
    slot.hooks.reapply();
    slot.quiet_until  = clock::now() + ContentionQuiet(timing, episodes);
    slot.next_repaint = slot.quiet_until + timing.repaint; // The re-apply was a repaint.
    Schedule(slot, slot.quiet_until);
}

void LogitechLightspeedReceiverWatcher::Schedule(Slot& slot, clock::time_point when)
{
    slot.due.insert(std::upper_bound(slot.due.begin(), slot.due.end(), when), when);
}
