/**
 * @file      Keyboard.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-10
 * 
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_LORA_PAGER

lv_obj_t *label1;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label1, " Keyboard example");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

}

void loop()
{
    char c;
    int state = instance.kb.getKey(&c);
    if (state == KB_PRESSED) {
        switch (c) {
        case 0x08:
            lv_label_set_text_fmt(label1, "<-Back");
            break;
        case 0x0A:
            lv_label_set_text_fmt(label1, "<-Enter");
            break;
        case 0x20:
            lv_label_set_text_fmt(label1, "<-Space");
            break;
        default:
            lv_label_set_text_fmt(label1, "Pressed : %c", c);
            break;
        }
        Serial.printf("Char:%c HEX:%X\n", c, c);
    }
    lv_timer_handler();
}


#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-LoRa-Pager"); delay(1000);
}

#endif