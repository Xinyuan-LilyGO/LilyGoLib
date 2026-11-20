/**
 * @file      ui_nrf24_scan.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-19
 *
 * Provides an automatically running nRF24 2.4 GHz channel activity spectrum.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_NRF24)

#include <LilyGoLog.h>

namespace {

static constexpr uint8_t kChannelCount = 80;
static constexpr uint16_t kChannelBaseMHz = 2400;
static constexpr uint16_t kListenWindowUs = 128;
static constexpr uint32_t kUiRefreshMs = 50;
static constexpr uint32_t kTaskStopTimeoutMs = 2000;

struct nrf24_scan_snapshot_t {
    uint8_t activity[kChannelCount];
    uint8_t peak_channel;
    uint8_t peak_activity;
    uint32_t sweep_ms;
    uint32_t sweep_count;
    int16_t error;
    bool valid;
};

static lv_obj_t *page_container = nullptr;
static lv_obj_t *status_dot = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *peak_label = nullptr;
static lv_obj_t *sweep_label = nullptr;
static lv_obj_t *chart = nullptr;
static lv_chart_series_t *series = nullptr;
static lv_timer_t *ui_timer = nullptr;

static TaskHandle_t scan_task = nullptr;
static volatile bool scan_stop_requested = false;
static portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static nrf24_scan_snapshot_t shared_snapshot = {};

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

static void publish_snapshot(const nrf24_scan_snapshot_t &snapshot)
{
    portENTER_CRITICAL(&snapshot_mux);
    shared_snapshot = snapshot;
    portEXIT_CRITICAL(&snapshot_mux);
}

static nrf24_scan_snapshot_t copy_snapshot()
{
    nrf24_scan_snapshot_t snapshot;
    portENTER_CRITICAL(&snapshot_mux);
    snapshot = shared_snapshot;
    portEXIT_CRITICAL(&snapshot_mux);
    return snapshot;
}

static int16_t prepare_radio()
{
    if (!hw_has_nrf24()) return RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;

    int16_t state = nrf24.standby();
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAutoAck(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setCrcFiltering(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAddressWidth(2);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setBitRate(1000);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setFrequency(kChannelBaseMHz);
    instance.unlockSPI();
    return state;
}

static int16_t sample_channel(uint8_t channel, bool &detected)
{
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    int16_t state = nrf24.setFrequency((float)kChannelBaseMHz + channel);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.startReceive();
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) return state;

    delayMicroseconds(kListenWindowUs);
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    detected = nrf24.isCarrierDetected();
    state = nrf24.standby();
    instance.unlockSPI();
    return state;
}

static int16_t restore_radio()
{
    if (!hw_has_nrf24()) return RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (!instance.lockSPI(pdMS_TO_TICKS(200))) return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    int16_t state = nrf24.standby();
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setFrequency(kChannelBaseMHz);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setBitRate(1000);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAddressWidth(5);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setCrcFiltering(true);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAutoAck(true);
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_E("NRF24_SCAN restore failed: %d", state);
    }
    return state;
}

static void scan_task_fn(void *)
{
    nrf24_scan_snapshot_t next = {};
    next.error = prepare_radio();
    if (next.error != RADIOLIB_ERR_NONE) {
        publish_snapshot(next);
        restore_radio();
        LILYGO_LOG_E("NRF24_SCAN setup failed: %d", next.error);
        scan_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    LILYGO_LOG_I("NRF24_SCAN worker started");
    while (!scan_stop_requested) {
        next.error = RADIOLIB_ERR_NONE;
        next.peak_activity = 0;
        next.peak_channel = 0;
        const uint32_t started_at = millis();

        for (uint8_t channel = 0; channel < kChannelCount && !scan_stop_requested; ++channel) {
            bool detected = false;
            const int16_t state = sample_channel(channel, detected);
            if (state != RADIOLIB_ERR_NONE) {
                next.error = state;
                break;
            }

            const uint16_t weighted = next.activity[channel] * 7U +
                                      (detected ? 100U : 0U);
            next.activity[channel] = weighted / 8U;
            if (next.activity[channel] > next.peak_activity) {
                next.peak_activity = next.activity[channel];
                next.peak_channel = channel;
            }
        }

        next.sweep_ms = millis() - started_at;
        ++next.sweep_count;
        next.valid = !scan_stop_requested && next.error == RADIOLIB_ERR_NONE;
        publish_snapshot(next);
        if ((next.sweep_count & 0x7F) == 0) {
            LILYGO_LOG_D("NRF24_SCAN sweep=%lu time=%lums peak_ch=%u activity=%u",
                         (unsigned long)next.sweep_count,
                         (unsigned long)next.sweep_ms,
                         next.peak_channel, next.peak_activity);
        }
        if (next.error != RADIOLIB_ERR_NONE) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    restore_radio();
    LILYGO_LOG_I("NRF24_SCAN worker stopped");
    scan_task = nullptr;
    vTaskDelete(nullptr);
}

static bool start_scan_task()
{
    scan_stop_requested = false;
    nrf24_scan_snapshot_t initial = {};
    publish_snapshot(initial);

    scan_task = nullptr;
    const BaseType_t result = xTaskCreate(scan_task_fn, "nrf24Scan", 4096, nullptr,
                                          2, &scan_task);
    if (result != pdPASS) {
        scan_task = nullptr;
        LILYGO_LOG_E("NRF24_SCAN task creation failed: %d", (int)result);
        initial.error = RADIOLIB_ERR_MEMORY_ALLOCATION_FAILED;
        publish_snapshot(initial);
        return false;
    }
    return true;
}

static void stop_scan_task()
{
    scan_stop_requested = true;
    const uint32_t started_at = millis();
    while (scan_task && millis() - started_at < kTaskStopTimeoutMs) delay(10);
    if (scan_task) LILYGO_LOG_E("NRF24_SCAN worker stop timeout");
}

static void ui_timer_cb(lv_timer_t *)
{
    if (!chart || !series) return;
    const nrf24_scan_snapshot_t snapshot = copy_snapshot();
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

    for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
        lv_chart_set_value_by_id(chart, series, channel, snapshot.activity[channel]);
    }
    lv_chart_refresh(chart);

    const bool active = snapshot.peak_activity >= 8;
    lv_label_set_text(status_label, active ? "ACTIVITY" : "SCANNING");
    lv_obj_set_style_bg_color(status_dot,
                              active ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
    lv_label_set_text_fmt(peak_label, "CH %02u   %.3f GHz   %u%%",
                          snapshot.peak_channel,
                          (kChannelBaseMHz + snapshot.peak_channel) / 1000.0f,
                          snapshot.peak_activity);
    lv_label_set_text_fmt(sweep_label, "%lums   #%lu",
                          (unsigned long)snapshot.sweep_ms,
                          (unsigned long)snapshot.sweep_count);
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
    lv_label_set_text(peak_label, "CH --   -.--- GHz   --%");
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
    lv_chart_set_point_count(chart, kChannelCount);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_style_bg_color(chart, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(chart, 4, 0);
    lv_obj_set_style_pad_all(chart, 2, 0);
    lv_obj_set_style_pad_column(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, UI_COLOR_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
    series = lv_chart_add_series(chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
        lv_chart_set_value_by_id(chart, series, channel, 0);
    }

    lv_obj_t *axis = create_plain_container(page_container);
    lv_obj_set_size(axis, LV_PCT(100), 16);
    lv_obj_t *start = lv_label_create(axis);
    lv_label_set_text(start, "2.400");
    lv_obj_align(start, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_t *middle = lv_label_create(axis);
    lv_label_set_text(middle, "2.440");
    lv_obj_center(middle);
    lv_obj_t *end = lv_label_create(axis);
    lv_label_set_text(end, "2.479 GHz");
    lv_obj_align(end, LV_ALIGN_RIGHT_MID, -2, 0);
    for (lv_obj_t *label : {start, middle, end}) {
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    }
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
    if (show_menu) menu_show();
}

static void back_event(lv_event_t *)
{
    close_page(true);
}

static void ui_nrf24_scan_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "nRF24 Activity", back_event);
    create_scanner_ui();
    start_scan_task();
    ui_timer = lv_timer_create(ui_timer_cb, kUiRefreshMs, nullptr);
}

static void ui_nrf24_scan_exit(lv_obj_t *)
{
    close_page(false);
}

} // namespace

app_t ui_nrf24_scan_main = {
    .setup_func_cb = ui_nrf24_scan_enter,
    .exit_func_cb = ui_nrf24_scan_exit,
    .user_data = nullptr,
};

#endif
