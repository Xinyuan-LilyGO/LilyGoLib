/**
 * @file      GPSFactory.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-06-16
 * @note      The T-WATCH-S3 instance does not have GPS function and requires an external GPS module to use
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#define SerialGPS  Serial1
lv_obj_t *label;


void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, "GPS Factory example");
    lv_obj_set_width(label, lv_pct(90));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_center(label);
    lv_task_handler();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    uint32_t baud[] = {38400, 57600, 115200, 9600};
    bool result = 0;
    int retry = 4;
    int i = 0;
    do {
        SerialGPS.updateBaudRate(baud[i]);
        Serial.printf("Try use %u baud\n", baud[i]);
        delay(10);
        result = instance.gps.init(&SerialGPS);
        i++;
        if (i >= sizeof(baud) / sizeof(baud[0])) {
            retry--;
            i = 0;
        }
        if (!retry)
            break;
    } while (!result);

    if (result) {
        Serial.println("UBlox GPS init succeeded");
    } else {
        Serial.println("Warning: Failed to find UBlox GPS Module");
        lv_label_set_text(label, "Failed to find UBlox GPS Module");
        return;
    }
    if (instance.gps.factory()) {
        Serial.println("UBlox GPS factory succeeded");
        lv_label_set_text(label, "UBlox GPS factory succeeded");
    } else {
        Serial.println("Warning: Failed to factory UBlox GPS");
        lv_label_set_text(label, "Failed to factory UBlox GPS");
        return;
    }
}

void loop()
{
    while (SerialGPS.available()) {
        Serial.write(SerialGPS.read());
    }
    while (Serial.available()) {
        SerialGPS.write(Serial.read());
    }
    lv_task_handler();
    delay(5);
}
