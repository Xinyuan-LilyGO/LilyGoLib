/**
 * @file      ui_cc1101_scan.cpp
 * @brief     CC1101 RSSI scan for the T-Embed-CC1101.
 */
#include "ui_define.h"

#if defined(ARDUINO_T_EMBED_CC1101) && !defined(EXCLUDE_CC1101_SCAN)

#include <math.h>

namespace {

static constexpr float kScanFrequencies[] = {
    300.000f, 302.757f, 303.875f, 303.900f, 304.250f, 307.000f,
    307.500f, 307.800f, 309.000f, 310.000f, 312.000f, 312.100f,
    312.200f, 313.000f, 313.850f, 314.000f, 314.350f, 314.980f,
    315.000f, 318.000f, 330.000f, 345.000f, 348.000f, 350.000f,
    387.000f, 390.000f, 418.000f, 430.000f, 430.500f, 431.000f,
    431.500f, 433.075f, 433.220f, 433.420f, 433.657f, 433.889f,
    433.920f, 434.075f, 434.177f, 434.190f, 434.390f, 434.420f,
    434.620f, 434.775f, 438.900f, 440.175f, 464.000f, 467.750f,
    779.000f, 868.350f, 868.400f, 868.800f, 868.950f, 906.400f,
    915.000f, 925.000f, 928.000f,
};

static constexpr uint8_t kRangeStart[] = {0, 24, 48, 0};
static constexpr uint8_t kRangeEnd[] = {23, 47, 56, 56};
static constexpr const char *kRangeNames[] = {
    "300-350 MHz", "387-468 MHz", "779-928 MHz", "All ranges"
};
static constexpr size_t kFrequencyCount = sizeof(kScanFrequencies) / sizeof(kScanFrequencies[0]);

static lv_obj_t *page_container = nullptr;
static lv_obj_t *chart = nullptr;
static lv_chart_series_t *series = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *peak_label = nullptr;
static lv_obj_t *range_dropdown = nullptr;
static lv_obj_t *scan_button = nullptr;
static lv_timer_t *scan_timer = nullptr;
static uint8_t range_index = 3;
static uint8_t scan_index = 0;
static bool scanning = false;
static bool radio_ready = false;
static float rssi_values[kFrequencyCount];
static float peak_rssi[kFrequencyCount];

static void set_status(const char *text)
{
    if (status_label) lv_label_set_text(status_label, text);
}

static void clear_samples()
{
    for (size_t i = 0; i < kFrequencyCount; ++i) {
        rssi_values[i] = -100.0f;
        peak_rssi[i] = -100.0f;
        if (series) lv_chart_set_value_by_id(chart, series, i, -100);
    }
    if (chart) lv_chart_refresh(chart);
}

static bool prepare_radio()
{
    if (!(hw_get_device_online() & HW_RADIO_ONLINE)) {
        set_status("CC1101 is offline");
        return false;
    }

    const float frequency = kScanFrequencies[kRangeStart[range_index]];
    instance.lockSPI();
    int16_t state = instance.setRFFrequencyBand(frequency);
    if (state == RADIOLIB_ERR_NONE) state = radio.setOOK(true);
    if (state == RADIOLIB_ERR_NONE) state = radio.setRxBandwidth(812.0f);
    if (state == RADIOLIB_ERR_NONE) state = radio.setBitRate(38.4f);
    if (state == RADIOLIB_ERR_NONE) state = radio.startReceive();
    instance.unlockSPI();

    radio_ready = state == RADIOLIB_ERR_NONE;
    if (!radio_ready) {
        char message[48];
        lv_snprintf(message, sizeof(message), "CC1101 setup failed: %d", state);
        set_status(message);
    }
    return radio_ready;
}

static int read_rssi(float frequency)
{
    instance.lockSPI();
    int16_t state = instance.setRFFrequencyBand(frequency);
    if (state == RADIOLIB_ERR_NONE) state = radio.startReceive();
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) return -100;

    // Allow the RSSI filter and AGC to settle at the new frequency.
    delay(5);
    instance.lockSPI();
    const float rssi = radio.getRSSI();
    radio.standby();
    instance.unlockSPI();
    if (!isfinite(rssi)) return -100;
    return constrain((int)lroundf(rssi), -100, -20);
}

static void stop_scan()
{
    scanning = false;
    if (scan_timer) {
        lv_timer_pause(scan_timer);
    }
    if (radio_ready) {
        instance.lockSPI();
        radio.standby();
        instance.unlockSPI();
    }
    radio_ready = false;
}

static void start_scan()
{
    stop_scan();
    clear_samples();
    scan_index = kRangeStart[range_index];
    if (!prepare_radio()) return;
    scanning = true;
    if (scan_timer) lv_timer_resume(scan_timer);
    set_status("Scanning...");
}

static void scan_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!scanning) return;

    const uint8_t start = kRangeStart[range_index];
    const uint8_t end = kRangeEnd[range_index];
    if (scan_index < start || scan_index > end) scan_index = start;

    const int rssi = read_rssi(kScanFrequencies[scan_index]);
    const size_t index = scan_index;
    rssi_values[index] = (float)rssi;
    if (rssi > peak_rssi[index]) peak_rssi[index] = (float)rssi;

    // Keep a visible bar for a little longer to make short peaks easier to see.
    int display_rssi = (int)rssi_values[index];
    if (display_rssi < -100) display_rssi = -100;
    lv_chart_set_value_by_id(chart, series, index, display_rssi);
    lv_chart_refresh(chart);

    int strongest = start;
    for (uint8_t i = start; i <= end; ++i) {
        if (peak_rssi[i] > peak_rssi[strongest]) strongest = i;
    }
    char message[96];
    lv_snprintf(message, sizeof(message), "%.3f MHz  %d dBm",
                 kScanFrequencies[index], rssi);
    set_status(message);
    if (peak_label) {
        lv_label_set_text_fmt(peak_label, "Peak %.3f MHz  %.0f dBm",
                              kScanFrequencies[strongest], peak_rssi[strongest]);
    }

    ++scan_index;
    if (scan_index > end) scan_index = start;
}

static void range_changed(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *dropdown = (lv_obj_t *)lv_event_get_target(event);
    range_index = (uint8_t)lv_dropdown_get_selected(dropdown);
    if (scanning) start_scan();
    else clear_samples();
    lv_label_set_text_fmt(peak_label, "Range: %s", kRangeNames[range_index]);
}

static void scan_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (scanning) {
        stop_scan();
        lv_label_set_text(lv_obj_get_child(scan_button, 0), LV_SYMBOL_PLAY " Start");
        set_status("Scan stopped");
    } else {
        start_scan();
        lv_label_set_text(lv_obj_get_child(scan_button, 0),
                         scanning ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Start");
    }
}

static void back_event(lv_event_t *event)
{
    (void)event;
    stop_scan();
    if (scan_timer) {
        lv_timer_delete(scan_timer);
        scan_timer = nullptr;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    chart = nullptr;
    series = nullptr;
    status_label = nullptr;
    peak_label = nullptr;
    range_dropdown = nullptr;
    scan_button = nullptr;
    menu_show();
}

} // namespace

static void ui_cc1101_scan_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "CC1101 Scan", back_event);
    lv_obj_t *state_card = ui_create_card(page_container, "RSSI Spectrum");

    status_label = lv_label_create(state_card);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_width(status_label, LV_PCT(100));
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_PRIMARY, 0);

    chart = lv_chart_create(state_card);
    lv_obj_set_size(chart, LV_PCT(100), 84);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart, kFrequencyCount);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_style_bg_color(chart, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, UI_COLOR_DIVIDER, 0);
    series = lv_chart_add_series(chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    clear_samples();

    peak_label = lv_label_create(state_card);
    lv_label_set_text(peak_label, "Range: All ranges");
    lv_obj_set_width(peak_label, LV_PCT(100));
    lv_obj_set_style_text_color(peak_label, UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *control_card = ui_create_card(page_container, "Scan Range");
    range_dropdown = lv_dropdown_create(control_card);
    lv_dropdown_set_options(range_dropdown,
                            "300-350 MHz\n387-468 MHz\n779-928 MHz\nAll ranges");
    lv_dropdown_set_selected(range_dropdown, range_index);
    lv_obj_set_width(range_dropdown, LV_PCT(100));
    lv_obj_add_event_cb(range_dropdown, range_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    scan_button = lv_btn_create(control_card);
    lv_obj_set_size(scan_button, LV_PCT(100), 36);
    lv_obj_add_style(scan_button, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(scan_button);
    lv_label_set_text(lv_label_create(scan_button), LV_SYMBOL_PLAY " Start");
    lv_obj_center(lv_obj_get_child(scan_button, 0));
    lv_obj_add_event_cb(scan_button, scan_button_event, LV_EVENT_CLICKED, nullptr);

    scan_timer = lv_timer_create(scan_tick, 25, nullptr);
    lv_timer_pause(scan_timer);
}

static void ui_cc1101_scan_exit(lv_obj_t *parent)
{
    (void)parent;
    back_event(nullptr);
}

app_t ui_cc1101_scan_main = {
    .setup_func_cb = ui_cc1101_scan_enter,
    .exit_func_cb = ui_cc1101_scan_exit,
    .user_data = nullptr,
};

#endif
