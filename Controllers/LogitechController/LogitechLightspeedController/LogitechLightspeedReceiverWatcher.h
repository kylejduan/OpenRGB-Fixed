/*---------------------------------------------------------*\
| LogitechLightspeedReceiverWatcher.h                       |
|                                                           |
|   Restores lighting after Lightspeed reconnects and       |
|   registers devices that were asleep during detection     |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <hidapi.h>
#include "BackgroundWorker.h"

struct LightspeedSlotHooks
{
    std::string                             name;
    std::function<int(int deadline_ms)>     read_control;   // Flags, -1 no reply, -2 unsupported.
    std::function<int(int deadline_ms)>     read_power;     // 1 full, 2 power save, 3 off, -1 no reply.
    std::function<bool()>                   static_colors;  // False for device-side animations.
    std::function<void()>                   reapply;
};

struct LightspeedReceiverHooks
{
    std::function<void()>                   lost;           // Reads keep failing: the receiver is gone.
    std::function<void()>                   recovered;      // Reads succeed again after a loss.
    std::function<void()>                   stopped;        // The watcher thread has exited.
};

struct LightspeedWatcherTiming
{
    std::chrono::milliseconds               read_timeout{250};
    std::chrono::milliseconds               poll{30000};
    std::chrono::milliseconds               poll_max{120000};
    int                                     reply_deadline_ms{1000};
    std::chrono::milliseconds               quiet{5000};
    /*-----------------------------------------------------------------*\
    | Refresh interval for host-painted colours. A device can repaint   |
    | its own default while keeping our software control, so ownership  |
    | checks alone cannot detect it. Zero disables the refresh.         |
    \*-----------------------------------------------------------------*/
    std::chrono::milliseconds               repaint{300000};
    /*-----------------------------------------------------------------*\
    | Re-apply colours after a reconnect: the first frame can be        |
    | acknowledged before the RGB engine renders again.                 |
    \*-----------------------------------------------------------------*/
    std::vector<std::chrono::milliseconds>  after_link{std::chrono::milliseconds(1500), std::chrono::milliseconds(10000)};
    std::vector<std::chrono::milliseconds>  create_after_link{std::chrono::milliseconds(500), std::chrono::milliseconds(3000), std::chrono::milliseconds(10000), std::chrono::milliseconds(60000), std::chrono::milliseconds(300000)};
    /*-----------------------------------------------------------------*\
    | A linked device that still fails setup after the schedule above   |
    | is retried at this interval for as long as it stays linked.       |
    \*-----------------------------------------------------------------*/
    std::chrono::milliseconds               create_retry{300000};
    /*-----------------------------------------------------------------*\
    | Repeated loss of control usually means another application is     |
    | driving the device. Each further loss doubles the quiet window up |
    | to poll_max, and the count resets after this long without one.    |
    \*-----------------------------------------------------------------*/
    std::chrono::milliseconds               contention_reset{600000};
    // Consecutive failed reads before the receiver is treated as removed.
    int                                     read_failures{3};
};

class LogitechLightspeedReceiverWatcher : public BackgroundWorker
{
public:
    using CreateHook = std::function<std::optional<LightspeedSlotHooks>(uint8_t slot)>;

    LogitechLightspeedReceiverWatcher(std::shared_ptr<hid_device> notifications, LightspeedWatcherTiming timing = {});
    ~LogitechLightspeedReceiverWatcher() override;

    // Configuration: call before Start().
    void SetCreateHook(CreateHook hook);
    void SetReceiverHooks(LightspeedReceiverHooks hooks);
    /*-----------------------------------------------------------------*\
    | Notifications arrive on the command handle, which device queries  |
    | also read. Reads are taken under this lock, and link reports may  |
    | be consumed by a query, so link state is not trusted.             |
    \*-----------------------------------------------------------------*/
    void SetSharedReadLock(std::shared_ptr<std::mutex> lock);
    void AddRegistered(uint8_t slot, LightspeedSlotHooks hooks);
    // linked: the receiver reported the slot's link up during enumeration.
    void AddPending(uint8_t slot, bool linked);

    void Start();
    void Stop() override;
    bool StopRequested() const;

private:
    using clock = std::chrono::steady_clock;

    struct Slot
    {
        bool                            registered      = false;
        bool                            linked          = true;
        bool                            monitored       = true;
        LightspeedSlotHooks             hooks;
        std::vector<clock::time_point>  due;            // Sorted one-shot checks.
        clock::time_point               next_poll{};
        std::chrono::milliseconds       interval{};
        clock::time_point               quiet_until{};
        clock::time_point               next_repaint{};
        bool                            power_saving    = false;
        bool                            repaint_on_due  = false;
        int                             lost_episodes   = 0;
        clock::time_point               last_loss{};
    };

    void Run();
    int  ReadNotification(unsigned char* report, int size);
    bool LinkTrusted() const;
    void OnReport(const unsigned char* report, int size, clock::time_point now);
    void Service(uint8_t index, Slot& slot, clock::time_point now);
    void CheckOwnership(uint8_t index, Slot& slot);
    void Repaint(uint8_t index, Slot& slot, const char* reason);
    static void Schedule(Slot& slot, clock::time_point when);

    std::shared_ptr<hid_device>     notifications;
    std::shared_ptr<std::mutex>     shared_reads;
    LightspeedWatcherTiming         timing;
    CreateHook                      create;
    LightspeedReceiverHooks         receiver;
    int                             failed_reads    = 0;
    bool                            receiver_lost   = false;
    bool                            stopped_reported = false;
    std::map<uint8_t, Slot>         slots;
    std::atomic<bool>               stop_requested{false};
    std::thread                     worker;
};
