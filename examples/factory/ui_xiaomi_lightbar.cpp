/**
 * @file      ui_xiaomi_lightbar.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-10-02
 *
 * MJGJD01YL light controls with header settings for remote identity and pairing.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if !defined(EXCLUDE_NRF24)

#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace {

constexpr uint32_t FALLBACK_REMOTE_ID = 0x111111;
constexpr uint32_t SLIDER_SEND_DELAY_MS = 120;
constexpr uint8_t CAPTURE_CONFIRM_FRAMES = 2;
constexpr uint16_t COMMAND_TOGGLE = 0x0100;
constexpr uint16_t COMMAND_COOLER = 0x0200;
constexpr uint16_t COMMAND_WARMER = 0x0300;
constexpr uint16_t COMMAND_BRIGHTER = 0x0400;
constexpr uint16_t COMMAND_DIMMER = 0x0500;
constexpr uint16_t COMMAND_RESET = 0x0600;

lv_obj_t *page_container = nullptr;
lv_obj_t *settings_button = nullptr;
lv_obj_t *settings_msgbox = nullptr;
lv_obj_t *id_card = nullptr;
lv_obj_t *capture_label = nullptr;
bool id_keyboard_transition = false;
bool settings_closing = false;
lv_obj_t *id_textarea = nullptr;
lv_obj_t *id_keyboard = nullptr;
ui_soft_keyboard_lift_t id_keyboard_lift = {nullptr, nullptr, -1};
lv_obj_t *status_label = nullptr;
lv_obj_t *brightness_slider = nullptr;
lv_obj_t *brightness_value = nullptr;
lv_obj_t *temperature_slider = nullptr;
lv_obj_t *temperature_value = nullptr;
lv_obj_t *unavailable_msgbox = nullptr;
lv_timer_t *brightness_send_timer = nullptr;
lv_timer_t *temperature_send_timer = nullptr;
lv_timer_t *capture_timer = nullptr;

uint32_t remote_id = FALLBACK_REMOTE_ID;
uint8_t packet_sequence = 0;
uint32_t capture_candidate_id = 0;
uint8_t capture_candidate_hits = 0;
bool capture_active = false;

uint32_t generated_remote_id()
{
#ifdef ARDUINO
    const uint32_t id = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
    if (id != 0 && id != 0xFFFFFF) return id;
#endif
    return FALLBACK_REMOTE_ID;
}

void set_status(const char *text)
{
    if (status_label) {
        lv_label_set_text(status_label, text);
    }
}

void load_remote_id()
{
#ifdef ARDUINO
    Preferences preferences;
    if (preferences.begin("mi-lightbar", false)) {
        if (preferences.isKey("remote_id")) {
            remote_id = preferences.getUInt("remote_id", generated_remote_id()) & 0xFFFFFF;
            if (remote_id == FALLBACK_REMOTE_ID) {
                remote_id = generated_remote_id();
                preferences.putUInt("remote_id", remote_id);
            }
        } else {
            remote_id = generated_remote_id();
            preferences.putUInt("remote_id", remote_id);
        }
        preferences.end();
        return;
    }
#endif
    remote_id = generated_remote_id();
}

void save_remote_id()
{
#ifdef ARDUINO
    Preferences preferences;
    if (preferences.begin("mi-lightbar", false)) {
        preferences.putUInt("remote_id", remote_id);
        preferences.end();
    }
#endif
}

void stop_remote_capture()
{
    if (capture_active) hw_stop_xiaomi_lightbar_remote_capture();
    capture_active = false;
    capture_candidate_id = 0;
    capture_candidate_hits = 0;
    if (capture_timer) lv_timer_pause(capture_timer);
    if (capture_label) lv_label_set_text(capture_label, "Scan original remote");
}

void capture_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!capture_active) return;

    uint32_t detected_id = 0;
    uint8_t channel = 0;
    if (!hw_poll_xiaomi_lightbar_remote(detected_id, channel)) return;

    if (capture_candidate_id == detected_id) {
        ++capture_candidate_hits;
    } else {
        capture_candidate_id = detected_id;
        capture_candidate_hits = 1;
    }

    char message[64];
    snprintf(message, sizeof(message), "ID %06lX CH%u (%u/%u)",
             (unsigned long)detected_id, channel, capture_candidate_hits,
             CAPTURE_CONFIRM_FRAMES);
    set_status(message);
    if (capture_candidate_hits < CAPTURE_CONFIRM_FRAMES) return;

    remote_id = detected_id;
    char formatted[7];
    snprintf(formatted, sizeof(formatted), "%06lX", (unsigned long)remote_id);
    if (id_textarea) lv_textarea_set_text(id_textarea, formatted);
    save_remote_id();
    stop_remote_capture();

    snprintf(message, sizeof(message), "Remote %s captured", formatted);
    set_status(message);
    hw_feedback();
}

void capture_button_cb(lv_event_t *event)
{
    (void)event;
    if (capture_active) {
        stop_remote_capture();
        set_status("Remote ID scan stopped");
        return;
    }

    const int16_t state = hw_start_xiaomi_lightbar_remote_capture();
    if (state != 0) {
        char message[40];
        snprintf(message, sizeof(message), "Scan failed (%d)", state);
        set_status(message);
        return;
    }

    capture_active = true;
    if (capture_label) lv_label_set_text(capture_label, "Stop scanning");
    capture_candidate_id = 0;
    capture_candidate_hits = 0;
    if (capture_timer) {
        lv_timer_reset(capture_timer);
        lv_timer_resume(capture_timer);
    }
    set_status("Turn or press original remote");
}

void commit_remote_id()
{
    if (!id_textarea) return;

    const uint32_t previous_id = remote_id;
    const char *text = lv_textarea_get_text(id_textarea);
    if (!text || text[0] == '\0') {
        remote_id = generated_remote_id();
    } else {
        remote_id = (uint32_t)strtoul(text, nullptr, 16) & 0xFFFFFF;
    }

    char formatted[7];
    snprintf(formatted, sizeof(formatted), "%06lX", (unsigned long)remote_id);
    lv_textarea_set_text(id_textarea, formatted);
    if (remote_id != previous_id) save_remote_id();

    char status[40];
    snprintf(status, sizeof(status), "Remote %s saved", formatted);
    set_status(status);
}

int16_t send_command(uint16_t command)
{
    stop_remote_capture();
    const int16_t state = hw_send_xiaomi_lightbar_command(remote_id, command,
                          packet_sequence++);
    if (state != 0) {
        char message[40];
        snprintf(message, sizeof(message), "Send failed (%d)", state);
        set_status(message);
        if (!status_label) ui_msg_pop_up("Mi Light Bar", message);
    }
    return state;
}

void send_absolute_brightness(uint8_t value)
{
    if (send_command(COMMAND_DIMMER - 16) == 0 &&
            send_command(COMMAND_BRIGHTER + value) == 0) {
        char message[40];
        snprintf(message, sizeof(message), "Brightness set to %u", value);
        set_status(message);
        hw_feedback();
    }
}

void send_absolute_temperature(uint8_t value)
{
    if (send_command(COMMAND_WARMER - 16) == 0 &&
            send_command(COMMAND_COOLER + value) == 0) {
        const unsigned kelvin = 2700U + ((unsigned)value * 3800U + 7U) / 15U;
        char message[44];
        snprintf(message, sizeof(message), "Color set to %u K", kelvin);
        set_status(message);
        hw_feedback();
    }
}

void update_brightness_value()
{
    if (!brightness_slider || !brightness_value) return;
    char value[12];
    snprintf(value, sizeof(value), "%ld / 15", (long)lv_slider_get_value(brightness_slider));
    lv_label_set_text(brightness_value, value);
}

void update_temperature_value()
{
    if (!temperature_slider || !temperature_value) return;
    const unsigned level = (unsigned)lv_slider_get_value(temperature_slider);
    const unsigned kelvin = 2700U + (level * 3800U + 7U) / 15U;
    char value[20];
    snprintf(value, sizeof(value), "%u K", kelvin);
    lv_label_set_text(temperature_value, value);
}

void schedule_slider_send(lv_timer_t *timer)
{
    if (!timer) return;
    if (lv_timer_get_paused(timer)) {
        lv_timer_reset(timer);
        lv_timer_resume(timer);
    }
}

void brightness_send_timer_cb(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    if (brightness_slider) {
        send_absolute_brightness((uint8_t)lv_slider_get_value(brightness_slider));
    }
}

void temperature_send_timer_cb(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    if (temperature_slider) {
        send_absolute_temperature((uint8_t)lv_slider_get_value(temperature_slider));
    }
}

void finish_id_edit()
{
    if (id_keyboard_transition) return;
    id_keyboard_transition = true;
    commit_remote_id();
    ui_soft_keyboard_hide(id_keyboard, &id_keyboard_lift);
    if (id_card) lv_obj_remove_local_style_prop(id_card, LV_STYLE_TEXT_FONT, 0);
    if (id_textarea) {
        lv_obj_remove_state(id_textarea, LV_STATE_EDITED);
        if (auto *group = lv_obj_get_group(id_textarea)) lv_group_set_editing(group, false);
    }
    disable_keyboard();
    id_keyboard_transition = false;
}

void id_event_cb(lv_event_t *event)
{
    if (settings_closing || id_keyboard_transition || ui_soft_keyboard_focus_transition_active()) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED ||
            (code == LV_EVENT_FOCUSED && lv_obj_has_state(id_textarea, LV_STATE_EDITED))) {
        enable_keyboard();
        if (id_keyboard) {
            if (!id_keyboard_lift.lifted_obj) {
                lv_obj_set_style_text_font(id_card, lv_obj_get_style_text_font(id_card, LV_PART_MAIN), 0);
            }
            ui_soft_keyboard_show(id_keyboard, id_textarea, id_card, &id_keyboard_lift);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
               (code == LV_EVENT_DEFOCUSED && !ui_soft_keyboard_focus_transition_active())) {
        finish_id_edit();
    }
}

void brightness_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        update_brightness_value();
        schedule_slider_send(brightness_send_timer);
    }
}

void temperature_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        update_temperature_value();
        schedule_slider_send(temperature_send_timer);
    }
}

lv_obj_t *layout_panel(lv_obj_t *parent)
{
    auto *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    return panel;
}

lv_obj_t *text_label(lv_obj_t *parent, const char *text)
{
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_PRIMARY, 0);
    return label;
}

void focus_control(lv_event_t *event)
{
    lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(event), LV_ANIM_ON);
}

void create_level_control(lv_obj_t *parent, bool brightness)
{
    auto *card = layout_panel(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    auto *heading = layout_panel(card);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 6, 0);
    auto *title = text_label(heading, brightness ? "Brightness" : "Color temperature");
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    auto *value = text_label(heading, "");
    lv_obj_add_style(value, &ui_styles.accent_text, 0);
    lv_obj_remove_local_style_prop(value, LV_STYLE_TEXT_COLOR, 0);

    auto *slider = lv_slider_create(card);
    lv_obj_set_size(slider, LV_PCT(100), 8);
    lv_slider_set_range(slider, 0, 15);
    lv_slider_set_value(slider, 8, LV_ANIM_OFF);
    lv_obj_add_style(slider, &ui_styles.slider_track, LV_PART_MAIN);
    lv_obj_add_style(slider, &ui_styles.slider_knob, LV_PART_KNOB);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(slider, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_style_margin_bottom(slider, 6, 0);
    ui_add_accent_focus_style(slider);
    ui_prepare_slider_for_encoder(slider);
    if (brightness) {
        brightness_slider = slider;
        brightness_value = value;
        lv_obj_add_event_cb(slider, brightness_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
        update_brightness_value();
    } else {
        temperature_slider = slider;
        temperature_value = value;
        lv_obj_add_event_cb(slider, temperature_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
        update_temperature_value();
    }
}

lv_obj_t *action_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    auto *button = lv_button_create(parent);
    lv_obj_set_size(button, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_add_style(button, &ui_styles.card, 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    lv_obj_set_style_radius(button, 8, 0);
    ui_add_accent_focus_style(button);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(button, focus_control, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    auto *label = text_label(button, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return button;
}

void toggle_cb(lv_event_t *event)
{
    (void)event;
    if (send_command(COMMAND_TOGGLE) == 0) {
        set_status("Power toggled");
        hw_feedback();
    }
}

void reset_cb(lv_event_t *event)
{
    (void)event;
    if (send_command(COMMAND_RESET) == 0) {
        char message[40];
        snprintf(message, sizeof(message), "Pair sent for %06lX", (unsigned long)remote_id);
        set_status(message);
        hw_feedback();
    }
}

void settings_deleted(lv_event_t *)
{
    settings_closing = true;
    finish_id_edit();
    stop_remote_capture();
    if (id_keyboard) lv_obj_delete(id_keyboard);
    id_keyboard = id_textarea = id_card = capture_label = status_label = settings_msgbox = nullptr;
    id_keyboard_lift = {nullptr, nullptr, -1};
    settings_closing = false;
}

void open_settings(lv_event_t *)
{
    if (settings_msgbox) return;
    // Finish deferred control changes before capture can take over the radio.
    if (brightness_send_timer && !lv_timer_get_paused(brightness_send_timer)) brightness_send_timer_cb(brightness_send_timer);
    if (temperature_send_timer && !lv_timer_get_paused(temperature_send_timer)) temperature_send_timer_cb(temperature_send_timer);
    settings_msgbox = ui_create_settings_page(page_container, "Light bar settings");
    lv_obj_add_event_cb(settings_msgbox, settings_deleted, LV_EVENT_DELETE, nullptr);
    auto *body = settings_msgbox;
    if (lv_obj_get_group(body)) lv_group_remove_obj(body);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_pad_row(body, 8, 0);

    id_card = layout_panel(body);
    lv_obj_set_size(id_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(id_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(id_card, 4, 0);
    lv_obj_set_style_pad_row(id_card, 4, 0);
    lv_obj_add_style(id_card, &ui_styles.card, 0);
    lv_obj_set_style_bg_opa(id_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(id_card, 8, 0);
    text_label(id_card, "Remote ID (hex)");
    id_textarea = lv_textarea_create(id_card);
    lv_obj_set_width(id_textarea, LV_PCT(100));
    lv_textarea_set_one_line(id_textarea, true);
    lv_textarea_set_accepted_chars(id_textarea, "0123456789ABCDEFabcdef");
    lv_textarea_set_max_length(id_textarea, 6);
    char id_text[7];
    snprintf(id_text, sizeof(id_text), "%06lX", (unsigned long)remote_id);
    lv_textarea_set_text(id_textarea, id_text);
    ui_prepare_textarea_for_encoder(id_textarea);
    ui_add_accent_focus_style(id_textarea);
    lv_obj_add_event_cb(id_textarea, id_event_cb, LV_EVENT_ALL, nullptr);

    auto *capture_button = action_button(body, "Scan original remote", capture_button_cb);
    capture_label = lv_obj_get_child(capture_button, 0);
    status_label = text_label(body, "Turn or press the original remote while scanning.");
    lv_obj_set_width(status_label, LV_PCT(100));
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    // Let the encoder reveal the complete status text in a short viewport.
    lv_obj_set_style_radius(status_label, 8, 0);
    lv_obj_add_flag(status_label, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ON_FOCUS |
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    ui_add_accent_focus_style(status_label);
    if (auto *group = lv_group_get_default()) lv_group_add_obj(group, status_label);
    lv_obj_add_event_cb(status_label, focus_control, LV_EVENT_FOCUSED, nullptr);
    action_button(body, "Pair / reset", reset_cb);
    if (ui_soft_keyboard_should_open()) {
        id_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_add_flag(id_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (lv_obj_get_group(id_keyboard)) lv_group_remove_obj(id_keyboard);
        auto ready = [](lv_event_t *) {
            finish_id_edit();
            if (id_textarea) lv_group_focus_obj(id_textarea);
        };
        lv_obj_add_event_cb(id_keyboard, ready, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(id_keyboard, ready, LV_EVENT_CANCEL, nullptr);
    }
    lv_group_focus_obj(id_textarea);
}

void cleanup()
{
    if (settings_msgbox) ui_destroy_settings_page(settings_msgbox);
    if (unavailable_msgbox) {
        destroy_msgbox(unavailable_msgbox);
        unavailable_msgbox = nullptr;
    }
    disable_keyboard();
    stop_remote_capture();
    for (auto **timer : {&capture_timer, &brightness_send_timer, &temperature_send_timer}) {
        if (*timer) lv_timer_delete(*timer);
        *timer = nullptr;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    settings_button = nullptr;
    brightness_slider = brightness_value = temperature_slider = temperature_value = nullptr;
}

void back_event_handler(lv_event_t *)
{
    cleanup();
    menu_show();
}

void unavailable_cb(lv_event_t *event)
{
    (void)event;
    if (unavailable_msgbox) {
        destroy_msgbox(unavailable_msgbox);
        unavailable_msgbox = nullptr;
    }
    menu_show();
}

} // namespace

void ui_xiaomi_lightbar_enter(lv_obj_t *parent)
{
    if (!hw_has_nrf24()) {
        static const char *buttons[] = {"OK", ""};
        unavailable_msgbox = create_msgbox(lv_screen_active(), "Mi Light Bar",
                                            "NRF24 module was not detected.",
                                            buttons, unavailable_cb, nullptr);
        return;
    }

    load_remote_id();
    packet_sequence = (uint8_t)(millis() & 0xFF);
    page_container = ui_create_app_page(parent, "Mi Light Bar", back_event_handler);

    settings_button = ui_app_page_add_header_button(page_container, LV_SYMBOL_SETTINGS, open_settings);
    const int width = lv_display_get_horizontal_resolution(nullptr);
    const int height = lv_display_get_vertical_resolution(nullptr);
    const bool horizontal = width >= 300 && width > height;
    lv_obj_set_style_pad_all(page_container, 4, 0);
    lv_obj_set_style_pad_gap(page_container, 8, 0);
    lv_obj_set_flex_flow(page_container, horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *power_panel = layout_panel(page_container);
    lv_obj_set_size(power_panel, horizontal ? LV_PCT(25) : LV_PCT(100),
                    horizontal ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(power_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(power_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *power = action_button(power_panel, LV_SYMBOL_POWER "  Power", toggle_cb);
    lv_obj_set_size(power, horizontal ? LV_PCT(100) : LV_PCT(65), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(power, 36, 0);
    lv_obj_align(power, LV_ALIGN_CENTER, 0, 0);

    auto *controls = layout_panel(page_container);
    lv_obj_set_size(controls, horizontal ? 0 : LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(controls, horizontal ? 1 : 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(controls, 8, 0);
    create_level_control(controls, true);
    create_level_control(controls, false);

    brightness_send_timer = lv_timer_create(brightness_send_timer_cb,
                                            SLIDER_SEND_DELAY_MS, nullptr);
    temperature_send_timer = lv_timer_create(temperature_send_timer_cb,
                                             SLIDER_SEND_DELAY_MS, nullptr);
    capture_timer = lv_timer_create(capture_timer_cb, 10, nullptr);
    lv_timer_pause(brightness_send_timer);
    lv_timer_pause(temperature_send_timer);
    lv_timer_pause(capture_timer);
}

void ui_xiaomi_lightbar_exit(lv_obj_t *parent)
{
    (void)parent;
    cleanup();
}

app_t ui_xiaomi_lightbar_main = {
    .setup_func_cb = ui_xiaomi_lightbar_enter,
    .exit_func_cb = ui_xiaomi_lightbar_exit,
    .user_data = nullptr,
};

#endif
