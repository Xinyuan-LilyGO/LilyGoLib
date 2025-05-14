/**
 * @file      factory_pager.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#ifdef ARDUINO
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include "hal_interface.h"
#include <WiFi.h>
#include "event_define.h"

extern void setupGui();

static const char *ntpServer1 = "pool.ntp.org";
static const char *ntpServer2 = "time.nist.gov";
static const uint64_t  gmtOffset_sec = GMT_OFFSET_SECOND;
static const int   daylightOffset_sec = 0;

// Callback function (gets called when time adjusts via NTP)
static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    // printLocalTime();
    instance.rtc.hwClockWrite();
}

// WARNING: This function is called from a separate FreeRTOS task (thread)!
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

void setup()
{

    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    sntp_set_time_sync_notification_cb(time_available);

    // Examples of different ways to register wifi events;
    // these handlers will be called from another thread.
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

#ifdef ARDUINO_T_LORA_PAGER
    extern const uint8_t img_logo_480x222_map[213120];
    instance.setBootImage((uint8_t*)img_logo_480x222_map);
#endif

#ifdef ARDUINO_T_WATCH_S3_ULTRA
    // TODO:
    // extern const uint8_t img_logo_480x222_map[213120];
    // instance.setBootImage(img_logo_480x222_map);
#endif

    instance.begin();

#ifdef USING_INPUT_DEV_KEYBOARD
    instance.attachKeyboardFeedback(true);
#endif

    beginLvglHelper(instance);

    hw_init();

    setupGui();

    Serial.println("Stated done. run main loop");
}


void loop()
{
    instance.loop();
    lv_timer_handler();
    delay(5);
}

#endif
