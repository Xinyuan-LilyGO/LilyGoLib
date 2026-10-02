/**
 * @file      ui_xiaomi_lightbar.cpp
 * @license   MIT
 * @brief     MJGJD01YL light bar controller for T-LoRa Pager and external NRF24.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#if defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_NRF24)

#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace {

constexpr uint32_t FALLBACK_REMOTE_ID = 0x111111;
constexpr uint32_t SLIDER_SEND_DELAY_MS = 120;
constexpr uint16_t COMMAND_TOGGLE = 0x0100;
constexpr uint16_t COMMAND_COOLER = 0x0200;
constexpr uint16_t COMMAND_WARMER = 0x0300;
constexpr uint16_t COMMAND_BRIGHTER = 0x0400;
constexpr uint16_t COMMAND_DIMMER = 0x0500;
constexpr uint16_t COMMAND_RESET = 0x0600;

lv_obj_t *page_container = nullptr;
lv_obj_t *id_textarea = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *brightness_slider = nullptr;
lv_obj_t *brightness_value = nullptr;
lv_obj_t *temperature_slider = nullptr;
lv_obj_t *temperature_value = nullptr;
lv_obj_t *unavailable_msgbox = nullptr;
lv_timer_t *brightness_send_timer = nullptr;
lv_timer_t *temperature_send_timer = nullptr;

uint32_t remote_id = FALLBACK_REMOTE_ID;
uint8_t packet_sequence = 0;

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

void commit_remote_id()
{
    if (!id_textarea) return;

    const char *text = lv_textarea_get_text(id_textarea);
    if (!text || text[0] == '\0') {
        remote_id = generated_remote_id();
    } else {
        remote_id = (uint32_t)strtoul(text, nullptr, 16) & 0xFFFFFF;
    }

    char formatted[7];
    snprintf(formatted, sizeof(formatted), "%06lX", (unsigned long)remote_id);
    lv_textarea_set_text(id_textarea, formatted);
    save_remote_id();

    char status[40];
    snprintf(status, sizeof(status), "Remote %s saved", formatted);
    set_status(status);
}

int16_t send_command(uint16_t command)
{
    const int16_t state = hw_send_xiaomi_lightbar_command(remote_id, command,
                          packet_sequence++);
    if (state != 0) {
        char message[40];
        snprintf(message, sizeof(message), "Send failed (%d)", state);
        set_status(message);
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

void id_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *textarea = (lv_obj_t *)lv_event_get_target(event);
    lv_group_t *group = lv_obj_get_group(textarea);

    if (code == LV_EVENT_KEY) {
        const lv_key_t key = *(lv_key_t *)lv_event_get_param(event);
        if (key == LV_KEY_ENTER) {
            commit_remote_id();
            if (group) lv_group_set_editing(group, false);
            disable_keyboard();
            lv_event_stop_processing(event);
            return;
        }
    }

    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
        commit_remote_id();
        if (group) lv_group_set_editing(group, false);
        disable_keyboard();
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        const bool edited = lv_obj_has_state(textarea, LV_STATE_EDITED);
        if (code == LV_EVENT_CLICKED && edited) {
            commit_remote_id();
            if (group) lv_group_set_editing(group, false);
            disable_keyboard();
        } else if (code == LV_EVENT_FOCUSED && edited) {
            enable_keyboard();
        }
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

void create_level_control(lv_obj_t *card, bool brightness)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), 42);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *slider = lv_slider_create(row);
    lv_slider_set_range(slider, 0, 15);
    lv_slider_set_value(slider, 8, LV_ANIM_OFF);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_height(slider, 8);
    lv_obj_add_style(slider, &ui_styles.slider_track, LV_PART_MAIN);
    lv_obj_add_style(slider, &ui_styles.slider_knob, LV_PART_KNOB);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    ui_add_accent_focus_style(slider);
    ui_prepare_slider_for_encoder(slider);

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_width(value, brightness ? 46 : 58);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(value, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);

    if (brightness) {
        brightness_slider = slider;
        brightness_value = value;
        lv_obj_add_event_cb(slider, brightness_event_cb, LV_EVENT_ALL, nullptr);
        update_brightness_value();
    } else {
        temperature_slider = slider;
        temperature_value = value;
        lv_obj_add_event_cb(slider, temperature_event_cb, LV_EVENT_ALL, nullptr);
        update_temperature_value();
    }
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

void back_event_handler(lv_event_t *event)
{
    (void)event;
    disable_keyboard();
    if (brightness_send_timer) {
        lv_timer_delete(brightness_send_timer);
        brightness_send_timer = nullptr;
    }
    if (temperature_send_timer) {
        lv_timer_delete(temperature_send_timer);
        temperature_send_timer = nullptr;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = nullptr;
    }
    id_textarea = nullptr;
    status_label = nullptr;
    brightness_slider = nullptr;
    brightness_value = nullptr;
    temperature_slider = nullptr;
    temperature_value = nullptr;
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

    lv_obj_t *card = ui_create_card(page_container, "MJGJD01YL");
    id_textarea = lv_textarea_create(card);
    lv_obj_set_width(id_textarea, 110);
    lv_textarea_set_one_line(id_textarea, true);
    lv_textarea_set_accepted_chars(id_textarea, "0123456789ABCDEFabcdef");
    lv_textarea_set_max_length(id_textarea, 6);
    lv_textarea_set_text_selection(id_textarea, false);
    lv_obj_set_style_bg_color(id_textarea, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(id_textarea, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(id_textarea, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(id_textarea, 1, 0);
    lv_obj_set_style_radius(id_textarea, 6, 0);
    lv_obj_set_style_pad_all(id_textarea, 5, 0);
    char id_text[7];
    snprintf(id_text, sizeof(id_text), "%06lX", (unsigned long)remote_id);
    lv_textarea_set_text(id_textarea, id_text);
    lv_obj_add_event_cb(id_textarea, id_event_cb, LV_EVENT_ALL, nullptr);
    ui_create_card_item(card, LV_SYMBOL_EDIT, "Remote ID", id_textarea);

    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "Ready");
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(status_label, 1);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    ui_create_card_item(card, LV_SYMBOL_WIFI, "Status", status_label);

    card = ui_create_card(page_container, "Brightness");
    create_level_control(card, true);

    card = ui_create_card(page_container, "Color temperature");
    create_level_control(card, false);

    brightness_send_timer = lv_timer_create(brightness_send_timer_cb,
                                            SLIDER_SEND_DELAY_MS, nullptr);
    temperature_send_timer = lv_timer_create(temperature_send_timer_cb,
                                             SLIDER_SEND_DELAY_MS, nullptr);
    lv_timer_pause(brightness_send_timer);
    lv_timer_pause(temperature_send_timer);

    card = ui_create_card(page_container, "Commands");
    ui_create_card_button(card, LV_SYMBOL_POWER, "Power", "Toggle", toggle_cb);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Pair / reset", "Send", reset_cb);
}

void ui_xiaomi_lightbar_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_xiaomi_lightbar_main = {
    .setup_func_cb = ui_xiaomi_lightbar_enter,
    .exit_func_cb = ui_xiaomi_lightbar_exit,
    .user_data = nullptr,
};

#endif
