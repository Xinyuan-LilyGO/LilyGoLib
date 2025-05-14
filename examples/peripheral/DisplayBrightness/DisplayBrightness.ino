/**
 * @file      DisplayBrightness.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

static void value_changed_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    int16_t value = lv_slider_get_value(slider);
    instance.setBrightness(value);
}

static void set_to_max(lv_event_t *e)
{
    instance.incrementalBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

static void set_to_min(lv_event_t *e)
{
    instance.decrementBrightness(DEVICE_MIN_BRIGHTNESS_LEVEL);
}

void setup(void)
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    // Create intput device group , only T-LoRa-Pager need.
    lv_group_t *group = lv_group_create();
    lv_set_default_group(group);

    lv_obj_t *btn1 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn1, set_to_max, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(btn1,  LV_PCT(80));
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -10);
    lv_group_add_obj(lv_group_get_default(), btn1);

    lv_obj_t *label;
    label = lv_label_create(btn1);
    lv_label_set_text(label, "Set to Max");
    lv_obj_center(label);

    lv_obj_t *btn2 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn2, set_to_min, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(btn2,  LV_PCT(80));
    lv_obj_align_to(btn2, btn1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_group_add_obj(lv_group_get_default(), btn2);

    label = lv_label_create(btn2);
    lv_label_set_text(label, "Set to Min");
    lv_obj_center(label);

    /*Create a slider in the center of the display*/
    lv_obj_t *slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, LV_PCT(80));
    lv_obj_set_height(slider, LV_PCT(10));
    lv_slider_set_range(slider, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_obj_add_event_cb(slider, value_changed_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align_to(slider, btn2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_group_add_obj(lv_group_get_default(), slider);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

void loop()
{
    // Handle device event
    instance.loop();
    lv_task_handler();
    delay(5);
}
