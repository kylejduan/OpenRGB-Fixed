// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real startup negotiation, including a lighting handle retained
// across a host restart and the K70's 1025-byte HID output report.
#include "CorsairPeripheralV2Controller.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

static corsair_v2_device device{};
const unsigned int CORSAIR_V2_DEVICE_COUNT=1;
static const corsair_v2_device* devices[]={&device};
const corsair_v2_device** corsair_v2_device_list=devices;
struct Reply {int length;unsigned char command,status;};
static std::deque<Reply> replies;
static std::string scenario;
static std::vector<unsigned int> endpoints;
static unsigned int closes=0,probe_reads=0;
static bool opened=false,flood=false,jumbo=false,software_mode=false;
static std::vector<unsigned char> payload;
static size_t expected_payload=0;
static volatile unsigned int wire_checksum=0;
extern "C" int hid_write(hid_device*,const unsigned char* bytes,size_t size)
{
    assert(size==65 || size==1025);
    // Reading the whole supplied report makes an oversized write fail under ASan.
    for(size_t i=0;i<size;++i)wire_checksum+=bytes[i];
    const unsigned char command=bytes[2];
    if(command==CORSAIR_V2_CMD_SET && bytes[3]==CORSAIR_V2_VALUE_MODE)
    {
        software_mode=bytes[5]==CORSAIR_V2_MODE_SW;
        replies.push_back({65,command,0});
        return static_cast<int>(size);
    }
    if(command==CORSAIR_V2_CMD_GET)
    {
        replies.push_back({bytes[3]==0x11 && jumbo?1024:65,command,0});
        return static_cast<int>(size);
    }
    if(command==CORSAIR_V2_CMD_START_TX)
    {
        endpoints.push_back(bytes[4]);
        const bool first=endpoints.size()==1;
        if(first && scenario=="write_error")return -1;
        if(first && scenario=="short_write")return 64;
        if(scenario=="flood"){flood=true;return 1025;}
        if((scenario=="silent" || (scenario=="silent_fallback" && bytes[4]==34)))
        {opened=true;replies.push_back({0,command,0});return 1025;}
        if(first && scenario=="timeout"){opened=true;replies.push_back({0,command,0});return 1025;}
        if(first && scenario=="read_error"){opened=true;replies.push_back({-1,command,0});return 1025;}
        if(first && scenario=="stale")replies.push_back({65,CORSAIR_V2_CMD_SET,1});
        if(first && scenario=="short_reply")replies.push_back({2,command,0});
        unsigned char status=0;
        if(opened || !software_mode)status=3;
        if(scenario=="busy_forever")status=bytes[4]==34?3:1;
        if(scenario=="busy_fallback" && bytes[4]==34)status=3;
        if(bytes[4]==CORSAIR_V2_LIGHT_CTRL2 && scenario.substr(0,9)=="fallback_")status=static_cast<unsigned char>(scenario.back()-'0');
        if(status==0)opened=true;
        replies.push_back({jumbo?1024:65,command,status});
        return jumbo?1025:static_cast<int>(size);
    }
    if(command==CORSAIR_V2_CMD_STOP_TX)
    {
        ++closes;opened=false;flood=false;replies.clear();replies.push_back({65,command,0});
    }
    if(command==CORSAIR_V2_CMD_BLK_W1)
    {
        expected_payload=bytes[4]+256U*bytes[5];payload.clear();
        const size_t n=std::min(expected_payload,size-8);payload.insert(payload.end(),bytes+8,bytes+8+n);
        replies.push_back({jumbo?1024:65,command,0});
    }
    if(command==CORSAIR_V2_CMD_BLK_WN)
    {
        const size_t n=std::min(expected_payload-payload.size(),size-4);payload.insert(payload.end(),bytes+4,bytes+4+n);
        replies.push_back({jumbo?1024:65,command,0});
    }
    return static_cast<int>(size);
}
extern "C" int hid_read_timeout(hid_device*,unsigned char* bytes,size_t size,int timeout)
{
    assert(timeout>=0 && timeout<=CORSAIR_V2_TIMEOUT);
    if(flood){++probe_reads;assert(probe_reads<=48);std::memset(bytes,0,size);bytes[1]=CORSAIR_V2_CMD_GET;bytes[2]=1;return 3;}
    if(replies.empty())return 0;
    const Reply reply=replies.front();replies.pop_front();
    if(reply.length<=0)return reply.length;
    const int n=static_cast<int>(std::min(size,static_cast<size_t>(reply.length)));
    std::memset(bytes,0,n);if(n>1)bytes[1]=reply.command;if(n>2)bytes[2]=reply.status;
    return n;
}
extern "C" int hid_get_serial_number_string(hid_device*,wchar_t*,size_t){return -1;}
extern "C" void hid_close(hid_device*){}
LogManager::LogManager(){}LogManager::~LogManager(){}
LogManager* LogManager::get(){static LogManager logger;return &logger;}
void LogManager::append(const char*,int,unsigned int,const char*,...){}
unsigned int LogManager::getLoglevel(){return 0;}
struct Driver:CorsairPeripheralV2Controller
{
    Driver():CorsairPeripheralV2Controller(nullptr,"fixture","fixture"){}
    void SetLedsDirect(std::vector<RGBColor*>)override{}
    unsigned int endpoint()const{return light_ctrl;}
};
int main(int argc,char** argv)
{
    assert(argc==2);scenario=argv[1];opened=scenario=="busy";jumbo=scenario=="max_report";
    software_mode=scenario!="hardware_mode";
    Driver driver;
    assert(software_mode && "Software ownership must precede lighting-resource negotiation");
    if(scenario=="busy_fallback" || scenario=="silent_fallback")
    {
        assert(driver.endpoint()==CORSAIR_V2_LIGHT_CTRL1);
        assert(endpoints==std::vector<unsigned int>({34,34,34,1}));
    }
    else if(scenario.substr(0,9)=="fallback_")
    {
        assert(driver.endpoint()==CORSAIR_V2_LIGHT_CTRL1);
        assert(endpoints==std::vector<unsigned int>({34,1}));
    }
    else
    {
        assert(driver.endpoint()==CORSAIR_V2_LIGHT_CTRL2 && "A busy or unanswered preferred resource is not unsupported");
        if(scenario=="busy_forever")assert(endpoints==std::vector<unsigned int>({34,34,34,1}));
        else if(scenario=="flood" || scenario=="silent")assert(endpoints==std::vector<unsigned int>({34,34,34,1,1,1}));
        else
        {
            assert(std::all_of(endpoints.begin(),endpoints.end(),[](unsigned int e){return e==34;}));
            if(scenario=="busy" || scenario=="timeout" || scenario=="read_error" || scenario=="write_error" || scenario=="short_write")assert(endpoints.size()==2);
            else assert(endpoints.size()==1);
        }
    }
    assert(!opened);
    if(scenario=="busy")assert(closes>=2);
    if(jumbo)
    {
        std::vector<unsigned char> colors(2000);
        for(size_t i=0;i<colors.size();++i)colors[i]=static_cast<unsigned char>(i*7);
        driver.SetLEDs(colors.data(),static_cast<uint16_t>(colors.size()));
        assert(payload==colors);
    }
}
