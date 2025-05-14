/**
 * @file      ui_sensor.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

LV_IMG_DECLARE(img_dog);

typedef struct {
    lv_obj_t *roll;
    lv_obj_t *pitch;
    lv_obj_t *heading;
} imu_label_t;

static imu_label_t label_imu;
static lv_obj_t *menu = NULL;
static lv_timer_t *timer = NULL;
static lv_obj_t *quit_btn = NULL;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_btn_is_root(menu, obj)) {

        hw_unregister_imu_process();

        if (timer) {
            lv_timer_del(timer); timer = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);

        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }

        menu_show();
    }
}

void ui_sensor_enter(lv_obj_t *parent)
{
    hw_register_imu_process();

    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, NULL);

#if  defined(USING_BHI260_SENSOR)
    lv_obj_t *arc = lv_arc_create(main_page);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);

    lv_obj_t *led1  = lv_img_create(arc);
    lv_obj_align(led1, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(led1, &img_dog);

    label_imu.roll = lv_label_create(main_page);
    lv_label_set_text_fmt(label_imu.roll, "roll:0");

    label_imu.pitch = lv_label_create(main_page);
    lv_label_set_text_fmt(label_imu.pitch, "pitch:0");

    label_imu.heading = lv_label_create(main_page);
    lv_label_set_text_fmt(label_imu.heading, "heading:0");

    timer = lv_timer_create([](lv_timer_t *t) {
        lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
        imu_params_t param;
        hw_get_imu_params(param);
        lv_label_set_text_fmt(label_imu.roll, "roll:%.2f", param.roll);
        lv_label_set_text_fmt(label_imu.pitch, "pitch:%.2f", param.pitch);
        lv_label_set_text_fmt(label_imu.heading, "heading:%.2f", param.heading);

        float posY = param.roll * 2;
        float posX = param.pitch;
        lv_obj_t *parent = lv_obj_get_parent(obj);
        lv_coord_t width = lv_obj_get_width(parent);
        lv_coord_t height = lv_obj_get_height(parent);
#ifdef ARDUINO
        posX = constrain(posX, -40, 40);
        posY = constrain(posY, -40, 40);
        Serial.printf("w:%d x:%.2f -- h:%d y:%.2f\n", width, posX, height, posY);
#endif
        lv_obj_align(obj, LV_ALIGN_CENTER, posX, posY);

    }, 100, led1);
#endif // USING_BHI260_SENSOR


#ifdef USING_BMA423_SENSOR

    lv_obj_t *cont = lv_obj_create(main_page);
    lv_obj_set_size(cont, 120, 120);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t *led1  = lv_led_create(cont);
    lv_led_set_brightness(led1, 150);
    lv_led_set_color(led1, lv_palette_main(LV_PALETTE_RED));
    lv_obj_align(led1, LV_ALIGN_TOP_LEFT, 5, 5);

    timer = lv_timer_create([](lv_timer_t *t) {
        lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
        imu_params_t param;
        hw_get_imu_params(param);
        if (param.orientation > 3) {
            return;
        }
        lv_align_t align[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT};

        lv_obj_align(obj, align[param.orientation], 0, 0);

    }, 1000, led1);

#endif // USING_BMA423_SENSOR

    lv_menu_set_page(menu, main_page);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, lv_event_get_target_obj(e));
    }, NULL);
#endif

}

void ui_sensor_exit(lv_obj_t *parent)
{

}

app_t ui_sensor_main = {
    .setup_func_cb = ui_sensor_enter,
    .exit_func_cb = ui_sensor_exit,
    .user_data = nullptr,
};


