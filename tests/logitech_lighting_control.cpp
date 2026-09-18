// SPDX-License-Identifier: GPL-2.0-or-later
// Exercises real lighting requests against a HID boundary model. RGB Effects
// fixtures follow the G502's 0x8071 v2 replies and the public HID++ definitions.
#include "LogitechProtocolCommon.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <condition_variable>
#include <future>
#include <memory>
#include <string>
#include <thread>

using Packet = std::array<unsigned char, 20>;
std::mutex stall_mutex;
std::condition_variable stall_cv;
bool stall_next_mouse_write = false;
bool mouse_write_entered = false;
bool resume_mouse_write = false;
struct hid_device_
{
    uint16_t page = 0x8071;
    uint8_t control = 3;
    uint8_t power = 1;
    std::deque<Packet> replies;
    std::vector<Packet> writes;
    bool unrelated_first = false;
    bool reject_control = false;
    bool short_control = false;
    bool fail_write = false;
    bool timeout = false;
    bool short_replies = false;
    bool wrong_selector_first = false;
    bool asynchronous_wake = false;
    bool wake_pending = false;
    bool release_during_wake = false;
    bool power_loss_during_wake = false;
    std::chrono::steady_clock::time_point rgb_ready_at{};
    std::array<unsigned char, 3> rendered_rgb{};
    bool slow_color_reply = false;
    bool slow_replies = false;
    bool short_wake_ack_once = false;
    std::chrono::steady_clock::time_point reply_available_at{};
};

extern "C" void hid_close(hid_device*) {}
extern "C" int hid_write(hid_device* dev, const unsigned char* bytes, size_t size)
{
    assert(size == 20);
    Packet request{};
    std::copy_n(bytes, size, request.begin());
    {
        std::unique_lock<std::mutex> guard(stall_mutex);
        if(stall_next_mouse_write && request[1] == 1)
        {
            stall_next_mouse_write = false;
            mouse_write_entered = true;
            stall_cv.notify_all();
            stall_cv.wait(guard, [] { return resume_mouse_write; });
        }
    }
    dev->writes.push_back(request);
    if(dev->fail_write) return -1;
    if(dev->timeout) return static_cast<int>(size);
    Packet reply = request;
    std::fill(reply.begin() + 4, reply.end(), 0);
    const auto fn = request[3] & 0xF0;
    if(dev->wake_pending && std::chrono::steady_clock::now() >= dev->rgb_ready_at)
    {
        dev->wake_pending = false;
        if(dev->release_during_wake) dev->control = 0;
        if(dev->power_loss_during_wake) dev->power = 3;
    }
    if(request[2] == 0)
    {
        const unsigned page = (request[4] << 8) | request[5];
        reply[4] = page == dev->page ? 9 : (page == 0x0005 ? 3 : 0);
    }
    else if(request[2] == 3) // Device name and type (0x0005).
    {
        static const std::string name = "G502 X PLUS";
        if(fn == 0x00) reply[4] = static_cast<unsigned char>(name.size());
        else if(fn == 0x10)
        {
            const size_t start = std::min<size_t>(request[4], name.size());
            std::copy_n(name.begin() + start, std::min<size_t>(16, name.size() - start), reply.begin() + 4);
        }
        else if(fn == 0x20) reply[4] = 3;
    }
    else if(fn == 0)
    {
        if(request[4] == 255) // G502 device-info fixture: one cluster.
        {
            const unsigned char data[] = {255, 0, 1, 0, 51, 0, 4, 5};
            std::copy(std::begin(data), std::end(data), reply.begin() + 4);
        }
        else if(request[5] == 255) // Two modeled effects: off and solid.
        {
            const unsigned char data[] = {0, 0, 0, 1, 2, 1};
            std::copy(std::begin(data), std::end(data), reply.begin() + 4);
        }
        else
        {
            reply[4] = request[4];
            reply[5] = request[5];
            reply[7] = request[5] == 1 ? 1 : 0;
        }
    }
    else if(fn == 0x50 && dev->page == 0x8071)
    {
        if(request[4] == 1 && dev->reject_control)
        {
            reply[2] = 0xFF;
            reply[3] = request[2];
            reply[4] = request[3];
            reply[5] = 5;
        }
        else
        {
            if(request[4] == 1) dev->control = request[5];
            reply[4] = request[4];
            reply[5] = dev->control;
            reply[6] = 5;
        }
    }
    else if(fn == 0x80)
    {
        if(dev->page == 0x8070) dev->control = request[4];
        else if(request[4] == 1)
        {
            if(dev->asynchronous_wake && dev->power != 1 && request[5] == 1)
            {
                // Report full power immediately, but ignore rendering during
                // a modeled transition. Hardware ACKed early frames without
                // showing them; this duration is a fixture, not firmware data.
                dev->rgb_ready_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                dev->wake_pending = true;
            }
            dev->power = request[5];
        }
        reply[4] = request[4];
        reply[5] = dev->power;
    }
    else if(fn == 0x10 && dev->page == 0x8071 && dev->power == 1 &&
            dev->control == 3 && std::chrono::steady_clock::now() >= dev->rgb_ready_at)
    {
        std::copy_n(request.begin() + 6, 3, dev->rendered_rgb.begin());
    }
    if(dev->unrelated_first)
    {
        Packet notification{};
        notification[0] = 0x11;
        notification[1] = request[1];
        notification[2] = request[2];
        notification[3] = 0; // Event, not this software's response.
        dev->replies.push_back(notification);
        Packet sibling = reply;
        sibling[1] = 7;
        dev->replies.push_back(sibling);
        Packet wrong = reply;
        wrong[2] ^= 1;
        dev->replies.push_back(wrong);
        wrong = reply;
        wrong[3] ^= 0x10;
        dev->replies.push_back(wrong);
        wrong = reply;
        wrong[3] = (request[3] & 0xF0) | 0x0B; // Another application's ID.
        dev->replies.push_back(wrong);
    }
    if(dev->wrong_selector_first && (fn == 0x50 || fn == 0x80))
    {
        Packet wrong = reply;
        wrong[4] ^= 1; // Same function/ID, but a stale set reply for a getter (or reverse).
        dev->replies.push_back(wrong);
    }
    dev->reply_available_at = (dev->slow_color_reply && fn == 0x10) || dev->slow_replies
                           ? std::chrono::steady_clock::now() + std::chrono::milliseconds(450)
                           : std::chrono::steady_clock::time_point{};
    dev->replies.push_back(reply);
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device* dev, unsigned char* bytes, size_t size, int timeout_ms)
{
    if(dev->replies.empty()) return 0;
    if(std::chrono::steady_clock::now() < dev->reply_available_at)
    {
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(dev->reply_available_at - std::chrono::steady_clock::now()) + std::chrono::milliseconds(1);
        std::this_thread::sleep_for(std::min(wait, std::chrono::milliseconds(timeout_ms)));
        if(std::chrono::steady_clock::now() < dev->reply_available_at) return 0;
    }
    Packet reply = dev->replies.front();
    dev->replies.pop_front();
    size_t count = std::min(size, reply.size());
    if(dev->short_replies) { reply[0] = 0x10; count = 7; }
    if(dev->short_control && (reply[3] & 0xF0) == 0x50) count = 4;
    if(dev->short_wake_ack_once && (reply[3] & 0xF0) == 0x80 && reply[4] == 1)
    {
        dev->short_wake_ack_once = false;
        count = 4;
    }
    std::copy_n(reply.begin(), count, bytes);
    return static_cast<int>(count);
}
extern "C" int hid_read(hid_device* dev, unsigned char* bytes, size_t size)
{
    return hid_read_timeout(dev, bytes, size, 0);
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

struct Fixture
{
    hid_device_ hid;
    char path[16] = "test-receiver";
    std::unique_ptr<logitech_device> device;
    std::shared_ptr<std::mutex> receiver_mutex = std::make_shared<std::mutex>();
    Fixture()
    {
        usages bundle;
        bundle.emplace(2, std::shared_ptr<hid_device>(&hid, hid_close));
        device = std::make_unique<logitech_device>(path, bundle, 1, false,
                                                   receiver_mutex);
        assert(device->getLED_count() == 1);
        hid.writes.clear();
        hid.replies.clear();
    }
    size_t count(unsigned fn, int set = -1) const
    {
        return std::count_if(hid.writes.begin(), hid.writes.end(), [=](const Packet& p)
        {
            return (p[3] & 0xF0) == fn && (set < 0 || p[4] == set);
        });
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string test = argv[1];
    Fixture f;
    if(test == "static")
    {
        // Use an explicit static parameter. In the hardware experiment a
        // value of 2 showed red; default 0 worked again after that recovery.
        for(const auto& rgb : {std::array<uint8_t, 3>{255, 0, 0},
                               std::array<uint8_t, 3>{0, 0, 255},
                               std::array<uint8_t, 3>{0, 0, 0}})
        {
            assert(f.device->setMode(1, 0, 0, rgb[0], rgb[1], rgb[2], 100) > 0);
            const auto& packet = f.hid.writes.back();
            assert((packet[3] & 0xF0) == 0x10 && packet[5] == 1);
            assert(packet[6] == rgb[0] && packet[7] == rgb[1] && packet[8] == rgb[2]);
            assert(packet[9] == 2 && "Use an explicit no-ramp static parameter, including black");
            assert(packet[16] == 1 && "Color updates must be volatile, not EEPROM writes");
        }
    }
    else if(test == "mode")
    {
        for(bool direct : {true, false})
        {
            f.hid.control = 0;
            f.device->setDirectMode(direct);
            assert(f.hid.control == 3 && "0x8071 mode changes must restore software lighting control");
        }
    }
    else if(test == "color")
    {
        f.hid.control = 0;
        int result = f.device->setMode(1, 0, 0, 255, 0, 0, 100);
        assert(result > 0);
        assert(f.hid.control == 3 && "A color update must recover ownership without re-enumerating");
        auto claim = std::find_if(f.hid.writes.begin(), f.hid.writes.end(), [](const Packet& p) { return (p[3] & 0xF0) == 0x50 && p[4] == 1; });
        auto color = std::find_if(f.hid.writes.begin(), f.hid.writes.end(), [](const Packet& p) { return (p[3] & 0xF0) == 0x10; });
        assert(claim < color && color != f.hid.writes.end());
        assert((*color)[4] == 0 && (*color)[5] == 1 && (*color)[6] == 255 && (*color)[16] == 1);
        f.hid.writes.clear();
        f.device->setMode(1, 0, 0, 0, 0, 255, 100);
        assert(f.count(0x50, 1) == 0 && "Do not repeatedly reset an existing software claim");
    }
    else if(test == "power" || test == "power_control" || test == "power_changed")
    {
        f.hid.power = 3;
        f.hid.asynchronous_wake = true;
        f.hid.release_during_wake = test == "power_control";
        f.hid.power_loss_during_wake = test == "power_changed";
        const int result = f.device->setMode(1, 0, 0, 0, 255, 0, 100);
        if(test == "power_changed")
        {
            assert(result < 0 && "Reject a power-state change during wake settling");
            assert(f.count(0x10) == 0 && "Do not paint after failed power verification");
        }
        else
        {
            assert(result > 0 && f.hid.power == 1 && f.hid.control == 3);
            const std::array<unsigned char, 3> green{0, 255, 0};
            assert(f.hid.rendered_rgb == green && "Power readback alone does not prove readiness to paint");
            assert(f.count(0x10) == 1 && "Recovery must apply the first requested frame");
        }
    }
    else if(test == "power_retry")
    {
        // A bad wake ACK can return early even though hardware already
        // changed power. GUI mode application immediately follows with LEDs.
        f.hid.power = 3;
        f.hid.asynchronous_wake = true;
        f.hid.short_wake_ack_once = true;
        assert(f.device->setDirectMode(true) < 0 && f.hid.power == 1);
        assert(f.device->setMode(1, 0, 0, 0, 255, 0, 100) > 0);
        const std::array<unsigned char, 3> green{0, 255, 0};
        assert(f.hid.rendered_rgb == green && "A failed ACK must not discard pending wake settling");
        assert(f.count(0x10) == 1);
    }
    else if(test == "slow_ack")
    {
        // A matched post-wake hardware reply arrived about 487 ms after
        // its write. Keep a bounded response window that accommodates it.
        f.hid.slow_color_reply = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) > 0);
        assert(f.count(0x10) == 1);
    }
    else if(test == "replies")
    {
        f.hid.control = 0;
        f.hid.unrelated_first = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) > 0);
        assert(f.hid.control == 3);
        assert(f.hid.replies.empty() && "Unrelated events and sibling replies cannot satisfy a request");
        for(const auto& packet : f.hid.writes)
            assert((packet[3] & 15) == 7 && "Use OpenRGB's identity, not another client's software ID");
    }
    else if(test == "short_ack")
    {
        f.hid.short_replies = true;
        f.hid.control = 0;
        f.hid.power = 3;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) == 7);
        assert(f.hid.control == 3 && f.hid.power == 1);
        assert(f.hid.replies.empty());
    }
    else if(test == "stale")
    {
        Packet stale{};
        stale[0] = 0x11; stale[1] = 1; stale[2] = 9; stale[3] = 0x57;
        f.hid.replies.push_back(stale); // Old getter said control=0; it is now 3.
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) > 0);
        assert(f.count(0x50, 1) == 0 && "Queued stale replies must not reset a current claim");
        assert(f.hid.replies.empty());
    }
    else if(test == "selector")
    {
        f.hid.wrong_selector_first = true;
        f.hid.control = 0;
        f.hid.power = 3;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) > 0);
        assert(f.hid.control == 3 && f.hid.power == 1);
        assert(f.hid.replies.empty());
    }
    else if(test == "queue_boundary" || test == "queue_busy")
    {
        Packet event{};
        event[0] = 0x11;
        f.hid.replies.assign(test == "queue_boundary" ? 64 : 65, event);
        const int result = f.device->setMode(1, 0, 0, 255, 0, 0, 100);
        if(test == "queue_boundary")
        {
            assert(result > 0 && "A full 64-report Windows HID queue can be drained successfully");
            assert(f.count(0x10) == 1);
        }
        else
        {
            assert(result < 0 && f.hid.writes.empty() && "Do not send into a queue that exceeds the drain budget");
        }
    }
    else if(test == "errors")
    {
        f.hid.control = 0;
        f.hid.reject_control = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) < 0);
        assert(f.count(0x10) == 0 && "Do not send color after the device rejects ownership");
    }
    else if(test == "short")
    {
        f.hid.short_control = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) < 0);
        assert(f.count(0x10) == 0);
    }
    else if(test == "write")
    {
        f.hid.fail_write = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) < 0);
        assert(f.count(0x10) == 0);
    }
    else if(test == "timeout")
    {
        f.hid.timeout = true;
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) < 0);
        assert(f.count(0x10) == 0);
    }
    else if(test == "legacy")
    {
        f.hid.page = 0x8070;
        f.device->feature_list.clear();
        f.device->feature_list.emplace(0x8070, 9);
        for(bool direct : {true, false})
        {
            f.hid.writes.clear();
            assert(f.device->setDirectMode(direct) > 0);
            assert(f.hid.writes.size() == 1);
            const auto& p = f.hid.writes.front();
            assert((p[3] & 0xF0) == 0x80 && p[4] == direct && p[5] == direct);
        }
        f.hid.writes.clear();
        assert(f.device->setMode(1, 0, 0, 255, 0, 0, 100) > 0);
        assert(f.hid.writes.size() == 1);
        const auto& p = f.hid.writes.front();
        assert((p[3] & 0xF0) == 0x30 && p[5] == 1 && p[6] == 255 && p[9] == 0 && p[16] == 0);
    }
    else if(test == "shared")
    {
        usages bundle;
        bundle.emplace(2, std::shared_ptr<hid_device>(&f.hid, hid_close));
        logitech_device sibling(f.path, bundle, 7, false, f.receiver_mutex);
        f.hid.writes.clear();
        f.hid.control = 0;
        stall_next_mouse_write = true;
        auto first = std::async(std::launch::async, [&] { return f.device->setMode(1, 0, 0, 255, 0, 0, 100); });
        {
            std::unique_lock<std::mutex> guard(stall_mutex);
            assert(stall_cv.wait_for(guard, std::chrono::seconds(5), [] { return mouse_write_entered; }));
        }
        std::promise<void> second_started;
        auto second = std::async(std::launch::async, [&] {
            second_started.set_value();
            return sibling.setMode(1, 0, 0, 0, 0, 255, 100);
        });
        second_started.get_future().wait();
        const auto status = second.wait_for(std::chrono::milliseconds(50));
        {
            std::lock_guard<std::mutex> guard(stall_mutex);
            resume_mouse_write = true;
        }
        stall_cv.notify_all();
        assert(first.get() > 0 && second.get() > 0);
        assert(status == std::future_status::timeout && "A sibling must wait for the shared receiver transaction");
        bool saw_sibling = false;
        for(const auto& packet : f.hid.writes)
        {
            if(packet[1] == 7) saw_sibling = true;
            else assert(!saw_sibling && "Claim and color requests must not interleave with sibling writes");
        }
        assert(saw_sibling);
    }
    else if(test == "control_read")
    {
        f.hid.control = 3;
        assert(f.device->readSoftwareControl(300) == 3);
        assert(f.hid.writes.size() == 1 && f.count(0x50, 0) == 1 && "Read ownership with one GET and no claim");
        f.hid.control = 0;
        assert(f.device->readSoftwareControl(300) == 0 && f.count(0x50, 1) == 0);
        f.hid.timeout = true;
        const auto started = std::chrono::steady_clock::now();
        assert(f.device->readSoftwareControl(300) == -1);
        assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(900) && "Honor the short watchdog deadline");
        f.hid.timeout = false;
        f.hid.fail_write = true;
        assert(f.device->readSoftwareControl(300) == -1);
        f.hid.fail_write = false;
        f.hid.page = 0x8070;
        f.device->feature_list.clear();
        f.device->feature_list.emplace(0x8070, 9);
        f.hid.writes.clear();
        assert(f.device->readSoftwareControl(300) == -2 && f.hid.writes.empty() && "Only 0x8071 devices are polled");
    }
    else if(test == "init_locked")
    {
        usages bundle;
        bundle.emplace(2, std::shared_ptr<hid_device>(&f.hid, hid_close));
        std::unique_lock<std::mutex> held(*f.receiver_mutex);
        auto created = std::async(std::launch::async, [&] {
            return std::make_unique<logitech_device>(f.path, bundle, 7, false, f.receiver_mutex);
        });
        assert(created.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
        assert(f.hid.writes.empty() && "A late device must not query the receiver during a sibling transaction");
        held.unlock();
        auto sibling = created.get();
        assert(sibling->getLED_count() == 1 && !f.hid.writes.empty());
    }
    else if(test == "init_slow")
    {
        // Replies measured about 487 ms after a wake. Initialization must pair
        // each query with its own reply instead of taking the next report.
        hid_device_ slow;
        slow.slow_replies = true;
        usages bundle;
        bundle.emplace(2, std::shared_ptr<hid_device>(&slow, hid_close));
        logitech_device late(f.path, bundle, 1, false, f.receiver_mutex);
        assert(late.device_name == "G502 X PLUS" && late.logitech_device_type == 3 && "Slow replies must not shift name queries");
        assert(late.RGB_feature_index == 9 && "Slow replies must not shift feature lookups");
        assert(late.getLED_count() == 1 && "Slow replies must not shift LED enumeration");
        assert(late.getLED_info(0).fx.size() == 2);
    }
    else if(test == "power_read")
    {
        for(unsigned char mode : {1, 2, 3})
        {
            f.hid.power = mode;
            f.hid.writes.clear();
            assert(f.device->readRgbPowerMode(300) == mode);
            assert(f.hid.writes.size() == 1 && f.count(0x80, 0) == 1 && "Read power with one GET and no set");
        }
        f.hid.timeout = true;
        assert(f.device->readRgbPowerMode(300) == -1);
        f.hid.timeout = false;
        f.hid.page = 0x8070;
        f.device->feature_list.clear();
        f.device->feature_list.emplace(0x8070, 9);
        f.hid.writes.clear();
        assert(f.device->readRgbPowerMode(300) == -2 && f.hid.writes.empty());
    }
    else { assert(false && "Unknown test case"); }
    std::printf("PASS: Logitech lighting %s\n", test.c_str());
}
