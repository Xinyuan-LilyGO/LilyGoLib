/**
 * @file      ui_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#ifdef ARDUINO
#include <Arduino.h>
#include <LilyGoLib.h>
#include <WiFi.h>
#include <esp_mac.h>
#else
#define RTC_DATA_ATTR
#endif
#include <lvgl.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <time.h>
#include <string.h>
#include "hal_interface.h"

using namespace std;

#define DEFAULT_OPA          100

typedef void (*app_func_t)(lv_obj_t *parent);

typedef struct {
    app_func_t setup_func_cb;
    app_func_t exit_func_cb;
    void *user_data;
} app_t;


enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
};
typedef uint8_t lv_menu_builder_variant_t;

#define MSG_MENU_NAME_CHANGED    100
#define MSG_LABEL_PARAM_CHANGE_1 200
#define MSG_LABEL_PARAM_CHANGE_2 201
#define MSG_TITLE_NAME_CHANGE    203
#define MSG_BLE_SEND_DATA_1      204
#define MSG_BLE_SEND_DATA_2      205
#define MSG_MUSIC_TIME_ID        300
#define MSG_MUSIC_TIME_END_ID    301
#define MSG_FFT_ID               400

extern lv_obj_t *main_screen;

lv_obj_t *ui_create_option(lv_obj_t *parent, const char *title, const char *symbol_txt, lv_obj_t *(*widget_create)(lv_obj_t *parent), lv_event_cb_t btn_event_cb);
lv_obj_t *create_text(lv_obj_t *parent, const char *icon, const char *txt,
                      lv_menu_builder_variant_t builder_variant);
lv_obj_t *create_slider(lv_obj_t *parent, const char *icon, const char *txt, int32_t min, int32_t max,
                        int32_t val, lv_event_cb_t cb, lv_event_code_t filter);
lv_obj_t *create_switch(lv_obj_t *parent, const char *icon, const char *txt, bool chk, lv_event_cb_t cb);
lv_obj_t *create_button(lv_obj_t *parent, const char *icon, const char *txt, lv_event_cb_t cb);
lv_obj_t *create_label(lv_obj_t *parent, const char *icon, const char *txt, const char *default_text);
lv_obj_t *create_dropdown(lv_obj_t *parent, const char *icon, const char *txt, const char *options, uint8_t default_sel, lv_event_cb_t cb);
lv_obj_t *create_msgbox(lv_obj_t *parent, const char *title_txt,
                        const char *msg_txt, const char **btns,
                        lv_event_cb_t btns_event_cb, void *user_data);
void destroy_msgbox(lv_obj_t *msgbox);

lv_indev_t *lv_get_encoder_indev();
lv_indev_t *lv_get_keyboard_indev();
void menu_show();
void menu_hidden();
void set_default_group(lv_group_t *group);

lv_obj_t *ui_create_process_bar(lv_obj_t *parent, const char *title);

void theme_init();

void disable_input_devices();
void enable_input_devices();

void set_low_power_mode_flag(bool enable);

void disable_keyboard();
void enable_keyboard();

lv_obj_t *create_floating_button(lv_event_cb_t event_cb, void* user_data);
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb);
lv_obj_t *create_radius_button(lv_obj_t *parent, const void *image, lv_event_cb_t event_cb, void* user_data);

#if LVGL_VERSION_MAJOR == 9
#define LV_MENU_ROOT_BACK_BTN_ENABLED   LV_MENU_ROOT_BACK_BUTTON_ENABLED
#define lv_menu_back_btn_is_root        lv_menu_back_button_is_root
#define lv_menu_set_mode_root_back_btn  lv_menu_set_mode_root_back_button
#define lv_mem_alloc                    lv_malloc
#define lv_mem_free                     lv_free
#define LV_IMG_CF_ALPHA_8BIT            LV_COLOR_FORMAT_L8
#define lv_point_t                      lv_point_precise_t
#else
#define lv_timer_get_user_data(x)       (x->user_data)
#define lv_indev_get_type(x)            (x->driver->type)
#endif

#if LVGL_VERSION_MAJOR == 8

#endif

