/**
 * @file      ui_nrf24_scan.cpp
 * @brief     nRF24 2.4 GHz channel activity scanner.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_NRF24)

namespace {

static constexpr uint8_t kChannelCount = 80;
static constexpr uint8_t kBarMaxHeight = 75;
static constexpr uint16_t kChannelBaseMHz = 2400;

static lv_obj_t *page_container = nullptr;
static lv_obj_t *chart = nullptr;
static lv_chart_series_t *series = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *peak_label = nullptr;
static lv_obj_t *scan_button = nullptr;
static lv_timer_t *scan_timer = nullptr;
static uint8_t channel_index = 0;
static uint8_t bar_heights[kChannelCount] = {};
static uint8_t peak_channel = 0;
static bool scanning = false;
static bool radio_ready = false;
static bool scroll_mode = false;

static void set_status(const char *text)
{
    if (status_label) lv_label_set_text(status_label, text);
}

static void clear_samples()
{
    memset(bar_heights, 0, sizeof(bar_heights));
    peak_channel = 0;
    if (series) {
        for (uint8_t i = 0; i < kChannelCount; ++i) {
            lv_chart_set_value_by_id(chart, series, i, 0);
        }
        lv_chart_refresh(chart);
    }
}

static bool prepare_radio()
{
    if (!hw_has_nrf24()) {
        set_status("nRF24 is offline");
        return false;
    }

    // RPD is independent of packet decoding. Disable ACK/CRC so any 2.4 GHz
    // activity can be observed.
    instance.lockSPI();
    int16_t state = nrf24.setAutoAck(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setCrcFiltering(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAddressWidth(2);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setBitRate(1000);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setFrequency(kChannelBaseMHz);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.startReceive();
    instance.unlockSPI();

    radio_ready = state == RADIOLIB_ERR_NONE;
    if (!radio_ready) {
        char message[48];
        lv_snprintf(message, sizeof(message), "nRF24 setup failed: %d", state);
        set_status(message);
    }
    return radio_ready;
}

static bool read_channel(uint8_t channel)
{
    instance.lockSPI();
    int16_t state = nrf24.setFrequency((float)kChannelBaseMHz + channel);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.startReceive();
    instance.unlockSPI();
    if (state != RADIOLIB_ERR_NONE) return false;

    // nRF24 RPD becomes valid after a short receive window.
    delayMicroseconds(128);
    instance.lockSPI();
    const bool detected = nrf24.isCarrierDetected();
    nrf24.standby();
    instance.unlockSPI();
    return detected;
}

static void stop_scan()
{
    scanning = false;
    if (scan_timer) lv_timer_pause(scan_timer);
    if (radio_ready || hw_has_nrf24()) {
        instance.lockSPI();
        nrf24.standby();
        // Leave the shared radio in the normal Factory configuration so the
        // regular NRF24 app does not inherit scanner-only settings.
        nrf24.setFrequency(kChannelBaseMHz);
        nrf24.setBitRate(1000);
        nrf24.setAddressWidth(5);
        nrf24.setCrcFiltering(true);
        instance.unlockSPI();
    }
    radio_ready = false;
}

static void start_scan()
{
    stop_scan();
    clear_samples();
    channel_index = 0;
    if (!prepare_radio()) return;
    scanning = true;
    if (scan_timer) lv_timer_resume(scan_timer);
    set_status("Scanning 2.400-2.479 GHz...");
}

static void scan_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!scanning) return;

    const bool detected = read_channel(channel_index);
    const uint8_t sample_height = detected ? kBarMaxHeight : 0;

    // Stronger detections rise immediately; weaker samples decay gradually.
    if (sample_height > bar_heights[channel_index]) {
        bar_heights[channel_index] = sample_height;
    } else {
        bar_heights[channel_index] -=
            (bar_heights[channel_index] - sample_height) / 2;
    }
    lv_chart_set_value_by_id(chart, series, channel_index,
                             bar_heights[channel_index]);
    lv_chart_refresh(chart);

    if (bar_heights[channel_index] > bar_heights[peak_channel]) {
        peak_channel = channel_index;
    }
    char message[96];
    lv_snprintf(message, sizeof(message), "CH %u  %.3f GHz  %s",
                 channel_index, (kChannelBaseMHz + channel_index) / 1000.0f,
                 detected ? "activity" : "quiet");
    set_status(message);
    if (peak_label) {
        lv_label_set_text_fmt(peak_label, "Peak CH %u  %.3f GHz",
                              peak_channel,
                              (kChannelBaseMHz + peak_channel) / 1000.0f);
    }

    channel_index = (channel_index + 1) % kChannelCount;
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

static void scan_chart_event(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    const lv_event_code_t code = lv_event_get_code(event);
    lv_group_t *group = (lv_group_t *)lv_obj_get_group(obj);

    if (code == LV_EVENT_FOCUSED) {
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_ON);
    } else if (code == LV_EVENT_CLICKED) {
        lv_indev_t *indev = lv_event_get_indev(event);
        if (indev && (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER ||
                      lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD)) {
            scroll_mode = !scroll_mode;
            if (group) lv_group_set_editing(group, scroll_mode);
        }
    } else if (code == LV_EVENT_ROTARY && scroll_mode) {
        const int32_t diff = lv_event_get_rotary_diff(event);
        lv_obj_scroll_to_y(page_container,
                           lv_obj_get_scroll_y(page_container) + diff * 24,
                           LV_ANIM_OFF);
        lv_event_stop_processing(event);
    } else if (code == LV_EVENT_KEY && scroll_mode) {
        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
            lv_obj_scroll_to_y(page_container,
                               lv_obj_get_scroll_y(page_container) + 24,
                               LV_ANIM_OFF);
            lv_event_stop_processing(event);
        } else if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
            lv_obj_scroll_to_y(page_container,
                               lv_obj_get_scroll_y(page_container) - 24,
                               LV_ANIM_OFF);
            lv_event_stop_processing(event);
        } else if (key == LV_KEY_ESC || key == LV_KEY_ENTER) {
            scroll_mode = false;
            if (group) lv_group_set_editing(group, false);
            lv_event_stop_processing(event);
        }
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_DELETE) {
        scroll_mode = false;
        if (group) lv_group_set_editing(group, false);
    }
}

static void back_event(lv_event_t *event)
{
    (void)event;
    stop_scan();
    scroll_mode = false;
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
    scan_button = nullptr;
    menu_show();
}

static void ui_nrf24_scan_enter(lv_obj_t *parent)
{
    if (!hw_has_nrf24()) {
        page_container = ui_create_app_page(parent, "nRF24 Scan", back_event);
        status_label = lv_label_create(page_container);
        lv_label_set_text(status_label, "nRF24 module was not detected");
        return;
    }

    page_container = ui_create_app_page(parent, "nRF24 Scan", back_event);
    lv_obj_t *state_card = ui_create_card(page_container, "2.4 GHz Activity");

    status_label = lv_label_create(state_card);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_width(status_label, LV_PCT(100));
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_PRIMARY, 0);

    chart = lv_chart_create(state_card);
    lv_obj_set_size(chart, LV_PCT(100), 84);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart, kChannelCount);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, kBarMaxHeight);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_style(chart, &ui_styles.accent_focus_ring, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(chart, scan_chart_event, LV_EVENT_ALL, nullptr);
    if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, chart);
    lv_obj_set_style_bg_color(chart, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_pad_column(chart, 1, LV_PART_MAIN);
    series = lv_chart_add_series(chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    clear_samples();

    lv_obj_t *range_label = lv_label_create(state_card);
    lv_label_set_text(range_label, "2.400 GHz       2.440 GHz       2.479 GHz");
    lv_obj_set_width(range_label, LV_PCT(100));
    lv_obj_set_style_text_color(range_label, UI_COLOR_TEXT_SECONDARY, 0);

    peak_label = lv_label_create(state_card);
    lv_label_set_text(peak_label, "Peak CH 0  2.400 GHz");
    lv_obj_set_width(peak_label, LV_PCT(100));
    lv_obj_set_style_text_color(peak_label, UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *control_card = ui_create_card(page_container, "Scanner");
    scan_button = lv_btn_create(control_card);
    lv_obj_set_size(scan_button, LV_PCT(100), 36);
    lv_obj_add_style(scan_button, &ui_styles.accent_btn, 0);
    ui_add_accent_focus_style(scan_button);
    lv_label_set_text(lv_label_create(scan_button), LV_SYMBOL_PLAY " Start");
    lv_obj_center(lv_obj_get_child(scan_button, 0));
    lv_obj_add_event_cb(scan_button, scan_button_event, LV_EVENT_CLICKED, nullptr);

    scan_timer = lv_timer_create(scan_tick, 12, nullptr);
    lv_timer_pause(scan_timer);
}

static void ui_nrf24_scan_exit(lv_obj_t *parent)
{
    (void)parent;
    back_event(nullptr);
}

} // namespace

app_t ui_nrf24_scan_main = {
    .setup_func_cb = ui_nrf24_scan_enter,
    .exit_func_cb = ui_nrf24_scan_exit,
    .user_data = nullptr,
};

#endif // !EXCLUDE_NRF24
