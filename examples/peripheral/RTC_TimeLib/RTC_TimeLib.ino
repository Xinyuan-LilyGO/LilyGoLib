/**
 * @file      RTC_TimeLib.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2023-04-23
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <time.h>


uint32_t lastMillis;
char buf[64];
lv_obj_t *label1;
lv_obj_t *label2;
lv_obj_t *label3;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_center(cont);

    label1 = lv_label_create(cont);
    lv_obj_set_width(label1, LV_PCT(90));  /*Set smaller width to make the lines wrap*/
    label2 = lv_label_create(cont);
    lv_obj_set_width(label2, LV_PCT(90));  /*Set smaller width to make the lines wrap*/
    label3 = lv_label_create(cont);
    lv_obj_set_width(label3, LV_PCT(90));  /*Set smaller width to make the lines wrap*/

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

}


void loop()
{
    if (millis() - lastMillis > 1000) {

        lastMillis = millis();

        struct tm timeinfo;
        // Get the time C library structure
        instance.rtc.getDateTime(&timeinfo);

        // Format the output using the strftime function
        // For more formats, please refer to :
        // https://man7.org/linux/man-pages/man3/strftime.3.html

        size_t written = strftime(buf, 64, "%A, %B %d %Y %H:%M:%S", &timeinfo);

        if (written != 0) {
            lv_label_set_text(label1, buf);
            Serial.println(buf);
        }

        written = strftime(buf, 64, "%b %d %Y %H:%M:%S", &timeinfo);
        if (written != 0) {
            lv_label_set_text(label2, buf);
            Serial.println(buf);
        }


        written = strftime(buf, 64, "%A, %d. %B %Y %I:%M:%S %p", &timeinfo);
        if (written != 0) {
            lv_label_set_text(label3, buf);
            Serial.println(buf);
        }
    }
    lv_task_handler();
    delay(5);
}



