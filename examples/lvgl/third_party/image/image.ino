/**
 * @file      ImageDecoder.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-05-01
 * @Note      You need to upload the files from the data file to the FFat
 *            file system before using this example
 * @note      platfromio user use <pio run -t uploadfs> ,
 * *  Note that you need to place the sample data folder in the same level directory as <platformio. ini>
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <FFat.h>

#if !LV_USE_LODEPNG || !LV_USE_BMP || !LV_USE_TJPGD
#error "lvgl png , bmp , sjpg decoder library is not enable!"
#endif

const char *filename[] = {
    "logo.bmp", "logoColor1.png", "logoColor2.png", "ttgo.jpg",
    "product0.png", "product1.png", "product2.png", "product3.png",
    "product4.png", "product5.png"
};

void setup(void)
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    String file =  String("/") + filename[0];
    if (!FFat.exists(file)) {
        while (1) {
            Serial.println("You need to upload the files from the data file to the FFat file system before using this example");
            delay(1000);
        }
    }

    lv_obj_t *img =  lv_img_create(lv_scr_act());
    lv_obj_center(img);

    lv_timer_create([](lv_timer_t *t) {
        static int i = 0;
        String fspath = LV_FS_POSIX_LETTER + String(":/") + filename[i];
        lv_img_set_src((lv_obj_t *)t->user_data, fspath.c_str());
        i++;
        i %= sizeof(filename) / sizeof(filename[0]);
    }, 1000, img);


    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

}

void loop()
{
    lv_task_handler();
    delay(5);
}

