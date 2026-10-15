// SPDX-License-Identifier: GPL-2.0-or-later
// Builds the real Lightspeed RGB controller from a fake 0x8071 mouse offering
// off, solid, spectrum and breathing effects, and checks which modes the
// periodic refresh may repaint. Breathing takes per-LED colours but is a
// device-side animation that a repaint would restart.
#include "RGBController_LogitechLightspeed.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>

using Packet = std::array<unsigned char, 20>;
struct hid_device_ { std::deque<Packet> replies; };

static const unsigned short effects[] = {0x0000, 0x0001, 0x0003, 0x000A};

extern "C" void hid_close(hid_device*) {}
extern "C" int hid_write(hid_device* dev, const unsigned char* bytes, size_t size)
{
    assert(size == 20);
    Packet request{}, reply{};
    std::copy_n(bytes, size, request.begin());
    std::copy_n(request.begin(), 4, reply.begin());
    const unsigned fn = request[3] & 0xF0;
    if(request[2] == 0)
    {
        const unsigned page = (request[4] << 8) | request[5];
        reply[4] = page == 0x8071 ? 9 : (page == 0x0005 ? 3 : 0);
    }
    else if(request[2] == 3)
    {
        static const std::string name = "G502 X PLUS";
        if(fn == 0x00) reply[4] = static_cast<unsigned char>(name.size());
        else if(fn == 0x10) std::copy(name.begin() + std::min<size_t>(request[4], name.size()), name.end(), reply.begin() + 4);
        else if(fn == 0x20) reply[4] = 3;
    }
    else if(request[2] == 9)
    {
        reply[4] = request[4];
        if(fn == 0x00 && request[4] == 0xFF) { reply[6] = 1; }                        // One cluster.
        else if(fn == 0x00 && request[5] == 0xFF) { reply[7] = 1; reply[8] = 4; }    // Logo, four effects.
        else if(fn == 0x00) { reply[6] = effects[request[5]] >> 8; reply[7] = effects[request[5]] & 0xFF; }
        else if(fn == 0x50) { reply[5] = 3; reply[6] = 5; }
        else if(fn == 0x80) { reply[5] = 1; }
    }
    dev->replies.push_back(reply);
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device* dev, unsigned char* bytes, size_t size, int)
{
    if(dev->replies.empty()) return 0;
    const Packet reply = dev->replies.front();
    dev->replies.pop_front();
    const size_t count = std::min(size, reply.size());
    std::copy_n(reply.begin(), count, bytes);
    return static_cast<int>(count);
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

int main()
{
    hid_device_ mouse;
    char path[] = "test-receiver";
    usages bundle;
    bundle.emplace(2, std::shared_ptr<hid_device>(&mouse, hid_close));
    LogitechLightspeedController* controller = new LogitechLightspeedController(&mouse, path);
    controller->lightspeed = new logitech_device(path, bundle, 1, false);
    RGBController_LogitechLightspeed rgb(controller);

    auto index = [&](const std::string& name)
    {
        for(std::size_t i = 0; i < rgb.modes.size(); i++) if(rgb.modes[i].name == name) return static_cast<int>(i);
        assert(false && "Mode missing");
        return -1;
    };
    assert(!rgb.ActiveModeUsesHostColors() && "Off is not host-painted");

    // Alternate the expected answer so every mode change is observable.
    const std::pair<const char*, bool> cases[] =
    {
        {"Direct", true}, {"Breathing", false}, {"Static", true},
        {"Spectrum Cycle", false}, {"Direct", true}, {"Off", false},
    };
    for(const std::pair<const char*, bool>& expected : cases)
    {
        rgb.active_mode = index(expected.first);
        rgb.UpdateMode();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(rgb.ActiveModeUsesHostColors() != expected.second && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(rgb.ActiveModeUsesHostColors() == expected.second && "Periodic refresh classification");
    }
    std::puts("PASS: only static host-painted Lightspeed modes are refreshed");
}
