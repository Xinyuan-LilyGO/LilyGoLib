/**
 * @file      main.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 */

#ifndef ARDUINO
#include <stdio.h>
#include "lvgl.h"
#include "app_hal.h"
#include "demos/lv_demos.h"
#include "examples/lv_examples.h"

#include "hal_interface.h"

extern void setupGui();
extern void hw_init();

extern "C" int main(void)
{
    lv_init();

    hal_setup();
    printf("hello lvgl\n");
    //****************** */
    hw_init();
    setupGui();


    //****************** */
    hal_loop();
}
#endif
