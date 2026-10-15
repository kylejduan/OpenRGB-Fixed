// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the wireless connection query and read-queue flush against a fake
// receiver short-report interface. A late device is built on the watcher
// thread, so neither may wait without a deadline.
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
    std::string mode;
    std::deque<Report> queue;
    size_t flood_reads = 0;
};

extern "C" void hid_close(hid_device*) {}
extern "C" int hid_write(hid_device* dev, const unsigned char* bytes, size_t size)
{
    // Off Windows every usage maps to this handle; only the connection
    // state register query is modelled, other requests go unanswered.
    if(size != 7 || bytes[0] != 0x10 || bytes[2] != 0x81 || bytes[3] != 0x02) return static_cast<int>(size);
    const unsigned char slot = bytes[1];
    if(dev->mode == "connected")       dev->queue.push_back({0x10, slot, 0x81, 0x02, 0x00, 0x01, 0x00});
    else if(dev->mode == "error")      dev->queue.push_back({0x10, slot, 0x8F, 0x81, 0x02, 0x09, 0x00});
    else if(dev->mode == "unrelated")
    {
        dev->queue.push_back({0x10, 0x07, 0x41, 0x0C, 0xA9, 0x5F, 0x40});        // Another slot's link report.
        dev->queue.push_back({0x10, 0x07, 0x8F, 0x81, 0x02, 0x09, 0x00});        // Another slot's error.
        dev->queue.push_back({0x10, slot, 0x81, 0x02, 0x00, 0x01, 0x00});
    }
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device* dev, unsigned char* bytes, size_t size, int timeout_ms)
{
    if(dev->mode == "flood" && dev->queue.empty())
    {
        ++dev->flood_reads;
        const Report report = {0x10, 0x07, 0x41, 0x0C, 0xA9, 0x5F, 0x40};
        std::copy_n(report.begin(), std::min(size, report.size()), bytes);
        return static_cast<int>(std::min(size, report.size()));
    }
    if(dev->queue.empty())
    {
        assert(timeout_ms >= 0 && "Receiver reads must have a deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    const Report report = dev->queue.front();
    dev->queue.pop_front();
    const size_t count = std::min(size, report.size());
    std::copy_n(report.begin(), count, bytes);
    return static_cast<int>(count);
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

int main(int argc, char** argv)
{
    assert(argc == 2);
    hid_device_ receiver;
    receiver.mode = argv[1];
    char path[] = "test-receiver";
    usages bundle;
    bundle.emplace(1, std::shared_ptr<hid_device>(&receiver, hid_close));
    auto mutex = std::make_shared<std::mutex>();

    const auto started = Clock::now();
    logitech_device device(path, bundle, 1, true, mutex);
    const bool connected = device.connected();
    const auto elapsed = Clock::now() - started;

    if(receiver.mode == "connected" || receiver.mode == "unrelated")
    {
        assert(connected && "Only this slot's reply decides the result");
    }
    else if(receiver.mode == "error")
    {
        assert(!connected && "ERR_RESOURCE_ERROR means the device is not connected");
    }
    else if(receiver.mode == "silent")
    {
        assert(!connected);
        assert(elapsed < 3500ms && "A silent receiver must not block the build forever");
    }
    else if(receiver.mode == "flood")
    {
        assert(!connected);
        assert(receiver.flood_reads > 0 && elapsed < 5000ms && "Flushing a streaming receiver must stop");
    }
    else { assert(false && "Unknown test case"); }

    assert(mutex->try_lock() && "The connection query releases the receiver lock");
    mutex->unlock();
    std::printf("PASS: Logitech connection query %s\n", receiver.mode.c_str());
}
