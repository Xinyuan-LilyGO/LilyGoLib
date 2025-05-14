/**
 * @file      LightSleep.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-17
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

bool  power_button_clicked = false;
RTC_DATA_ATTR int sleepCount = 0;
lv_obj_t *label ;
lv_obj_t *slider;

#if defined(ARDUINO_T_LORA_PAGER)
void waitButtonPressed()
{
    const uint8_t boot_pin = 0;
    pinMode(boot_pin, INPUT);
    // Waiting to press the crown to go to sleep
    while (digitalRead(boot_pin) == HIGH) {
        // Handle device event
        instance.loop();
        // Handle lvgl event
        lv_task_handler();
        delay(5);
    }
}
#else
void waitButtonPressed()
{
    while (!power_button_clicked) {
        // Handle device event
        instance.loop();
        // Handle lvgl event
        lv_task_handler();
        delay(5);
    }
    power_button_clicked = false;
}
#endif

void event_cb(lv_event_t * e)
{
    int32_t value =  lv_bar_get_value(slider);
    instance.setBrightness(value);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);


    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

#ifdef USING_PMU_MANAGE
    instance.onEvent([](DeviceEvent_t event, void * user_data) {
        power_button_clicked = true;
    }, PMU_EVENT_KEY_CLICKED, NULL);
#endif

    label = lv_label_create(lv_scr_act());
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider, lv_pct(80), 20);
    lv_slider_set_range(slider, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_obj_align_to(slider, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_add_event_cb(slider, event_cb, LV_EVENT_VALUE_CHANGED, NULL);

}

void loop()
{
    lv_label_set_text_fmt(label, "Sleep counter: %d", ++sleepCount);

    // Waiting to press the button to go to sleep
    waitButtonPressed();

    instance.decrementBrightness(0);

    /*
    * T-Watch-S3 light sleep (TouchPanel + PowerButton + BootButton Wakeup ) about ~2.38mA
    * T-Watch-S3-Ultra light sleep (TouchPanel + PowerButton + BootButton Wakeup ) about ~4.6mA
    */
    // instance.lightSleep();

    /*
    * T-Watch-S3 does not have a touch reset pin connected, so if you set the touch screen to sleep, the touch will not work.
    * T-Watch-S3-Ultra light sleep (PowerButton + BootButton Wakeup ) about ~2.1mA
    */
    // instance.lightSleep((WakeupSource_t)(WAKEUP_SRC_POWER_KEY | WAKEUP_SRC_BOOT_BUTTON));

    /*
    * T-LoRa-Pager light-sleep about ~2.26mA
    * LoRa Sleep other peripherals power off
    * */
    instance.lightSleep((WakeupSource_t)(WAKEUP_SRC_ROTARY_BUTTON | WAKEUP_SRC_BOOT_BUTTON));

    instance.incrementalBrightness(255);

}
