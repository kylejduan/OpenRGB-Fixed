// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the real receiver enumeration against a fake Powerplay receiver.
// Report bytes mirror OpenRGB logs from a G502 X PLUS (slot 1) and a Powerplay
// mat (slot 7): normal starts announced both devices before the acknowledgement;
// the 2026-09-16 21:37 boot acknowledged first and announced late.
#include "LogitechProtocolCommon.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock  = std::chrono::steady_clock;
using Report = std::vector<unsigned char>;

struct hid_device_
{
    struct Queued { Report bytes; Clock::time_point available; };
    std::deque<Queued> queue;
    std::vector<Report> writes;
    bool announce_late  = false;
    bool mouse_asleep   = false;
    unsigned char reporting_flags = 0x01;

    void push(Report bytes, std::chrono::milliseconds delay = 0ms)
    {
        queue.push_back({std::move(bytes), Clock::now() + delay});
    }
};

extern "C" void hid_close(hid_device*) {}
extern "C" int hid_write(hid_device* dev, const unsigned char* bytes, size_t size)
{
    assert(size == 7 && bytes[0] == 0x10 && bytes[1] == 0xFF && "Receiver register requests are short reports");
    Report request(bytes, bytes + size);
    dev->writes.push_back(request);
    const unsigned char sub_id = bytes[2], reg = bytes[3];
    if(sub_id == 0x81 && reg == 0x00) dev->push({0x10, 0xFF, 0x81, 0x00, 0x00, dev->reporting_flags, 0x00});
    else if(sub_id == 0x80 && reg == 0x00) { dev->reporting_flags = bytes[5]; dev->push({0x10, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00}); }
    else if(sub_id == 0x81 && reg == 0x02) dev->push({0x10, 0xFF, 0x81, 0x02, 0x00, 0x02, 0x00});
    else if(sub_id == 0x80 && reg == 0x02 && bytes[4] == 0x02)
    {
        const Report mouse = {0x10, 0x01, 0x41, 0x11, static_cast<unsigned char>(dev->mouse_asleep ? 0x62 : 0xA2), 0x99, 0x40};
        const Report mat   = {0x10, 0x07, 0x41, 0x0C, 0xA9, 0x5F, 0x40};
        const Report ack   = {0x10, 0xFF, 0x80, 0x02, 0x00, 0x00, 0x00};
        if(dev->announce_late)
        {
            dev->push(ack);
            dev->push(mouse, 800ms);
            dev->push(mat, 850ms);
        }
        else
        {
            dev->push(mouse);
            dev->push(mat);
            dev->push(ack);
        }
    }
    else assert(false && "Unexpected receiver request");
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device* dev, unsigned char* bytes, size_t size, int timeout_ms)
{
    const auto give_up = Clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 60000 : timeout_ms);
    while(true)
    {
        if(!dev->queue.empty() && Clock::now() >= dev->queue.front().available)
        {
            Report report = dev->queue.front().bytes;
            dev->queue.pop_front();
            const size_t count = std::min(size, report.size());
            std::copy_n(report.begin(), count, bytes);
            return static_cast<int>(count);
        }
        if(Clock::now() >= give_up) return 0;
        std::this_thread::sleep_for(1ms);
    }
}
extern "C" int hid_read(hid_device* dev, unsigned char* bytes, size_t size)
{
    return hid_read_timeout(dev, bytes, size, -1);
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string test = argv[1];
    hid_device_ receiver;
    receiver.announce_late = test == "boot_order";
    receiver.mouse_asleep  = test == "asleep";
    if(test == "stale_reply")
    {
        // A connection report left from earlier activity must not be read as
        // the reply to the paired-device count query.
        receiver.push({0x10, 0x07, 0x41, 0x0C, 0xA9, 0x5F, 0x40});
    }
    if(test == "enable_notifications") receiver.reporting_flags = 0x00;

    usages bundle;
    bundle.emplace(1, std::shared_ptr<hid_device>(&receiver, hid_close));
    wireless_map devices;
    std::map<uint8_t, bool> link_up;
    const auto started = Clock::now();
    const int count = getWirelessDevice(bundle, 0xC53A, &devices, &link_up);

    assert(count == 2 && devices.size() == 2 && "Both paired devices must be enumerated");
    assert(devices.count(0x4099) && devices.at(0x4099) == 1);
    assert(devices.count(0x405F) && devices.at(0x405F) == 7);
    for(const auto& entry : devices) assert(entry.second != 0 && entry.second != 0xFF && "No receiver or empty slots");
    assert(link_up.count(1) && link_up.at(1) == !receiver.mouse_asleep);
    assert(link_up.count(7) && link_up.at(7));
    assert(Clock::now() - started < 3500ms);
    if(test == "normal") assert(Clock::now() - started < 500ms && "Stop reading once every device is announced");
    if(test == "enable_notifications")
    {
        const auto set = std::find_if(receiver.writes.begin(), receiver.writes.end(), [](const Report& r) { return r[2] == 0x80 && r[3] == 0x00; });
        assert(set != receiver.writes.end() && ((*set)[5] & 1) && "Enable wireless notifications when they are off");
    }
    std::printf("PASS: Logitech receiver enumeration %s\n", test.c_str());
}
