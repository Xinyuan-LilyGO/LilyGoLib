/**
 * @file      BMA423_Temperature.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_WATCH_S3

uint32_t interval;
lv_obj_t *label1;

void setup()
{
    // To monitor data, the USB_CDC_BOOT must be set to enable in the Arduino IDE
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    if (interval < millis()) {
        interval = millis() + 1000;
        //Obtain the temperature value on the accelerometer
        float accel_celsius = instance.sensor.getTemperature(SensorBMA423::TEMP_DEG);
        lv_label_set_text_fmt(label1, "Accel: %.2f°C", accel_celsius);
    }
    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-Watch-S3"); delay(1000);
}

#endif


