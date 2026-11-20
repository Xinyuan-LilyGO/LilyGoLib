/**
 * @file      ui_cc1101_scan.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-19
 *
 * Provides an automatically running CC1101 RSSI spectrum for T-Embed-Plus.
 */
#include "ui_define.h"

#if defined(ARDUINO_T_EMBED_PLUS) && !defined(EXCLUDE_CC1101_SCAN)

#include <LilyGoLog.h>
#include <math.h>

namespace {

static constexpr float kScanFrequencies[] = {
    300.000f, 302.757f, 303.875f, 303.900f, 304.250f, 307.000f,
    307.500f, 307.800f, 309.000f, 310.000f, 312.000f, 312.100f,
    312.200f, 313.000f, 313.850f, 314.000f, 314.350f, 314.980f,
    315.000f, 318.000f, 330.000f, 345.000f, 348.000f,
    387.000f, 390.000f, 418.000f, 430.000f, 430.500f, 431.000f,
    431.500f, 433.075f, 433.220f, 433.420f, 433.657f, 433.889f,
    433.920f, 434.075f, 434.177f, 434.190f, 434.390f, 434.420f,
    434.620f, 434.775f, 438.900f, 440.175f, 464.000f,
    779.000f, 868.350f, 868.400f, 868.800f, 868.950f, 906.400f,
    915.000f, 925.000f, 928.000f,
};

static constexpr uint8_t kRangeCount = 3;
static constexpr uint8_t kRangeStart[kRangeCount] = {0, 23, 46};
static constexpr uint8_t kRangeEnd[kRangeCount] = {22, 45, 54};
static constexpr const char *kRangeButtonLabels[kRangeCount] = {
    "300", "433", "868 / 915"
};
static constexpr int16_t kRssiFloor = -100;
static constexpr int16_t kRssiCeiling = -20;
static constexpr uint32_t kUiRefreshMs = 50;
static constexpr uint32_t kTaskStopTimeoutMs = 2000;

struct cc1101_scan_snapshot_t {
    int16_t rssi[24];
    uint8_t count;
    uint8_t range;
    uint8_t peak_index;
    int16_t peak_rssi;
    int16_t noise_floor;
    int16_t error;
    uint32_t sweep_ms;
    uint32_t sweep_count;
    bool valid;
};

static lv_obj_t *page_container = nullptr;
static lv_obj_t *status_dot = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *peak_label = nullptr;
static lv_obj_t *sweep_label = nullptr;
static lv_obj_t *chart = nullptr;
static lv_chart_series_t *series = nullptr;
static lv_obj_t *axis_start_label = nullptr;
static lv_obj_t *axis_end_label = nullptr;
static lv_obj_t *range_buttons[kRangeCount] = {};
static lv_timer_t *ui_timer = nullptr;

static TaskHandle_t scan_task = nullptr;
static volatile bool scan_stop_requested = false;
static volatile uint8_t requested_range = 1;
static portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static cc1101_scan_snapshot_t shared_snapshot = {};

static uint8_t range_frequency_count(uint8_t range)
{
    return kRangeEnd[range] - kRangeStart[range] + 1;
}

static lv_obj_t *create_plain_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void publish_snapshot(const cc1101_scan_snapshot_t &snapshot)
{
    portENTER_CRITICAL(&snapshot_mux);
    shared_snapshot = snapshot;
    portEXIT_CRITICAL(&snapshot_mux);
}

static cc1101_scan_snapshot_t copy_snapshot()
{
    cc1101_scan_snapshot_t snapshot;
    portENTER_CRITICAL(&snapshot_mux);
    snapshot = shared_snapshot;
    portEXIT_CRITICAL(&snapshot_mux);
    return snapshot;
}

static void reset_shared_snapshot(uint8_t range)
{
    cc1101_scan_snapshot_t snapshot = {};
    snapshot.range = range;
    snapshot.count = range_frequency_count(range);
    snapshot.peak_rssi = kRssiFloor;
    snapshot.noise_floor = kRssiFloor;
    for (uint8_t i = 0; i < snapshot.count; ++i) snapshot.rssi[i] = kRssiFloor;
    publish_snapshot(snapshot);
}

static int16_t configure_radio(float frequency)
{
    if (!(hw_get_device_online() & HW_RADIO_ONLINE)) return RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;

    int16_t state = radio.standby();
    if (state == RADIOLIB_ERR_NONE) state = instance.setRFFrequencyBand(frequency);
    if (state == RADIOLIB_ERR_NONE) state = radio.setOOK(true);
    if (state == RADIOLIB_ERR_NONE) state = radio.setRxBandwidth(203.0f);
    if (state == RADIOLIB_ERR_NONE) state = radio.setBitRate(38.4f);
    // In packet mode RadioLib returns the RSSI cached with the last packet.
    // Direct receive makes getRSSI() read the live CC1101 status register.
    if (state == RADIOLIB_ERR_NONE) state = radio.receiveDirectAsync();
    instance.unlockSPI();
    return state;
}

static int16_t sample_rssi(float frequency, int16_t &rssi)
{
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    int16_t state = instance.setRFFrequencyBand(frequency);
    if (state == RADIOLIB_ERR_NONE) state = radio.receiveDirectAsync();
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) return state;

    delay(5);
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    const float measured = radio.getRSSI();
    instance.unlockSPI();
    if (!isfinite(measured)) return RADIOLIB_ERR_UNKNOWN;

    rssi = constrain((int)lroundf(measured), (int)kRssiFloor, (int)kRssiCeiling);
    return RADIOLIB_ERR_NONE;
}

static int16_t restore_radio()
{
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    int16_t state = radio.standby();
    if (state == RADIOLIB_ERR_NONE) state = radio.packetMode();
    if (state == RADIOLIB_ERR_NONE) state = radio.variablePacketLengthMode();
    if (state == RADIOLIB_ERR_NONE) state = radio.setPromiscuousMode(false);
    if (state == RADIOLIB_ERR_NONE) {
        state = radio.setDIOMapping(0, RADIOLIB_CC1101_GDOX_HIGH_Z);
    }
    if (state == RADIOLIB_ERR_NONE) {
        state = radio.setDIOMapping(2, RADIOLIB_CC1101_GDOX_HIGH_Z);
    }
    if (state == RADIOLIB_ERR_NONE) state = radio.standby();
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_E("CC1101_SCAN restore failed: %d", state);
    }
    return state;
}

static int16_t calculate_noise_floor(const int16_t *values, uint8_t count)
{
    int16_t sorted[24];
    memcpy(sorted, values, count * sizeof(sorted[0]));
    for (uint8_t i = 1; i < count; ++i) {
        const int16_t value = sorted[i];
        int8_t j = i - 1;
        while (j >= 0 && sorted[j] > value) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = value;
    }

    const uint8_t baseline_count = count > 1 ? count / 2 : 1;
    int32_t total = 0;
    for (uint8_t i = 0; i < baseline_count; ++i) total += sorted[i];
    return total / baseline_count;
}

static void scan_task_fn(void *)
{
    uint8_t active_range = requested_range;
    int16_t smoothed[24];
    for (int16_t &value : smoothed) value = kRssiFloor;

    int16_t state = configure_radio(kScanFrequencies[kRangeStart[active_range]]);
    if (state != RADIOLIB_ERR_NONE) {
        cc1101_scan_snapshot_t failed = copy_snapshot();
        failed.error = state;
        publish_snapshot(failed);
        restore_radio();
        LILYGO_LOG_E("CC1101_SCAN setup failed: %d", state);
        scan_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    LILYGO_LOG_I("CC1101_SCAN worker started");
    uint32_t sweep_count = 0;
    while (!scan_stop_requested) {
        const uint8_t range = requested_range;
        if (range != active_range) {
            active_range = range;
            for (int16_t &value : smoothed) value = kRssiFloor;
        }

        cc1101_scan_snapshot_t next = {};
        next.range = active_range;
        next.count = range_frequency_count(active_range);
        next.peak_rssi = kRssiFloor;
        const uint32_t started_at = millis();
        const uint8_t start = kRangeStart[active_range];

        for (uint8_t i = 0; i < next.count && !scan_stop_requested; ++i) {
            int16_t sample = kRssiFloor;
            state = sample_rssi(kScanFrequencies[start + i], sample);
            if (state != RADIOLIB_ERR_NONE) {
                next.error = state;
                LILYGO_LOG_E("CC1101_SCAN sample failed: frequency=%.3f state=%d",
                             kScanFrequencies[start + i], state);
                break;
            }
            if (sample >= smoothed[i]) smoothed[i] = sample;
            else smoothed[i] = (smoothed[i] * 3 + sample) / 4;
            next.rssi[i] = smoothed[i];
            if (next.rssi[i] > next.peak_rssi) {
                next.peak_rssi = next.rssi[i];
                next.peak_index = i;
            }
        }

        if (active_range != requested_range) continue;

        next.sweep_ms = millis() - started_at;
        next.sweep_count = ++sweep_count;
        next.noise_floor = calculate_noise_floor(next.rssi, next.count);
        next.valid = !scan_stop_requested && next.error == RADIOLIB_ERR_NONE;
        publish_snapshot(next);
        if (sweep_count == 1) {
            for (uint8_t i = 0; i < next.count; ++i) {
                LILYGO_LOG_D("CC1101_SCAN point frequency=%.3f rssi=%ddBm",
                             kScanFrequencies[start + i], next.rssi[i]);
            }
        }
        if ((sweep_count & 0x3F) == 0) {
            LILYGO_LOG_D("CC1101_SCAN sweep=%lu range=%u time=%lums peak=%ddBm noise=%ddBm",
                         (unsigned long)sweep_count, active_range,
                         (unsigned long)next.sweep_ms, next.peak_rssi,
                         next.noise_floor);
        }
        if (next.error != RADIOLIB_ERR_NONE) break;
        taskYIELD();
    }

    restore_radio();
    LILYGO_LOG_I("CC1101_SCAN worker stopped");
    scan_task = nullptr;
    vTaskDelete(nullptr);
}

static bool start_scan_task()
{
    scan_stop_requested = false;
    reset_shared_snapshot(requested_range);
    scan_task = nullptr;
    const BaseType_t result = xTaskCreate(scan_task_fn, "cc1101Scan", 4096, nullptr,
                                          2, &scan_task);
    if (result != pdPASS) {
        scan_task = nullptr;
        LILYGO_LOG_E("CC1101_SCAN task creation failed: %d", (int)result);
        cc1101_scan_snapshot_t failed = copy_snapshot();
        failed.error = RADIOLIB_ERR_MEMORY_ALLOCATION_FAILED;
        publish_snapshot(failed);
        return false;
    }
    return true;
}

static void stop_scan_task()
{
    scan_stop_requested = true;
    const uint32_t started_at = millis();
    while (scan_task && millis() - started_at < kTaskStopTimeoutMs) delay(10);
    if (scan_task) LILYGO_LOG_E("CC1101_SCAN worker stop timeout");
}

static void update_range_button_styles()
{
    for (uint8_t i = 0; i < kRangeCount; ++i) {
        if (!range_buttons[i]) continue;
        const bool selected = i == requested_range;
        lv_obj_set_style_bg_color(range_buttons[i],
                                  selected ? UI_COLOR_ACCENT_DIM : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_width(range_buttons[i], selected ? 1 : 0, 0);
        lv_obj_set_style_border_color(range_buttons[i], UI_COLOR_ACCENT, 0);
    }
}

static void range_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const uint8_t range = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (range >= kRangeCount || range == requested_range) return;
    requested_range = range;
    reset_shared_snapshot(range);
    update_range_button_styles();
    hw_feedback();
}

static void configure_chart(uint8_t range)
{
    lv_chart_set_point_count(chart, range_frequency_count(range));
    const float start = kScanFrequencies[kRangeStart[range]];
    const float end = kScanFrequencies[kRangeEnd[range]];
    lv_label_set_text_fmt(axis_start_label, "%.0f", start);
    lv_label_set_text_fmt(axis_end_label, "%.0f MHz", end);
}

static void ui_timer_cb(lv_timer_t *)
{
    if (!chart || !series) return;
    const cc1101_scan_snapshot_t snapshot = copy_snapshot();
    static uint8_t displayed_range = UINT8_MAX;
    if (displayed_range != snapshot.range) {
        displayed_range = snapshot.range;
        configure_chart(displayed_range);
    }

    if (snapshot.error != RADIOLIB_ERR_NONE) {
        lv_label_set_text_fmt(status_label, "RADIO ERROR %d", snapshot.error);
        lv_obj_set_style_bg_color(status_dot, UI_COLOR_WARNING, 0);
        return;
    }
    if (!snapshot.valid) {
        lv_label_set_text(status_label, "STARTING");
        lv_obj_set_style_bg_color(status_dot, UI_COLOR_TEXT_SECONDARY, 0);
        return;
    }

    for (uint8_t i = 0; i < snapshot.count; ++i) {
        lv_chart_set_value_by_id(chart, series, i, snapshot.rssi[i]);
    }
    lv_chart_refresh(chart);

    const uint8_t absolute_peak = kRangeStart[snapshot.range] + snapshot.peak_index;
    const int16_t margin = snapshot.peak_rssi - snapshot.noise_floor;
    lv_label_set_text(status_label, margin >= 10 ? "SIGNAL" : "SCANNING");
    lv_obj_set_style_bg_color(status_dot,
                              margin >= 10 ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
    lv_label_set_text_fmt(peak_label, "%.3f MHz   %d dBm",
                          kScanFrequencies[absolute_peak], snapshot.peak_rssi);
    lv_label_set_text_fmt(sweep_label, "%lums   +%d",
                          (unsigned long)snapshot.sweep_ms, margin);
}

static lv_obj_t *create_range_button(lv_obj_t *parent, uint8_t range)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_height(button, LV_PCT(100));
    lv_obj_set_width(button, 1);
    lv_obj_set_flex_grow(button, range == 2 ? 2 : 1);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(button);
    lv_obj_add_event_cb(button, range_button_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)range);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, kRangeButtonLabels[range]);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    lv_obj_center(label);
    return button;
}

static void create_scanner_ui()
{
    lv_obj_set_style_pad_all(page_container, 4, 0);
    lv_obj_set_style_pad_row(page_container, 2, 0);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *metrics = create_plain_container(page_container);
    lv_obj_set_size(metrics, LV_PCT(100), 22);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(metrics, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(metrics, 6, 0);

    status_dot = lv_obj_create(metrics);
    lv_obj_set_size(status_dot, 7, 7);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_set_style_bg_color(status_dot, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_remove_flag(status_dot, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(metrics);
    lv_label_set_text(status_label, "STARTING");
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_10, 0);

    peak_label = lv_label_create(metrics);
    lv_label_set_text(peak_label, "-- MHz   -- dBm");
    lv_obj_set_width(peak_label, 1);
    lv_obj_set_flex_grow(peak_label, 1);
    lv_label_set_long_mode(peak_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(peak_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(peak_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(peak_label, &lv_font_montserrat_12, 0);

    sweep_label = lv_label_create(metrics);
    lv_label_set_text(sweep_label, "--ms");
    lv_obj_set_style_text_color(sweep_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(sweep_label, &lv_font_montserrat_10, 0);

    chart = lv_chart_create(page_container);
    lv_obj_set_width(chart, LV_PCT(100));
    lv_obj_set_height(chart, 1);
    lv_obj_set_flex_grow(chart, 1);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, kRssiFloor, kRssiCeiling);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_style_bg_color(chart, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(chart, 4, 0);
    lv_obj_set_style_pad_all(chart, 2, 0);
    lv_obj_set_style_pad_column(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, UI_COLOR_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
    series = lv_chart_add_series(chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *axis = create_plain_container(page_container);
    lv_obj_set_size(axis, LV_PCT(100), 12);
    axis_start_label = lv_label_create(axis);
    lv_obj_align(axis_start_label, LV_ALIGN_LEFT_MID, 2, 0);
    axis_end_label = lv_label_create(axis);
    lv_obj_align(axis_end_label, LV_ALIGN_RIGHT_MID, -2, 0);
    for (lv_obj_t *label : {axis_start_label, axis_end_label}) {
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    }

    lv_obj_t *ranges = create_plain_container(page_container);
    lv_obj_set_size(ranges, LV_PCT(100), 24);
    lv_obj_set_flex_flow(ranges, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ranges, 3, 0);
    for (uint8_t i = 0; i < kRangeCount; ++i) {
        range_buttons[i] = create_range_button(ranges, i);
    }
    update_range_button_styles();
    configure_chart(requested_range);
}

static void close_page(bool show_menu)
{
    if (!page_container && !scan_task) return;
    if (ui_timer) {
        lv_timer_delete(ui_timer);
        ui_timer = nullptr;
    }
    stop_scan_task();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    status_dot = nullptr;
    status_label = nullptr;
    peak_label = nullptr;
    sweep_label = nullptr;
    chart = nullptr;
    series = nullptr;
    axis_start_label = nullptr;
    axis_end_label = nullptr;
    memset(range_buttons, 0, sizeof(range_buttons));
    if (show_menu) menu_show();
}

static void back_event(lv_event_t *)
{
    close_page(true);
}

} // namespace

static void ui_cc1101_scan_enter(lv_obj_t *parent)
{
    requested_range = 1;
    page_container = ui_create_app_page(parent, "CC1101 Spectrum", back_event);
    create_scanner_ui();
    start_scan_task();
    ui_timer = lv_timer_create(ui_timer_cb, kUiRefreshMs, nullptr);
}

static void ui_cc1101_scan_exit(lv_obj_t *)
{
    close_page(false);
}

app_t ui_cc1101_scan_main = {
    .setup_func_cb = ui_cc1101_scan_enter,
    .exit_func_cb = ui_cc1101_scan_exit,
    .user_data = nullptr,
};

#endif
