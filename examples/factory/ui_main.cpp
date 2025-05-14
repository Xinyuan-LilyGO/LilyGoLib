/**
 * @file      ui.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#include "ui_define.h"

LV_IMG_DECLARE(img_microphone);
LV_IMG_DECLARE(img_controller);
LV_IMG_DECLARE(img_music);
LV_IMG_DECLARE(img_wifi);
LV_IMG_DECLARE(img_configuration);
LV_IMG_DECLARE(img_radio);
LV_IMG_DECLARE(img_gps);
LV_IMG_DECLARE(img_power);
LV_IMG_DECLARE(img_monitoring);
LV_IMG_DECLARE(img_calendar);
LV_IMG_DECLARE(img_keyboard);
LV_IMG_DECLARE(img_gyroscope);
LV_IMG_DECLARE(img_msgchat);
LV_IMG_DECLARE(img_bluetooth);
LV_IMG_DECLARE(img_test);
LV_IMG_DECLARE(img_sports);
LV_IMG_DECLARE(img_background);
LV_IMG_DECLARE(img_battery);
LV_IMG_DECLARE(img_MotionRecognition);
LV_IMG_DECLARE(img_MotorLearning);
LV_IMG_DECLARE(img_camera);

LV_IMG_DECLARE(img_background2);

LV_FONT_DECLARE(font_alibaba_12);
LV_FONT_DECLARE(font_alibaba_24);
LV_FONT_DECLARE(font_alibaba_40);
LV_FONT_DECLARE(font_alibaba_60);
LV_FONT_DECLARE(font_alibaba_100);

#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)

lv_obj_t *main_screen;
lv_obj_t *menu_panel;
lv_group_t *menu_g, *app_g;
static lv_timer_t *clock_timer;
static lv_obj_t *clock_page;
static lv_timer_t *disp_timer = NULL;
static uint32_t disp_time_ms = 0;

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *seg;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_label;
} clock_label_t;;

static clock_label_t clock_label;

#if LVGL_VERSION_MAJOR == 9
static uint32_t name_change_id;
#endif


static lv_obj_t *desc_label;
static RTC_DATA_ATTR uint8_t brightness_level = 0;
static RTC_DATA_ATTR uint8_t keyboard_level = 0;

void set_low_power_mode_flag(bool enable)
{
    if (enable) {
        lv_obj_add_flag(main_screen, DEVICE_CAN_SLEEP);
    } else {
        lv_obj_clear_flag(main_screen, DEVICE_CAN_SLEEP);
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
    lv_obj_set_tile_id(main_screen, 0, 0, LV_ANIM_ON);
    lv_timer_resume(disp_timer);
    lv_disp_trig_activity(NULL);
}

void menu_hidden()
{
    lv_obj_set_tile_id(main_screen, 0, 1, LV_ANIM_ON);
    lv_timer_pause(disp_timer);
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


static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    char *text = (char *)lv_event_get_user_data(e);
    if (c == LV_EVENT_FOCUSED) {
#if LVGL_VERSION_MAJOR == 9
        lv_obj_send_event(desc_label, (lv_event_code_t )name_change_id, text);
#else
        lv_msg_send(MSG_MENU_NAME_CHANGED, text);
#endif
    }
}

static void create_app(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app_fun)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_coord_t w = 150;
    lv_coord_t h = LV_PCT(100);

    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);
    uint32_t phy_hor_res = lv_disp_get_physical_hor_res(NULL);
    if (phy_hor_res < 320) {
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_set_user_data(btn, (void *)name);

    if (img != NULL) {
        lv_obj_t *icon = lv_img_create(btn);
        lv_img_set_src(icon, img);
        lv_obj_center(icon);
    }
    /* Text change event callback */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_FOCUSED, (void *)name);

    /* Click to select event callback */
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        if (c == LV_EVENT_CLICKED) {
            set_default_group(app_g);
            if (func_cb->setup_func_cb) {
                (*func_cb->setup_func_cb)(parent);
            }
            menu_hidden();
        }
    },
    LV_EVENT_CLICKED, app_fun);
}


void menu_name_label_event_cb(lv_event_t *e)
{
#if LVGL_VERSION_MAJOR == 9
    const char *v = (const char *)lv_event_get_param(e);
    if (v) {
        lv_label_set_text(lv_event_get_target_obj(e), v);
    }
#else
    lv_obj_t *label = lv_event_get_target(e);
    lv_msg_t *m = lv_event_get_msg(e);
    const char *v = (const char *)lv_msg_get_payload(m);
    if (v) {
        lv_label_set_text(label, v);
    }
#endif
}



static void clock_update_datetime(lv_timer_t *t)
{
    lv_obj_has_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) ?
    lv_obj_clear_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) :
    lv_obj_add_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN);

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(clock_label.hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(clock_label.minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(clock_label.date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
    monitor_params_t params;
    hw_get_monitor_params(params);
    lv_bar_set_value(clock_label.battery_bar, params.battery_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(clock_label.battery_label, "%d%%", params.battery_percent);
}

lv_obj_t *setupClock()
{

    const  lv_font_t *font = &font_alibaba_100;

    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_style_bg_img_src(page, &img_background2, LV_PART_MAIN);

    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_coord_t w = LV_PCT(35);
    lv_coord_t h = LV_PCT(70);

    int x_offset = 35;
    int y_offset = -20;

    uint32_t phy_hor_res = lv_disp_get_physical_hor_res(NULL);
    if (phy_hor_res < 320) {
        font = &font_alibaba_60;
        x_offset = 10;
        y_offset = -20;
        w = LV_PCT(40);
        h = LV_PCT(48);
    }


    lv_obj_t *hour_cout = lv_obj_create(page);
    lv_obj_set_size(hour_cout, w, h);
    lv_obj_align(hour_cout, LV_ALIGN_LEFT_MID, x_offset, y_offset);
    lv_obj_set_style_bg_opa(hour_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hour_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_clear_flag(hour_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_cout = lv_obj_create(page);
    lv_obj_set_size(min_cout, w, h);
    lv_obj_align(min_cout, LV_ALIGN_RIGHT_MID, -x_offset, y_offset);
    lv_obj_set_style_bg_opa(min_cout, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_opa(min_cout, LV_OPA_60, LV_PART_MAIN);
    lv_obj_clear_flag(min_cout, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(page);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -10 + y_offset);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, ":");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    clock_label.seg = label;

    label = lv_label_create(hour_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "12");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    clock_label.hour = label;

    label = lv_label_create(min_cout);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_label_set_text(label, "34");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    clock_label.minute = label;

    int offset = -5;
    if (lv_disp_get_physical_ver_res(NULL) > 320) {
        offset = -45;
    }

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_24, LV_PART_MAIN);
    lv_label_set_text(label, "03-24 Mon");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, offset);
    clock_label.date = label;


    lv_obj_t *img = lv_img_create(page);
    lv_img_set_src(img, &img_battery);
    lv_obj_align_to(img, min_cout, LV_ALIGN_OUT_BOTTOM_RIGHT, -10, 20);

    lv_obj_t *bar = lv_bar_create(img);
    lv_obj_set_size(bar, img_battery.header.w - 8, img_battery.header.h - 12);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_make(0, 255, 0), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_CENTER, -1, 0);
    clock_label.battery_bar = bar;


    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &font_alibaba_12, LV_PART_MAIN);
    lv_label_set_text(label, "100%");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align_to(label, img, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    clock_label.battery_label = label;

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 4);
    lv_style_set_line_color(&style_line, lv_color_white());

    lv_obj_t *line1;
    line1 = lv_line_create(page);
    static lv_point_t line_points[] = {
        {0, 0},
        {150, 0}
    };
    lv_line_set_points(line1, line_points, 2);
    lv_obj_add_style(line1, &style_line, 0);
    lv_obj_align(line1, LV_ALIGN_BOTTOM_MID, 0, 15);
    lv_obj_set_style_line_opa(line1, LV_OPA_60, LV_PART_MAIN);


    clock_timer = lv_timer_create(clock_update_datetime, 1000, NULL);
    lv_timer_pause(clock_timer);

    return page;
}

#ifdef USING_TOUCHPAD
typedef struct {
    lv_obj_t *obj;
    int id;
} ChildObject;

static void scrollbar_change_cb(lv_event_t *e)
{
    lv_obj_t *tileview = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *panel = lv_event_get_target_obj(e);
    lv_coord_t view_x = lv_obj_get_scroll_x(panel);
    lv_coord_t view_width = lv_obj_get_width(panel);
    int child_count = lv_obj_get_child_count(panel);
    ChildObject *child_objects = (ChildObject *)lv_mem_alloc(child_count * sizeof(ChildObject));

    for (int i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(panel, i);
        child_objects[i].obj = child;
        child_objects[i].id = i;
    }

    uint8_t view_obj_count = 0;
    int32_t last_id = -1;
    for (int i = 0; i < child_count; i++) {
        lv_obj_t *child = child_objects[i].obj;
        lv_coord_t child_x = lv_obj_get_x(child);
        lv_coord_t child_width = lv_obj_get_width(child);

        if (child_x + child_width > view_x && child_x < view_x + view_width) {
            last_id = child_objects[i].id;
            view_obj_count++;
        }
    }

    if (last_id != -1) {
        lv_obj_t *obj = child_objects[last_id].obj;
        if (lv_obj_get_child(panel, 1) == obj || view_obj_count == 3) {
            last_id -= 1;
        }
        const char *name = (const char *)lv_obj_get_user_data(child_objects[last_id].obj);
        if (name) {
#if LVGL_VERSION_MAJOR == 9
            lv_obj_send_event(desc_label, (lv_event_code_t )name_change_id, (void*)name);
#else
            lv_msg_send(MSG_MENU_NAME_CHANGED, (void*)name);
#endif
        }
    }

    lv_mem_free(child_objects);

}
#endif

void setupGui()
{

    disable_keyboard();

    const lv_font_t  *main_font = NULL;

#if defined(ARDUINO_T_LORA_PAGER)
    main_font = &lv_font_montserrat_16;
#elif defined(ARDUINO_T_WATCH_S3)
    main_font = &lv_font_montserrat_12;
#else
    main_font = &lv_font_montserrat_22;
#endif

    lv_theme_default_init(NULL, lv_color_black(), lv_palette_darken(LV_PALETTE_GREY, 3),
                          LV_THEME_DEFAULT_DARK, main_font);

    theme_init();

    // Create group
    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    static lv_style_t style_frameless;
    lv_style_init(&style_frameless);
    lv_style_set_radius(&style_frameless, 0);
    lv_style_set_border_width(&style_frameless, 0);
    lv_style_set_bg_color(&style_frameless, lv_color_white());
    lv_style_set_shadow_width(&style_frameless, 55);
    lv_style_set_shadow_color(&style_frameless, lv_color_black());

    /* opening animation */
    main_screen = lv_tileview_create(lv_scr_act());
    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    /* Create two views for switching menus and app UI */
    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Initialize the menu view */
    lv_obj_t *panel = lv_obj_create(menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(70));
    lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(panel, &style_frameless, 0);
#ifdef USING_TOUCHPAD
    lv_obj_add_event_cb(panel, scrollbar_change_cb, LV_EVENT_SCROLL_END, NULL);
#endif

    extern app_t ui_sys_main ;
    extern app_t ui_radio_main ;
    extern app_t ui_audio_main ;
    extern app_t ui_wireless_main ;
    extern app_t ui_gps_main ;
    extern app_t ui_monitor_main ;
    extern app_t ui_power_main ;
    extern app_t ui_calendar_main;
    extern app_t ui_info_main;
    extern app_t ui_microphone_main;
    extern app_t ui_keyboard_main;
    extern app_t ui_sensor_main;
    extern app_t ui_msgchat_main;
    extern app_t ui_ble_main;
    extern app_t ui_ble_kb_main;
    extern app_t ui_factory_main;
    // extern app_t ui_camera_remote_main;

    // #ifndef ARDUINO
    //     extern app_t ui_nfc_main;
    //     create_app(panel, "NFC", &img_test, &ui_nfc_main);
    // #endif

    /* Add application */
    // create_app(panel, "Info", &img_controller, &ui_info_main);
    // create_app(panel, "Calendar", &img_calendar, &ui_calendar_main);

#if defined(USING_EXTERN_NRF2401)
    extern app_t ui_nrf24_main;
    create_app(panel, "NRF24", &img_radio, &ui_nrf24_main);
#endif

#if defined(USING_BLE_CONTROL)
    create_app(panel, "Camera Remote", &img_camera, &ui_camera_remote_main);
#endif

    create_app(panel, "Screen Test", &img_test, &ui_factory_main);
    create_app(panel, "Setting", &img_configuration, &ui_sys_main);
    create_app(panel, "Wireless", &img_wifi, &ui_wireless_main);

#if defined(USING_UART_BLE)
    create_app(panel, "Bluetooth", &img_bluetooth, &ui_ble_main);
#endif

#if defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_has_keyboard()) {
        create_app(panel, "BLE Keyboard", &img_bluetooth, &ui_ble_kb_main);
        create_app(panel, "Keyboard", &img_keyboard, &ui_keyboard_main);
    }
#endif

    create_app(panel, "Music", &img_music, &ui_audio_main);
    create_app(panel, "Radio", &img_radio, &ui_radio_main);
    create_app(panel, "LoRa Chat", &img_msgchat, &ui_msgchat_main);
    create_app(panel, "GPS", &img_gps, &ui_gps_main);
    create_app(panel, "Monitor", &img_monitoring, &ui_monitor_main);
    create_app(panel, "Power", &img_power, &ui_power_main);
    create_app(panel, "Microphone", &img_microphone, &ui_microphone_main);
    create_app(panel, "IMU", &img_gyroscope, &ui_sensor_main);

    int offset = -10;
    if (lv_disp_get_physical_ver_res(NULL) > 320) {
        offset = -45;
    }
    /* Initialize the label */
    desc_label = lv_label_create(menu_panel);
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, offset);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);

    if (lv_disp_get_physical_hor_res(NULL) < 320) {
        lv_obj_set_style_text_font(desc_label, &font_alibaba_24, 0);
        lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -25);
    } else {
        lv_obj_set_style_text_font(desc_label, &font_alibaba_40, 0);
    }
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

#if LVGL_VERSION_MAJOR == 9
    name_change_id =  lv_event_register_id();
    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, (lv_event_code_t )name_change_id, NULL);
    lv_obj_send_event(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);
#else
    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, LV_EVENT_MSG_RECEIVED, NULL);
    lv_msg_subsribe_obj(MSG_MENU_NAME_CHANGED, desc_label, NULL);
    lv_event_send(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);
#endif

    lv_obj_update_snap(panel, LV_ANIM_ON);

#define SCREEN_TIMEOUT 10000

    clock_page = setupClock();
    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);

    disp_timer = lv_timer_create([](lv_timer_t *t) {
        bool timeout = lv_disp_get_inactive_time(NULL) > SCREEN_TIMEOUT;


        // printf("timer run: %u\n", lv_disp_get_inactive_time(NULL));
        if (timeout) {
            if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN) && get_enter_low_power_flag()) {
                lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);

                keyboard_level = hw_get_kb_backlight();
                hw_set_kb_backlight(0);
                lv_obj_clear_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                lv_timer_resume(clock_timer);

                hw_set_cpu_freq(80);

                if (hw_get_disp_timeout_ms() != 0) {
                    disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
                } else {
                    disp_time_ms = 0;
                }
                // printf("disp_time_ms start:%u\n", disp_time_ms);
            }
        } else {
            if (!lv_obj_has_flag(clock_page, LV_OBJ_FLAG_HIDDEN)) {

                hw_set_cpu_freq(240);

                lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(clock_timer);

                hw_set_kb_backlight(keyboard_level);
            }
        }

        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            bool disp_on = hw_get_disp_is_on();
            // printf("lv_tick_get run:%u disp_on:%d\n", lv_tick_get(), disp_on);
            if (disp_on && disp_time_ms != 0) {
                if (lv_tick_get() > disp_time_ms) {
                    printf("Disp off\n");

                    brightness_level =  hw_get_disp_backlight();
                    printf("brightness_level:%d\n", brightness_level);

                    hw_dec_brightness(0);

                    hw_low_power_loop();
#ifdef NO_ENTER_LIGHT_SLEEP
                    printf("Enter sleep\n");
                    pinMode(0, INPUT_PULLUP);
                    while (digitalRead(0) == HIGH) {
                        delay(10);
                    }
                    printf("Wakeup\n");
#endif
                    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                    lv_timer_pause(clock_timer);

                    hw_set_cpu_freq(240);

                    lv_refr_now(NULL);

                    lv_disp_trig_activity(NULL);

                    hw_inc_brightness(brightness_level);

                    hw_set_kb_backlight(keyboard_level);
                }
            }
        }
    }, 1000, NULL);


    // Allow low power mode
    set_low_power_mode_flag(true);
    lv_disp_trig_activity(NULL);

}




static lv_obj_t *canvas;
static lv_indev_t *touch_indev;

void touch_panel_init()
{
    uint32_t width = lv_disp_get_hor_res(NULL);
    uint32_t height = lv_disp_get_ver_res(NULL);
#if 1
    lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
    uint32_t buffer_size =    LV_DRAW_BUF_SIZE(width, height, cf);
    uint8_t *buf_draw_buf = (uint8_t*)malloc(buffer_size);
    uint16_t stride_size = LV_DRAW_BUF_STRIDE(width, cf);

    printf("data_size:%u\n", buffer_size);
    printf("stride:%u\n", stride_size);
    printf("cf:%u\n", cf);

    static lv_draw_buf_t draw_buf = {
        .header = {
            .magic = (0x19),
            .cf = (cf),
            .flags = LV_IMAGE_FLAGS_MODIFIABLE,
            .w = (width),
            .h = (height),
            .stride = stride_size,
            .reserved_2 = 0,
        },
        .data_size = buffer_size,
        .data = buf_draw_buf,
        .unaligned_data = buf_draw_buf,
    };

    lv_image_header_t *header = &draw_buf.header;
    lv_draw_buf_init(&draw_buf, header->w, header->h,
                     (lv_color_format_t)header->cf,
                     header->stride,
                     buf_draw_buf,
                     buffer_size);
    lv_draw_buf_set_flag(&draw_buf, LV_IMAGE_FLAGS_MODIFIABLE);

    printf("data_size:%u\n", draw_buf.data_size);
    printf("stride:%u\n", draw_buf.header.stride);
    printf("cf:%u\n", draw_buf.header.cf);

#else
    // /*Create a buffer for the canvas*/
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
#endif

    /*Create a canvas and initialize its palette*/
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_draw_buf(canvas, &draw_buf);
    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);
    lv_obj_center(canvas);


    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            touch_indev = indev;
            break;
        }
        indev = lv_indev_get_next(indev);
    }
    lv_indev_type_t type = lv_indev_get_type(touch_indev);
    if (type != LV_INDEV_TYPE_POINTER) {
        return;
    }

    lv_timer_create([](lv_timer_t*t) {

#undef lv_point_t
        lv_point_t  point;
        lv_indev_state_t state =  lv_indev_get_state(touch_indev);
        if ( state == LV_INDEV_STATE_PRESSED ) {
            lv_indev_get_point(touch_indev, &point);
            printf("%d %d\n", point.x, point.y);

            lv_layer_t layer;
            lv_canvas_init_layer(canvas, &layer);

            lv_draw_arc_dsc_t dsc;
            lv_draw_arc_dsc_init(&dsc);
            dsc.color = lv_palette_main(LV_PALETTE_RED);
            dsc.width = 2;
            dsc.center.x =  point.x;
            dsc.center.y = point.y;
            dsc.width = 10;
            dsc.radius = 6;
            dsc.start_angle = 0;
            dsc.end_angle = 360;
            lv_draw_arc(&layer, &dsc);
            lv_canvas_finish_layer(canvas, &layer);
        }
    }, 30, NULL);

}


























