// SPDX-License-Identifier: GPL-2.0-or-later
// Real Corsair RGB/keepalive code, with a gated lower driver and no USB device.
#include "RGBController_CorsairV2Hardware.h"
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <string>

using namespace std::chrono_literals;

struct State
{
    std::mutex mutex;
    std::condition_variable changed;
    int active = 0, calls = 0, gate_call = 1;
    bool gate = false, release = false, overlap = false;
};

static corsair_v2_device device_data{}; // No zones: protocol transport is the fixture boundary.
CorsairPeripheralV2Controller::CorsairPeripheralV2Controller(hid_device*, const char*, std::string) {}
CorsairPeripheralV2Controller::~CorsairPeripheralV2Controller() {}
const corsair_v2_device* CorsairPeripheralV2Controller::GetDeviceData() { return &device_data; }
std::string CorsairPeripheralV2Controller::GetName() { return "Corsair fixture"; }
std::string CorsairPeripheralV2Controller::GetFirmwareString() { return "fixture"; }
std::string CorsairPeripheralV2Controller::GetDeviceLocation() { return "fixture"; }
std::string CorsairPeripheralV2Controller::GetSerialString() { return ""; }
unsigned int CorsairPeripheralV2Controller::GetKeyboardLayout() { return CORSAIR_V2_KB_LAYOUT_ANSI; }

struct Driver : CorsairPeripheralV2Controller
{
    State& state;
    explicit Driver(State& value) : CorsairPeripheralV2Controller(nullptr, "fixture", "fixture"), state(value) {}
    ~Driver() override { std::lock_guard<std::mutex> lock(state.mutex); assert(state.active == 0); }
    void SetLedsDirect(std::vector<RGBColor*>) override
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.overlap |= ++state.active > 1;
        const bool first = ++state.calls == state.gate_call;
        state.changed.notify_all();
        if(first && state.gate) { state.changed.wait(lock, [&] { return state.release; }); }
        --state.active;
        state.changed.notify_all();
    }
};

LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

template<class Controller>
auto fast(Driver* driver, int) -> decltype(new Controller(driver, 10ms, 30ms)) { return new Controller(driver, 10ms, 30ms); }
template<class Controller>
Controller* fast(Driver* driver, ...) { return new Controller(driver); }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string test = argv[1];
    State state;
    state.gate = test == "serialize";
    device_data.type = DEVICE_TYPE_KEYBOARD;
    RGBController_CorsairV2HW* rgb = test == "arming" ? fast<RGBController_CorsairV2HW>(new Driver(state), 0)
                                                    : new RGBController_CorsairV2HW(new Driver(state));
    const auto wait_for_calls = [&](int count)
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.changed.wait_for(lock, 5s, [&] { return state.calls >= count; }));
    };
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        assert(state.changed.wait_for(lock, 5s, [&] { return state.calls >= 1; }));
    }
    double pending_stop_ms = 0;
    if(test == "pending")
    {
        { std::lock_guard<std::mutex> lock(state.mutex); state.gate = true; state.gate_call = 2; }
        rgb->UpdateLEDs(); wait_for_calls(2);
        const auto started = std::chrono::steady_clock::now();
        auto destroying = std::async(std::launch::async, [&] { delete rgb; });
        assert(destroying.wait_for(50ms) == std::future_status::timeout);
        { std::lock_guard<std::mutex> lock(state.mutex); state.release = true; state.changed.notify_all(); }
        destroying.get(); rgb = nullptr;
        pending_stop_ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    }
    else if(test == "arming")
    {
        rgb->active_mode = 1; rgb->DeviceUpdateMode();
        std::this_thread::sleep_for(70ms);
        { std::lock_guard<std::mutex> lock(state.mutex); assert(state.calls == 1); }
        rgb->active_mode = 0; rgb->DeviceUpdateMode(); wait_for_calls(2);
        rgb->active_mode = 1; rgb->DeviceUpdateMode();
        std::this_thread::sleep_for(70ms);
        { std::lock_guard<std::mutex> lock(state.mutex); assert(state.calls == 2); }
        rgb->SetCustomMode(); rgb->UpdateLEDs(); wait_for_calls(3);
        wait_for_calls(4); // The SDK-style color update must re-arm periodic refresh too.
    }
    else if(test == "serialize")
    {
        // The first real keepalive transaction is held open. Every foreground
        // path must wait for it, rather than interleave another device frame.
        auto full = std::async(std::launch::async, [&] { rgb->DeviceUpdateLEDs(); });
        auto zone = std::async(std::launch::async, [&] { rgb->UpdateZoneLEDs(0); });
        auto led = std::async(std::launch::async, [&] { rgb->UpdateSingleLED(0); });
        full.wait_for(100ms);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.release = true; state.changed.notify_all();
        }
        full.get(); zone.get(); led.get();
    }
    else { assert(test == "stop"); }
    std::this_thread::sleep_for(50ms); // First send returned; the old worker is in its long sleep.
    if(rgb) rgb->StopDeviceThread();
    const auto start = std::chrono::steady_clock::now();
    const size_t controller_size = sizeof(RGBController_CorsairV2HW);
    delete rgb;
    const double stop_ms = pending_stop_ms + std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("{\"stop_ms\":%.3f,\"overlap\":%s,\"calls\":%d,\"sizeof_controller\":%zu}\n", stop_ms, state.overlap ? "true" : "false", state.calls, controller_size);
    std::fflush(stdout);
    assert(!state.overlap && "Foreground frame overlapped the keepalive transaction");
    assert(stop_ms < 1000 && "Controller stop waited out the keepalive sleep");
    if(test != "arming") { assert(state.calls == (test == "serialize" ? 4 : test == "pending" ? 2 : 1)); }
}
