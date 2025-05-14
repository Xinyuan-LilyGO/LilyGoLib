/**
 * @file      gif.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-05-01
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#if !LV_USE_GIF
#error "lvgl gid decoder library is not enable!"
#endif

LV_IMG_DECLARE(image);

void setup()
{
    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *img = lv_gif_create(lv_screen_active());
    lv_gif_set_src(img, &image);
    lv_obj_center(img);

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

