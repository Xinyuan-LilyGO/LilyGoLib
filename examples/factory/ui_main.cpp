/**
 * @file      ui.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#if defined(ARDUINO)
#include "esp_arduino_version.h"
#endif
#include <math.h>

LV_IMG_DECLARE(img_microphone);
LV_IMG_DECLARE(img_ir_remote);
LV_IMG_DECLARE(img_music);
LV_IMG_DECLARE(img_wifi);
LV_IMG_DECLARE(img_configuration);
LV_IMG_DECLARE(img_scan);
LV_IMG_DECLARE(img_sdcard);
LV_IMG_DECLARE(img_mic);
LV_IMG_DECLARE(img_radio);
LV_IMG_DECLARE(img_gps);
LV_IMG_DECLARE(img_power);
LV_IMG_DECLARE(img_monitoring);
LV_IMG_DECLARE(img_keyboard);
LV_IMG_DECLARE(img_gyroscope);
LV_IMG_DECLARE(img_bluetooth);
LV_IMG_DECLARE(img_test);
LV_IMG_DECLARE(img_si4735);
LV_IMG_DECLARE(img_track);
LV_IMG_DECLARE(img_compass);
LV_IMG_DECLARE(img_nfc);
LV_IMG_DECLARE(img_batter_low);
LV_IMG_DECLARE(img_walkie);
LV_IMG_DECLARE(img_ir_recv);
LV_IMG_DECLARE(img_led);
LV_IMG_DECLARE(img_face);

LV_IMG_DECLARE(img_temperature);
LV_IMG_DECLARE(img_file_manager);
LV_IMG_DECLARE(img_game);
LV_IMG_DECLARE(img_lorawan);
LV_IMG_DECLARE(img_gps_track);

LV_IMG_DECLARE(img_qrcode);
LV_IMG_DECLARE(img_tools);
LV_IMG_DECLARE(img_wireless);
LV_IMG_DECLARE(img_energy);
LV_IMG_DECLARE(img_clock);
LV_IMG_DECLARE(img_network_radio);
LV_IMG_DECLARE(img_usb);

LV_FONT_DECLARE(font_alibaba_24);
LV_FONT_DECLARE(font_alibaba_60);
LV_FONT_DECLARE(font_alibaba_100);

#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)
#define SCREEN_TIMEOUT 10000
#define GRID_MAX_APPS  48

#if defined(USING_TOUCHPAD) || defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
#define LOCKSCREEN_HAS_TOUCH_UNLOCK 1
#else
#define LOCKSCREEN_HAS_TOUCH_UNLOCK 0
#endif

#if defined(ARDUINO) && (defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK) || !LOCKSCREEN_HAS_TOUCH_UNLOCK)
#define LOCKSCREEN_HAS_BOOT_UNLOCK 1
#else
#define LOCKSCREEN_HAS_BOOT_UNLOCK 0
#endif

lv_obj_t *main_screen;
lv_obj_t *menu_panel;
lv_group_t *menu_g, *app_g;
static lv_timer_t *clock_timer;
static lv_obj_t *clock_page;
static lv_timer_t *disp_timer = NULL;
static lv_timer_t *dev_timer = NULL;
static uint32_t disp_time_ms = 0;

/* ── App registry for grid layout ── */
typedef struct {
    const char *name;
    const lv_img_dsc_t *icon;
    app_t *app_func;
} app_entry_t;

static app_entry_t app_registry[GRID_MAX_APPS];
static uint32_t app_count = 0;

/* Grid layout computed values */
static uint32_t grid_cols = 3;
static uint32_t grid_rows = 3;
static uint32_t grid_pages = 1;
static uint32_t grid_current_page = 0;
static lv_obj_t **grid_page_containers = NULL;
static lv_obj_t **grid_tiles = NULL;
static lv_obj_t *page_indicator_cont = NULL;
static bool grid_compact_240 = false;

/* ── Clock ── */
typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *seg;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_icon;
    lv_obj_t *battery_label;
    lv_coord_t battery_y;
    lv_obj_t *overlay;
} clock_label_t;

static clock_label_t clock_label;
static lv_anim_t colon_anim;

static RTC_DATA_ATTR uint8_t brightness_level = 0;
static RTC_DATA_ATTR uint8_t keyboard_level = 0;

/* ── Status bar timer ── */
static lv_timer_t *status_bar_timer = NULL;
static lv_timer_t *power_menu_timer = NULL;
static volatile bool power_menu_pending = false;
static lv_obj_t *power_menu_overlay = NULL;

#if FACTORY_HAS_TOUCH_INPUT
static lv_obj_t *touch_guide_overlay = NULL;
static lv_group_t *touch_guide_group = NULL;
static lv_group_t *touch_guide_prev_group = NULL;
#endif

enum power_menu_action_t {
    POWER_MENU_SHUTDOWN = 0,
    POWER_MENU_SLEEP,
    POWER_MENU_RESTART,
    POWER_MENU_CANCEL,
};

#if FACTORY_HAS_TOUCH_INPUT
static void close_touch_guide(bool dismiss_permanently)
{
    if (dismiss_permanently) {
        hw_set_touch_guide_dismissed(true);
    }
    if (touch_guide_overlay) {
        lv_obj_delete(touch_guide_overlay);
        touch_guide_overlay = NULL;
    }
    if (touch_guide_prev_group) {
        set_default_group(touch_guide_prev_group);
    }
    if (touch_guide_group) {
        lv_group_delete(touch_guide_group);
        touch_guide_group = NULL;
    }
    touch_guide_prev_group = NULL;
    set_low_power_mode_flag(true);
    lv_display_trigger_activity(NULL);
    hw_feedback();
}

static void touch_guide_button_cb(lv_event_t *e)
{
    bool dismiss_permanently = (bool)(intptr_t)lv_event_get_user_data(e);
    close_touch_guide(dismiss_permanently);
}

static lv_obj_t *create_touch_guide_button(lv_obj_t *parent, const char *text,
        lv_color_t color, bool dismiss_permanently, bool compact)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(48), compact ? 42 : 40);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
    lv_obj_add_event_cb(btn, touch_guide_button_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)dismiss_permanently);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, compact ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);
    lv_obj_center(label);

    if (touch_guide_group) {
        lv_group_add_obj(touch_guide_group, btn);
    }
    return btn;
}

static void show_touch_guide(void)
{
    if (touch_guide_overlay || hw_get_touch_guide_dismissed()) return;

    int32_t width = lv_display_get_horizontal_resolution(NULL);
    int32_t height = lv_display_get_vertical_resolution(NULL);
    bool compact = width <= 240 || height <= 240;

    touch_guide_prev_group = lv_group_get_default();
    touch_guide_group = lv_group_create();
    if (touch_guide_group) {
        set_default_group(touch_guide_group);
    }

    set_low_power_mode_flag(false);
    touch_guide_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(touch_guide_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(touch_guide_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(touch_guide_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(touch_guide_overlay, 0, 0);
    lv_obj_set_style_radius(touch_guide_overlay, 0, 0);
    lv_obj_set_style_pad_all(touch_guide_overlay, compact ? 8 : 12, 0);
    lv_obj_remove_flag(touch_guide_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_guide_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(touch_guide_overlay);
    lv_obj_set_size(panel, LV_PCT(94), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, compact ? 10 : 14, 0);
    lv_obj_set_style_pad_row(panel, compact ? 8 : 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Touch navigation");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, compact ? &lv_font_montserrat_16 : &lv_font_montserrat_18, 0);

    lv_obj_t *gesture = lv_label_create(panel);
    lv_label_set_text(gesture, LV_SYMBOL_RIGHT "   SWIPE   " LV_SYMBOL_LEFT);
    lv_obj_set_width(gesture, LV_PCT(100));
    lv_obj_set_style_text_align(gesture, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(gesture, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(gesture, compact ? &lv_font_montserrat_18 : &lv_font_montserrat_22, 0);

    lv_obj_t *description = lv_label_create(panel);
    lv_label_set_text(description,
                      "Swipe inward from either screen edge\nto return to the previous page.");
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(description, LV_PCT(100));
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(description, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(description, compact ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);

    lv_obj_t *button_row = lv_obj_create(panel);
    lv_obj_set_size(button_row, LV_PCT(100), compact ? 44 : 42);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_row, 0, 0);
    lv_obj_set_style_pad_all(button_row, 0, 0);
    lv_obj_set_style_pad_column(button_row, 8, 0);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *got_it = create_touch_guide_button(button_row, "Got it", UI_COLOR_ACCENT, false, compact);
    create_touch_guide_button(button_row,
                              compact ? "Don't show\nagain" : "Don't show again",
                              lv_color_hex(0x3A3A3F), true, compact);
    if (touch_guide_group) {
        lv_group_focus_obj(got_it);
    }
}
#endif

static void status_bar_timer_cb(lv_timer_t *t)
{
    ui_status_bar_update();
}

extern "C" void lilygo_power_menu_request(void)
{
    power_menu_pending = true;
}

extern "C" void lilygo_right_button_long_press(void)
{
    lilygo_power_menu_request();
}

static void power_menu_close(void)
{
    if (power_menu_overlay) {
        lv_obj_delete(power_menu_overlay);
        power_menu_overlay = NULL;
    }
}

static void power_action_screen(const char *text)
{
    power_menu_close();
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

static const char *power_menu_primary_label(bool compact)
{
    switch (hw_get_power_off_mode()) {
    case HW_POWER_OFF_SHIP_MODE:
        return compact ? "Ship\nMode" : "Ship Mode";
    case HW_POWER_OFF_DEEP_SLEEP:
        return compact ? "Deep\nSleep" : "Deep Sleep";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return compact ? "Power\nOff" : "Power Off";
    }
}

static const char *power_menu_transition_text()
{
    switch (hw_get_power_off_mode()) {
    case HW_POWER_OFF_SHIP_MODE:
        return "Entering ship mode...";
    case HW_POWER_OFF_DEEP_SLEEP:
        return "Entering deep sleep...";
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return "Powering off...";
    }
}

static lv_color_t power_menu_primary_color()
{
    switch (hw_get_power_off_mode()) {
    case HW_POWER_OFF_SHIP_MODE:
        return lv_color_hex(0xE39A18);
    case HW_POWER_OFF_DEEP_SLEEP:
        return lv_color_hex(0x2B7DE9);
    case HW_POWER_OFF_SHUTDOWN:
    default:
        return lv_color_hex(0xD94B4B);
    }
}

static void power_menu_action_cb(lv_event_t *e)
{
    power_menu_action_t action = (power_menu_action_t)(intptr_t)lv_event_get_user_data(e);
    hw_feedback();

    if (action == POWER_MENU_CANCEL) {
        power_menu_close();
        return;
    }

    if (action == POWER_MENU_SHUTDOWN) {
        if (!hw_can_shutdown()) {
            power_menu_close();
            ui_msg_pop_up("Ship mode unavailable", "Disconnect USB-C, then try again.");
            return;
        }
        power_action_screen(power_menu_transition_text());
        lv_delay_ms(hw_get_power_off_mode() == HW_POWER_OFF_SHUTDOWN ? 1000 : 600);
        hw_shutdown();
        return;
    }

    if (action == POWER_MENU_SLEEP) {
        power_menu_close();
        hw_sleep();
        return;
    }

    if (action == POWER_MENU_RESTART) {
        power_action_screen("Restarting...");
        lv_delay_ms(300);
#ifdef ARDUINO
        ESP.restart();
#endif
    }
}

static lv_obj_t *power_menu_create_button(lv_obj_t *parent, const char *symbol, const char *text,
        lv_color_t color, power_menu_action_t action)
{
    const bool compact_240 = lv_display_get_horizontal_resolution(NULL) == 240 &&
                             lv_display_get_vertical_resolution(NULL) == 240;
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn,
                    compact_240 ? 58 : (is_screen_small() ? 64 : 86),
                    compact_240 ? 56 : (is_screen_small() ? 62 : 68));
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
    lv_obj_set_style_pad_row(btn, 4, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(btn, power_menu_action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)action);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, compact_240 ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return btn;
}

static void show_power_menu(void)
{
    if (power_menu_overlay) return;

    power_menu_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(power_menu_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(power_menu_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(power_menu_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(power_menu_overlay, 0, 0);
    lv_obj_set_style_radius(power_menu_overlay, 0, 0);
    lv_obj_set_style_pad_all(power_menu_overlay, 0, 0);
    lv_obj_remove_flag(power_menu_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(power_menu_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(power_menu_overlay, power_menu_action_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)POWER_MENU_CANCEL);

    lv_obj_t *panel = lv_obj_create(power_menu_overlay);
    lv_obj_set_size(panel, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, [](lv_event_t *e) {
        lv_event_stop_bubbling(e);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text_fmt(title, "POWER / %s", hw_get_power_controller_name());
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, is_screen_small() ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    const bool compact_240 = lv_display_get_horizontal_resolution(NULL) == 240 &&
                             lv_display_get_vertical_resolution(NULL) == 240;
    power_menu_create_button(row, LV_SYMBOL_POWER, power_menu_primary_label(compact_240),
                             power_menu_primary_color(), POWER_MENU_SHUTDOWN);
    if (hw_get_power_off_mode() != HW_POWER_OFF_DEEP_SLEEP) {
        power_menu_create_button(row, LV_SYMBOL_PAUSE, "Sleep", lv_color_hex(0x2B7DE9), POWER_MENU_SLEEP);
    }
    power_menu_create_button(row, LV_SYMBOL_REFRESH, "Restart", lv_color_hex(0x5B5B62), POWER_MENU_RESTART);

    lv_obj_t *hint = lv_label_create(panel);
    lv_label_set_text(hint, "Tap outside to cancel");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void power_menu_timer_cb(lv_timer_t *t)
{
    if (!power_menu_pending) return;
    power_menu_pending = false;
    if (!main_screen || !clock_page) return;
    lv_display_trigger_activity(NULL);
    show_power_menu();
}

/* ── Power management (unchanged logic) ── */

void set_low_power_mode_flag(bool enable)
{
    if (enable) {
        lv_obj_add_flag(main_screen, DEVICE_CAN_SLEEP);
    } else {
        lv_obj_remove_flag(main_screen, DEVICE_CAN_SLEEP);
    }
}

bool get_enter_low_power_flag()
{
    bool rlst = lv_obj_has_flag(main_screen, DEVICE_CAN_SLEEP);
    return rlst;
}

void menu_show()
{
    set_default_group(menu_g);
    // lv_obj_t *menu_tile = lv_obj_get_child(main_screen, 0);
    // if (menu_tile) {
    //     lv_tileview_set_tile(main_screen, menu_tile, LV_ANIM_ON);
    // }
    lv_tileview_set_tile_by_index(main_screen, 0, 0, LV_ANIM_ON);
    lv_timer_resume(disp_timer);
    if (status_bar_timer) lv_timer_resume(status_bar_timer);
    lv_disp_trig_activity(NULL);
    hw_feedback();
}

void menu_hidden()
{
    // lv_obj_t *app_tile = lv_obj_get_child(main_screen, 1);
    // if (app_tile) {
    //     /* App pages build their detail widgets asynchronously; show the page immediately. */
    //     lv_tileview_set_tile(main_screen, app_tile, LV_ANIM_OFF);
    // }
    lv_tileview_set_tile_by_index(main_screen, 0, 1, LV_ANIM_ON);
    lv_timer_pause(disp_timer);
    if (status_bar_timer) lv_timer_pause(status_bar_timer);
}

bool isinMenu()
{
    return !lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

void set_default_group(lv_group_t *group)
{
    lv_indev_t *cur_drv = NULL;
    for (;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv) {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_POINTER) {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

/* ── Grid page indicator update ── */
void update_page_indicator(void)
{
    if (!page_indicator_cont) return;
    const lv_coord_t active_size = grid_compact_240 ? 8 : 10;
    const lv_coord_t inactive_size = grid_compact_240 ? 4 : 6;
    uint32_t cnt = lv_obj_get_child_count(page_indicator_cont);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *dot = lv_obj_get_child(page_indicator_cont, i);
        if (i == grid_current_page) {
            lv_obj_set_size(dot, active_size, active_size);
            lv_obj_set_style_bg_color(dot, UI_COLOR_ACCENT, 0);
        } else {
            lv_obj_set_size(dot, inactive_size, inactive_size);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x555558), 0);
        }
    }
}

/* ── Grid icon button callbacks ── */
static void grid_icon_click_cb(lv_event_t *e)
{
    app_t *func_cb = (app_t *)lv_event_get_user_data(e);
    if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) return;

    set_default_group(app_g);
    hw_feedback();
    if (func_cb && func_cb->setup_func_cb) {
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        (*func_cb->setup_func_cb)(parent);
    }
    menu_hidden();
}

static void grid_icon_focus_cb(lv_event_t *e)
{
    /* Update status bar with focused app name */
}

static bool grid_is_240_square(uint32_t w, uint32_t h)
{
    return w <= 240 && h <= 240;
}

static const char *grid_short_app_name(const char *name)
{
    if (!grid_compact_240 || !name) return name;

    if (strcmp(name, "Recorder") == 0) return "Record";
    if (strcmp(name, "DisplayTest") == 0) return "Display";
    if (strcmp(name, "I2C Scan") == 0) return "I2C";
    if (strcmp(name, "Clock Tools") == 0) return "Clock";
    if (strcmp(name, "SD Info") == 0) return "SD";
    if (strcmp(name, "File Manager") == 0) return "Files";
    if (strcmp(name, "USB Disk") == 0) return "USB";
    if (strcmp(name, "Setting") == 0) return "Set";
    if (strcmp(name, "WiFi Analyzer") == 0) return "Analyzer";
    if (strcmp(name, "WiFi Tools") == 0) return "Tools";
    if (strcmp(name, "Keyboard") == 0) return "Keys";
    if (strcmp(name, "Net Radio") == 0) return "NetRadio";
    if (strcmp(name, "Music Eyes") == 0) return "Eyes";
    if (strcmp(name, "I2S Test") == 0) return "I2S";
    if (strcmp(name, "NFC Card") == 0) return "Card";
    if (strcmp(name, "BLE Scan") == 0) return "BLE";
    if (strcmp(name, "BLE HID") == 0) return "HID";
    if (strcmp(name, "Trackball") == 0) return "Ball";
    if (strcmp(name, "Track Log") == 0) return "Track";
    if (strcmp(name, "Sub-G Tools") == 0) return "Sub-G";
    if (strcmp(name, "LoRaWAN") == 0) return "WAN";
    if (strcmp(name, "QRCode") == 0) return "QR";

    return name;
}

/* ── Create a single grid icon with name below ── */
static lv_obj_t *create_grid_icon(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app_fun,
                                  lv_coord_t icon_size, lv_coord_t cell_width)
{
    const lv_coord_t label_h = 14;
    const lv_coord_t total_h = icon_size + label_h + 2;
    if (cell_width < icon_size) cell_width = icon_size;

    /* Container: fixed size, no flex — absolute positioning */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, cell_width, total_h);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    /* Icon button — fixed at top */
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, icon_size, icon_size);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_radius(btn, 16, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 16, LV_STATE_FOCUSED);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);

    /* Icon — recolor to white and scale to fit */
    if (img != NULL) {
        lv_obj_t *icon = lv_image_create(btn);
        lv_image_set_src(icon, img);
        lv_obj_set_style_image_recolor(icon, lv_color_white(), 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
        uint32_t img_w = img->header.w;
        uint32_t img_h = img->header.h;
        uint32_t target = (icon_size > 16) ? (icon_size - 8) : icon_size;
        if (img_w > 0 && img_h > 0) lv_image_set_scale(icon, (target * 256) / (img_w > img_h ? img_w : img_h));
        lv_obj_center(icon);
    }

    /* Name label — use the full grid cell; truncate only when the name still does not fit. */
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, grid_short_app_name(name));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, cell_width);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Events — on the button so encoder can click */
    lv_obj_add_event_cb(btn, grid_icon_click_cb, LV_EVENT_CLICKED, app_fun);
    lv_obj_add_event_cb(btn, grid_icon_focus_cb, LV_EVENT_FOCUSED, (void *)name);

    return cont;
}

/* ── Build grid pages ── */
static void build_grid_menu(lv_obj_t *parent)
{
    uint32_t phy_hor = lv_display_get_physical_horizontal_resolution(NULL);
    uint32_t phy_ver = lv_display_get_physical_vertical_resolution(NULL);
    grid_compact_240 = grid_is_240_square(phy_hor, phy_ver);

    /* Compute grid dimensions based on screen size */
    if (grid_compact_240) {
        grid_cols = 3; grid_rows = 3;           /* 240x240: 9 per page */
    } else if (phy_hor <= 320) {
        grid_cols = 3; grid_rows = 2;           /* 320x240: 6 per page */
    } else if (phy_ver <= 260) {
        grid_cols = 4; grid_rows = 2;           /* 480x222: 8 per page (wide, short) */
    } else {
        grid_cols = 4; grid_rows = 4;           /* 502x410: 16 per page (large) */
    }

    uint32_t per_page = grid_cols * grid_rows;
    grid_pages = (app_count + per_page - 1) / per_page;
    if (grid_pages == 0) grid_pages = 1;

    /* Icon size calculation — account for tileview padding, flex gaps, and label height */
    lv_coord_t avail_w = grid_compact_240 ? (phy_hor - 18) : (phy_hor - 40); /* tileview scroll padding + margins */
    lv_coord_t avail_h = grid_compact_240 ? (phy_ver - 42) : (phy_ver - 80); /* status bar + indicator */
    lv_coord_t gap = grid_compact_240 ? 10 : 8;
    lv_coord_t label_h = grid_compact_240 ? 13 : 16; /* height reserved for icon name label */
    lv_coord_t icon_w = (avail_w - gap * (grid_cols + 1)) / grid_cols;
    lv_coord_t icon_h = (avail_h - gap * (grid_rows + 1)) / grid_rows - label_h;
    lv_coord_t icon_size = icon_w < icon_h ? icon_w : icon_h;
    if (grid_compact_240) {
        if (icon_size > 48) icon_size = 48;
        if (icon_size < 42) icon_size = 42;
    } else {
        if (icon_size > 70) icon_size = 70;
        if (icon_size < 40) icon_size = 40;
    }

    /* Status bar */
    ui_create_status_bar(parent);

    /* Tileview for pages — offset down on round screens to avoid corner clipping */
    int top_offset = grid_compact_240 ? 18 : 20;
    int row_gap = grid_compact_240 ? 10 : 8;
#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    top_offset = 30;  /* Push down for round screen corners */
    row_gap = 16;     /* More vertical spacing */
#endif

    lv_obj_t *tv = lv_tileview_create(parent);
    lv_obj_set_size(tv, LV_PCT(100),
                    grid_compact_240 ? (phy_ver - top_offset - 20) : (avail_h - (top_offset - 20)));
    lv_obj_align(tv, LV_ALIGN_TOP_MID, 0, top_offset);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_style_radius(tv, 0, 0);

    grid_page_containers = (lv_obj_t **)lv_malloc(grid_pages * sizeof(lv_obj_t *));
    grid_tiles = (lv_obj_t **)lv_malloc(grid_pages * sizeof(lv_obj_t *));

    for (uint32_t p = 0; p < grid_pages; p++) {
        lv_obj_t *tile = lv_tileview_add_tile(tv, p, 0, LV_DIR_HOR);
        grid_tiles[p] = tile;

        /* Grid container — vertical flex of rows */
        lv_obj_t *grid = lv_obj_create(tile);
        lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_radius(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_pad_row(grid, row_gap, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, gap / 2);

        grid_page_containers[p] = grid;

        uint32_t start = p * per_page;
        uint32_t end = start + per_page;
        if (end > app_count) end = app_count;

        /* Create rows with explicit column control */
        for (uint32_t row = 0; row < grid_rows; row++) {
            uint32_t row_start = start + row * grid_cols;
            if (row_start >= end) break;

            lv_obj_t *row_obj = lv_obj_create(grid);
            lv_obj_set_size(row_obj, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row_obj, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row_obj, 0, 0);
            lv_obj_set_style_radius(row_obj, 0, 0);
            lv_obj_set_style_pad_all(row_obj, 0, 0);
            lv_obj_set_style_pad_column(row_obj, gap, 0);
            lv_obj_set_flex_flow(row_obj, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row_obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            for (uint32_t col = 0; col < grid_cols; col++) {
                uint32_t idx = row_start + col;
                if (idx < end) {
                    create_grid_icon(row_obj, app_registry[idx].name, app_registry[idx].icon,
                                     app_registry[idx].app_func, icon_size, icon_w);
                } else {
                    /* Invisible placeholder to keep grid aligned */
                    lv_obj_t *spacer = lv_obj_create(row_obj);
                    lv_coord_t spacer_w = icon_w > icon_size ? icon_w : icon_size;
                    lv_obj_set_size(spacer, spacer_w, icon_size + 16);
                    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(spacer, 0, 0);
                    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
                }
            }
        }
    }

    /* Page indicator (dot indicators) */
    if (grid_pages > 1) {
        page_indicator_cont = lv_obj_create(parent);
        lv_obj_set_size(page_indicator_cont, LV_SIZE_CONTENT, grid_compact_240 ? 8 : 20);
        lv_obj_set_style_bg_opa(page_indicator_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page_indicator_cont, 0, 0);
        lv_obj_set_style_radius(page_indicator_cont, 0, 0);
        lv_obj_set_flex_flow(page_indicator_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(page_indicator_cont, 8, 0);
        lv_obj_set_style_pad_all(page_indicator_cont, 0, 0);
        lv_obj_align(page_indicator_cont, LV_ALIGN_BOTTOM_MID, 0, grid_compact_240 ? -2 : -8);

        for (uint32_t i = 0; i < grid_pages; i++) {
            lv_obj_t *dot = lv_obj_create(page_indicator_cont);
            lv_obj_set_size(dot, grid_compact_240 ? 4 : 6, grid_compact_240 ? 4 : 6);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x555558), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }
        update_page_indicator();
    }

    /* Tileview scroll callback for page indicator update */
    lv_obj_add_event_cb(tv, [](lv_event_t *e) {
        lv_obj_t *tv_obj = (lv_obj_t *)lv_event_get_target(e);
        lv_obj_t *active_tile = lv_tileview_get_tile_active(tv_obj);
        uint32_t idx = 0;
        for (uint32_t i = 0; i < grid_pages; i++) {
            if (grid_tiles[i] == active_tile) {
                idx = i;
                break;
            }
        }
        if (idx != grid_current_page) {
            grid_current_page = idx;
            update_page_indicator();
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    /* Focus the first icon */
    if (app_count > 0) {
        lv_obj_t *first_grid = grid_page_containers[0];
        if (lv_obj_get_child_count(first_grid) > 0) {
            lv_obj_t *first_btn = lv_obj_get_child(first_grid, 0);
            lv_group_focus_obj(first_btn);
        }
    }
}

/* ── Register an app into the grid ── */
static void register_app(const char *name, const lv_img_dsc_t *img, app_t *app_fun)
{
    if (app_count >= GRID_MAX_APPS) return;
    app_registry[app_count].name = name;
    app_registry[app_count].icon = img;
    app_registry[app_count].app_func = app_fun;
    app_count++;
}

static bool device_has_external_i2c()
{
#ifdef ARDUINO
    const LilyGoDeviceCapability &capability = instance.getCapability();
    return capability.hasExternalI2c;
#else
    return true;
#endif
}

static bool device_has_sd_storage()
{
#ifdef ARDUINO
    const LilyGoDeviceCapability &capability = instance.getCapability();
    return capability.hasSd && !(instance.getDeviceProbe() & HW_SD_UNAVAILABLE);
#else
    return FACTORY_HAS_SD;
#endif
}


/* ── Clock page ── */

static void clock_update_datetime(lv_timer_t *t)
{
    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    static struct tm timeinfo = {0};
    hw_get_date_time(timeinfo);

    /* Check if time is valid (not all zeros from unsynced RTC) */
    // bool valid = (timeinfo.tm_year > 100); /* tm_year=0 means 1900 = not synced */

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    // if (valid) {
    lv_label_set_text_fmt(clock_label.hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(clock_label.minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(clock_label.date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
    // } else {
    //     lv_label_set_text(clock_label.hour, "--");
    //     lv_label_set_text(clock_label.minute, "--");
    //     lv_label_set_text(clock_label.date, "--/-- ---");
    // }

    monitor_params_t params;
    hw_get_monitor_params(params);
    if (clock_label.battery_bar) {
        lv_bar_set_value(clock_label.battery_bar, params.battery_percent, LV_ANIM_OFF);
        if (params.battery_percent > 50) {
            lv_obj_set_style_bg_color(clock_label.battery_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
        } else if (params.battery_percent > 20) {
            lv_obj_set_style_bg_color(clock_label.battery_bar, lv_color_hex(0xFFB800), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(clock_label.battery_bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
        }
    }
    if (clock_label.battery_icon && clock_label.battery_label) {
        if (params.charging) {
            lv_label_set_text(clock_label.battery_icon, LV_SYMBOL_CHARGE);
            lv_obj_set_style_text_color(clock_label.battery_icon, UI_COLOR_ACCENT, 0);
            lv_obj_align(clock_label.battery_icon, LV_ALIGN_CENTER, 0, clock_label.battery_y);
            lv_obj_add_flag(clock_label.battery_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(clock_label.battery_icon, LV_SYMBOL_BATTERY_FULL);
            lv_obj_set_style_text_color(clock_label.battery_icon, UI_COLOR_TEXT_SECONDARY, 0);
            lv_obj_align(clock_label.battery_icon, LV_ALIGN_CENTER, -30, clock_label.battery_y);
            lv_label_set_text_fmt(clock_label.battery_label, "%d%%", params.battery_percent);
            lv_obj_remove_flag(clock_label.battery_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void colon_anim_exec_cb(void *var, int32_t val)
{
    lv_obj_set_style_opa((lv_obj_t *)var, val, 0);
}

/* ── Swipe wake flag (set by swipe zone or button, read by poll timer) ── */
static volatile bool clock_swipe_wake = false;

/* BOOT button (GPIO0) unlock is kept separate from keyboard/button callbacks. */
static lv_obj_t *charge_edge_ref = NULL; /* charging edge glow */

/* ── Particle system for lock screen (disabled by default) ── */
/* #define ENABLE_LOCKSCREEN_PARTICLES */

#ifdef ENABLE_LOCKSCREEN_PARTICLES
#define PARTICLE_MAX      80
#define PARTICLE_DAMPING  0.96f
#define PARTICLE_GRAVITY  0.8f
#define PARTICLE_BOUNCE   0.6f
#define PARTICLE_MAX_VEL  8.0f
#define PARTICLE_DRIFT    0.3f

typedef struct {
    lv_obj_t *obj;
    float x, y;
    float vx, vy;
    int size;
} particle_t;

static particle_t particles[PARTICLE_MAX];
static lv_timer_t *particle_timer = NULL;
static int particle_count = 0;
static bool particle_has_imu = false;
#endif /* ENABLE_LOCKSCREEN_PARTICLES */

#ifdef ENABLE_LOCKSCREEN_PARTICLES
static void particle_timer_cb(lv_timer_t *t)
{
    if (particle_count == 0) return;

    float gx = 0, gy = 0;

    if (particle_has_imu) {
        imu_params_t imu;
        hw_get_imu_params(imu);
        float roll_rad = imu.roll * M_PI / 180.0f;
        float pitch_rad = imu.pitch * M_PI / 180.0f;
        gx = sinf(pitch_rad) * PARTICLE_GRAVITY;
        gy = sinf(roll_rad) * PARTICLE_GRAVITY;
    } else {
        gx = ((float)(rand() % 100) / 100.0f - 0.5f) * PARTICLE_DRIFT;
        gy = ((float)(rand() % 100) / 100.0f - 0.5f) * PARTICLE_DRIFT;
    }

    int32_t scr_w = lv_display_get_horizontal_resolution(NULL);
    int32_t scr_h = lv_display_get_vertical_resolution(NULL);

    for (int i = 0; i < particle_count; i++) {
        particle_t *p = &particles[i];
        if (!p->obj) continue;

        p->vx += gx;
        p->vy += gy;
        p->vx *= PARTICLE_DAMPING;
        p->vy *= PARTICLE_DAMPING;

        if (p->vx > PARTICLE_MAX_VEL) p->vx = PARTICLE_MAX_VEL;
        if (p->vx < -PARTICLE_MAX_VEL) p->vx = -PARTICLE_MAX_VEL;
        if (p->vy > PARTICLE_MAX_VEL) p->vy = PARTICLE_MAX_VEL;
        if (p->vy < -PARTICLE_MAX_VEL) p->vy = -PARTICLE_MAX_VEL;

        p->x += p->vx;
        p->y += p->vy;

        lv_coord_t max_x = scr_w - p->size;
        lv_coord_t max_y = scr_h - p->size;
        if (p->x < 0) {
            p->x = 0;
            if (p->vx < 0) p->vx = -p->vx * PARTICLE_BOUNCE;
        }
        if (p->x > max_x) {
            p->x = max_x;
            if (p->vx > 0) p->vx = -p->vx * PARTICLE_BOUNCE;
        }
        if (p->y < 0) {
            p->y = 0;
            if (p->vy < 0) p->vy = -p->vy * PARTICLE_BOUNCE;
        }
        if (p->y > max_y) {
            p->y = max_y;
            if (p->vy > 0) p->vy = -p->vy * PARTICLE_BOUNCE;
        }

        lv_obj_set_pos(p->obj, (lv_coord_t)p->x, (lv_coord_t)p->y);
    }
}
#endif /* ENABLE_LOCKSCREEN_PARTICLES */

lv_obj_t *setupClock()
{
    const lv_font_t *font = &font_alibaba_100;
    uint32_t phy_hor = lv_display_get_physical_horizontal_resolution(NULL);
    uint32_t phy_ver = lv_display_get_physical_vertical_resolution(NULL);

    bool small = (phy_hor <= 240 || phy_ver <= 240);
    bool wide = (phy_hor == 320 && phy_ver == 240);

    if (small || wide) {
        font = &font_alibaba_60;
    }

    /* Background with image + dark overlay */
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE); /* NOT clickable — prevents tap wake */
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);

    /* Semi-transparent overlay — NOT clickable */
    clock_label.overlay = lv_obj_create(page);
    lv_obj_set_size(clock_label.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(clock_label.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(clock_label.overlay, lv_color_hex(0x050510), 0);
    lv_obj_set_style_bg_opa(clock_label.overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(clock_label.overlay, 0, 0);
    lv_obj_set_style_radius(clock_label.overlay, 0, 0);
    lv_obj_center(clock_label.overlay);

    /* Decorative accent glow — top-left circle with breathing animation */
    lv_obj_t *glow1 = lv_obj_create(page);
    int glow_size = small ? 140 : 220;
    lv_obj_set_size(glow1, glow_size, glow_size);
    lv_obj_set_style_bg_color(glow1, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(glow1, LV_OPA_30, 0);
    lv_obj_set_style_radius(glow1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow1, 0, 0);
    lv_obj_align(glow1, LV_ALIGN_TOP_LEFT, -glow_size / 4, -glow_size / 4);

    /* Breathing animation for top-left glow */
    static lv_anim_t glow1_anim;
    lv_anim_init(&glow1_anim);
    lv_anim_set_var(&glow1_anim, glow1);
    lv_anim_set_values(&glow1_anim, LV_OPA_10, LV_OPA_40);
    lv_anim_set_duration(&glow1_anim, 4000);
    lv_anim_set_playback_duration(&glow1_anim, 4000);
    lv_anim_set_repeat_count(&glow1_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&glow1_anim, [](void *var, int32_t val) {
        lv_obj_set_style_opa((lv_obj_t *)var, val, 0);
    });
    lv_anim_start(&glow1_anim);

    /* Decorative accent glow — bottom-right circle with breathing animation */
    lv_obj_t *glow2 = lv_obj_create(page);
    lv_obj_set_size(glow2, glow_size, glow_size);
    lv_obj_set_style_bg_color(glow2, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_bg_opa(glow2, LV_OPA_30, 0);
    lv_obj_set_style_radius(glow2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow2, 0, 0);
    lv_obj_align(glow2, LV_ALIGN_BOTTOM_RIGHT, glow_size / 4, glow_size / 4);

    /* Breathing animation for bottom-right glow — offset phase */
    static lv_anim_t glow2_anim;
    lv_anim_init(&glow2_anim);
    lv_anim_set_var(&glow2_anim, glow2);
    lv_anim_set_values(&glow2_anim, LV_OPA_40, LV_OPA_10);
    lv_anim_set_duration(&glow2_anim, 4000);
    lv_anim_set_playback_duration(&glow2_anim, 4000);
    lv_anim_set_repeat_count(&glow2_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&glow2_anim, [](void *var, int32_t val) {
        lv_obj_set_style_opa((lv_obj_t *)var, val, 0);
    });
    lv_anim_start(&glow2_anim);

    /* ── Charging glow — soft shadow edge effect when USB connected ── */
    charge_edge_ref = lv_obj_create(page);
    lv_obj_set_size(charge_edge_ref, LV_PCT(90), LV_PCT(85));
    lv_obj_set_style_bg_opa(charge_edge_ref, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(charge_edge_ref, 0, 0);
    lv_obj_set_style_radius(charge_edge_ref, 20, 0);
    lv_obj_set_style_shadow_width(charge_edge_ref, 40, 0);
    lv_obj_add_style(charge_edge_ref, &ui_styles.accent_shadow, 0); /* follows theme */
    lv_obj_set_style_shadow_spread(charge_edge_ref, 10, 0);
    lv_obj_center(charge_edge_ref);
    lv_obj_remove_flag(charge_edge_ref, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(charge_edge_ref, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(charge_edge_ref, LV_OBJ_FLAG_HIDDEN);

    bool charging = hw_adapter_is_connected();
    if (charging) {
        lv_obj_set_style_shadow_opa(charge_edge_ref, LV_OPA_30, 0);
        static lv_anim_t charge_anim;
        lv_anim_init(&charge_anim);
        lv_anim_set_var(&charge_anim, charge_edge_ref);
        lv_anim_set_values(&charge_anim, LV_OPA_10, LV_OPA_50);
        lv_anim_set_duration(&charge_anim, 2500);
        lv_anim_set_playback_duration(&charge_anim, 2500);
        lv_anim_set_repeat_count(&charge_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&charge_anim, [](void *var, int32_t val) {
            lv_obj_set_style_shadow_opa((lv_obj_t *)var, val, 0);
        });
        lv_anim_start(&charge_anim);
    } else {
        lv_obj_set_style_shadow_opa(charge_edge_ref, LV_OPA_TRANSP, 0);
    }

    /* ── Particle system (opt-in via ENABLE_LOCKSCREEN_PARTICLES) ── */
#ifdef ENABLE_LOCKSCREEN_PARTICLES
    srand(lv_tick_get());

    particle_has_imu = (hw_get_sensor_type() == SENSOR_TYPE_IMU);
    if (particle_has_imu) {
        hw_register_imu_process();
    }

    uint32_t pixels = phy_hor * phy_ver;
    int p_count, p_min_size, p_max_size;

    if (pixels <= 57600) {
        p_count = 25; p_min_size = 8; p_max_size = 16;
    } else if (pixels <= 76800) {
        p_count = 35; p_min_size = 10; p_max_size = 20;
    } else if (pixels <= 106560) {
        p_count = 45; p_min_size = 12; p_max_size = 22;
    } else {
        p_count = 60; p_min_size = 14; p_max_size = 26;
    }

    if (p_count > PARTICLE_MAX) p_count = PARTICLE_MAX;
    particle_count = p_count;

    for (int i = 0; i < particle_count; i++) {
        particle_t *p = &particles[i];
        p->size = p_min_size + rand() % (p_max_size - p_min_size + 1);
        p->x = rand() % (phy_hor - p->size);
        p->y = rand() % (phy_ver - p->size);
        p->vx = 0;
        p->vy = 0;

        p->obj = lv_obj_create(page);
        lv_obj_set_size(p->obj, p->size, p->size);
        lv_obj_set_style_radius(p->obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(p->obj, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(p->obj, 30 + rand() % 50, 0);
        lv_obj_set_style_border_width(p->obj, 0, 0);
        lv_obj_set_style_shadow_width(p->obj, 0, 0);
        lv_obj_set_pos(p->obj, (lv_coord_t)p->x, (lv_coord_t)p->y);
        lv_obj_remove_flag(p->obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(p->obj, LV_OBJ_FLAG_CLICKABLE);
    }
    particle_timer = lv_timer_create(particle_timer_cb, 40, NULL);
#endif /* ENABLE_LOCKSCREEN_PARTICLES */

    /* ── Time display ── */
    int y_time = small ? -(phy_ver / 6) : -(phy_ver / 5);
    int gap = small ? 2 : 4; /* gap between digits and colon */

    /* Measure digit width to position hour and minute symmetrically */
    /* Use the colon as anchor, place hour to its left and minute to its right */

    /* Colon separator with breathing animation — centered, anchor point */
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_add_style(label, &ui_styles.accent_text, 0);
    lv_label_set_text(label, ":");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_time);
    clock_label.seg = label;

    /* Breathing animation for colon */
    lv_anim_init(&colon_anim);
    lv_anim_set_var(&colon_anim, label);
    lv_anim_set_values(&colon_anim, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&colon_anim, 2000);
    lv_anim_set_playback_duration(&colon_anim, 2000);
    lv_anim_set_repeat_count(&colon_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&colon_anim, colon_anim_exec_cb);
    lv_anim_start(&colon_anim);

    /* Hour label — right-aligned to the left of colon */
    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_add_style(label, &ui_styles.accent_text, 0);
    lv_obj_set_style_shadow_width(label, 20, 0);
    lv_obj_set_style_shadow_opa(label, LV_OPA_20, 0);
    lv_obj_set_style_shadow_spread(label, 5, 0);
    lv_label_set_text(label, "12");
    lv_obj_align_to(label, clock_label.seg, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
    clock_label.hour = label;

    /* Minute label — left-aligned to the right of colon */
    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_add_style(label, &ui_styles.accent_text, 0);
    lv_obj_set_style_shadow_width(label, 20, 0);
    lv_obj_set_style_shadow_opa(label, LV_OPA_20, 0);
    lv_obj_set_style_shadow_spread(label, 5, 0);
    lv_label_set_text(label, "34");
    lv_obj_align_to(label, clock_label.seg, LV_ALIGN_OUT_RIGHT_MID, gap, 0);
    clock_label.minute = label;

    /* ── Divider line with accent color ── */
    lv_obj_t *div = lv_obj_create(page);
    lv_obj_set_size(div, small ? LV_PCT(40) : LV_PCT(30), 2);
    lv_obj_set_style_bg_color(div, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_30, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 1, 0);
    lv_obj_align(div, LV_ALIGN_CENTER, 0, y_time + (small ? 30 : 40));

    /* ── Unlock hint at bottom ── */
    lv_obj_t *hint = lv_label_create(page);
#if LOCKSCREEN_HAS_TOUCH_UNLOCK && LOCKSCREEN_HAS_BOOT_UNLOCK
    lv_label_set_text(hint, LV_SYMBOL_UP " Swipe up or press BOOT");
#elif LOCKSCREEN_HAS_TOUCH_UNLOCK
    lv_label_set_text(hint, LV_SYMBOL_UP " Swipe up to unlock");
#elif LOCKSCREEN_HAS_BOOT_UNLOCK
    lv_label_set_text(hint, LV_SYMBOL_POWER " Press BOOT Button to unlock");
#else
    lv_label_set_text(hint, "Unlock unavailable");
#endif
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_opa(hint, LV_OPA_40, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Breathing animation for hint */
    static lv_anim_t hint_anim;
    lv_anim_init(&hint_anim);
    lv_anim_set_var(&hint_anim, hint);
    lv_anim_set_values(&hint_anim, LV_OPA_20, LV_OPA_100);
    lv_anim_set_duration(&hint_anim, 3000);
    lv_anim_set_playback_duration(&hint_anim, 3000);
    lv_anim_set_repeat_count(&hint_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&hint_anim, colon_anim_exec_cb);
    lv_anim_start(&hint_anim);

    /* BOOT button (GPIO0) unlocks on DeckV2 and non-touch boards. */
#if LOCKSCREEN_HAS_BOOT_UNLOCK
    pinMode(0, INPUT_PULLUP);
#endif

    /* Swipe zone — full-screen for finger tracking on touch-capable boards. */
    lv_obj_t *swipe_zone = lv_obj_create(page);
    lv_obj_set_size(swipe_zone, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(swipe_zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swipe_zone, 0, 0);
    lv_obj_set_style_radius(swipe_zone, 0, 0);
    lv_obj_center(swipe_zone);
#if LOCKSCREEN_HAS_TOUCH_UNLOCK
    lv_obj_add_flag(swipe_zone, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_remove_flag(swipe_zone, LV_OBJ_FLAG_CLICKABLE);
#endif

    static lv_coord_t swipe_start_y = 0;
    static bool swipe_tracking = false;
    static lv_coord_t screen_h = 0;

    lv_obj_add_event_cb(swipe_zone, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        if (code == LV_EVENT_PRESSED) {
            swipe_start_y = p.y;
            swipe_tracking = true;
            screen_h = lv_display_get_vertical_resolution(NULL);
            /* Hide charging edge glow during swipe */
            if (charge_edge_ref) lv_obj_add_flag(charge_edge_ref, LV_OBJ_FLAG_HIDDEN);
            /* Show menu behind clock */
            lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
        } else if (code == LV_EVENT_PRESSING && swipe_tracking) {
            lv_coord_t dy = swipe_start_y - p.y;
            if (dy < 0) dy = 0;
            if (dy > screen_h) dy = screen_h;
            lv_obj_set_y(clock_page, -dy);
        } else if (code == LV_EVENT_RELEASED && swipe_tracking) {
            swipe_tracking = false;
            lv_coord_t dy = swipe_start_y - p.y;
            if (dy > screen_h / 4) {
                /* Swiped far enough */
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, clock_page);
                lv_anim_set_values(&a, lv_obj_get_y(clock_page), -(lv_coord_t)screen_h);
                lv_anim_set_time(&a, 200);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
                lv_anim_set_exec_cb(&a, [](void *var, int32_t val) {
                    lv_obj_set_y((lv_obj_t *)var, val);
                });
                lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
                    hw_enable_feedback();
                    hw_feedback();
                    clock_swipe_wake = true;
                });
                lv_anim_start(&a);
            } else {
                /* Spring back */
                lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, clock_page);
                lv_anim_set_values(&a, lv_obj_get_y(clock_page), 0);
                lv_anim_set_time(&a, 200);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *var, int32_t val) {
                    lv_obj_set_y((lv_obj_t *)var, val);
                });
                lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
                    /* Restore charging edge after spring-back */
                    if (charge_edge_ref) lv_obj_remove_flag(charge_edge_ref, LV_OBJ_FLAG_HIDDEN);
                });
                lv_anim_start(&a);
            }
        }
    }, LV_EVENT_ALL, NULL);

    /* ── Date label — below divider ── */
    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_24, 0);
    lv_label_set_text(label, "03-24 Mon");
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_time + (small ? 50 : 65));
    clock_label.date = label;

    /* ── Battery indicator — centered below date ── */
    int batt_y = y_time + (small ? 70 : 105);

    lv_obj_t *batt_icon = lv_label_create(page);
    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(batt_icon, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(batt_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(batt_icon, LV_ALIGN_CENTER, -30, batt_y);
    clock_label.battery_icon = batt_icon;
    clock_label.battery_y = batt_y;

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_text(label, "100%");
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align_to(label, batt_icon, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    clock_label.battery_label = label;

    /* Battery bar — hidden, only icon + percentage needed */
    clock_label.battery_bar = NULL;

    clock_timer = lv_timer_create(clock_update_datetime, 1000, NULL);
    lv_timer_pause(clock_timer);

    return page;
}


/* ── Battery low handler ── */
static void hw_device_poll(lv_timer_t *t)
{
    monitor_params_t params;
    hw_get_monitor_params(params);
    if (params.battery_voltage < 3300 && params.usb_voltage == 0) {
        LILYGO_LOG_PRINTF("Low battery voltage: %lu mV USB Voltage: %lu mV\n", params.battery_voltage, params.usb_voltage);
        lv_obj_clean(lv_screen_active());
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_radius(lv_screen_active(), 0, 0);

        lv_obj_t *image = lv_image_create(lv_screen_active());
        lv_image_set_src(image, &img_batter_low);
        lv_obj_center(image);

        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text_fmt(label, "Battery Low!\n%s", power_menu_transition_text());
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -30);

        lv_refr_now(NULL);
        lv_delay_ms(3000);
        hw_shutdown();
    }
}

/* (swipe detection handled by swipe zone in setupClock) */

/* ── Custom clock display timeout (independent of LVGL activity) ── */
static uint32_t clock_display_off_tick = 0;

/* ── Sleep/wake poll timer ── */
static void ui_poll_timer_callback(lv_timer_t *t)
{
    /* ── Clock page is showing ── */
    if (!lv_obj_has_flag(clock_page, LV_OBJ_FLAG_HIDDEN)) {

#if LOCKSCREEN_HAS_BOOT_UNLOCK
        /* BOOT button (GPIO0) unlocks directly; do not enable the keyboard. */
        if (digitalRead(0) == LOW) {
            clock_swipe_wake = true;
        }
#endif

        /* Check if swipe/button triggered wake */
        if (clock_swipe_wake) {
            clock_swipe_wake = false;

            hw_set_cpu_freq(240);

#ifdef ENABLE_LOCKSCREEN_PARTICLES
            if (particle_timer) {
                lv_timer_del(particle_timer);
                particle_timer = NULL;
            }
            if (particle_count > 0) {
                for (int i = 0; i < particle_count; i++) {
                    if (particles[i].obj) {
                        lv_obj_delete(particles[i].obj);
                        particles[i].obj = NULL;
                    }
                }
                if (particle_has_imu) {
                    hw_unregister_imu_process();
                }
                particle_count = 0;
            }
#endif

            lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(clock_page, 0);
            lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(clock_timer);
            hw_set_kb_backlight(keyboard_level);
            /* Restore hardware feedback */
            hw_enable_feedback();
            if (!hw_get_disp_is_on()) {
                hw_inc_brightness(brightness_level);
            }
            /* Reset inactivity timer so lock screen doesn't re-appear immediately */
            lv_display_trigger_activity(NULL);
            return;
        }

        /* Display-off timeout — use custom tick, not LVGL activity */
        if (clock_display_off_tick != 0 && lv_tick_get() > clock_display_off_tick) {
            bool disp_on = hw_get_disp_is_on();
            if (disp_on) {
                brightness_level = hw_get_disp_backlight();
                hw_dec_brightness(0);
                hw_low_power_loop();
#ifdef NO_ENTER_LIGHT_SLEEP
                LILYGO_LOG_PRINTF("Enter sleep\n");
                pinMode(0, INPUT_PULLUP);
                while (digitalRead(0) == HIGH) {
                    delay(10);
                }
                LILYGO_LOG_PRINTF("Wakeup\n");
#endif
                /* Woke up — stay on clock page, reset display timer */
                hw_set_cpu_freq(240);
                hw_inc_brightness(brightness_level);
                hw_set_kb_backlight(keyboard_level);
                if (hw_get_disp_timeout_ms() != 0) {
                    clock_display_off_tick = lv_tick_get() + hw_get_disp_timeout_ms();
                }
                clock_swipe_wake = false;
                lv_refr_now(NULL);
            }
        }
        return;
    }

    if (power_menu_overlay) {
        return;
    }

    /* ── Menu is showing — check for inactivity timeout ── */
    bool timeout = lv_display_get_inactive_time(NULL) > SCREEN_TIMEOUT;
    if (timeout) {
        if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN) && get_enter_low_power_flag()) {
            lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);

            keyboard_level = hw_get_kb_backlight();
            hw_set_kb_backlight(0);
            lv_obj_remove_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_timer_resume(clock_timer);
            clock_swipe_wake = false;

            /* Restore charging edge visibility when re-entering clock */
            if (charge_edge_ref) {
                lv_obj_remove_flag(charge_edge_ref, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_y(clock_page, 0);
            }

            hw_set_cpu_freq(80);

            /* Disable hardware feedback */
            hw_disable_feedback();

            /* Set custom display-off timer (not dependent on LVGL activity) */
            if (hw_get_disp_timeout_ms() != 0) {
                clock_display_off_tick = lv_tick_get() + hw_get_disp_timeout_ms();
            } else {
                clock_display_off_tick = 0;
            }
        }
    }

    /* Display-off timeout — turn off backlight, enter low-power wait */
    if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
        bool disp_on = hw_get_disp_is_on();
        if (disp_on && disp_time_ms != 0) {
            if (lv_tick_get() > disp_time_ms) {
                LILYGO_LOG_PRINTF("Disp off\n");

                brightness_level = hw_get_disp_backlight();
                hw_dec_brightness(0);

                hw_low_power_loop();
#ifdef NO_ENTER_LIGHT_SLEEP
                LILYGO_LOG_PRINTF("Enter sleep\n");
                pinMode(0, INPUT_PULLUP);
                while (digitalRead(0) == HIGH) {
                    delay(10);
                }
                LILYGO_LOG_PRINTF("Wakeup\n");
#endif
                /* Woke up — restore display but stay on clock page (require swipe-up) */
                hw_set_cpu_freq(240);
                hw_inc_brightness(brightness_level);
                hw_set_kb_backlight(keyboard_level);
                /* Restore hardware feedback */
                hw_enable_feedback();

                /* Reset display timeout so clock stays visible */
                if (hw_get_disp_timeout_ms() != 0) {
                    disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
                }
                clock_swipe_wake = false;

                lv_refr_now(NULL);
            }
        }
    }
}


/* ── Boot screen with progress bar ── */
static lv_obj_t *boot_progress_bar;
static lv_obj_t *boot_line;
static uint32_t boot_start_tick;

static void boot_anim_timer_cb(lv_timer_t *t)
{
    uint32_t elapsed = lv_tick_get() - boot_start_tick;
    uint32_t duration = 3500;
    int32_t pct = (int32_t)((elapsed * 100) / duration);
    if (pct > 100) pct = 100;
    lv_bar_set_value(boot_progress_bar, pct, LV_ANIM_ON);

    /* Animate line width */
    lv_coord_t max_w = lv_display_get_physical_horizontal_resolution(NULL) / 2;
    lv_coord_t cur_w = (lv_coord_t)((int32_t)max_w * pct / 100);
    static lv_point_t pts[2];
    pts[0].x = max_w - cur_w;
    pts[1].x = max_w + cur_w;

    LV_UNUSED(t);
}


/* ============================================================
 *  setupGui  — main entry
 * ============================================================ */
void setupGui()
{
    /* Load saved theme preset */
    user_setting_params_t settings;
    hw_get_user_setting(settings);
    if (settings.theme_preset_idx < UI_THEME_PRESET_COUNT) {
        ui_apply_theme_preset(settings.theme_preset_idx);
    }
    ui_styles_init();

    lv_obj_set_style_bg_color(lv_screen_active(), UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);

    /* ── Boot screen ── */
    LV_FONT_DECLARE(font_logo_84);

    lv_obj_t *boot_bg = lv_obj_create(lv_screen_active());
    lv_obj_set_size(boot_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(boot_bg, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(boot_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(boot_bg, 0, 0);
    lv_obj_set_style_radius(boot_bg, 0, 0);

    /* Logo — clean accent color */
    lv_obj_t *start_logo = lv_label_create(boot_bg);
    lv_label_set_text(start_logo, "LilyGo");
    lv_obj_set_style_text_font(start_logo, &font_logo_84, 0);
    lv_obj_set_style_text_color(start_logo, UI_COLOR_ACCENT, 0);
    lv_obj_align(start_logo, LV_ALIGN_CENTER, 0, -30);

    /* Decorative accent line below logo */
    lv_obj_t *line = lv_obj_create(boot_bg);
    lv_obj_set_size(line, lv_pct(40), 2);
    lv_obj_set_style_bg_color(line, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 1, 0);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, -8);

    /* Loading text */
    lv_obj_t *loading_label = lv_label_create(boot_bg);
    lv_label_set_text(loading_label, "Loading...");
    lv_obj_set_style_text_color(loading_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_14, 0);
    lv_obj_align(loading_label, LV_ALIGN_CENTER, 0, 20);

    /* Progress bar */
    boot_progress_bar = lv_bar_create(boot_bg);
    lv_obj_set_size(boot_progress_bar, lv_display_get_physical_horizontal_resolution(NULL) * 60 / 100, 6);
    lv_bar_set_range(boot_progress_bar, 0, 100);
    lv_bar_set_value(boot_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(boot_progress_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_progress_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(boot_progress_bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(boot_progress_bar, LV_ALIGN_CENTER, 0, 40);

    lv_refr_now(NULL);
    hw_play_boot_sound_async();
    boot_start_tick = lv_tick_get();
    lv_timer_t *boot_timer = lv_timer_create(boot_anim_timer_cb, 30, NULL);

    /* Refresh display before backlight */
    lv_refr_now(NULL);
    // lv_timer_handler();
    user_setting_params_t param;
    hw_get_user_setting(param);
    /* Enable display backlight */
    hw_set_disp_backlight(param.brightness_level);
    // hw_disp_enable_backlight(true);
    /* Enable keyboard backlight */
    hw_kb_enable_backlight(true);

    /* Wait ~3.5 seconds, processing LVGL timers so progress bar updates */
    uint32_t boot_end = lv_tick_get() + 3500;
    while (lv_tick_get() < boot_end) {
        lv_timer_handler();
        lv_delay_ms(10);
    }

    /* Clean up boot screen */
    lv_bar_set_value(boot_progress_bar, 100, LV_ANIM_OFF);
    if (boot_timer) lv_timer_del(boot_timer);
    lv_obj_delete(boot_bg);

    /* ── Initialize theme & groups ── */
    disable_keyboard();

    const lv_font_t  *main_font = MAIN_FONT;
    lv_theme_default_init(NULL, UI_COLOR_BG, lv_palette_darken(LV_PALETTE_GREY, 3),
                          LV_THEME_DEFAULT_DARK, main_font);

    theme_init();

    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    /* ── Main tileview ── */
    main_screen = lv_tileview_create(lv_screen_active());
    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_TRANSP, 0);

    /* Tile (0,0) = menu, Tile (0,1) = app content */
    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Register all apps ── */
    extern app_t ui_sys_main;
    extern app_t ui_radio_main;
    extern app_t ui_lora_hub_main;
    extern app_t ui_audio_main;
    extern app_t ui_music_player_main;
    extern app_t ui_net_radio_main;
    extern app_t ui_wireless_main;
    extern app_t ui_monitor_main;
    extern app_t ui_power_main;
    extern app_t ui_info_main;
#if !defined(EXCLUDE_DRV2605)
    extern app_t ui_vibration_main;
#endif
    extern app_t ui_microphone_main;
    extern app_t ui_music_eyes_main;
    extern app_t ui_keyboard_main;
    extern app_t ui_sensor_main;
    extern app_t ui_sensor_logger_main;
    extern app_t ui_msgchat_main;
    extern app_t ui_ble_hid_main;
    extern app_t ui_factory_main;
    extern app_t ui_i2c_scanner_main;
#if !defined(EXCLUDE_MAX30102)
    extern app_t ui_max30102_main;
    extern bool ui_max30102_probe(void);
#endif
#if !defined(EXCLUDE_SD_APPS)
    extern app_t ui_sdcard_main;
#endif
#if !defined(EXCLUDE_SD_MANAGER)
    extern app_t ui_sd_manager_main;
#endif
#if !defined(EXCLUDE_USB_DISK)
    extern app_t ui_usb_disk_main;
#endif
#if !defined(EXCLUDE_BAD_USB)
    extern app_t ui_bad_usb_main;
#endif

#if !defined(EXCLUDE_CLOCK_TOOLS)
    extern app_t ui_clock_tools_main;
#endif
#if !defined(EXCLUDE_QRCODE)
    extern app_t ui_qrcode_main;
#endif
#if !defined(EXCLUDE_WIFI_TOOLS)
    extern app_t ui_wifi_tools_main;
#endif
#if defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_WS2812_STRIP)
    extern app_t ui_ws2812_main;
#endif

#if !defined(EXCLUDE_IR_REMOTE)
    extern app_t ui_ir_remote_main;
    register_app("IR Send", &img_ir_remote, &ui_ir_remote_main);
#endif

#if !defined(EXCLUDE_IR_RECORDER)
    extern app_t ui_ir_recorder_main;
    if (device_has_sd_storage()) {
        register_app("IR Record", &img_ir_recv, &ui_ir_recorder_main);
    }
#endif

#if !defined(EXCLUDE_IR_PLAYER)
    extern app_t ui_ir_player_main;
    if (device_has_sd_storage()) {
        register_app("IR Play", &img_ir_remote, &ui_ir_player_main);
    }
#endif

#if !defined(EXCLUDE_NRF24)
    extern app_t ui_nrf24_main;
    register_app("NRF24", &img_radio, &ui_nrf24_main);
#endif

#if !defined(EXCLUDE_SI4735_RADIO_WF)
    extern app_t ui_si4735_radio_wf_main;
    register_app("Radio", &img_si4735, &ui_si4735_radio_wf_main);
#endif

#if !defined(EXCLUDE_COMPASS)
    extern app_t ui_compass_main;
    register_app("Compass", &img_compass, &ui_compass_main);
#endif

#if !defined(EXCLUDE_BLE_SCANNER)
    extern app_t ui_ble_scanner_main;
    register_app("BLE Scan", &img_bluetooth, &ui_ble_scanner_main);
#endif

#if !defined(EXCLUDE_BLE_HID)
    register_app("BLE HID", &img_bluetooth, &ui_ble_hid_main);
#endif

#if !defined(EXCLUDE_TRACKBALL)
    extern app_t ui_trackball_main;
    register_app("Trackball", &img_track, &ui_trackball_main);

    // extern app_t ui_paw_mouse_main;
#endif

#if !defined(EXCLUDE_NFC)
    extern app_t ui_nfc_main;
    register_app("NFC", &img_nfc, &ui_nfc_main);
#endif
#if defined(ARDUINO) && defined(ARDUINO_TWATCH_BASE) && !defined(EXCLUDE_PN532)
    if (hw_get_device_online() & HW_NFC_ONLINE) {
        extern app_t ui_pn532_main;
        register_app("PN532", &img_nfc, &ui_pn532_main);
    }
#endif
#if !defined(EXCLUDE_NFC_EMULATION)
    extern app_t ui_nfc_emulation_main;
    register_app("NFC Card", &img_nfc, &ui_nfc_emulation_main);
#endif

#if !defined(EXCLUDE_AUDIO_RECORDER)
    extern app_t ui_recorder_main;
    register_app("Recorder", &img_microphone, &ui_recorder_main);
#endif

    register_app("DisplayTest", &img_test, &ui_factory_main);

    register_app("I2C Scan", &img_scan, &ui_i2c_scanner_main);

#if !defined(EXCLUDE_CLOCK_TOOLS)
    register_app("Clock Tools", &img_clock, &ui_clock_tools_main);
#endif
#if !defined(EXCLUDE_QRCODE)
    register_app("QRCode", &img_qrcode, &ui_qrcode_main);
#endif

#if !defined(EXCLUDE_SD_APPS)
    if (device_has_sd_storage()) {
        register_app("SD Info", &img_sdcard, &ui_sdcard_main);

#if !defined(EXCLUDE_SD_MANAGER)
        register_app("File Manager", &img_file_manager, &ui_sd_manager_main);
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !ARDUINO_USB_MODE && !defined(EXCLUDE_USB_DISK)
        register_app("USB Disk", &img_sdcard, &ui_usb_disk_main);
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !ARDUINO_USB_MODE && !defined(EXCLUDE_BAD_USB)
        register_app("BadUSB", &img_usb, &ui_bad_usb_main);
#endif
    }

#endif

    register_app("Setting", &img_configuration, &ui_sys_main);

    register_app("WiFi", &img_wifi, &ui_wireless_main);

#ifndef EXCLUDE_WIFI_ANALYZER
    extern app_t ui_wifi_analyzer_main;
    register_app("WiFi Analyzer", &img_wireless, &ui_wifi_analyzer_main);
#endif

#if !defined(EXCLUDE_WIFI_TOOLS)
    register_app("WiFi Tools", &img_tools, &ui_wifi_tools_main);
#endif

#if !defined(EXCLUDE_KEYBOARD)
    if (hw_has_keyboard()) {
        register_app("Keyboard", &img_keyboard, &ui_keyboard_main);
    }
#endif

#if !defined(EXCLUDE_AUDIO_PLAYER)
    // register_app("Music", &img_music, &ui_audio_main);
    register_app("Player", &img_music, &ui_music_player_main);
    register_app("Net Radio", &img_network_radio, &ui_net_radio_main);

#if !defined(EXCLUDE_I2S_TEST)
    extern app_t ui_i2s_test_main;
    register_app("I2S Test", &img_music, &ui_i2s_test_main);
#endif

#endif

    register_app("Music Eyes", &img_face, &ui_music_eyes_main);

#if !defined(EXCLUDE_NES) && (defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_LORA_PAGER))
    extern app_t ui_nes_main;
    register_app("NES", &img_game, &ui_nes_main);
#endif

#if (defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_WS2812_STRIP)) || !defined(EXCLUDE_LORA)
    const bool has_lora_hardware = hw_has_lora_hardware();
#endif

#if defined(ARDUINO_T_LORA_PAGER) && !defined(EXCLUDE_WS2812_STRIP)
    if (!has_lora_hardware) {
        register_app("LED Strip", &img_led, &ui_ws2812_main);
    }
#endif

#if !defined(EXCLUDE_LORA)
    if (has_lora_hardware) {
#if !defined(EXCLUDE_CC1101_TOOL)
        extern app_t ui_cc1101_tool_main;
        register_app("Sub-G", &img_radio, &ui_cc1101_tool_main);
#else
        register_app("LoRa", &img_radio, &ui_lora_hub_main);
#endif
#if !defined(EXCLUDE_LORAWAN)
        extern app_t ui_lorawan_lr1121_main;
        register_app("LoRaWAN", &img_lorawan, &ui_lorawan_lr1121_main);
#endif

        // register_app("Sub-G Tools", &img_radio, &ui_radio_main);

#ifndef EXCLUDE_LORA_SCANNER
        // extern app_t ui_lora_scanner_main;
        // register_app("LoRa Scan", &img_radio, &ui_lora_scanner_main);
#endif
    }

#if defined(ARDUINO)
#if !defined(EXCLUDE_WALKIE) && \
    (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(4,0,0))
    extern app_t ui_walkie_main;
    register_app("Walkie", &img_walkie, &ui_walkie_main);
#endif
#endif
#endif

#if !defined(EXCLUDE_GPS)
    extern app_t ui_gnss_main;
    register_app("GNSS", &img_gps, &ui_gnss_main);

#if !defined(EXCLUDE_TRACK_LOGGER)
    extern app_t ui_track_logger_main;
    if (device_has_sd_storage()) {
        register_app("Track Log", &img_gps_track, &ui_track_logger_main);
    }
#endif

#endif

    register_app("Monitor", &img_monitoring, &ui_monitor_main);

#if !defined(EXCLUDE_DRV2605)
    if (hw_get_device_online() & HW_DRV_ONLINE) {
        register_app("Vibration", &img_tools, &ui_vibration_main);
    }
#endif


#if !defined(EXCLUDE_INA219)
    extern app_t ui_ina219_main;
    if (device_has_external_i2c()) {
        register_app("INA219", &img_energy, &ui_ina219_main);
    }
#endif

#if !defined(EXCLUDE_THERMAL)
    extern app_t ui_thermal_main;
    if (device_has_external_i2c()) {
        register_app("Thermal", &img_temperature, &ui_thermal_main);
    }
#endif

#if !defined(EXCLUDE_MAX30102)
    if (ui_max30102_probe()) {
        register_app("MAX30102", &img_monitoring, &ui_max30102_main);
    }
#endif

    register_app("Power", &img_power, &ui_power_main);

#if !defined(EXCLUDE_MICROPHONE)
    register_app("Mic", &img_mic, &ui_microphone_main);
#endif

#if !defined(EXCLUDE_IMU)
    const char *sensorName = hw_get_sensor_type() == SENSOR_TYPE_IMU ? "IMU" : "Accel";
    register_app(sensorName, &img_gyroscope, &ui_sensor_main);
#endif
#if !defined(EXCLUDE_SENSOR_LOGGER)
    // register_app("Sensor Log", &img_gyroscope, &ui_sensor_logger_main);
#endif

    /* ── Build the grid menu ── */
    build_grid_menu(menu_panel);

    /* ── Status bar timer ── */
    status_bar_timer = lv_timer_create(status_bar_timer_cb, 10000, NULL);

    /* ── Clock page ── */
    clock_page = setupClock();
    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);

    /* ── Sleep/wake timer — 200ms for responsive button wake ── */
    disp_timer = lv_timer_create(ui_poll_timer_callback, 200, NULL);
    power_menu_timer = lv_timer_create(power_menu_timer_cb, 200, NULL);

    /* Allow low power mode */
    set_low_power_mode_flag(true);
    lv_display_trigger_activity(NULL);
#if FACTORY_HAS_TOUCH_INPUT
    show_touch_guide();
#endif
}
