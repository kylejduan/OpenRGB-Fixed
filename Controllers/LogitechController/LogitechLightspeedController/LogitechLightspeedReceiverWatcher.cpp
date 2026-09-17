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
        entry.second.interval  = timing.poll;
        entry.second.next_poll = now + timing.poll;

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
}

bool LogitechLightspeedReceiverWatcher::StopRequested() const
{
    return stop_requested.load();
}

void LogitechLightspeedReceiverWatcher::Run()
{
    LOG_INFO("[Lightspeed watcher] Watching %u receiver slot(s)", static_cast<unsigned int>(slots.size()));
    unsigned char   report[64];
    bool            read_failed = false;

    while(!stop_requested.load())
    {
        const int size = hid_read_timeout(notifications.get(), report, sizeof(report), static_cast<int>(timing.read_timeout.count()));
        if(size < 0)
        {
            if(!read_failed)
            {
                LOG_WARNING("[Lightspeed watcher] Receiver notification read failed");
            }
            read_failed = true;
            std::this_thread::sleep_for(timing.read_timeout);
        }
        else if(size > 0)
        {
            read_failed = false;
            OnReport(report, size, clock::now());
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

void LogitechLightspeedReceiverWatcher::OnReport(const unsigned char* report, int size, clock::time_point now)
{
    if(size < CONNECTION_REPORT_SIZE || report[0] != HIDPP_SHORT_REPORT || report[2] != DJ_CONNECTION_NOTIFICATION)
    {
        return;
    }
    const std::map<uint8_t, Slot>::iterator found = slots.find(report[1]);
    if(found == slots.end())
    {
        return;
    }

    Slot&       slot = found->second;
    const bool  up   = (report[4] & DJ_LINK_NOT_ESTABLISHED) == 0;
    LOG_DEBUG("[Lightspeed watcher] Slot %u link %s", report[1], up ? "up" : "down");

    if(slot.registered && !slot.monitored)
    {
        return;
    }
    slot.due.clear();
    slot.linked = up;
    if(!up)
    {
        return;
    }
    slot.interval  = timing.poll;
    slot.next_poll = now + timing.poll;
    for(std::chrono::milliseconds offset : (slot.registered ? timing.after_link : timing.create_after_link))
    {
        Schedule(slot, now + offset);
    }
}

void LogitechLightspeedReceiverWatcher::Service(uint8_t index, Slot& slot, clock::time_point now)
{
    if(!slot.linked || (slot.registered && !slot.monitored))
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

    if(due_now || now >= slot.next_poll)
    {
        CheckOwnership(index, slot);
    }
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
        slot.interval  = std::min(slot.interval * 2, timing.poll_max);
        slot.next_poll = after + slot.interval;
        LOG_DEBUG("[Lightspeed watcher] %s on slot %u did not answer", slot.hooks.name.c_str(), index);
        return;
    }

    slot.interval  = timing.poll;
    slot.next_poll = after + timing.poll;
    if((control & 3) == 3)
    {
        slot.lost_episodes = 0;
        return;
    }
    if(after < slot.quiet_until)
    {
        return; // The previous re-apply is still settling; its verification is scheduled.
    }

    slot.lost_episodes++;
    LOG_INFO("[Lightspeed watcher] %s on slot %u lost software lighting control (%02X); re-applying", slot.hooks.name.c_str(), index, control);
    if(slot.lost_episodes == 3)
    {
        LOG_WARNING("[Lightspeed watcher] %s keeps losing lighting control; another application may be controlling it", slot.hooks.name.c_str());
    }
    slot.hooks.reapply();
    slot.quiet_until = clock::now() + timing.quiet;
    Schedule(slot, slot.quiet_until);
}

void LogitechLightspeedReceiverWatcher::Schedule(Slot& slot, clock::time_point when)
{
    slot.due.insert(std::upper_bound(slot.due.begin(), slot.due.end(), when), when);
}
