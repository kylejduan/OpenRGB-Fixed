// SPDX-License-Identifier: GPL-2.0-or-later
// Validate the real packet builder through changing frame sizes and both endpoint formats.
#include "CorsairPeripheralV2HardwareController.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

#ifndef OPENRGB_FIXED_TSAN
static std::atomic<size_t> allocations{0};
static bool count_allocations=false;
void* operator new(size_t n){if(count_allocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
#endif
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static size_t PublicArgumentCopy(std::vector<RGBColor*> colors)
{
    return colors.size();
}
static corsair_v2_device device{};
const unsigned int CORSAIR_V2_DEVICE_COUNT=1;
static const corsair_v2_device* devices[]={&device};
const corsair_v2_device** corsair_v2_device_list=devices;
static std::array<unsigned char,4096> payload{};
static size_t payload_size=0,payload_offset=0;
extern "C" int hid_write(hid_device*,const unsigned char* bytes,size_t size)
{
    assert(bytes[0]==0);
    if(bytes[2]==CORSAIR_V2_CMD_START_TX)payload_offset=0;
    if(bytes[2]==CORSAIR_V2_CMD_BLK_W1)
    {
        payload_size=bytes[4]+256U*bytes[5];assert(payload_size<=payload.size());
        const size_t copy=std::min(payload_size,size-8);
        std::memcpy(payload.data(),bytes+8,copy);payload_offset=copy;
    }
    if(bytes[2]==CORSAIR_V2_CMD_BLK_WN)
    {
        const size_t copy=std::min(payload_size-payload_offset,size-4);
        std::memcpy(payload.data()+payload_offset,bytes+4,copy);payload_offset+=copy;
    }
    return static_cast<int>(size);
}
// A timeout does not overwrite the caller's output buffer.
extern "C" int hid_read_timeout(hid_device*,unsigned char*,size_t,int){return 0;}
extern "C" int hid_get_serial_number_string(hid_device*,wchar_t*,size_t){return -1;}
extern "C" void hid_close(hid_device*){}
LogManager::LogManager(){}LogManager::~LogManager(){}
LogManager* LogManager::get(){static LogManager logger;return &logger;}
void LogManager::append(const char*,int,unsigned int,const char*,...){}
unsigned int LogManager::getLoglevel(){return 0;}
struct Driver:CorsairPeripheralV2HWController
{
    Driver():CorsairPeripheralV2HWController(nullptr,"fixture","fixture"){}
    void endpoint(int value){light_ctrl=static_cast<uint8_t>(value);}
};
int main()
{
    Driver driver;
    for(int endpoint:{CORSAIR_V2_LIGHT_CTRL1,CORSAIR_V2_LIGHT_CTRL2})
    {
        driver.endpoint(endpoint);
        for(size_t count:{0U,1U,115U,512U,3U,115U})
        {
            std::vector<RGBColor> colors(count);
            std::vector<RGBColor*> pointers;
            for(size_t i=0;i<count;++i){colors[i]=ToRGBColor(i%256,(i*3)%256,(i*7)%256);pointers.push_back(&colors[i]);}
            driver.SetLedsDirect(pointers);
            const size_t prefix=endpoint==CORSAIR_V2_LIGHT_CTRL2?2:0;
            assert(payload_size==count*3+prefix && payload_offset==payload_size);
            if(prefix){assert(payload[0]==0x12 && payload[1]==0);}
            for(size_t i=0;i<count;++i)
            {
                const size_t r=prefix?prefix+3*i:i;
                const size_t g=prefix?r+1:count+i;
                const size_t b=prefix?r+2:2*count+i;
                assert(payload[r]==RGBGetRValue(colors[i]));
                assert(payload[g]==RGBGetGValue(colors[i]));
                assert(payload[b]==RGBGetBValue(colors[i]));
            }
            // ThreadSanitizer supplies its own allocation operators. The other
            // builds enforce the allocation budget; every build checks the bytes.
#ifndef OPENRGB_FIXED_TSAN
            allocations=0;count_allocations=true;
            // MSVC Debug also allocates an iterator proxy for a vector. Measure
            // one public argument copy using this build's standard library.
            for(int frame=0;frame<20;++frame)assert(PublicArgumentCopy(pointers)==pointers.size());
            const size_t copy_budget=allocations.load();
            allocations=0;
            for(int frame=0;frame<20;++frame)driver.SetLedsDirect(pointers);
            count_allocations=false;
            assert(allocations.load()<=copy_budget);
#endif
        }
    }
}
