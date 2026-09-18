// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the receiver watcher with a fake short-report interface and fake
// slot hooks. Link reports mirror detection logs: flags A2 awake, 62 asleep.
#include "LogitechLightspeedReceiverWatcher.h"
#include "LogManager.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>

using namespace std::chrono_literals;

struct hid_device_
{
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<unsigned char>> reports;
};

extern "C" void hid_close(hid_device*) {}
extern "C" int hid_read_timeout(hid_device* dev, unsigned char* data, size_t size, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(dev->mutex);
    if(!dev->ready.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return !dev->reports.empty(); })) return 0;
    std::vector<unsigned char> report = dev->reports.front();
    dev->reports.pop_front();
    const size_t count = std::min(size, report.size());
    std::copy_n(report.begin(), count, data);
    return static_cast<int>(count);
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

static void push(hid_device_& dev, std::vector<unsigned char> report)
{
    { std::lock_guard<std::mutex> lock(dev.mutex); dev.reports.push_back(std::move(report)); }
    dev.ready.notify_all();
}
static void link(hid_device_& dev, uint8_t slot, bool up)
{
    push(dev, {0x10, slot, 0x41, 0x11, static_cast<unsigned char>(up ? 0xA2 : 0x62), 0x99, 0x40});
}
static bool eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(condition()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return condition();
}

struct Mouse
{
    std::mutex mutex;
    std::vector<int> script{3};
    std::vector<int> power_script{1};
    bool static_colors = true;
    size_t reads = 0, power_reads = 0;
    int reapplies = 0;
    LightspeedSlotHooks hooks()
    {
        LightspeedSlotHooks h;
        h.name = "G502 test";
        h.read_control = [this](int deadline_ms) {
            assert(deadline_ms == 1000 && "Post-wake replies take about 487 ms");
            std::lock_guard<std::mutex> lock(mutex);
            return script[std::min(reads++, script.size() - 1)];
        };
        h.read_power = [this](int deadline_ms) {
            assert(deadline_ms == 1000);
            std::lock_guard<std::mutex> lock(mutex);
            return power_script[std::min(power_reads++, power_script.size() - 1)];
        };
        h.static_colors = [this] { std::lock_guard<std::mutex> lock(mutex); return static_colors; };
        h.reapply = [this] { std::lock_guard<std::mutex> lock(mutex); ++reapplies; };
        return h;
    }
    size_t read_count() { std::lock_guard<std::mutex> lock(mutex); return reads; }
    int reapply_count() { std::lock_guard<std::mutex> lock(mutex); return reapplies; }
};

static LightspeedWatcherTiming fast()
{
    LightspeedWatcherTiming t;
    t.read_timeout      = 5ms;
    t.poll              = 60000ms; // Only scheduled checks unless a case shortens it.
    t.poll_max          = 240000ms;
    t.quiet             = 100ms;
    t.after_link        = {10ms, 60ms};
    t.repaint           = 0ms; // Off unless a case asks for it.
    t.create_after_link = {10ms, 40ms, 80ms};
    return t;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string test = argv[1];
    hid_device_ receiver;
    std::shared_ptr<hid_device> handle(&receiver, hid_close);
    LightspeedWatcherTiming timing = fast();
    Mouse mouse;
    std::atomic<int> creates{0};
    int succeed_on = 1;

    if(test == "backoff")     { timing.poll = 20ms; timing.poll_max = 160ms; mouse.script = {-1}; }
    if(test == "backoff_long_poll") { timing.poll = 200ms; timing.poll_max = 100ms; mouse.script = {-1}; }
    if(test == "poll_reapply"){ timing.poll = 20ms; mouse.script = {3, 3, 0, 3}; }
    if(test == "quiet")       { timing.poll = 10ms; timing.quiet = 400ms; mouse.script = {0}; }
    if(test == "unsupported") { timing.poll = 10ms; mouse.script = {-2}; }
    if(test == "link_down")   { timing.poll = 20ms; }
    if(test == "stop")        { timing.read_timeout = 250ms; }
    if(test == "power_return"){ timing.poll = 20ms; mouse.power_script = {2, 2, 1, 1}; }
    if(test == "periodic_repaint")      { timing.poll = 600000ms; timing.repaint = 60ms; }
    if(test == "no_periodic_for_effect"){ timing.poll = 600000ms; timing.repaint = 60ms; mouse.static_colors = false; }
    if(test == "no_repaint_in_power_save"){ timing.poll = 600000ms; timing.repaint = 60ms; mouse.power_script = {2}; }
    if(test == "create_retry"){ succeed_on = 3; }
    // Ownership stays ours: only the reconnect itself may trigger the re-apply.
    if(test == "link_reapply"){ mouse.script = {3}; }

    LogitechLightspeedReceiverWatcher watcher(handle, timing);
    std::atomic<int> created_slot{0};
    watcher.SetCreateHook([&](uint8_t slot) -> std::optional<LightspeedSlotHooks> {
        created_slot = slot;
        if(++creates < succeed_on) return std::nullopt;
        return mouse.hooks();
    });
    const bool pending = test == "late_create" || test == "create_retry" || test == "asleep" || test == "malformed";
    if(test == "pending_linked") watcher.AddPending(1, true);
    else if(pending) watcher.AddPending(1, false);
    else if(test != "unknown_slot") watcher.AddRegistered(1, mouse.hooks());
    watcher.Start();

    if(test == "late_create" || test == "create_retry")
    {
        std::this_thread::sleep_for(50ms);
        assert(creates == 0 && "Do not probe a sleeping slot without a link notification");
        link(receiver, 1, true);
        assert(eventually([&] { return creates == succeed_on; }));
        assert(eventually([&] { return mouse.read_count() >= 1; }) && "Verify ownership after late registration");
        std::this_thread::sleep_for(150ms);
        assert(creates == succeed_on && "Stop creation attempts after success");
    }
    else if(test == "unknown_slot")
    {
        // Enumeration can miss a late boot announcement; the announcement or a
        // later wake still reaches the watcher as a link notification.
        link(receiver, 3, false);
        std::this_thread::sleep_for(100ms);
        assert(creates == 0 && "An unknown sleeping slot is not probed");
        link(receiver, 3, true);
        assert(eventually([&] { return creates == 1 && created_slot == 3; }));
        assert(eventually([&] { return mouse.read_count() >= 1; }));
    }
    else if(test == "pending_linked")
    {
        // Detection failed while the link was up: no link notification will
        // follow, so creation must be retried on its own schedule.
        assert(eventually([&] { return creates == 1; }));
        assert(eventually([&] { return mouse.read_count() >= 1; }));
    }
    else if(test == "asleep" || test == "malformed")
    {
        if(test == "asleep") link(receiver, 1, false);
        else
        {
            push(receiver, {0x10, 1, 0x41});                                  // Truncated.
            push(receiver, {0x10, 0, 0x41, 0x11, 0xA2, 0x99, 0x40});          // No slot 0.
            push(receiver, {0x10, 0xFF, 0x41, 0x11, 0xA2, 0x99, 0x40});       // Receiver itself.
            push(receiver, {0x10, 1, 0x42, 0x11, 0xA2, 0x99, 0x40});          // Other notification.
            push(receiver, {0x11, 1, 0x41, 0x11, 0xA2, 0x99, 0x40});          // Long report.
        }
        std::this_thread::sleep_for(200ms);
        assert(creates == 0);
    }
    else if(test == "link_reapply")
    {
        // A reconnect re-initialises the device's LEDs even when it keeps our
        // software control, so the colours must be re-applied unconditionally.
        link(receiver, 1, true);
        assert(eventually([&] { return mouse.reapply_count() >= 1; }));
        std::this_thread::sleep_for(200ms);
        const int after_link = mouse.reapply_count();
        assert(after_link >= 1 && after_link <= 2 && "One re-apply per scheduled post-link frame");
    }
    else if(test == "power_return")
    {
        // Returning from RGB power save to full power repaints the device.
        assert(eventually([&] { return mouse.reapply_count() >= 1; }));
        std::this_thread::sleep_for(200ms);
        assert(mouse.reapply_count() == 1 && "Only the transition back to full power repaints");
    }
    else if(test == "periodic_repaint")
    {
        assert(eventually([&] { return mouse.reapply_count() >= 2; }) && "Static colours are refreshed periodically");
    }
    else if(test == "no_repaint_in_power_save")
    {
        // Refreshing colours must not wake the RGB engine out of power save.
        std::this_thread::sleep_for(400ms);
        assert(mouse.reapply_count() == 0 && "A refresh must not force RGB power back on");
        assert(mouse.read_count() == 0 && "A refresh checks power, not ownership");
    }
    else if(test == "no_periodic_for_effect")
    {
        std::this_thread::sleep_for(400ms);
        assert(mouse.reapply_count() == 0 && "A device-side animation must not be restarted");
    }
    else if(test == "poll_reapply")
    {
        assert(eventually([&] { return mouse.reapply_count() == 1; }));
    }
    else if(test == "quiet")
    {
        assert(eventually([&] { return mouse.reapply_count() == 1; }));
        std::this_thread::sleep_for(120ms);
        assert(mouse.read_count() >= 3 && mouse.reapply_count() == 1 && "One re-apply per quiet window");
        assert(eventually([&] { return mouse.reapply_count() >= 2; }));
    }
    else if(test == "backoff")
    {
        std::this_thread::sleep_for(500ms);
        const size_t reads = mouse.read_count();
        assert(reads >= 3 && reads <= 8 && "No-reply polls back off exponentially");
        assert(mouse.reapply_count() == 0);
    }
    else if(test == "backoff_long_poll")
    {
        // A configured poll longer than the backoff cap must never be shortened
        // for a device that stopped answering.
        std::this_thread::sleep_for(700ms);
        const size_t reads = mouse.read_count();
        // Correct: reads at 200, 400 and 600 ms. Capped at 100 ms it read 5 times.
        assert(reads >= 1 && reads <= 3 && "No-reply backoff must not poll faster than the configured interval");
    }
    else if(test == "unsupported")
    {
        assert(eventually([&] { return mouse.read_count() == 1; }));
        link(receiver, 1, true);
        std::this_thread::sleep_for(200ms);
        assert(mouse.read_count() == 1 && "Stop polling a slot without 0x8071 control");
    }
    else if(test == "link_down")
    {
        assert(eventually([&] { return mouse.read_count() >= 2; }));
        link(receiver, 1, false);
        std::this_thread::sleep_for(150ms);
        const size_t asleep = mouse.read_count();
        std::this_thread::sleep_for(200ms);
        assert(mouse.read_count() == asleep && "No requests while the link is down");
        link(receiver, 1, true);
        assert(eventually([&] { return mouse.read_count() > asleep; }));
    }
    else if(test == "stop")
    {
        std::this_thread::sleep_for(20ms);
    }
    else { assert(false && "Unknown test case"); }

    const auto stopping = std::chrono::steady_clock::now();
    watcher.Stop();
    assert(std::chrono::steady_clock::now() - stopping < 600ms && "Stop must not wait for idle polls");
    std::printf("PASS: Lightspeed watcher %s\n", test.c_str());
}
