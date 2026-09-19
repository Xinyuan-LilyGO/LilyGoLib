/**
 * @file      ui_paw_mouse.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-05
 * 
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <math.h>

#if !defined(EXCLUDE_TRACKBALL)

#ifdef ARDUINO
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEAdvertising.h>
#include <NimBLEAdvertisementData.h>
#include <NimBLECharacteristic.h>
#include <NimBLEConnInfo.h>
#include "esp_heap_caps.h"
#include <vector>
#endif

static constexpr int32_t PAW_MOUSE_SENS_MIN = 1;
static constexpr int32_t PAW_MOUSE_SENS_MAX = 24;
static constexpr int32_t PAW_MOUSE_SENS_DEFAULT = 8;
static constexpr uint32_t PAW_MOUSE_SEND_PERIOD_MS = 10;
static constexpr uint32_t PAW_MOUSE_BLE_START_TIMEOUT_MS = 8000;
static constexpr uint16_t PAW_MOUSE_APPEARANCE = 0x03C2;

static lv_obj_t *page_container = NULL;
static lv_timer_t *send_timer = NULL;
static lv_timer_t *ui_timer = NULL;
static lv_obj_t *sensor_unavailable_msgbox = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *sens_label = NULL;
static lv_obj_t *delta_label = NULL;
static lv_obj_t *button_label = NULL;
static lv_obj_t *cursor_dot = NULL;
static int32_t preview_max_x = 0;
static int32_t preview_max_y = 0;
static int32_t preview_x = 0;
static int32_t preview_y = 0;
static int32_t sensitivity = PAW_MOUSE_SENS_DEFAULT;
static float pending_x = 0.0f;
static float pending_y = 0.0f;
static int32_t last_report_x = 0;
static int32_t last_report_y = 0;
static uint32_t motion_count = 0;
static char button_text[16] = "--";

#ifdef ARDUINO
static bool paw_mouse_connected = false;
static bool paw_mouse_active = false;
static volatile bool paw_mouse_app_active = false;
static volatile bool ble_starting = false;
static volatile bool ble_start_failed = false;
static volatile bool ble_start_timeout = false;
static uint32_t ble_start_ms = 0;
static TaskHandle_t ble_start_task_handle = NULL;

static void log_ble_heap(const char *stage)
{
    LILYGO_LOG_PRINTF("PAWMOUSE BLE %s heap=%u internal=%u psram=%u\n",
           stage,
           (unsigned)ESP.getFreeHeap(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

class PawMouseServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        paw_mouse_connected = true;
        server->updateConnParams(connInfo.getConnHandle(), 160, 320, 0, 200);
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        paw_mouse_connected = false;
        if (paw_mouse_active && paw_mouse_app_active) {
            NimBLEDevice::startAdvertising();
        }
    }
};

class PawMouseHid
{
public:
    bool begin(const char *device_name)
    {
        if (started) {
            return true;
        }

        log_ble_heap("begin");
        if (!NimBLEDevice::isInitialized()) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE init begin name=%s\n", device_name);
            if (!NimBLEDevice::init(device_name)) {
                LILYGO_LOG_PRINTF("PAWMOUSE BLE init failed\n");
                return false;
            }
            LILYGO_LOG_PRINTF("PAWMOUSE BLE init ok\n");
        } else {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE reuse existing NimBLE stack\n");
        }

        paw_mouse_connected = false;
        paw_mouse_active = true;
        LILYGO_LOG_PRINTF("PAWMOUSE BLE create server\n");
        server = NimBLEDevice::createServer();
        if (!server) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE create server failed\n");
            paw_mouse_active = false;
            return false;
        }
        server->setCallbacks(&callbacks, false);
        server->advertiseOnDisconnect(false);

        if (!hid) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE create HID\n");
            hid = new NimBLEHIDDevice(server);
            if (!hid) {
                LILYGO_LOG_PRINTF("PAWMOUSE BLE create HID failed\n");
                paw_mouse_active = false;
                return false;
            }

            input = hid->getInputReport(0);
            hid->setManufacturer("LilyGo");
            hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
            hid->setHidInfo(0x00, 0x02);
            hid->setReportMap((uint8_t *)report_map, sizeof(report_map));
            hid->setBatteryLevel(100);
        } else if (!input) {
            input = hid->getInputReport(0);
        }

        LILYGO_LOG_PRINTF("PAWMOUSE BLE server start\n");
        if (!server->start()) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE server start failed\n");
            paw_mouse_active = false;
            return false;
        }

        started = startAdvertising(device_name);
        if (!started) {
            paw_mouse_active = false;
        }

        return started;
    }

    void end()
    {
        paw_mouse_active = false;
        paw_mouse_connected = false;
        if (NimBLEDevice::isInitialized()) {
            NimBLEDevice::stopAdvertising();
            if (server) {
                server->advertiseOnDisconnect(false);
                server->setCallbacks(nullptr, false);
                std::vector<uint16_t> peers = server->getPeerDevices();
                for (uint16_t conn_handle : peers) {
                    server->disconnect(conn_handle);
                }
            }
        }
        started = false;
        connected_buttons = 0;
        paw_mouse_connected = false;
    }

    bool isStarted() const
    {
        return started;
    }

    bool isConnected() const
    {
        return paw_mouse_connected;
    }

    void move(int8_t x, int8_t y, int8_t wheel = 0, int8_t h_wheel = 0)
    {
        sendReport(connected_buttons, x, y, wheel, h_wheel);
    }

    void click(uint8_t button)
    {
        if (!isConnected()) {
            return;
        }
        sendReport(connected_buttons | button, 0, 0, 0, 0);
        sendReport(connected_buttons, 0, 0, 0, 0);
    }

private:
    bool startAdvertising(const char *device_name)
    {
        if (!server) {
            return false;
        }

        LILYGO_LOG_PRINTF("PAWMOUSE BLE advertising setup\n");
        NimBLEDevice::setSecurityAuth(false, false, false);
        NimBLEAdvertising *advertising = server->getAdvertising();
        if (!advertising) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE advertising object missing\n");
            return false;
        }
        advertising->stop();
        advertising->reset();
        advertising->clearData();
        advertising->setPreferredParams(160, 320);

        NimBLEAdvertisementData adv_data;
        adv_data.setFlags(BLE_HS_ADV_F_BREDR_UNSUP | BLE_HS_ADV_F_DISC_GEN);
        adv_data.setName(device_name, true);
        adv_data.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
        adv_data.setAppearance(PAW_MOUSE_APPEARANCE);

        NimBLEAdvertisementData scan_data;
        scan_data.setManufacturerData("LilyGo");

        advertising->setAdvertisementData(adv_data);
        advertising->setScanResponseData(scan_data);
        advertising->enableScanResponse(true);
        advertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);

        if (!advertising->start()) {
            LILYGO_LOG_PRINTF("PAWMOUSE BLE advertising start failed\n");
            return false;
        }

        LILYGO_LOG_PRINTF("PAWMOUSE BLE advertising started\n");
        return true;
    }

    void sendReport(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t h_wheel)
    {
        if (!input || !isConnected()) {
            return;
        }
        uint8_t report[5] = {
            buttons,
            (uint8_t)x,
            (uint8_t)y,
            (uint8_t)wheel,
            (uint8_t)h_wheel,
        };
        input->setValue(report, sizeof(report));
        input->notify();
    }

    static const uint8_t report_map[67];
    static PawMouseServerCallbacks callbacks;
    NimBLEServer *server = NULL;
    NimBLEHIDDevice *hid = NULL;
    NimBLECharacteristic *input = NULL;
    bool started = false;
    uint8_t connected_buttons = 0;
};

const uint8_t PawMouseHid::report_map[67] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x05, 0x81, 0x02, 0x75, 0x03, 0x95, 0x01,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x75,
    0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

PawMouseServerCallbacks PawMouseHid::callbacks;
static PawMouseHid paw_mouse;

static void ble_start_task(void *param)
{
    const char *device_name = (const char *)param;
    LILYGO_LOG_PRINTF("PAWMOUSE BLE start task\n");
    log_ble_heap("task");
    bool ok = paw_mouse.begin(device_name);
    if (!paw_mouse_app_active) {
        paw_mouse.end();
    }
    ble_start_failed = !ok;
    ble_start_timeout = false;
    ble_starting = false;
    ble_start_task_handle = NULL;
    LILYGO_LOG_PRINTF("PAWMOUSE BLE start task done ok=%d\n", ok ? 1 : 0);
    vTaskDelete(NULL);
}

static bool start_ble_async(void)
{
    if (ble_start_task_handle) {
        LILYGO_LOG_PRINTF("PAWMOUSE BLE start skipped: task already running\n");
        ble_start_failed = true;
        ble_start_timeout = true;
        ble_starting = false;
        return false;
    }

    ble_start_failed = false;
    ble_start_timeout = false;
    ble_starting = true;
    ble_start_ms = millis();
    BaseType_t ok = xTaskCreatePinnedToCore(ble_start_task, "pawBle", 6144,
                                            (void *)"T-Deck PAW Mouse",
                                            tskIDLE_PRIORITY + 1,
                                            &ble_start_task_handle, 0);
    if (ok != pdPASS) {
        LILYGO_LOG_PRINTF("PAWMOUSE BLE create start task failed\n");
        ble_start_task_handle = NULL;
        ble_starting = false;
        ble_start_failed = true;
        return false;
    }
    return true;
}

static bool wait_ble_start_task(uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (ble_start_task_handle && (millis() - start < timeout_ms)) {
        delay(10);
    }
    return ble_start_task_handle == NULL;
}
#endif

static int8_t clamp_hid_delta(float value)
{
    if (value > 127.0f) {
        return 127;
    }
    if (value < -127.0f) {
        return -127;
    }
    return (int8_t)value;
}

static bool check_sensor_available(void)
{
    return (hw_get_device_online() & HW_PAW_A350_ONLINE);
}

static void update_sensitivity_label()
{
    if (sens_label) {
        lv_label_set_text_fmt(sens_label, "%ldx", (long)sensitivity);
    }
}

static void update_preview_dot(int8_t dx, int8_t dy)
{
    if (!cursor_dot) {
        return;
    }

    preview_x += dx;
    preview_y += dy;
    if (preview_x < -preview_max_x) preview_x = -preview_max_x;
    if (preview_x > preview_max_x) preview_x = preview_max_x;
    if (preview_y < -preview_max_y) preview_y = -preview_max_y;
    if (preview_y > preview_max_y) preview_y = preview_max_y;
    lv_obj_align(cursor_dot, LV_ALIGN_CENTER, preview_x, preview_y);
}

static void trackball_callback(int8_t delta_x, int8_t delta_y)
{
    pending_x += (float)delta_x * (float)sensitivity;
    pending_y += (float)delta_y * (float)sensitivity;
    last_report_x = delta_x;
    last_report_y = delta_y;
    motion_count++;
    update_preview_dot(delta_x, delta_y);
}

static void button_callback(uint8_t id, uint8_t state)
{
    if (state != BUTTON_EVENT_CLICK) {
        return;
    }

    uint8_t mouse_button = 0;
    const char *name = "--";
    if (id == BUTTON_LEFT) {
        mouse_button = 0x01;
        name = "Left";
    } else if (id == BUTTON_RIGHT) {
        mouse_button = 0x02;
        name = "Right";
    } else if (id == BUTTON_CENTER) {
        mouse_button = 0x04;
        name = "Middle";
    }

    snprintf(button_text, sizeof(button_text), "%s", name);
    if (button_label) {
        lv_label_set_text(button_label, button_text);
    }

#ifdef ARDUINO
    if (mouse_button) {
        paw_mouse.click(mouse_button);
    }
#endif
}

static void send_timer_cb(lv_timer_t *timer)
{
    (void)timer;
#ifdef ARDUINO
    if (!paw_mouse.isConnected()) {
        pending_x = 0.0f;
        pending_y = 0.0f;
        return;
    }

    int8_t out_x = clamp_hid_delta(pending_x);
    int8_t out_y = clamp_hid_delta(pending_y);
    if (out_x == 0 && out_y == 0) {
        return;
    }

    pending_x -= (float)out_x;
    pending_y -= (float)out_y;
    paw_mouse.move(out_x, out_y);
#endif
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (state_label) {
#ifdef ARDUINO
        if (ble_starting && (millis() - ble_start_ms > PAW_MOUSE_BLE_START_TIMEOUT_MS)) {
            ble_start_timeout = true;
            ble_starting = false;
            LILYGO_LOG_PRINTF("PAWMOUSE BLE start timeout after %lu ms\n", (unsigned long)(millis() - ble_start_ms));
        }
        const char *state = "Stopped";
        if (ble_start_timeout) {
            state = "BLE Timeout";
        } else if (ble_start_failed) {
            state = "BLE Failed";
        } else if (ble_starting) {
            state = "Starting";
        } else if (paw_mouse.isConnected()) {
            state = "Connected";
        } else if (paw_mouse.isStarted()) {
            state = "Advertising";
        }
#else
        const char *state = "N.A";
#endif
        lv_label_set_text(state_label, state);
    }
    if (delta_label) {
        lv_label_set_text_fmt(delta_label, "%ld,%ld  %lu",
                              (long)last_report_x,
                              (long)last_report_y,
                              (unsigned long)motion_count);
    }
}

static void sensitivity_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    sensitivity = lv_slider_get_value(slider);
    update_sensitivity_label();
}

static void cleanup(void)
{
    if (send_timer) {
        lv_timer_delete(send_timer);
        send_timer = NULL;
    }
    if (ui_timer) {
        lv_timer_delete(ui_timer);
        ui_timer = NULL;
    }
    hw_set_trackball_callback(NULL);
    hw_set_button_callback(NULL);
#ifdef ARDUINO
    paw_mouse_app_active = false;
    if (!ble_start_task_handle || wait_ble_start_task(1500)) {
        paw_mouse.end();
    }
    ble_starting = false;
#endif
    pending_x = 0.0f;
    pending_y = 0.0f;
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    cleanup();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    state_label = NULL;
    sens_label = NULL;
    delta_label = NULL;
    button_label = NULL;
    cursor_dot = NULL;
    menu_show();
}

static void sensor_unavailable_msgbox_cb(lv_event_t *e)
{
    (void)e;
    if (sensor_unavailable_msgbox) {
        destroy_msgbox(sensor_unavailable_msgbox);
        sensor_unavailable_msgbox = NULL;
    }
    menu_show();
}

static void show_sensor_unavailable_msgbox(void)
{
    if (sensor_unavailable_msgbox) {
        return;
    }
    static const char *btns[] = {"OK", ""};
    sensor_unavailable_msgbox = create_msgbox(
                                    lv_scr_act(),
                                    "PAW350",
                                    "PAW350 not detected.",
                                    btns,
                                    sensor_unavailable_msgbox_cb,
                                    NULL);
}

void ui_paw_mouse_enter(lv_obj_t *parent)
{
    if (!check_sensor_available()) {
        show_sensor_unavailable_msgbox();
        return;
    }
    cleanup();
    page_container = ui_create_app_page(parent, "PAW Mouse", back_event_handler);

#ifdef ARDUINO
    paw_mouse_app_active = true;
#endif

    lv_obj_t *status_card = ui_create_card(page_container, "Status");
    lv_obj_t *row = ui_create_card_info(status_card, LV_SYMBOL_BLUETOOTH, "BLE", "Starting");
    state_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_SETTINGS, "Sensitivity", "8x");
    sens_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_REFRESH, "Delta", "0,0  0");
    delta_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    row = ui_create_card_info(status_card, LV_SYMBOL_OK, "Button", "--");
    button_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);

    lv_obj_t *slider = lv_slider_create(lv_obj_create(status_card));
    lv_slider_set_range(slider, PAW_MOUSE_SENS_MIN, PAW_MOUSE_SENS_MAX);
    lv_slider_set_value(slider, sensitivity, LV_ANIM_OFF);
    lv_obj_set_size(slider, LV_PCT(50), 12);
    lv_obj_set_flex_grow(slider, 1);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_add_event_cb(slider, sensitivity_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(status_card, LV_SYMBOL_SETTINGS, "Gain", slider);
    update_sensitivity_label();

    lv_obj_t *preview_card = ui_create_card(page_container, NULL);
    lv_obj_set_height(preview_card, is_screen_small() ? 72 : 100);
    lv_obj_remove_flag(preview_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *track_area = lv_obj_create(preview_card);
    lv_obj_set_size(track_area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(track_area, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(track_area, LV_OPA_70, 0);
    lv_obj_set_style_border_color(track_area, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(track_area, 1, 0);
    lv_obj_set_style_radius(track_area, 4, 0);
    lv_obj_set_style_pad_all(track_area, 0, 0);
    lv_obj_set_scrollbar_mode(track_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(track_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_update_layout(track_area);
    int32_t track_w = lv_obj_get_width(track_area);
    int32_t track_h = lv_obj_get_height(track_area);
    preview_max_x = (track_w - 12) / 2;
    preview_max_y = (track_h - 12) / 2;
    preview_x = 0;
    preview_y = 0;

    cursor_dot = lv_obj_create(track_area);
    lv_obj_set_size(cursor_dot, 12, 12);
    lv_obj_set_style_radius(cursor_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cursor_dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(cursor_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cursor_dot, 0, 0);
    lv_obj_center(cursor_dot);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cursor_dot, LV_OBJ_FLAG_CLICKABLE);

    snprintf(button_text, sizeof(button_text), "--");
    last_report_x = 0;
    last_report_y = 0;
    motion_count = 0;
    pending_x = 0.0f;
    pending_y = 0.0f;

    hw_set_trackball_callback(trackball_callback);
    hw_set_button_callback(button_callback);
    send_timer = lv_timer_create(send_timer_cb, PAW_MOUSE_SEND_PERIOD_MS, NULL);
    ui_timer = lv_timer_create(ui_timer_cb, 250, NULL);
#ifdef ARDUINO
    start_ble_async();
#endif
    ui_timer_cb(ui_timer);
}

void ui_paw_mouse_exit(lv_obj_t *parent)
{
    (void)parent;
    cleanup();
}

app_t ui_paw_mouse_main = {
    .setup_func_cb = ui_paw_mouse_enter,
    .exit_func_cb  = ui_paw_mouse_exit,
    .user_data     = nullptr,
};

#endif
