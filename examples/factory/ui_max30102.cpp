/**
 * @file      ui_max30102.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-14
 *
 * MAX30102 raw optical sensor monitor.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_MAX30102)

#if defined(ARDUINO) && FACTORY_HAS_I2C && __has_include(<MAX30105.h>)
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#ifdef I2C_BUFFER_LENGTH
#undef I2C_BUFFER_LENGTH
#endif
#include <MAX30105.h>
#define MAX30102_APP_HAS_DRIVER 1
#else
#define MAX30102_APP_HAS_DRIVER 0
#endif

#define MAX30102_I2C_ADDRESS       0x57
#define MAX30102_PART_ID_REGISTER  0xFF
#define MAX30102_MODE_REGISTER     0x09
#define MAX30102_EXPECTED_PART_ID  0x15
#define MAX30102_SHUTDOWN_BIT      0x80
#define MAX30102_SAMPLE_TIMER_MS   20
#define MAX30102_SAMPLE_TASK_MS    5
#define MAX30102_SAMPLE_TASK_STACK 4096
#define MAX30102_TASK_STOP_MS      1000
#define MAX30102_UI_REFRESH_MS     100
#define MAX30102_SAMPLE_TIMEOUT_MS 1200
#define MAX30102_RAW_POINTS        80
#define MAX30102_FINGER_IR_ON      62000UL
#define MAX30102_FINGER_RED_ON     24000UL
#define MAX30102_FINGER_IR_OFF     59000UL
#define MAX30102_FINGER_RED_OFF    22000UL

static lv_obj_t *page_container = NULL;
static lv_timer_t *sample_timer = NULL;
static lv_obj_t *pulse_indicator = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *finger_status_label = NULL;
static lv_obj_t *ir_label = NULL;
static lv_obj_t *red_label = NULL;
static lv_obj_t *revision_label = NULL;
static lv_obj_t *raw_chart = NULL;
static lv_chart_series_t *raw_ir_series = NULL;
static lv_chart_series_t *raw_red_series = NULL;
static uint32_t raw_ir_history[MAX30102_RAW_POINTS] = {};
static uint32_t raw_red_history[MAX30102_RAW_POINTS] = {};
static uint16_t raw_history_count = 0;

static uint32_t latest_ir = 0;
static uint32_t latest_red = 0;
static uint32_t last_sample_ms = 0;
static uint32_t last_ui_refresh_ms = 0;
static bool finger_present = false;
static bool sensor_running = false;
static uint8_t sensor_revision = 0;

#if MAX30102_APP_HAS_DRIVER
static MAX30105 max30102;
struct max30102_raw_sample_t {
    uint32_t red;
    uint32_t ir;
};
static QueueHandle_t raw_sample_queue = NULL;
static volatile TaskHandle_t sample_task = NULL;
static volatile bool sample_task_stop_requested = false;
#endif

static lv_obj_t *card_value_label(lv_obj_t *row)
{
    if (!row) return NULL;
    uint32_t count = lv_obj_get_child_count(row);
    return count ? lv_obj_get_child(row, count - 1) : NULL;
}

#if MAX30102_APP_HAS_DRIVER
static bool read_register(uint8_t reg, uint8_t *value)
{
    if (!value) return false;

    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)MAX30102_I2C_ADDRESS, (uint8_t)1) != 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

static bool write_register(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}
#endif

bool ui_max30102_probe(void)
{
#if MAX30102_APP_HAS_DRIVER
    uint8_t part_id = 0;
    if (!read_register(MAX30102_PART_ID_REGISTER, &part_id) ||
            part_id != MAX30102_EXPECTED_PART_ID) {
        return false;
    }

    uint8_t mode = 0;
    if (read_register(MAX30102_MODE_REGISTER, &mode)) {
        write_register(MAX30102_MODE_REGISTER, mode | MAX30102_SHUTDOWN_BIT);
    }
    LILYGO_LOG_I("MAX30102 detected at 0x%02X", MAX30102_I2C_ADDRESS);
    return true;
#else
    return false;
#endif
}

static void reset_measurement(void)
{
    latest_ir = 0;
    latest_red = 0;
    last_sample_ms = 0;
    last_ui_refresh_ms = 0;
    finger_present = false;
    raw_history_count = 0;
    memset(raw_ir_history, 0, sizeof(raw_ir_history));
    memset(raw_red_history, 0, sizeof(raw_red_history));
}

static void clear_raw_chart(void)
{
    raw_history_count = 0;
    memset(raw_ir_history, 0, sizeof(raw_ir_history));
    memset(raw_red_history, 0, sizeof(raw_red_history));
    if (raw_chart && raw_ir_series && raw_red_series) {
        lv_chart_set_all_values(raw_chart, raw_ir_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_values(raw_chart, raw_red_series, LV_CHART_POINT_NONE);
        lv_chart_set_axis_range(raw_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 262143);
        lv_chart_refresh(raw_chart);
    }
}

static void update_raw_chart(uint32_t red, uint32_t ir)
{
    if (!raw_chart || !raw_ir_series || !raw_red_series) return;

    if (raw_history_count < MAX30102_RAW_POINTS) {
        raw_ir_history[raw_history_count] = ir;
        raw_red_history[raw_history_count] = red;
        raw_history_count++;
    } else {
        memmove(raw_ir_history, raw_ir_history + 1,
                sizeof(raw_ir_history[0]) * (MAX30102_RAW_POINTS - 1));
        memmove(raw_red_history, raw_red_history + 1,
                sizeof(raw_red_history[0]) * (MAX30102_RAW_POINTS - 1));
        raw_ir_history[MAX30102_RAW_POINTS - 1] = ir;
        raw_red_history[MAX30102_RAW_POINTS - 1] = red;
    }

    lv_chart_set_next_value(raw_chart, raw_ir_series, (int32_t)ir);
    lv_chart_set_next_value(raw_chart, raw_red_series, (int32_t)red);

    uint32_t minimum = raw_ir_history[0];
    uint32_t maximum = raw_ir_history[0];
    for (uint16_t i = 0; i < raw_history_count; ++i) {
        if (raw_ir_history[i] < minimum) minimum = raw_ir_history[i];
        if (raw_red_history[i] < minimum) minimum = raw_red_history[i];
        if (raw_ir_history[i] > maximum) maximum = raw_ir_history[i];
        if (raw_red_history[i] > maximum) maximum = raw_red_history[i];
    }

    uint32_t span = maximum - minimum;
    uint32_t margin = span / 10U;
    if (margin < 100U) margin = 100U;
    uint32_t axis_min = minimum > margin ? minimum - margin : 0;
    uint32_t axis_max = maximum + margin;
    if (axis_max > 262143U) axis_max = 262143U;
    if (axis_max <= axis_min) axis_max = axis_min + 100U;
    lv_chart_set_axis_range(raw_chart, LV_CHART_AXIS_PRIMARY_Y,
                            (int32_t)axis_min, (int32_t)axis_max);
}

static void set_indicator(lv_color_t color, uint8_t brightness)
{
    if (!pulse_indicator) return;
    lv_led_set_color(pulse_indicator, color);
    lv_led_set_brightness(pulse_indicator, brightness);
}

static void refresh_display(uint32_t now)
{
    if (!status_label) return;

    bool sample_online = last_sample_ms != 0 &&
                         lv_tick_elaps(last_sample_ms) < MAX30102_SAMPLE_TIMEOUT_MS;

    if (!sample_online) {
        lv_label_set_text(status_label, "No data");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF445A), 0);
        set_indicator(lv_color_hex(0xFF445A), 80);
    } else {
        lv_label_set_text(status_label, "Sensor active");
        lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
        set_indicator(UI_COLOR_ACCENT, 150);
    }

    if (!sample_online) {
        lv_label_set_text(finger_status_label, "--");
        lv_obj_set_style_text_color(finger_status_label, UI_COLOR_TEXT_SECONDARY, 0);
    } else if (finger_present) {
        lv_label_set_text(finger_status_label, "Finger detected");
        lv_obj_set_style_text_color(finger_status_label, UI_COLOR_ACCENT, 0);
    } else {
        lv_label_set_text(finger_status_label, "No finger");
        lv_obj_set_style_text_color(finger_status_label, UI_COLOR_TEXT_SECONDARY, 0);
    }

    lv_label_set_text_fmt(ir_label, "%lu", (unsigned long)latest_ir);
    lv_label_set_text_fmt(red_label, "%lu", (unsigned long)latest_red);
    (void)now;
}

#if MAX30102_APP_HAS_DRIVER
static void process_sample(uint32_t red, uint32_t ir, uint32_t now)
{
    latest_red = red;
    latest_ir = ir;
    last_sample_ms = now;
    if (finger_present) {
        finger_present = ir >= MAX30102_FINGER_IR_OFF &&
                         red >= MAX30102_FINGER_RED_OFF;
    } else {
        finger_present = ir >= MAX30102_FINGER_IR_ON &&
                         red >= MAX30102_FINGER_RED_ON;
    }
}

static void sample_task_fn(void *parameter)
{
    (void)parameter;
    while (!sample_task_stop_requested) {
        max30102.check();
        uint32_t now = millis();
        while (max30102.available()) {
            max30102_raw_sample_t sample = {
                .red = max30102.getFIFORed(),
                .ir = max30102.getFIFOIR(),
            };
            process_sample(sample.red, sample.ir, now);
            if (raw_sample_queue) {
                (void)xQueueSend(raw_sample_queue, &sample, 0);
            }
            max30102.nextSample();
        }
        vTaskDelay(pdMS_TO_TICKS(MAX30102_SAMPLE_TASK_MS));
    }

    if (sensor_running) {
        max30102.setPulseAmplitudeRed(0);
        max30102.setPulseAmplitudeIR(0);
        max30102.shutDown();
        sensor_running = false;
    }
    sample_task = NULL;
    vTaskDelete(NULL);
}

static void sample_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!sensor_running) return;

    bool chart_updated = false;
    if (!sample_task && !sample_task_stop_requested) {
        max30102.check();
        uint32_t now = millis();
        while (max30102.available()) {
            uint32_t red = max30102.getFIFORed();
            uint32_t ir = max30102.getFIFOIR();
            process_sample(red, ir, now);
            update_raw_chart(red, ir);
            chart_updated = true;
            max30102.nextSample();
        }
    } else if (raw_sample_queue) {
        max30102_raw_sample_t sample = {};
        while (xQueueReceive(raw_sample_queue, &sample, 0) == pdTRUE) {
            update_raw_chart(sample.red, sample.ir);
            chart_updated = true;
        }
    }

    if (chart_updated) {
        lv_chart_refresh(raw_chart);
    }

    uint32_t now = millis();
    if (last_ui_refresh_ms == 0 ||
            lv_tick_elaps(last_ui_refresh_ms) >= MAX30102_UI_REFRESH_MS) {
        last_ui_refresh_ms = now;
        refresh_display(now);
    }
}
#endif

static void clear_widget_refs(void)
{
    pulse_indicator = NULL;
    status_label = NULL;
    finger_status_label = NULL;
    ir_label = NULL;
    red_label = NULL;
    revision_label = NULL;
    raw_chart = NULL;
    raw_ir_series = NULL;
    raw_red_series = NULL;
}

static void stop_sensor(void)
{
    if (sample_timer) {
        lv_timer_del(sample_timer);
        sample_timer = NULL;
    }
#if MAX30102_APP_HAS_DRIVER
    sample_task_stop_requested = true;
    uint32_t stop_started = millis();
    while (sample_task && millis() - stop_started < MAX30102_TASK_STOP_MS) {
        delay(1);
    }
    if (sample_task) {
        LILYGO_LOG_E("MAX30102 sample task did not stop within %ums",
                     (unsigned)MAX30102_TASK_STOP_MS);
    }
    if (!sample_task && sensor_running) {
        max30102.setPulseAmplitudeRed(0);
        max30102.setPulseAmplitudeIR(0);
        max30102.shutDown();
        sensor_running = false;
    }
    // Keep the queue alive if a task failed to stop in time; deleting it here
    // would let the still-running task use freed queue storage.
    if (!sample_task && raw_sample_queue) {
        vQueueDelete(raw_sample_queue);
        raw_sample_queue = NULL;
    }
#else
    sensor_running = false;
#endif
}

static void close_page(bool show_menu)
{
    stop_sensor();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    clear_widget_refs();
    reset_measurement();
    if (show_menu) {
        menu_show();
    }
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page(true);
}

static void create_dashboard(void)
{
    lv_obj_t *card = ui_create_card(page_container, NULL);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    pulse_indicator = lv_led_create(row);
    lv_obj_set_size(pulse_indicator, 18, 18);
    set_indicator(UI_COLOR_TEXT_SECONDARY, 70);

    status_label = lv_label_create(row);
    lv_label_set_text(status_label, "Starting");
    lv_obj_set_flex_grow(status_label, 1);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    lv_obj_t *finger_row = ui_create_card_info(card, LV_SYMBOL_EYE_OPEN,
                                               "Finger", "--");
    finger_status_label = card_value_label(finger_row);

    lv_obj_t *info_row = ui_create_card_info(card, LV_SYMBOL_EYE_OPEN, "IR", "0");
    ir_label = card_value_label(info_row);
    lv_obj_set_style_text_color(ir_label, lv_color_hex(0x36D7FF), 0);
    info_row = ui_create_card_info(card, LV_SYMBOL_TINT, "Red", "0");
    red_label = card_value_label(info_row);
    lv_obj_set_style_text_color(red_label, lv_color_hex(0xFF6B35), 0);
}

static void create_optical_card(void)
{
    lv_obj_t *card = ui_create_card(page_container, "Raw waveform");
    raw_chart = lv_chart_create(card);
    lv_obj_set_size(raw_chart, LV_PCT(100), is_screen_small() ? 150 : 210);
    lv_chart_set_type(raw_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(raw_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(raw_chart, MAX30102_RAW_POINTS);
    lv_chart_set_axis_range(raw_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 262143);
    lv_chart_set_div_line_count(raw_chart, 4, 5);
    lv_obj_set_style_bg_color(raw_chart, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(raw_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(raw_chart, 1, 0);
    lv_obj_set_style_border_color(raw_chart, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(raw_chart, 0, 0);
    lv_obj_set_style_pad_all(raw_chart, 4, 0);
    lv_obj_set_style_line_width(raw_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(raw_chart, lv_color_hex(0x263238), LV_PART_MAIN);
    lv_obj_set_style_line_width(raw_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(raw_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(raw_chart, LV_OBJ_FLAG_SCROLLABLE);
    raw_ir_series = lv_chart_add_series(raw_chart, lv_color_hex(0x36D7FF),
                                        LV_CHART_AXIS_PRIMARY_Y);
    raw_red_series = lv_chart_add_series(raw_chart, lv_color_hex(0xFF6B35),
                                         LV_CHART_AXIS_PRIMARY_Y);
    clear_raw_chart();
}

static void create_device_card(void)
{
    lv_obj_t *card = ui_create_card(page_container, "Device");
    ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Address", "0x57");
    lv_obj_t *row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Revision", "--");
    revision_label = card_value_label(row);
    lv_label_set_text_fmt(revision_label, "0x%02X", sensor_revision);
    ui_create_card_info(card, LV_SYMBOL_REFRESH, "Mode", "Raw data");
}

void ui_max30102_enter(lv_obj_t *parent)
{
    reset_measurement();
    page_container = ui_create_app_page(parent, "MAX30102", back_event_handler);

#if MAX30102_APP_HAS_DRIVER
    if (!max30102.begin(Wire, I2C_SPEED_FAST, MAX30102_I2C_ADDRESS)) {
        lv_obj_t *card = ui_create_card(page_container, "Unavailable");
        ui_create_card_info(card, LV_SYMBOL_WARNING, "MAX30102", "Disconnected");
        return;
    }

    sensor_revision = max30102.getRevisionID();
    max30102.setup(0x1F, 4, 2, 100, 411, 4096);
    max30102.setPulseAmplitudeRed(0x0A);
    max30102.setPulseAmplitudeIR(0x1F);
    max30102.clearFIFO();
    sensor_running = true;
    sample_task_stop_requested = false;

    create_dashboard();
    create_optical_card();
    create_device_card();
    refresh_display(lv_tick_get());

    raw_sample_queue = xQueueCreate(128, sizeof(max30102_raw_sample_t));
    TaskHandle_t created_task = NULL;
    if (raw_sample_queue) {
        BaseType_t task_result = xTaskCreate(sample_task_fn, "max30102",
                                             MAX30102_SAMPLE_TASK_STACK, NULL, 2,
                                             &created_task);
        if (task_result == pdPASS) {
            sample_task = created_task;
        } else {
            vQueueDelete(raw_sample_queue);
            raw_sample_queue = NULL;
            LILYGO_LOG_E("MAX30102 failed to create sample task; using LVGL timer fallback");
        }
    } else {
        LILYGO_LOG_E("MAX30102 failed to create raw sample queue; using LVGL timer fallback");
    }
    sample_timer = lv_timer_create(sample_timer_cb, MAX30102_SAMPLE_TIMER_MS, NULL);
#else
    lv_obj_t *card = ui_create_card(page_container, "Unavailable");
    ui_create_card_info(card, LV_SYMBOL_WARNING, "MAX30102", "Driver disabled");
#endif
}

void ui_max30102_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page(false);
}

app_t ui_max30102_main = {
    .setup_func_cb = ui_max30102_enter,
    .exit_func_cb = ui_max30102_exit,
    .user_data = NULL,
};

#endif /* EXCLUDE_MAX30102 */
