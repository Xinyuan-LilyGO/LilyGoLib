/**
 * @file      ui_power.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static lv_obj_t *page_container = NULL;
static lv_obj_t *power_status_label = NULL;
static lv_obj_t *primary_detail_label = NULL;
static lv_timer_t *power_status_timer = NULL;

static const char *power_mode_title(hw_power_off_mode_t mode)
{
    switch (mode) {
    case HW_POWER_OFF_SHIP_MODE:
        return "Ship Mode";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "Deep Sleep";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "Power Off";
    }
}

static const char *power_mode_eyebrow(hw_power_off_mode_t mode)
{
    switch (mode) {
    case HW_POWER_OFF_SHIP_MODE:
        return "BATTERY ISOLATION";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "LOW-POWER STANDBY";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "HARDWARE SHUTDOWN";
    }
}

static const char *power_mode_action_detail(hw_power_off_mode_t mode)
{
    switch (mode) {
    case HW_POWER_OFF_SHIP_MODE:
        return hw_can_shutdown() ? "Disconnect battery path" : "Disconnect USB-C to continue";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "Simulate power off until BOOT";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "Cut system power completely";
    }
}

static const char *sleep_action_detail(hw_power_off_mode_t mode)
{
    return mode == HW_POWER_OFF_SHUTDOWN ? "Deep sleep until Power button" : "Deep sleep until BOOT";
}

static const char *power_mode_transition(hw_power_off_mode_t mode)
{
    switch (mode) {
    case HW_POWER_OFF_SHIP_MODE:
        return "Entering ship mode...";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "Entering deep sleep...";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "Powering off...";
    }
}

static lv_color_t power_mode_color(hw_power_off_mode_t mode)
{
    switch (mode) {
    case HW_POWER_OFF_SHIP_MODE:
        return lv_color_hex(0xFFB020);
    case HW_POWER_OFF_DEEP_SLEEP:
        return lv_color_hex(0x2B7DE9);
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return lv_color_hex(0xE05252);
    }
}

static void refresh_power_status()
{
    if (!power_status_label || !primary_detail_label) return;

    const hw_power_off_mode_t mode = hw_get_power_off_mode();
    if (mode == HW_POWER_OFF_SHIP_MODE) {
        const bool usb_connected = hw_adapter_is_connected();
        lv_label_set_text(power_status_label,
                          usb_connected ? LV_SYMBOL_USB " USB-C CONNECTED" : LV_SYMBOL_OK " READY");
        lv_obj_set_style_text_color(power_status_label,
                                    usb_connected ? UI_COLOR_WARNING : UI_COLOR_ACCENT, 0);
    } else if (mode == HW_POWER_OFF_DEEP_SLEEP) {
        lv_label_set_text(power_status_label, LV_SYMBOL_PAUSE " SLEEP POWER-OFF");
        lv_obj_set_style_text_color(power_status_label, power_mode_color(mode), 0);
    } else {
        lv_label_set_text(power_status_label, LV_SYMBOL_OK " READY");
        lv_obj_set_style_text_color(power_status_label, UI_COLOR_ACCENT, 0);
    }
    lv_label_set_text(primary_detail_label, power_mode_action_detail(mode));
}

static void power_status_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    refresh_power_status();
}

static void page_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (power_status_timer) {
        lv_timer_del(power_status_timer);
        power_status_timer = NULL;
    }
    power_status_label = NULL;
    primary_detail_label = NULL;
    page_container = NULL;
}

static void show_power_transition(const char *text)
{
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);

    LV_IMG_DECLARE(img_poweroff);
    lv_obj_t *image = lv_image_create(lv_screen_active());
    lv_image_set_src(image, &img_poweroff);
    lv_obj_center(image);

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_refr_now(NULL);
}

static void shutdown_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    hw_feedback();

    const hw_power_off_mode_t mode = hw_get_power_off_mode();
    if (!hw_can_shutdown()) {
        ui_msg_pop_up("Ship mode unavailable", "Disconnect USB-C, then try again.");
        refresh_power_status();
        return;
    }

    show_power_transition(power_mode_transition(mode));
    lv_delay_ms(mode == HW_POWER_OFF_SHUTDOWN ? 1000 : 600);
    hw_shutdown();
}

static void sleep_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    hw_feedback();
    show_power_transition("Entering deep sleep...");
    lv_delay_ms(500);
    hw_sleep();
}

static void restart_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    hw_feedback();
    show_power_transition("Restarting...");
    lv_delay_ms(300);
#ifdef ARDUINO
    ESP.restart();
#endif
}

static void back_event_handler(lv_event_t *e)
{
    LV_UNUSED(e);
    if (page_container) {
        ui_destroy_app_page(page_container);
    }
    menu_show();
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *icon, const char *title,
                                      const char *detail, lv_color_t color, bool primary,
                                      lv_event_cb_t event_cb)
{
    const bool small = is_screen_small();
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(button, primary ? color : UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_color(button, primary ? lv_color_darken(color, LV_OPA_20) : UI_COLOR_CARD_FOCUS,
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(button, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_pad_all(button, small ? 9 : 12, 0);
    lv_obj_set_style_pad_column(button, small ? 9 : 12, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(button);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(icon_label, primary ? lv_color_white() : color, 0);

    lv_obj_t *text_column = lv_obj_create(button);
    lv_obj_set_size(text_column, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_column, 1);
    lv_obj_set_style_bg_opa(text_column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_column, 0, 0);
    lv_obj_set_style_radius(text_column, 0, 0);
    lv_obj_set_style_pad_all(text_column, 0, 0);
    lv_obj_set_style_pad_row(text_column, 2, 0);
    lv_obj_set_flex_flow(text_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(text_column, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(text_column);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_obj_set_style_text_font(title_label, small ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *detail_label = lv_label_create(text_column);
    lv_label_set_text(detail_label, detail);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_label, LV_PCT(100));
    lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detail_label,
                                primary ? lv_color_hex(0xF4F4F4) : UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *arrow = lv_label_create(button);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, primary ? lv_color_white() : UI_COLOR_TEXT_SECONDARY, 0);

    if (primary) primary_detail_label = detail_label;
    return button;
}

static void create_power_overview(lv_obj_t *parent, hw_power_off_mode_t mode)
{
    const bool small = is_screen_small();
    const lv_color_t mode_color = power_mode_color(mode);

    lv_obj_t *overview = lv_obj_create(parent);
    lv_obj_set_size(overview, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(overview, lv_color_hex(0x111418), 0);
    lv_obj_set_style_bg_opa(overview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overview, 1, 0);
    lv_obj_set_style_border_color(overview, mode_color, 0);
    lv_obj_set_style_border_opa(overview, LV_OPA_50, 0);
    lv_obj_set_style_radius(overview, 8, 0);
    lv_obj_set_style_pad_all(overview, small ? 11 : 16, 0);
    lv_obj_set_style_pad_row(overview, small ? 5 : 7, 0);
    lv_obj_set_flex_flow(overview, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(overview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overview, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(overview);
    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, overview);
    }

    lv_obj_t *eyebrow = lv_label_create(overview);
    lv_label_set_text(eyebrow, power_mode_eyebrow(mode));
    lv_obj_set_style_text_font(eyebrow, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(eyebrow, mode_color, 0);

    lv_obj_t *title = lv_label_create(overview);
    lv_label_set_text_fmt(title, LV_SYMBOL_POWER "  %s", power_mode_title(mode));
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_font(title, small ? &lv_font_montserrat_18 : &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);

    lv_obj_t *description = lv_label_create(overview);
    lv_label_set_text(description, hw_get_device_power_tips_string());
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(description, LV_PCT(100));
    lv_obj_set_style_text_font(description, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(description, UI_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *meta = lv_obj_create(overview);
    lv_obj_set_size(meta, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_style_radius(meta, 0, 0);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_set_style_pad_top(meta, 3, 0);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(meta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *controller = lv_label_create(meta);
    lv_label_set_text_fmt(controller, "CTRL  %s", hw_get_power_controller_name());
    lv_obj_set_style_text_font(controller, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(controller, UI_COLOR_TEXT_SECONDARY, 0);

    power_status_label = lv_label_create(meta);
    lv_obj_set_style_text_font(power_status_label, &lv_font_montserrat_12, 0);
}

void ui_power_enter(lv_obj_t *parent)
{
    const hw_power_off_mode_t mode = hw_get_power_off_mode();
    page_container = ui_create_app_page(parent, "Power", back_event_handler);
    lv_obj_add_event_cb(page_container, page_delete_cb, LV_EVENT_DELETE, NULL);

    create_power_overview(page_container, mode);

    create_action_button(page_container, LV_SYMBOL_POWER, power_mode_title(mode),
                         power_mode_action_detail(mode), power_mode_color(mode), true,
                         shutdown_event_cb);

    if (mode != HW_POWER_OFF_DEEP_SLEEP) {
        create_action_button(page_container, LV_SYMBOL_PAUSE, "Sleep",
                             sleep_action_detail(mode), lv_color_hex(0x2B7DE9), false,
                             sleep_event_cb);
    }

    create_action_button(page_container, LV_SYMBOL_REFRESH, "Restart",
                         "Reboot the device now", UI_COLOR_ACCENT, false,
                         restart_event_cb);

    refresh_power_status();
    if (mode == HW_POWER_OFF_SHIP_MODE) {
        power_status_timer = lv_timer_create(power_status_timer_cb, 1000, NULL);
    }

#ifdef USING_TOUCHPAD
    create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif
}

void ui_power_exit(lv_obj_t *parent)
{
    LV_UNUSED(parent);
}

app_t ui_power_main = {
    .setup_func_cb = ui_power_enter,
    .exit_func_cb = ui_power_exit,
    .user_data = nullptr,
};
