/**
 * @file      BLEKeyboard.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-25
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_LORA_PAGER

#include <BleKeyboard.h>
BleKeyboard bleKeyboard(USB_PRODUCT);



const char *title =
    "Keyboard example\n"
    "1.Turn on your phone's Bluetooth and search "
    USB_PRODUCT
    "\n"
    "2. Click Connect and the device will act as a keyboard device";

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, title);
    lv_obj_center(label1);

    bleKeyboard.begin();

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
        if (bleKeyboard.isConnected()) {
            bleKeyboard.print(c);
        }
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
    Serial.println("The example only support T-LoRa-Pager"); delay(1000);
}

#endif
