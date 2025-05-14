/**
 * @file      DisplayRotation.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

lv_obj_t *label1;
bool  power_button_clicked = false;
uint8_t rotation = 1;

void setRotation()
{
    lv_display_rotation_t  r = (lv_display_rotation_t)rotation++;

#ifdef ARDUINO_T_WATCH_S3_ULTRA
    switch (r) {
    case LV_DISPLAY_ROTATION_0:
    case LV_DISPLAY_ROTATION_180:
        break;
    default:
        // T-Watch-S3-Ultra can't set rotation 90° or 270°
        r = (lv_display_rotation_t)rotation++;
        break;
    }
#endif /*ARDUINO_T_WATCH_S3_ULTRA*/

    rotation %= 4;
    lv_display_set_rotation(lv_display_get_default(), r);
}

static void event_handler(lv_event_t *e)
{
    setRotation();
}

void setup(void)
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);     /*Break the long lines*/
    lv_label_set_text(label1, "0000");
    lv_obj_set_width(label1, 150);  /*Set smaller width to make the lines wrap*/
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn1 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(btn1,  LV_PCT(80));
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, 45);

    lv_obj_t *label;
    label = lv_label_create(btn1);
    lv_label_set_text(label, "Set Rotation");
    lv_obj_center(label);

    instance.onEvent([](DeviceEvent_t event, void *user_data) {
        setRotation();
    }, PMU_EVENT_KEY_CLICKED, NULL);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

#ifdef ARDUINO_T_LORA_PAGER
    // Create intput device group , only T-LoRa-Pager need.
    lv_group_t *group = lv_group_create();
    lv_set_default_group(group);
    lv_group_add_obj(lv_group_get_default(), btn1);
#endif

}

void loop()
{
    // Handle device event
    instance.loop();

#ifndef ARDUINO_T_LORA_PAGER
    lv_point_t point;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev) {
        if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            lv_indev_get_point(indev, &point);
            lv_label_set_text_fmt(label1, "Rotation %d \n X:%d Y:%d",
                                  instance.getRotation(),
                                  point.x, point.y);
        }
    }
#endif /*ARDUINO_T_LORA_PAGER*/

    lv_task_handler();
    delay(5);
}
