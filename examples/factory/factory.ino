/**
 * @file      factory.ino
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
static const int   daylightOffset_sec = 0;
static SemaphoreHandle_t xSemaphore = NULL;

String ssid;
String sifipw;
int8_t timezone;

void instanceLockTake()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) != pdTRUE) {
            log_e("Failed to take semaphore");
            assert(0);
        }
    }
}

void instanceLockGive()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreGive(xSemaphore) != pdTRUE) {
            log_e("Failed to give semaphore");
            assert(0);
        }
    }
}

// Callback function (gets called when time adjusts via NTP)
static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
    // printLocalTime();
    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        instance.rtc.hwClockWrite();
    }
}

// WARNING: This function is called from a separate FreeRTOS task (thread)!
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    user_setting_params_t setting;
    hw_get_user_setting(setting);
    configTime(setting.gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

void setup()
{
    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    xSemaphore = xSemaphoreCreateMutex();
    if (xSemaphore == NULL) {
        log_e("Failed to create mutex");
        assert(0);
    }

    sntp_set_time_sync_notification_cb(time_available);

    // Examples of different ways to register wifi events;
    // these handlers will be called from another thread.
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

    instance.begin(/*NO_HW_LORA|NO_HW_RTC|NO_HW_GPS|NO_HW_LORA*/);

    beginLvglHelper(instance);

    hw_init();

    setupGui();

    Serial.println("Start done. run main loop");
}

extern void loopNFCReader();

void loop()
{
    instanceLockTake();
    instance.loop();
#if defined(USING_ST25R3916)
    loopNFCReader();
#endif
    lv_timer_handler();
    instanceLockGive();
    delay(5);
}

#endif
