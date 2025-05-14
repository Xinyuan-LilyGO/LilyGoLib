/**
 * @file      WakeUpFromBootButton.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-05-03
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA)

RTC_DATA_ATTR int bootCount = 0;

bool  power_button_clicked = false;
lv_obj_t *label1;


const char *get_wakeup_reason()
{
    switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0 : return ("Wakeup caused by external signal using RTC_IO");
    case ESP_SLEEP_WAKEUP_EXT1 : return ("Wakeup caused by external signal using RTC_CNTL");
    case ESP_SLEEP_WAKEUP_TIMER : return ("Wakeup caused by timer");
    case ESP_SLEEP_WAKEUP_TOUCHPAD : return ("Wakeup caused by touchpad");
    case ESP_SLEEP_WAKEUP_ULP : return ("Wakeup caused by ULP program");
    default : return ("Wakeup was not caused");
    }
}

void setup()
{

    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(label, LV_PCT(90));
    lv_label_set_text_fmt(label, "Boot counter: %d", ++bootCount);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -64);

    label1 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(label1, LV_PCT(90));
    lv_label_set_text(label1, "Waiting to press the crown to go to sleep...");
    lv_obj_center(label1);

    lv_obj_t *label2 = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(label2, LV_PCT(90));
    lv_label_set_text(label2, get_wakeup_reason());
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, -32);


    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

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

    for (int i = 5; i > 0; i--) {
        lv_label_set_text_fmt(label1, "Go to sleep after %d seconds", i);
        lv_task_handler();
        delay(1000);
    }

    lv_label_set_text(label1, "Sleep now ...");
    lv_task_handler();
    delay(1000);

    /*
    * Set to wake up by boot button
    * T-LoRa-Pager deep sleep is about 530 uA
    * T-Watch-S3-Plus deep sleep is about 840 uA
    * */

    // Only T-LoRa-Pager
    // instance.sleep((WakeupSource_t)(WAKEUP_SRC_BOOT_BUTTON | WAKEUP_SRC_ROTARY_BUTTON));

    instance.sleep(WAKEUP_SRC_BOOT_BUTTON);


    Serial.println("This will never be printed");
}

void loop()
{

}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-Watch-S3 or T-Watch-Ultra"); delay(1000);
}

#endif

