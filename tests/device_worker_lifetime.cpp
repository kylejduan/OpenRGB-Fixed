// SPDX-License-Identifier: GPL-2.0-or-later
// Uses the real RGBController worker with a blocked device transaction.
#include "RGBController.h"
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>

struct Transaction
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, finished = false;
};

struct Device : RGBController
{
    Transaction& transaction;
    explicit Device(Transaction& state) : transaction(state) {}
    ~Device() override { assert(transaction.finished && "Driver destroyed while its worker was still using it"); }
    void SetupZones() override {}
    void ResizeZone(int, int) override {}
    void UpdateZoneLEDs(int) override {}
    void UpdateSingleLED(int) override {}
    void DeviceUpdateMode() override {}
    void DeviceUpdateLEDs() override
    {
        std::unique_lock<std::mutex> lock(transaction.mutex);
        transaction.entered = true;
        transaction.changed.notify_all();
        transaction.changed.wait(lock, [&] { return transaction.released; });
        transaction.finished = true;
    }
};

template<class Controller>
auto stop(Controller* device, int) -> decltype(device->StopDeviceThread(), void()) { device->StopDeviceThread(); }
template<class Controller>
void stop(Controller*, ...) {} // Original cleanup has no stop before derived destruction.

int main()
{
    using namespace std::chrono_literals;
    Transaction transaction;
    Device* device = new Device(transaction);
    std::unique_lock<std::mutex> lock(transaction.mutex);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while(!transaction.entered && std::chrono::steady_clock::now() < deadline)
    {
        device->UpdateLEDs();
        transaction.changed.wait_for(lock, 1ms);
    }
    assert(transaction.entered);
    lock.unlock();
    std::promise<void> stopping;
    auto stopped = std::async(std::launch::async, [&] { stopping.set_value(); stop(device, 0); });
    stopping.get_future().wait();
    assert(stopped.wait_for(50ms) == std::future_status::timeout && "Cleanup did not wait for the active device transaction");
    lock.lock();
    transaction.released = true;
    transaction.changed.notify_all();
    lock.unlock();
    stopped.get();
    stop(device, 0); // Idempotent when called again by cleanup or the base destructor.
    delete device;
    std::puts("PASS: worker finishes before driver destruction; repeated stop is safe");
}
