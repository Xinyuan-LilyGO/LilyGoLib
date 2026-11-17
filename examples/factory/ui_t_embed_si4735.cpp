/**
 * @file      ui_t_embed_si4735.cpp
 * @brief     Compact SI4735 radio UI for the original 320 x 170 T-Embed.
 * @license   MIT
 */
#include "ui_define.h"

#if defined(ARDUINO_T_EMBED) && !defined(EXCLUDE_SI4735_RADIO_WF)

LV_FONT_DECLARE(font_alibaba_24);

static lv_obj_t *page_container = nullptr;
static lv_obj_t *frequency_label = nullptr;
static lv_obj_t *signal_label = nullptr;
static lv_obj_t *band_label = nullptr;
static lv_obj_t *step_label = nullptr;
static lv_obj_t *volume_label = nullptr;
static lv_obj_t *volume_slider = nullptr;
static lv_timer_t *update_timer = nullptr;
static uint8_t current_volume = 30;

static void update_frequency()
{
    const uint16_t frequency = hw_si4735_get_freq();
    if (hw_si4735_is_fm()) {
        lv_label_set_text_fmt(frequency_label, "%u.%02u MHz",
                              frequency / 100, frequency % 100);
    } else {
        lv_label_set_text_fmt(frequency_label, "%u kHz", frequency);
    }
    lv_label_set_text_fmt(band_label, "%s  %s", hw_si4735_get_band_name(),
                          hw_si4735_is_fm() ? "FM" : "AM");
    const uint16_t step = hw_si4735_get_current_step();
    lv_label_set_text_fmt(step_label, "%u k", hw_si4735_is_fm() ? step * 10 : step);
}

static void update_signal()
{
    lv_label_set_text_fmt(signal_label, "RSSI %u  SNR %u",
                          hw_si4735_get_rssi(), hw_si4735_get_snr());
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_frequency();
    update_signal();
}

static lv_obj_t *make_row(lv_coord_t height)
{
    lv_obj_t *row = lv_obj_create(page_container);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 66, 28);
    lv_obj_set_style_radius(button, 5, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, UI_COLOR_DIVIDER, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(label);
    return label;
}

static void tune_down_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_set_freq_down();
    update_frequency();
}

static void tune_up_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_set_freq_up();
    update_frequency();
}

static void seek_down_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_seek_down();
    update_frequency();
}

static void seek_up_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_seek_up();
    update_frequency();
}

static void band_down_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_band_down();
    update_frequency();
}

static void band_up_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_band_up();
    update_frequency();
}

static void step_cb(lv_event_t *event)
{
    (void)event;
    hw_si4735_update_steps();
    update_frequency();
}

static void volume_cb(lv_event_t *event)
{
    current_volume = lv_slider_get_value((lv_obj_t *)lv_event_get_target(event));
    hw_si4735_set_volume(current_volume);
    lv_label_set_text_fmt(volume_label, "%u", current_volume);
}

static void close_page()
{
    if (update_timer) {
        lv_timer_delete(update_timer);
        update_timer = nullptr;
    }
    hw_si4735_set_power(false);
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    frequency_label = signal_label = band_label = step_label = nullptr;
    volume_label = volume_slider = nullptr;
}

static void back_event_handler(lv_event_t *event)
{
    (void)event;
    close_page();
    menu_show();
}

static void ui_t_embed_si4735_enter(lv_obj_t *parent)
{
    set_low_power_mode_flag(false);
    page_container = ui_create_app_page(parent, "Radio", back_event_handler);
    lv_obj_set_style_pad_hor(page_container, 7, 0);
    lv_obj_set_style_pad_ver(page_container, 3, 0);
    lv_obj_set_style_pad_row(page_container, 3, 0);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_row = make_row(33);
    frequency_label = lv_label_create(status_row);
    lv_obj_set_width(frequency_label, 162);
    lv_obj_set_style_text_font(frequency_label, &font_alibaba_24, 0);
    lv_obj_set_style_text_color(frequency_label, UI_COLOR_ACCENT, 0);

    signal_label = lv_label_create(status_row);
    lv_obj_set_width(signal_label, 130);
    lv_obj_set_style_text_align(signal_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(signal_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(signal_label, UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *tune_row = make_row(28);
    make_button(tune_row, LV_SYMBOL_PREV, seek_down_cb);
    make_button(tune_row, LV_SYMBOL_MINUS, tune_down_cb);
    make_button(tune_row, LV_SYMBOL_PLUS, tune_up_cb);
    make_button(tune_row, LV_SYMBOL_NEXT, seek_up_cb);

    lv_obj_t *band_row = make_row(28);
    make_button(band_row, "B-", band_down_cb);
    band_label = lv_label_create(band_row);
    lv_obj_set_width(band_label, 80);
    lv_obj_set_style_text_align(band_label, LV_TEXT_ALIGN_CENTER, 0);
    step_label = make_button(band_row, "1 k", step_cb);
    lv_obj_set_width(lv_obj_get_parent(step_label), 62);
    make_button(band_row, "B+", band_up_cb);

    lv_obj_t *volume_row = make_row(25);
    lv_obj_t *volume_icon = lv_label_create(volume_row);
    lv_label_set_text(volume_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(volume_icon, UI_COLOR_ACCENT, 0);

    volume_slider = lv_slider_create(volume_row);
    lv_obj_set_size(volume_slider, 240, 10);
    lv_slider_set_range(volume_slider, 0, 63);
    lv_slider_set_value(volume_slider, current_volume, LV_ANIM_OFF);
    ui_prepare_slider_for_encoder(volume_slider);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    volume_label = lv_label_create(volume_row);
    lv_obj_set_width(volume_label, 24);
    lv_obj_set_style_text_align(volume_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(volume_label, "%u", current_volume);

    hw_si4735_set_power(true);
    hw_si4735_set_volume(current_volume);
    update_frequency();
    update_signal();
    update_timer = lv_timer_create(update_timer_cb, 350, nullptr);
}

static void ui_t_embed_si4735_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page();
}

app_t ui_t_embed_si4735_main = {
    .setup_func_cb = ui_t_embed_si4735_enter,
    .exit_func_cb = ui_t_embed_si4735_exit,
    .user_data = nullptr,
};

#endif
