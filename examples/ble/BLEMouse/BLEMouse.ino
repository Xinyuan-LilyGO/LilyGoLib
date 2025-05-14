/**
 * @file      BLEMouse.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-30
 * @note      For devices with touch, use the touch screen to simulate a mouse
 *            For devices with a rotary encoder, use the encoder to simulate the middle scroll wheel
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#include <BleMouse.h>

BleMouse bleMouse;
lv_obj_t *label1 ;
lv_point_t last_point;
const uint32_t DOUBLE_CLICK_TIME_THRESHOLD = 500;
uint32_t lastClickTime = 0;
bool waitingForSecondClick = false;
bool fingerWasLifted = false;

void touch_simulate_mouse();
void encoder_simulate_mouse();

const char *title =
    "BLE Mouse example\n"
    "1.Turn on your phone's Bluetooth and search "
    USB_PRODUCT
    "\n"
    "2. Click Connect and the device will act as a mouse device\n"
#ifdef ARDUINO_T_LORA_PAGER
    "Use a rotary encoder to simulate a mouse middle\n button scroll wheel operation";
#else
    "Use the touch screen to simulate mouse operation\ndouble-click the screen as the left click of the mouse";
#endif

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, title);
    lv_obj_center(label1);

    Serial.println("Starting BLE work!");

    bleMouse.begin();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

void loop()
{
    lv_timer_handler();

    if (bleMouse.isConnected()) {

#ifdef ARDUINO_T_LORA_PAGER
        encoder_simulate_mouse();
#else
        touch_simulate_mouse();
#endif
    }
}

void encoder_simulate_mouse()
{
#ifdef ARDUINO_T_LORA_PAGER
    RotaryMsg_t msg =  instance.getRotary();
    switch (msg.dir) {
    case ROTARY_DIR_UP:
        bleMouse.move(0, 0, 1);
        break;
    case ROTARY_DIR_DOWN:
        bleMouse.move(0, 0, -1);
        break;
    default:
        break;
    }
    if (msg.centerBtnPressed) {
        bleMouse.click(MOUSE_LEFT);
    }
#endif
}


void touch_simulate_mouse()
{
#ifndef ARDUINO_T_LORA_PAGER
    lv_point_t point;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_state_t  state = lv_indev_get_state(indev);
        if (state == LV_INDEV_STATE_PRESSED ) {
            lv_indev_get_point(indev, &point);
            lv_label_set_text_fmt(label1, "Rotation %d \n X:%d Y:%d",
                                  instance.getRotation(),
                                  point.x, point.y);
            int offset_step = 10;
            if (point.x > last_point.x + 5) {
                bleMouse.move(offset_step, 0);
            } else if (point.x < last_point.x - 5) {
                bleMouse.move(-offset_step, 0);
            } else if (point.y > last_point.y + 5) {
                bleMouse.move(0, offset_step);
            } else if (point.y < last_point.y - 5) {
                bleMouse.move(0, -offset_step);
            }
            last_point = point;

            if (fingerWasLifted) {
                uint32_t currentTime = millis();
                if (!waitingForSecondClick) {
                    lastClickTime = currentTime;
                    waitingForSecondClick = true;
                } else {
                    uint32_t duration = currentTime - lastClickTime;
                    if (duration <= DOUBLE_CLICK_TIME_THRESHOLD) {
                        bleMouse.click(MOUSE_LEFT);
                    }
                    waitingForSecondClick = false;
                }
                fingerWasLifted = false;
            }
        } else {
            fingerWasLifted = true;
            if (waitingForSecondClick) {
                uint32_t currentTime = millis();
                uint32_t duration = currentTime - lastClickTime;
                if (duration > DOUBLE_CLICK_TIME_THRESHOLD) {
                    waitingForSecondClick = false;
                }
            }
        }
    }
#endif /*ARDUINO_T_LORA_PAGER*/
}