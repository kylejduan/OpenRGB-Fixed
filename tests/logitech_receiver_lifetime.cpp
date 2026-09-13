// SPDX-License-Identifier: GPL-2.0-or-later
// Exercises the real protocol object's ownership; only the external HID and log APIs are faked.
#include "LogitechProtocolCommon.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>

struct hid_device_ { unsigned closes = 0; };
extern "C" void hid_close(hid_device* device) { ++device->closes; }
extern "C" int hid_write(hid_device* device, const unsigned char*, size_t size)
{
    assert(device->closes == 0);
    return static_cast<int>(size);
}
extern "C" int hid_read(hid_device* device, unsigned char* bytes, size_t size)
{
    assert(device->closes == 0);
    std::memset(bytes, 0, size);
    if(size > 5) bytes[5] = 0x09; // Actual protocol's disconnected response.
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device* device, unsigned char*, size_t, int)
{
    assert(device->closes == 0);
    return 0;
}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

template<class Handle> Handle adopt(hid_device* device)
{
    if constexpr(std::is_pointer_v<Handle>) return device;
    else return Handle(device, hid_close);
}

int main()
{
    char path[] = "test-powerplay";
    for(bool reverse : {false, true})
    {
        std::array<hid_device_, 3> handles;
        usages bundle;
        for(unsigned i = 0; i < handles.size(); ++i)
            bundle.emplace(i + 1, adopt<usages::mapped_type>(&handles[i]));
        auto mutex = std::make_shared<std::mutex>();
        auto mat = std::make_unique<logitech_device>(path, bundle, 7, true, mutex);
        auto mouse = std::make_unique<logitech_device>(path, bundle, 1, true, mutex);
        bundle.clear();
        if(reverse) mouse.reset(); else mat.reset();
        for(const auto& handle : handles)
            assert(handle.closes == 0 && "Destroying one controller closed a sibling's live HID handle");
        auto& survivor = reverse ? mat : mouse;
        survivor->connected();
        survivor.reset();
        for(const auto& handle : handles) assert(handle.closes == 1);
    }
    hid_device_ single;
    usages bundle;
    bundle.emplace(1, adopt<usages::mapped_type>(&single));
    for(unsigned attempt = 0; attempt < 5; ++attempt)
    {
        logitech_device failed(path, bundle, 1, true);
        assert(!failed.is_valid());
    }
    assert(single.closes == 0);
    bundle.clear();
    assert(single.closes == 1);
    std::puts("PASS: shared controllers, both destruction orders, and failed attempts close each handle exactly once");
}
