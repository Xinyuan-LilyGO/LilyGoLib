/**
 * @file      DRV2605_Basic.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

lv_obj_t *slider;

void value_changed_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    int16_t value = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label, "Effect:%d", value);
}

void vibration_feedback_effect_handler(lv_timer_t *time)
{
    // get the effect value
    int16_t value = lv_slider_get_value(slider);
    // set the effect to play
    instance.drv.setWaveform(0, value);  // play effect
    // play the effect!
    instance.drv.run();
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    /*Create a slider in the center of the display*/
    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, LV_PCT(80));
    lv_obj_set_height(slider, LV_PCT(10));
    lv_slider_set_range(slider, 0, 113);
    lv_obj_center(slider);

    // Create intput device group , only T-LoRa-Pager need.
    lv_group_t *group = lv_group_create();
    lv_set_default_group(group);
    lv_group_add_obj(lv_group_get_default(), slider);

    /*Create a label below the slider*/
    lv_obj_t *slider_label = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_label, "Effect:0");
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_add_event_cb(slider, value_changed_event_cb, LV_EVENT_VALUE_CHANGED, slider_label);

    lv_timer_create(vibration_feedback_effect_handler, 1000, NULL);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    lv_task_handler();
    delay(5);
}



