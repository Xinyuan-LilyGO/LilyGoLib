/**
 * @file      factory.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#ifdef ARDUINO
#include <LilyGoLog.h>
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include "hal_interface.h"
#include <WiFi.h>
#include "event_define.h"
#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
#include "screen_web_server.h"
#endif

extern void setupGui();
#if defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_WS2812_STRIP)
extern void ui_ws2812_init(void);
#endif

static const char *ntpServer1 = "pool.ntp.org";
static const char *ntpServer2 = "time.nist.gov";
static SemaphoreHandle_t xSemaphore = NULL;

#if defined(USING_BHI260_SENSOR)
static TaskHandle_t bhi260InitTaskHandle = NULL;
#endif

void instanceLockTake()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreTakeRecursive(xSemaphore, portMAX_DELAY) != pdTRUE) {
            LILYGO_LOG_E("Failed to take semaphore");
            assert(0);
        }
    }
}

void instanceLockGive()
{
    if (xSemaphore != NULL) {
        if (xSemaphoreGiveRecursive(xSemaphore) != pdTRUE) {
            LILYGO_LOG_E("Failed to give semaphore");
            assert(0);
        }
    }
}

#if defined(USING_BHI260_SENSOR)
static void deferredBhi260InitTask(void *param)
{
    (void)param;
    LILYGO_LOG_I("Deferred BHI260 init start");

    bool ok = false;
    instanceLockTake();
    if ((instance.getDeviceProbe() & HW_BHI260AP_ONLINE) == 0) {
        ok = instance.initSensor();
    } else {
        ok = true;
    }
    instanceLockGive();

    LILYGO_LOG_I("Deferred BHI260 init %s", ok ? "succeeded" : "failed");
    bhi260InitTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void startDeferredBhi260Init()
{
    if (bhi260InitTaskHandle != NULL) {
        return;
    }

    BaseType_t res = xTaskCreate(deferredBhi260InitTask,
                                 "bhi260/init",
                                 10 * 1024,
                                 NULL,
                                 2,
                                 &bhi260InitTaskHandle);
    if (res != pdPASS) {
        bhi260InitTaskHandle = NULL;
        LILYGO_LOG_E("Failed to create BHI260 init task");
    }
}
#endif

// Callback function (gets called when time adjusts via NTP)
static void time_available(struct timeval *t)
{
    LILYGO_LOG_PRINTLN("Got time adjustment from NTP!");
    // printLocalTime();
    hw_write_rtc_from_system_time();
}

// WARNING: This function is called from a separate FreeRTOS task (thread)!
void WiFiGotIP(arduino_event_t *event)
{
    if (!event || event->event_id != ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        return;
    }

    LILYGO_LOG_PRINTLN("WiFi connected");
    LILYGO_LOG_PRINTLN("IP address: ");
    LILYGO_LOG_PRINTLN(IPAddress(event->event_info.got_ip.ip_info.ip.addr));
    configTime(hw_get_timezone_offset(), hw_get_daylight_offset(), ntpServer1, ntpServer2);
}

void setup()
{
    setCpuFrequencyMhz(240);

    Serial.begin(115200);

    xSemaphore = xSemaphoreCreateRecursiveMutex();
    if (xSemaphore == NULL) {
        LILYGO_LOG_E("Failed to create mutex");
        assert(0);
    }

    sntp_set_time_sync_notification_cb(time_available);

    // Examples of different ways to register wifi events;
    // these handlers will be called from another thread.
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);

#if defined(USING_BHI260_SENSOR)
    // Disabling sensor initialization is time-consuming; 
    // sensor initialization will resume after the display is complete.
    LilyGoDeviceInitOptions initOptions = instance.getDefaultInitOptions();
    initOptions.initSensor = false;
    instance.begin(initOptions);
#else
    instance.begin();
#endif

#if defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_WS2812_STRIP)
    ui_ws2812_init();
#endif

    beginLvglHelper(instance);

    hw_init();

    setupGui();
#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
    screen_web_server_setup();
#endif

#if defined(USING_BHI260_SENSOR)
    startDeferredBhi260Init();
#endif

    LILYGO_LOG_PRINTLN("Start done. run main loop");
}

void loop()
{
    instanceLockTake();
    instance.loop();
#if defined(USING_ST25R3916)
    hw_loop_nfc();
#endif
    lv_timer_handler();
    instanceLockGive();
    delay(5);
}

#endif
