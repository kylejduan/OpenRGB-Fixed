// SPDX-License-Identifier: GPL-2.0-or-later
// Real controller workers, with no hardware or Qt: exercise publication,
// callback reentry, coalescing, ordering and shutdown while notifications race.
#include "RGBController.h"
#include <cassert>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;

struct Device : RGBController
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<char> calls;
    bool block = false, release = false, requeue = false;

    ~Device() override { StopDeviceThread(); }
    void SetupZones() override {}
    void ResizeZone(int, int) override {}
    void UpdateZoneLEDs(int) override {}
    void UpdateSingleLED(int) override {}
    void DeviceUpdateMode() override { Record('M'); }
    void DeviceUpdateLEDs() override { Record('L'); }

    void Record(char kind)
    {
        std::unique_lock<std::mutex> lock(mutex);
        calls.push_back(kind);
        changed.notify_all();
        if(requeue && calls.size() == 1) { UpdateLEDs(); }
        if(block && calls.size() == 1) { changed.wait(lock, [&] { return release; }); }
    }

    void WaitFor(size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(changed.wait_for(lock, 5s, [&] { return calls.size() >= count; }));
    }
};

template<class Controller>
auto restore(Controller& device, int) -> decltype(device.UpdateModeAndLEDsWithoutSignal(), void()) { device.UpdateModeAndLEDsWithoutSignal(); }
template<class Controller>
void restore(Controller& device, ...) { device.UpdateMode(); } // Original automatic-restore path.

struct Callback
{
    std::atomic<int> count{0};
    static void Count(void* arg) { ++static_cast<Callback*>(arg)->count; }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string test = argv[1];
    Device device;
    // Set options before the first publication; that publication must make
    // the configuration visible to the worker too.
    device.flags = (test == "wake" || test == "reentry" || test == "coalesce" || test == "ordering" || test == "contention")
                 ? CONTROLLER_FLAG_RESET_BEFORE_UPDATE : 0;
    if(test == "wake" || test == "wake_default")
    {
        const size_t iterations = test == "wake_default" ? 100 : 1000;
        for(size_t i = 1; i <= iterations; ++i)
        {
            device.UpdateLEDs();
            device.WaitFor(i);
            if(test == "wake_default") { std::this_thread::sleep_for(2ms); }
        }
        device.UpdateMode();
        device.WaitFor(iterations+1);
    }
    else if(test == "reentry")
    {
        device.requeue = true;
        device.UpdateLEDs();
        device.WaitFor(2);
    }
    else if(test == "coalesce" || test == "ordering" || test == "ordering_default")
    {
        device.block = true;
        device.UpdateLEDs();
        device.WaitFor(1);
        for(int i = 0; i < 1000; ++i) { device.UpdateLEDs(); }
        if(test != "coalesce") { device.UpdateMode(); }
        {
            std::lock_guard<std::mutex> lock(device.mutex);
            device.release = true;
            device.changed.notify_all();
        }
        device.WaitFor(test == "ordering" ? 3 : 2);
        device.StopDeviceThread();
        assert(device.calls == (test == "ordering" ? std::vector<char>{'L','M','L'} : test == "ordering_default" ? std::vector<char>{'L','M'} : std::vector<char>{'L','L'}));
    }
    else if(test == "reset_after")
    {
        device.flags = 0;
        device.requeue = true;
        device.UpdateLEDs();
        device.WaitFor(1);
        std::this_thread::sleep_for(50ms);
        device.StopDeviceThread();
        assert(device.calls == std::vector<char>{'L'});
    }
    else if(test == "contention")
    {
        device.block = true;
        device.UpdateLEDs(); device.WaitFor(1);
        std::vector<std::thread> publishers;
        for(int i=0;i<4;++i) publishers.emplace_back([&] { for(int j=0;j<1000;++j) { device.UpdateLEDs(); device.UpdateMode(); } });
        for(std::thread& publisher : publishers) publisher.join();
        device.UpdateLEDs(); // Must remain pending while the first device call is gated.
        {
            std::lock_guard<std::mutex> lock(device.mutex);
            device.release = true; device.changed.notify_all();
        }
        device.WaitFor(3); device.StopDeviceThread();
        assert(device.calls == std::vector<char>({'L','M','L'}));
    }
    else if(test == "profile" || test == "profile_effect" || test == "profile_invalid" || test == "profile_empty")
    {
        device.modes.resize(1);
        device.modes[0].color_mode = test == "profile" ? MODE_COLORS_PER_LED : MODE_COLORS_MODE_SPECIFIC;
        if(test == "profile_invalid") { device.active_mode = 99; }
        if(test == "profile_empty") { device.modes.clear(); }
        Callback callback;
        device.RegisterUpdateCallback(Callback::Count, &callback);
        restore(device, 0);
        device.WaitFor(test == "profile" ? 2 : 1);
        if(test == "profile_invalid") { std::this_thread::sleep_for(2ms); device.active_mode = -1; restore(device, 0); device.WaitFor(2); }
        device.StopDeviceThread();
        assert(device.calls == (test == "profile" ? std::vector<char>{'M','L'} : test == "profile_invalid" ? std::vector<char>{'M','M'} : std::vector<char>{'M'}));
        assert(callback.count == 0 && "Automatic restore must not deliver UI callbacks");
        device.ClearCallbacks();
    }
    else if(test == "stop" || test == "stop_default")
    {
        auto stopped = std::async(std::launch::async, [&] { device.StopDeviceThread(); });
        assert(stopped.wait_for(5s) == std::future_status::ready);
        stopped.get();
    }
    else { assert(false && "Unknown test case"); }
    device.StopDeviceThread(); // Sequential repeated stop remains harmless.
}
