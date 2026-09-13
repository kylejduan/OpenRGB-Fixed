/*---------------------------------------------------------*\
| LogitechProtocolCommon.cpp                                |
|                                                           |
|   Common functionality for Logitech RAP and FAP protocols |
|                                                           |
|   Chris M (Dr_No)                             04 May 2021 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <LogitechProtocolCommon.h>
#include <chrono>

const char* logitech_led_locations[] =
{
    "Unknown",
    "Primary",
    "Logo",
    "Left",
    "Right",
    "Combined",
    "Group One",
    "Group Two",
    "Group Three",
    "Group Four",
    "Group Five",
    "Group Six"
};

const int NUM_LOGITECH_LED_LOCATIONS = sizeof(logitech_led_locations);

static std::vector<uint16_t> logitech_RGB_pages =
{
    LOGITECH_HIDPP_PAGE_RGB_EFFECTS1,
    LOGITECH_HIDPP_PAGE_RGB_EFFECTS2
};

int getWirelessDevice(usages device_usages, uint16_t pid, wireless_map *wireless_devices)
{
    hid_device* dev_use1;
    usages::iterator find_usage = device_usages.find(1);
    if (find_usage == device_usages.end())
    {
        LOG_INFO("Unable get_Wireless_Device due to missing FAP Short Message (0x10) usage");
        LOG_DEBUG("Dumping device usages:");
        for(usages::iterator dev = device_usages.begin(); dev != device_usages.end(); dev++)
        {
            LOG_DEBUG("Usage index:\t%i", dev->first);
        }
    }
    else
    {
        dev_use1 = find_usage->second.get();
        /*-----------------------------------------------------------------*\
        | Create a buffer for reads                                         |
        \*-----------------------------------------------------------------*/
        blankFAPmessage response;
        response.init();

        shortFAPrequest get_connected_devices;
        get_connected_devices.init(LOGITECH_RECEIVER_DEVICE_INDEX, LOGITECH_GET_REGISTER_REQUEST);

        hid_write(dev_use1, get_connected_devices.buffer, get_connected_devices.size());
        hid_read_timeout(dev_use1, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        bool wireless_notifications = response.data[1] & 1;  //Connected devices is a flag

        if (!wireless_notifications)
        {
            response.init(); //zero out the response
            get_connected_devices.init(LOGITECH_RECEIVER_DEVICE_INDEX, LOGITECH_SET_REGISTER_REQUEST);
            get_connected_devices.data[1] = 1;
            hid_write(dev_use1, get_connected_devices.buffer, get_connected_devices.size());
            hid_read_timeout(dev_use1, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);

            if(get_connected_devices.feature_index == 0x8F)
            {
                LOG_ERROR("Logitech Protocol error: %02X %02X %02X %02X %02X %02X %02X", get_connected_devices.report_id, get_connected_devices.device_index, get_connected_devices.feature_index, get_connected_devices.feature_command, get_connected_devices.data[0], get_connected_devices.data[1], get_connected_devices.data[2]);
            }
        }

        response.init(); //zero out the response
        get_connected_devices.init(LOGITECH_RECEIVER_DEVICE_INDEX, LOGITECH_GET_REGISTER_REQUEST);
        get_connected_devices.feature_command       = 0x02;    //0x02 Connection State register. Essentially asking for count of paired devices
        hid_write(dev_use1, get_connected_devices.buffer, get_connected_devices.size());
        hid_read_timeout(dev_use1, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);

        unsigned int device_count = response.data[1];
        LOG_INFO("Count of connected devices to %4X: %i", pid, device_count);

        if (device_count > 0)
        {
            LOG_INFO("Faking a reconnect to get device list");
            device_count++;     //Add 1 to the device_count to include the receiver

            response.init();
            get_connected_devices.init(LOGITECH_RECEIVER_DEVICE_INDEX, LOGITECH_SET_REGISTER_REQUEST);
            get_connected_devices.feature_index     = LOGITECH_SET_REGISTER_REQUEST;
            get_connected_devices.feature_command   = 0x02;    //0x02 Connection State register
            get_connected_devices.data[0]           = 0x02;    //Writting 0x02 to the connection state register will ask the receiver to fake a reconnect of paired devices
            hid_write(dev_use1, get_connected_devices.buffer, get_connected_devices.size());

            for(size_t i = 0; i < device_count; i++)
            {
                blankFAPmessage devices;
                devices.init();

                hid_read_timeout(dev_use1, devices.buffer, devices.size(), LOGITECH_PROTOCOL_TIMEOUT);
                unsigned int wireless_PID = (devices.data[2] << 8) | devices.data[1];
                LOG_INFO("Connected Device Index %i:\tVirtualID=%04X\t\t%02X %02X %02X %02X %02X %02X %02X", i, wireless_PID, devices.buffer[0], devices.buffer[1], devices.buffer[2], devices.buffer[3], devices.buffer[4], devices.buffer[5], devices.buffer[6]);

                /*-----------------------------------------------------------------*\
                | We need to read the receiver from the HID device queue but        |
                |    there is no need to add it as it's own device                  |
                \*-----------------------------------------------------------------*/
                if(devices.device_index != LOGITECH_RECEIVER_DEVICE_INDEX)
                {
                    wireless_devices->emplace(wireless_PID, devices.device_index);
                }
            }
        }
        else
        {
            LOG_WARNING("No devices were found connected to receiver!");
        }
    }

    return((int)wireless_devices->size());
}

logitech_device::logitech_device(char *path, usages _usages, uint8_t _device_index, bool _wireless)
{
    device_index        = _device_index;
    location            = path;
    device_usages       = _usages;
    wireless            = _wireless;
    RGB_feature_index   = 0;
    mutex               = nullptr;

    initialiseDevice();
}

logitech_device::logitech_device(char *path, usages _usages, uint8_t _device_index, bool _wireless, std::shared_ptr<std::mutex> mutex_ptr)
{
    device_index        = _device_index;
    location            = path;
    device_usages       = _usages;
    wireless            = _wireless;
    RGB_feature_index   = 0;
    mutex               = mutex_ptr;

    initialiseDevice();
}

logitech_device::~logitech_device() = default;

void logitech_device::initialiseDevice()
{
    bool is_connected = connected();
    flushReadQueue();

    if(is_connected)
    {
        getDeviceName();

        /*-----------------------------------------------------------------*\
        | If this is running with DEBUG or higher loglevel then             |
        |   dump the entire Feature list to log                             |
        \*-----------------------------------------------------------------*/
        if(LogManager::get()->getLoglevel() > 4)
        {
            getDeviceFeatureList();     //This will populate the feature list
        }

        /*-----------------------------------------------------------------*\
        | Check device for known RGB Effects Feature pages & save the index |
        \*-----------------------------------------------------------------*/
        for(std::vector<uint16_t>::iterator page = logitech_RGB_pages.begin(); page != logitech_RGB_pages.end(); page++)
        {
            int feature_index = getFeatureIndex(*page);
            if(feature_index > 0)
            {
                feature_list.emplace(*page, feature_index);
                RGB_feature_index = feature_index;
                break;
            }
        }

        /*-----------------------------------------------------------------*\
        | If there was no RGB Effect Feature  page found                    |
        |   dump the entire Feature list to log                             |
        \*-----------------------------------------------------------------*/
        if (RGB_feature_index == 0)
        {
            LOG_INFO("[%s] Unable add this device due to missing RGB Effects Feature", device_name.c_str());
        }
        else
        {
            getRGBconfig();
        }
    }
}

bool logitech_device::is_valid()
{
    bool is_connected = connected();
    bool valid_test = false;

    if(is_connected)
    {
        LOG_DEBUG("[%s] valid_test - type %i   led_count - %i   RGB_index - %i", device_name.c_str(), logitech_device_type, leds.size(), RGB_feature_index);

        valid_test          = !device_name.empty()                                          // Check if device name exists
                            && logitech_device_type <= 8                                    // Check if device type has a valid index
                            && (device_name[0] >= 32 && device_name[0] < 122)               // Check for non valid characters in device name
                            && device_name.length() > 3                                     // Check for valid device names lenght
                            && leds.size() > 0                                              // Check if a device has at least 1 led
                            && RGB_feature_index > 0;                                       // Check if a feature index is "valid"
    }
    else
    {
        LOG_INFO("Unable add this Logitech device: Not Connected");
    }

    return(valid_test);
}

bool logitech_device::connected()
{
    /*-----------------------------------------------------------------*\
    | If this is a wireless device test that it's connected             |
    |   Wired devices will always be connected.                         |
    \*-----------------------------------------------------------------*/
    if(wireless)
    {
        bool test = false;
        hid_device* dev_use1 = getDevice(1);

        if(dev_use1)
        {
            shortFAPrequest get_connected_devices;
            get_connected_devices.init(device_index, LOGITECH_GET_REGISTER_REQUEST);
            get_connected_devices.feature_command = 0x02;    //0x02 Connection State register. Essentially asking for count of paired devices

            hid_write(dev_use1, get_connected_devices.buffer, get_connected_devices.size());
            //This hid_read will not timeout as we need to be sure the wireless device is connected
            hid_read(dev_use1, get_connected_devices.buffer, get_connected_devices.size());
            test = (get_connected_devices.data[1] != 0x09);  //ERR_RESOURCE_ERROR i.e. not currently connected
            LOG_DEBUG("Wireless device index %i is %s - %02X %02X %02X", get_connected_devices.device_index,
                      (test ? "connected" : "disconnected"), get_connected_devices.data[0], get_connected_devices.data[1],  get_connected_devices.data[2]);
        }
        return(test);
    }
    else
    {
        return(true);
    }
}

uint8_t logitech_device::getLED_count()
{
    return((uint8_t)leds.size());
}

logitech_led logitech_device::getLED_info(uint8_t LED_num)
{
    /*-----------------------------------------------------------------*\
    | Get all info about the LEDs and Zones                             |
    \*-----------------------------------------------------------------*/
    if(!(LED_num > leds.size()))
    {
        return(leds[LED_num]);
    }
    else
    {
        return(leds[0]);
    }
}

void logitech_device::flushReadQueue()
{
    /*-----------------------------------------------------------------*\
    | Create a buffer for reads                                         |
    \*-----------------------------------------------------------------*/
    blankFAPmessage response;
    response.init();

    for(usages::iterator dev = device_usages.begin(); dev != device_usages.end(); dev++)
    {
        //Flush the buffer
        int flushed     = 0;
        int result      = 1;

        while( result > 0 )
        {
            result = hid_read_timeout(dev->second.get(), response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
            if (result > 0)
            {
                flushed++;
            }
        }
        //device_name has not yet been set so can not use it in the log
        LOG_DEBUG("Preparing read queue for device %i - flushed %i packet%s", dev->first, flushed, ((flushed == 1) ? "" : "s"));
    }
}

hid_device* logitech_device::getDevice(uint8_t usage_index)
{
    /*-----------------------------------------------------------------*\
    | Check the usage map for usage_index                               |
    |   Return the associated device if found otherwise a nullptr       |
    \*-----------------------------------------------------------------*/
#ifdef WIN32
    usages::iterator find_usage = device_usages.find(usage_index);
#else
    //Linux does not need bundle the device usages hence .begin()
    usages::iterator find_usage = device_usages.begin();
#endif //WIN32

    if (find_usage == device_usages.end())
    {
        LOG_INFO("Unable add this device due to missing FAP Message usage %i", usage_index);
        return(nullptr);
    }
    else
    {
        return(find_usage->second.get());
    }
}

uint8_t logitech_device::getFeatureIndex(uint16_t feature_page)
{
    /*-----------------------------------------------------------------*\
    | Get the feature index from the Root Index                         |
    |   Return the mapped feature_index of the given feature page       |
    |   for this device or else return 0                                |
    \*-----------------------------------------------------------------*/
    uint8_t     feature_index   = 0;
    hid_device* dev_use2        = getDevice(2);

    if(dev_use2)
    {
        blankFAPmessage response;
        longFAPrequest get_index;
        get_index.init(device_index, LOGITECH_HIDPP_PAGE_ROOT_IDX, LOGITECH_CMD_ROOT_GET_FEATURE);
        get_index.data[0]       = feature_page >> 8;
        get_index.data[1]       = feature_page & 0xFF;

        hid_write(dev_use2, get_index.buffer, get_index.size());
        hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);

        feature_index           = response.data[0];

        LOG_DEBUG("[%s] Feature Page %04X found @ index %02X - %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), feature_page, feature_index,
                response.data[0], response.data[1],  response.data[2],  response.data[3], response.data[4], response.data[5],  response.data[6],  response.data[7]);
    }

    return(feature_index);
}

uint16_t logitech_device::getFeaturePage(uint8_t feature_index)
{
    /*-----------------------------------------------------------------*\
    | Get the feature page from the feature_list                        |
    |   Return the mapped feature page given the feature index          |
    |   for this device or else return 0                                |
    \*-----------------------------------------------------------------*/
    rvrse_features rvrse_feature_list = reverse_map(feature_list);

    rvrse_features::iterator find_page = rvrse_feature_list.find(feature_index);
    if (find_page == rvrse_feature_list.end())
    {
        LOG_DEBUG("[%s] Feature index %02X not found!", device_name.c_str(), feature_index);

        //TODO: Handle cache miss
        return(0);
    }
    else
    {
        return(find_page->second);
    }
}

int logitech_device::getDeviceFeatureList()
{
    /*-----------------------------------------------------------------*\
    | Check the usage map for usage2 (0x11 Long FAP Message)            |
    |   then list all features for device                               |
    \*-----------------------------------------------------------------*/
    hid_device* dev_use2 = getDevice(2);

    if(dev_use2)
    {
        /*-----------------------------------------------------------------*\
        | Create a buffer for reads                                         |
        \*-----------------------------------------------------------------*/
        blankFAPmessage response;
        response.init();

        /*-----------------------------------------------------------------*\
        | Query the root index for the index of the feature list            |
        |   This is done for safety as it is generaly at feature index 0x01 |
        \*-----------------------------------------------------------------*/
        int feature_index = getFeatureIndex(LOGITECH_HIDPP_PAGE_FEATURE_SET);

        /*-----------------------------------------------------------------*\
        | Get the count of Features                                         |
        \*-----------------------------------------------------------------*/
        longFAPrequest get_count;
        get_count.init(device_index, feature_index, LOGITECH_CMD_FEATURE_SET_GET_COUNT);

        hid_write(dev_use2, get_count.buffer, get_count.size());
        hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        unsigned int feature_count = response.data[0];

        longFAPrequest get_features;
        get_features.init(device_index, feature_index, LOGITECH_CMD_FEATURE_SET_GET_ID);
        for(std::size_t i = 1; feature_list.size() < feature_count; i++ )
        {
            get_features.data[0] = (uint8_t)i;
            hid_write(dev_use2, get_features.buffer, get_features.size());
            hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
            LOG_DEBUG("[%s] Feature %04X @ index: %02X", device_name.c_str(), (response.data[0] << 8) | response.data[1], i);
            feature_list.emplace((uint16_t)((response.data[0] << 8) | response.data[1]), (uint8_t)i);
        }
    }
    else
    {
        LOG_INFO("[%s] Unable get the feature index list - missing FAP Long Message (0x11) usage", device_name.c_str());
    }

    return((int)feature_list.size());
}

int logitech_device::getDeviceName()
{
    /*-----------------------------------------------------------------*\
    | Check the usage map for usage2 (0x11 Long FAP Message)            |
    |   Then use it to get the name for this device                     |
    \*-----------------------------------------------------------------*/
    hid_device* dev_use2 = getDevice(2);

    if(dev_use2)
    {
        /*-----------------------------------------------------------------*\
        | Create a buffer for reads                                         |
        \*-----------------------------------------------------------------*/
        blankFAPmessage response;
        response.init();

        /*-----------------------------------------------------------------*\
        | Query the root index for the index of the name feature            |
        \*-----------------------------------------------------------------*/
        int feature_index = getFeatureIndex(LOGITECH_HIDPP_PAGE_DEVICE_NAME_TYPE);

        /*-----------------------------------------------------------------*\
        | Get the device name length                                        |
        \*-----------------------------------------------------------------*/
        if(feature_index > 0)
        {
            longFAPrequest get_length;
            get_length.init(device_index, feature_index, LOTITECH_CMD_DEVICE_NAME_TYPE_GET_COUNT);
            hid_write(dev_use2, get_length.buffer, get_length.size());
            hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
            unsigned int name_length = response.data[0];
            LOG_DEBUG("[%s] Name Length %02i - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), name_length,
                response.data[0], response.data[1],  response.data[2],  response.data[3],  response.data[4],  response.data[5],  response.data[6],  response.data[7],
                response.data[8], response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);

            longFAPrequest get_name;
            get_name.init(device_index, feature_index, LOGITECH_CMD_DEVICE_NAME_TYPE_GET_DEVICE_NAME);
            while(device_name.length() < name_length)
            {
                get_name.data[0] = (uint8_t)device_name.length();   //This sets the character index to get from the device
                hid_write(dev_use2, get_name.buffer, get_name.size());
                hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                std::string temp = (char *)&response.data;
                device_name.append(temp);
                LOG_DEBUG("[%s] Get Name %02i - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), device_name.length(),
                    response.data[0], response.data[1],  response.data[2],  response.data[3],  response.data[4],  response.data[5],  response.data[6],  response.data[7],
                    response.data[8], response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);
            }

            get_name.init(device_index, feature_index, LOGITECH_CMD_DEVICE_NAME_TYPE_GET_TYPE);
            hid_write(dev_use2, get_name.buffer, get_name.size());
            hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
            logitech_device_type = response.data[0];
            LOG_DEBUG("[%s] Get Type %02i - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), logitech_device_type,
                response.data[0], response.data[1],  response.data[2],  response.data[3],  response.data[4],  response.data[5],  response.data[6],  response.data[7],
                response.data[8], response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);
        }
    }

    return((int)device_name.length());
}

void logitech_device::getRGBconfig()
{
    /*-----------------------------------------------------------------*\
    | Check the usage map for usage2 (0x11 Long FAP Message)            |
    |   Then use it to get the name for this device                     |
    \*-----------------------------------------------------------------*/
    hid_device* dev_use2    = getDevice(2);
    uint16_t feature_page   = getFeaturePage(RGB_feature_index);
    uint8_t led_response    = 0;
    uint8_t led_counter     = 0;

    if(dev_use2)
    {
        /*-----------------------------------------------------------------*\
        | Create a buffer for reads                                         |
        \*-----------------------------------------------------------------*/
        blankFAPmessage response;
        response.init();
        int result;

        longFAPrequest get_count;
        get_count.init(device_index, RGB_feature_index, LOGITECH_CMD_RGB_EFFECTS_GET_COUNT);

        if(feature_page == LOGITECH_HIDPP_PAGE_RGB_EFFECTS1)
        {
            result = hid_write(dev_use2, get_count.buffer, get_count.size());
            do
            {
                result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                LOG_DEBUG("[%s] FP8070 - LED Count - %02X :   %04X %04X %04X %04X   %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(),
                    response.data[0], (response.data[1] << 8 | response.data[2]), (response.data[3] << 8 | response.data[4]), (response.data[5] << 8 | response.data[6]),
                    (response.data[7] << 8 | response.data[8]), response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);
            } while ((result == 20) && (get_count.feature_index != response.feature_index) && (get_count.feature_command != response.feature_command));

            led_response = response.data[0];

            get_count.feature_command = LOGITECH_CMD_RGB_EFFECTS_GET_INFO;
            for(size_t i = 0; i < led_response; i++)
            {
                get_count.data[0] = (uint8_t)i;
                result = hid_write(dev_use2, get_count.buffer, get_count.size());
                result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                LOG_DEBUG("[%s] FP8070 - LED %02i - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), get_count.data[0],
                    response.data[0], response.data[1],  response.data[2],  response.data[3],  response.data[4],  response.data[5],  response.data[6],  response.data[7],
                    response.data[8], response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);
                if( result == 20 &&
                    get_count.feature_index == response.feature_index &&
                    get_count.feature_command == response.feature_command &&
                    response.data[0] != 0x10 &&
                    response.data[1] != 0x02
                  )
                {
                    //If the response is the correct length (i.e. no USB error) and is for the RGB_feature_index and LOGITECH_CMD_RGB_EFFECTS_GET_INFO and no error occured with the led_counter then bump the counter
                    logitech_led new_led;

                    new_led.location    = response.data[1] << 8 | response.data[2];
                    new_led.fx_count    = response.data[3];

                    for(uint8_t i = 0; i < new_led.fx_count; i++)
                    {
                        blankFAPmessage fx_response;
                        fx_response.init();

                        longFAPrequest get_effect;
                        get_effect.init(device_index, RGB_feature_index, LOGITECH_CMD_RGB_EFFECTS_GET_CONTROL);

                        get_effect.data[0] = get_count.data[0];
                        get_effect.data[1] = i;
                        result = hid_write(dev_use2, get_effect.buffer, get_effect.size());
                        result = hid_read_timeout(dev_use2, fx_response.buffer, fx_response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                        LOG_DEBUG("[%s] FP8070 - LED %02i Effect %02X - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), get_count.data[0], i,
                            fx_response.data[0], fx_response.data[1],  fx_response.data[2],  fx_response.data[3],  fx_response.data[4],  fx_response.data[5],  fx_response.data[6],  fx_response.data[7],
                            fx_response.data[8], fx_response.data[9], fx_response.data[10], fx_response.data[11], fx_response.data[12], fx_response.data[13], fx_response.data[14], fx_response.data[15]);

                        logitech_fx new_fx;

                        new_fx.index    = i;
                        new_fx.mode     = static_cast<LOGITECH_DEVICE_MODE>(fx_response.data[2] << 8 | fx_response.data[3]);
                        new_fx.speed    = fx_response.data[6] << 8 | fx_response.data[7];

                        new_led.fx.push_back(new_fx);
                    }

                    leds.emplace(response.data[0], new_led);
                }
            }
        }
        else if(feature_page == LOGITECH_HIDPP_PAGE_RGB_EFFECTS2)
        {
            get_count.data[0] = 0xFF;
            get_count.data[1] = 0xFF;
            get_count.data[2] = 0;
            get_count.data[3] = 0;
            get_count.data[4] = 0;

            result = hid_write(dev_use2, get_count.buffer, get_count.size());
            result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
            LOG_DEBUG("[%s] FP8071 - LED Count - %04X :   %02X %04X %04X %04X %04X   %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(),
                (response.data[1] << 8 | response.data[2]), response.data[0], (response.data[1] << 8 | response.data[2]), (response.data[3] << 8 | response.data[4]), (response.data[5] << 8 | response.data[6]),
                (response.data[7] << 8 | response.data[8]), response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);

            led_response = (response.data[1] << 8 | response.data[2]);
            for(size_t i = 0; i < led_response; i++)
            {
                get_count.data[0] = (uint8_t)i;
                get_count.data[1] = 0xFF;
                get_count.data[2] = 0;

                result = hid_write(dev_use2, get_count.buffer, get_count.size());
                result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                LOG_DEBUG("[%s] FP8071 - LED %02i - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), i,
                    response.data[0], response.data[1],  response.data[2],  response.data[3],  response.data[4],  response.data[5],  response.data[6],  response.data[7],
                    response.data[8], response.data[9], response.data[10], response.data[11], response.data[12], response.data[13], response.data[14], response.data[15]);

                logitech_led new_led;

                new_led.location    = response.data[2] << 8 | response.data[3];
                new_led.fx_count    = response.data[4];

                for(uint8_t i = 0; i < new_led.fx_count; i++)
                {
                    blankFAPmessage fx_response;
                    fx_response.init();

                    longFAPrequest get_effect;
                    get_effect.init(device_index, RGB_feature_index, LOGITECH_CMD_RGB_EFFECTS_GET_COUNT);

                    get_effect.data[0] = get_count.data[0];
                    get_effect.data[1] = i;
                    result = hid_write(dev_use2, get_effect.buffer, get_effect.size());
                    result = hid_read_timeout(dev_use2, fx_response.buffer, fx_response.size(), LOGITECH_PROTOCOL_TIMEOUT);
                    LOG_DEBUG("[%s] FP8071 - LED %02i Effect %02X - %02X %02X %02X %02X %02X %02X %02X %02X   %02X %02X %02X %02X %02X %02X %02X %02X", device_name.c_str(), get_count.data[0], i,
                        fx_response.data[0], fx_response.data[1],  fx_response.data[2],  fx_response.data[3],  fx_response.data[4],  fx_response.data[5],  fx_response.data[6],  fx_response.data[7],
                        fx_response.data[8], fx_response.data[9], fx_response.data[10], fx_response.data[11], fx_response.data[12], fx_response.data[13], fx_response.data[14], fx_response.data[15]);

                    logitech_fx new_fx;

                    new_fx.index    = i;
                    new_fx.mode     = static_cast<LOGITECH_DEVICE_MODE>(fx_response.data[2] << 8 | fx_response.data[3]);
                    new_fx.speed    = fx_response.data[6] << 8 | fx_response.data[7];

                    new_led.fx.push_back(new_fx);
                }

                leds.emplace(response.data[0], new_led);
            }
            /*-----------------------------------------------------------------*\
            | Set the config to SW control mode                                 |
            \*-----------------------------------------------------------------*/
            set8071Effects(5);
        }

        /*get_count.feature_command = LOGITECH_CMD_RGB_EFFECTS_GET_STATE;
        for(std::size_t i = 0; i < feature_count; i++ )
        {
            get_count.data[0] = i;
            result = hid_write(dev_use2, get_count.buffer, get_count.size());
            result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        }

        get_count.feature_command = LOGITECH_CMD_RGB_EFFECTS_GET_CONFIG;
        for(std::size_t i = 0; i < feature_count; i++ )
        {
            get_count.data[0] = i;
            result = hid_write(dev_use2, get_count.buffer, get_count.size());
            result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        }*/
    }

    LOG_DEBUG("[%s] led_response returned %i led_counter returned %i : setting controller to %i LED%s", device_name.c_str(), led_response, led_counter, leds.size(), ((leds.size() == 1) ? "" : "s"));
}

int logitech_device::sendLightingRequest(hid_device* dev, longFAPrequest& request, blankFAPmessage& response, bool match_selector)
{
    // Discard queued replies from earlier transactions. The shared receiver
    // lock means no sibling has an in-flight request on this handle.
    for(int queued = 0; ; queued++)
    {
        const int drained = hid_read_timeout(dev, response.buffer, response.size(), 0);
        if(drained == 0) break;
        if(queued == 64)
        {
            LOG_WARNING("[%s] Lighting input queue did not drain", device_name.c_str());
            return -1;
        }
        if(drained < 0)
        {
            LOG_WARNING("[%s] Lighting HID queue read failed", device_name.c_str());
            return -1;
        }
    }
    // Match the newer OpenRGB HIDPP20 driver's software identity. Zero is
    // reserved for events; other applications use their own nonzero IDs.
    request.feature_command = (request.feature_command & 0xF0) | 0x07;
    response.init();
    if(hid_write(dev, request.buffer, request.size()) != request.size())
    {
        LOG_WARNING("[%s] Lighting HID write failed", device_name.c_str());
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(LOGITECH_PROTOCOL_TIMEOUT);
    while(std::chrono::steady_clock::now() < deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if(remaining <= 0) break;
        response.init();
        int result = hid_read_timeout(dev, response.buffer, response.size(), static_cast<int>(remaining));
        if(result < 0)
        {
            LOG_WARNING("[%s] Lighting HID read failed", device_name.c_str());
            return -1;
        }
        if(result < 4 || response.device_index != request.device_index ||
           (response.report_id != LOGITECH_LONG_MESSAGE && response.report_id != LOGITECH_SHORT_MESSAGE))
        {
            continue;
        }
        if((response.feature_index == 0xFF || response.feature_index == 0x8F) && result >= 7 &&
           response.feature_command == request.feature_index && response.data[0] == request.feature_command)
        {
            LOG_WARNING("[%s] Lighting HID++ error %02X for %02X/%02X",
                        device_name.c_str(), response.data[1], request.feature_index, request.feature_command);
            return -1;
        }
        if(response.feature_index != request.feature_index || response.feature_command != request.feature_command)
        {
            continue;
        }
        const int report_size = response.report_id == LOGITECH_SHORT_MESSAGE
                              ? LOGITECH_SHORT_MESSAGE_LEN : LOGITECH_LONG_MESSAGE_LEN;
        if(result < report_size)
        {
            LOG_WARNING("[%s] Truncated lighting reply (%i bytes)", device_name.c_str(), result);
            return -1;
        }
        if(match_selector && response.data[0] != request.data[0])
        {
            continue; // 0x8071 get/set share a function but echo this selector.
        }
        return result;
    }
    LOG_WARNING("[%s] No matching lighting reply for %02X/%02X before timeout",
                device_name.c_str(), request.feature_index, request.feature_command);
    return -1;
}

int logitech_device::prepare8071Lighting(hid_device* dev)
{
    // Re-query actual state: firmware or another host can release our claim
    // after detection. Do not reset a claim that is already held.
    longFAPrequest request;
    blankFAPmessage response;
    request.init(device_index, RGB_feature_index, LOGITECH_FP8071_CONTROL);
    int result = sendLightingRequest(dev, request, response, true);
    if(result < 0) return result;
    if(response.data[0] != 0) return -1;
    if((response.data[1] & 3) != 3)
    {
        request.data[0] = 1;
        request.data[1] = 3; // RGB clusters and power modes.
        request.data[2] = 5; // Use the same event flags as detection.
        result = sendLightingRequest(dev, request, response, true);
        if(result < 0) return result;
        request.init(device_index, RGB_feature_index, LOGITECH_FP8071_CONTROL);
        result = sendLightingRequest(dev, request, response, true);
        if(result < 0) return result;
        if(response.data[0] != 0 || (response.data[1] & 3) != 3)
        {
            LOG_WARNING("[%s] Software lighting control was not acquired", device_name.c_str());
            return -1;
        }
    }

    request.init(device_index, RGB_feature_index, LOGITECH_FP8071_PWR_MODE);
    result = sendLightingRequest(dev, request, response, true);
    if(result < 0) return result;
    if(response.data[0] != 0 || response.data[1] < 1 || response.data[1] > 3) return -1;
    if(response.data[1] != 1)
    {
        request.data[0] = 1;
        request.data[1] = 1; // An explicit color command wakes the RGB engine.
        result = sendLightingRequest(dev, request, response, true);
        if(result < 0) return result;
        request.init(device_index, RGB_feature_index, LOGITECH_FP8071_PWR_MODE);
        result = sendLightingRequest(dev, request, response, true);
        if(result < 0) return result;
        if(response.data[0] != 0 || response.data[1] != 1)
        {
            LOG_WARNING("[%s] RGB power mode was not restored", device_name.c_str());
            return -1;
        }
    }
    return result;
}

int logitech_device::setDirectMode(bool direct)
{
    hid_device* dev = getDevice(2);
    if(!dev) return -1;
    std::unique_lock<std::mutex> guard;
    if(mutex) guard = std::unique_lock<std::mutex>(*mutex);
    const uint16_t page = getFeaturePage(RGB_feature_index);
    if(page == LOGITECH_HIDPP_PAGE_RGB_EFFECTS2)
    {
        // On 0x8071 fn8 controls RGB power, not software ownership. All
        // host-selected effects require fn5 ownership, including Static.
        return prepare8071Lighting(dev);
    }
    if(page != LOGITECH_HIDPP_PAGE_RGB_EFFECTS1) return -1;
    longFAPrequest request;
    blankFAPmessage response;
    request.init(device_index, RGB_feature_index, LOGITECH_FP8070_SET_SW_CTL);
    request.data[0] = direct ? 1 : 0;
    request.data[1] = request.data[0];
    return sendLightingRequest(dev, request, response);
}

int logitech_device::setMode(uint8_t mode, uint16_t speed, uint8_t zone, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness)
{
    hid_device* dev = getDevice(2);
    const uint16_t page = getFeaturePage(RGB_feature_index);
    const auto led = leds.find(zone);
    if(!dev || led == leds.end() || mode >= led->second.fx.size() ||
       (page != LOGITECH_HIDPP_PAGE_RGB_EFFECTS1 && page != LOGITECH_HIDPP_PAGE_RGB_EFFECTS2))
    {
        return -1;
    }
    // Both the mat and mouse use this handle. Keep the entire claim/power/
    // color transaction serialized, not just individual writes.
    std::unique_lock<std::mutex> guard;
    if(mutex) guard = std::unique_lock<std::mutex>(*mutex);
    const bool fp8070 = page == LOGITECH_HIDPP_PAGE_RGB_EFFECTS1;
    if(!fp8070 && prepare8071Lighting(dev) < 0) return -1;

    longFAPrequest request;
    blankFAPmessage response;
    request.init(device_index, RGB_feature_index,
                 fp8070 ? static_cast<uint8_t>(LOGITECH_FP8070_SET_EFFECT) : static_cast<uint8_t>(LOGITECH_FP8071_SET_LED_EFFECT));
    request.data[0] = zone;
    request.data[1] = mode;
    request.data[2] = red;
    request.data[3] = green;
    request.data[4] = blue;
    request.data[12] = fp8070 ? 0 : 1; // 0x8071 volatile, full-power effect.
    speed *= 100;
    switch(led->second.fx[mode].mode)
    {
        case LOGITECH_DEVICE_LED_ON:
            // Select the explicit no-ramp static parameter, also used by
            // the HIDPP20 controller, instead of inheriting firmware defaults.
            if(!fp8070) request.data[5] = 2;
            break;
        case LOGITECH_DEVICE_LED_SPECTRUM:
            request.data[7] = speed >> 8;
            request.data[8] = speed & 0xFF;
            request.data[9] = brightness;
            break;
        case LOGITECH_DEVICE_LED_BREATHING:
            request.data[5] = speed >> 8;
            request.data[6] = speed & 0xFF;
            request.data[8] = brightness;
            break;
        default:
            break;
    }
    const int result = sendLightingRequest(dev, request, response);
    LOG_DEBUG("[%s] LED command: zone=%i mode=%i RGB=%02X%02X%02X request=%02X/%02X/%02X reply=%02X/%02X/%02X bytes=%i",
              device_name.c_str(), zone, mode, red, green, blue,
              request.device_index, request.feature_index, request.feature_command,
              response.device_index, response.feature_index, response.feature_command, result);
    return result;
}

int logitech_device::set8071Effects(uint8_t events)
{
    hid_device* dev = getDevice(2);
    if(!dev) return -1;
    std::unique_lock<std::mutex> guard;
    if(mutex) guard = std::unique_lock<std::mutex>(*mutex);
    longFAPrequest request;
    blankFAPmessage response;
    request.init(device_index, RGB_feature_index, LOGITECH_FP8071_CONTROL);
    request.data[0] = 1;
    request.data[1] = 3;
    request.data[2] = events;
    return sendLightingRequest(dev, request, response, true);
}

uint8_t logitech_device::set8071TimeoutControl(uint8_t /*control*/)
{
    int result = 0;

    /*-----------------------------------------------------------------*\
    | Check the usage map for usage2 (0x11 Long FAP Message)            |
    |   then set the device into direct mode via register 0x80          |
    \*-----------------------------------------------------------------*/
    hid_device* dev_use2 = getDevice(2);

    if(dev_use2)
    {
        /*-----------------------------------------------------------------*\
        | Create a buffer for reads                                         |
        \*-----------------------------------------------------------------*/
        blankFAPmessage response;
        response.init();

        /*-----------------------------------------------------------------*\
        | Turn the direct mode on or off via the RGB_feature_index          |
        \*-----------------------------------------------------------------*/
        longFAPrequest set_control;
        set_control.init(device_index, RGB_feature_index, LOGITECH_FP8071_PWR_CFG);
        set_control.data[0] = 1; //1;
        set_control.data[3] = 0; //0x3C;        //Inactive Lighting timeout MSB
        set_control.data[4] = 5; //0x3C;       //Inactive Lighting timeout LSB
        set_control.data[5] = 0; //1;           //Lights off timeout MSB
        set_control.data[6] = 20; //0x2C;        //Lights off timeout LSB

        /*-----------------------------------------------------*\
        | Send packet                                           |
        | This code has to be protected to avoid crashes when   |
        | this is called at the same time to change a powerplay |
        | mat and its paired wireless mouse leds. It will       |
        | happen when using effects engines with high framerate |
        \*-----------------------------------------------------*/
        if(mutex)
        {
            std::lock_guard<std::mutex> guard(*mutex);

            result = hid_write(dev_use2, set_control.buffer, set_control.size());
            result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        }
        else
        {
            result = hid_write(dev_use2, set_control.buffer, set_control.size());
            result = hid_read_timeout(dev_use2, response.buffer, response.size(), LOGITECH_PROTOCOL_TIMEOUT);
        }
    }
    return(result);
}
