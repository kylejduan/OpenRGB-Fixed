// SPDX-License-Identifier: GPL-2.0-or-later
// Real Corsair transport. A fake HID returns errors or an endless report stream.
#include "CorsairPeripheralV2Controller.h"
#include <cassert>
#include <cstring>
#include <string>

static corsair_v2_device device{};
const unsigned int CORSAIR_V2_DEVICE_COUNT = 1;
static const corsair_v2_device* devices[] = {&device};
const corsair_v2_device** corsair_v2_device_list = devices;
static bool inject = false, flood = false;
static int reads = 0;

extern "C" int hid_write(hid_device*, const unsigned char*, size_t size) { return static_cast<int>(size); }
extern "C" int hid_read_timeout(hid_device*, unsigned char* bytes, size_t size, int)
{
    std::memset(bytes, 0, size);
    if(!inject) { return 0; }
    assert(++reads < 1000 && "HID drain failed to stop; fail fast without spinning a test process");
    return flood ? 1 : -1;
}
extern "C" int hid_get_serial_number_string(hid_device*, wchar_t*, size_t) { return -1; }
extern "C" void hid_close(hid_device*) {}
LogManager::LogManager() {}
LogManager::~LogManager() {}
LogManager* LogManager::get() { static LogManager logger; return &logger; }
void LogManager::append(const char*, int, unsigned int, const char*, ...) {}
unsigned int LogManager::getLoglevel() { return 0; }

struct Driver : CorsairPeripheralV2Controller
{
    Driver() : CorsairPeripheralV2Controller(nullptr, "fixture", "fixture") {}
    void SetLedsDirect(std::vector<RGBColor*>) override {}
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    flood = std::string(argv[1]) == "flood";
    Driver driver;
    inject = true;
    uint8_t colors[] = {1,2,3};
    driver.SetLEDs(colors, sizeof(colors));
    assert(reads > 0 && reads < 100);
}
