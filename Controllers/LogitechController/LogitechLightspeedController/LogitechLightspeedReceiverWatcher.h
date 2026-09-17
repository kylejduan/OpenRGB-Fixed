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
    std::function<void()>                   reapply;
};

struct LightspeedWatcherTiming
{
    std::chrono::milliseconds               read_timeout{250};
    std::chrono::milliseconds               poll{30000};
    std::chrono::milliseconds               poll_max{120000};
    int                                     reply_deadline_ms{1000};
    std::chrono::milliseconds               quiet{5000};
    std::vector<std::chrono::milliseconds>  after_link{std::chrono::milliseconds(2000), std::chrono::milliseconds(10000)};
    std::vector<std::chrono::milliseconds>  create_after_link{std::chrono::milliseconds(500), std::chrono::milliseconds(3000), std::chrono::milliseconds(10000), std::chrono::milliseconds(60000), std::chrono::milliseconds(300000)};
};

class LogitechLightspeedReceiverWatcher : public BackgroundWorker
{
public:
    using CreateHook = std::function<std::optional<LightspeedSlotHooks>(uint8_t slot)>;

    LogitechLightspeedReceiverWatcher(std::shared_ptr<hid_device> notifications, LightspeedWatcherTiming timing = {});
    ~LogitechLightspeedReceiverWatcher() override;

    // Configuration: call before Start().
    void SetCreateHook(CreateHook hook);
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
        int                             lost_episodes   = 0;
    };

    void Run();
    void OnReport(const unsigned char* report, int size, clock::time_point now);
    void Service(uint8_t index, Slot& slot, clock::time_point now);
    void CheckOwnership(uint8_t index, Slot& slot);
    static void Schedule(Slot& slot, clock::time_point when);

    std::shared_ptr<hid_device>     notifications;
    LightspeedWatcherTiming         timing;
    CreateHook                      create;
    std::map<uint8_t, Slot>         slots;
    std::atomic<bool>               stop_requested{false};
    std::thread                     worker;
};
