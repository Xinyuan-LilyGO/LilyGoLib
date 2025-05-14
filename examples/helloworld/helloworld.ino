/**
 * @file      helloworld.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-28
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;
        /*Get the first child of the button which is the label and change its text*/
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Button: %d", cnt);
        Serial.printf("Button :%d\n", cnt);
    }
}

void setup()
{
    // To enable serial print output, you need to enable USB output settings
    // Arduino IDE-> Tools -> USB CDC On Boot -> Enabled
    Serial.begin(115200);

    // Initialize the LilyGoLib instance
    instance.begin();

    // Call lvgl initialization
    beginLvglHelper(instance);

    lv_obj_t *label = lv_label_create(lv_screen_active());        /*Add a label the current screen*/
    lv_label_set_text(label, "Hello World");                 /*Set label text*/
    lv_obj_center(label);                                   /*Set center alignment*/

    lv_obj_t *btn = lv_button_create(lv_screen_active());            /*Add a button the current screen*/
    lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);  /*Assign a callback to the button*/
    lv_obj_align_to(btn, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 0); /*Set the label to it and align it in the center below the label*/

    lv_obj_t *btn_label = lv_label_create(btn);           /*Add a label to the button*/
    lv_label_set_text(btn_label, "Button");               /*Set the labels text*/
    lv_obj_center(btn_label);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    // lvgl task processing should be placed in the loop function
    lv_timer_handler();
    delay(2);
}

















