/**
 * @file      ui_factory.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

static uint8_t *image_data = NULL;
static lv_timer_t *timer = NULL;
static uint8_t nextColors = 0;

static void generate_gray_gradient(uint8_t *image_data, uint16_t width, uint16_t height)
{
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t gray_value = (uint8_t)((float)x / width * 255);
            image_data[y * width + x] = gray_value;
        }
    }
}


static void display_gray_image(lv_obj_t *parent)
{
    uint16_t width = lv_disp_get_hor_res(NULL);
    uint16_t height = lv_disp_get_ver_res(NULL);

    image_data = (uint8_t *)lv_mem_alloc(width * height);
    if (image_data == NULL) {
        printf("memory failed!\n");
        return;
    }
    generate_gray_gradient(image_data, width, height);
    static lv_img_dsc_t gray_image;
    gray_image.header.cf = LV_IMG_CF_ALPHA_8BIT;
    gray_image.header.w = width;
    gray_image.header.h = height;
    gray_image.data_size = width * height;
    gray_image.data = image_data;

    lv_obj_set_style_bg_img_src(parent, &gray_image, LV_PART_MAIN);
}

static void factory_timer_callback(lv_timer_t *t)
{
    lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_clean(obj);
    switch (nextColors) {
    case 0:
        display_gray_image(obj);
        break;
    case 1:
        lv_obj_set_style_bg_img_src(obj, NULL, LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        break;
    case 2:
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 255, 0), LV_PART_MAIN);
        break;
    case 3:
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 0, 255), LV_PART_MAIN);
        break;
    case 4:
        lv_obj_set_style_border_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 255, 255), LV_PART_MAIN);
        break;
    case 5:
        lv_obj_set_style_border_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 0, 0), LV_PART_MAIN);
        break;
    case 6:
        if (timer) {
            if (image_data) {
                lv_mem_free(image_data);
            }
            lv_obj_del(obj);
            lv_timer_del(timer);
            timer = NULL;
            nextColors = 0;
            enable_input_devices();
            menu_show();
            set_low_power_mode_flag(true);
        }
        return;
    case 7:
    default:
        break;
    }
    nextColors++;
}

static void _event_cb(lv_event_t*e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (timer) {
        if (image_data) {
            lv_mem_free(image_data);
        }
        lv_obj_del(obj);
        lv_timer_del(timer);
        timer = NULL;
        nextColors = 0;
        enable_input_devices();
        menu_show();
        set_low_power_mode_flag(true);
    }
}

void ui_factory_enter(lv_obj_t *parent)
{
    if (timer) {
        return;
    }
    disable_input_devices();
    set_low_power_mode_flag(false);

    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_center(obj);
    timer = lv_timer_create(factory_timer_callback, 3000, obj);
    lv_timer_ready(timer);
    lv_group_add_obj(lv_group_get_default(), obj);
    lv_obj_add_event(obj, _event_cb, LV_EVENT_CLICKED, NULL);
}

void ui_factory_exit(lv_obj_t *parent)
{
}

app_t ui_factory_main = {
    .setup_func_cb = ui_factory_enter,
    .exit_func_cb = ui_factory_exit,
    .user_data = nullptr,
};

