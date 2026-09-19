/**
 * @file      LilyGoGeneral.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-11
 *
 */
#include "LilyGoLog.h"
#include <Arduino.h>
#include "LilyGoGeneral.h"
#include "soc/rtc.h"

static uint32_t calibrate_one(
#if (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(4,0,0))
    rtc_cal_sel_t
#else
    soc_clk_freq_calculation_src_t
#endif
    cal_clk)
{
    const uint32_t cal_count = 1000;
    uint32_t cali_val;
    for (int i = 0; i < 5; ++i) {
        cali_val = rtc_clk_cal(cal_clk, cal_count);
    }
    return cali_val;
}


bool esp_enable_slow_crystal()
{
    rtc_clk_32k_enable(true);

    calibrate_one(RTC_CAL_RTC_MUX);
    uint32_t cal_32k = calibrate_one(RTC_CAL_32K_XTAL);

    if (cal_32k == 0) {
        LILYGO_LOG_E("32K XTAL OSC has not started up");
        return false;
    } else {
        rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
        LILYGO_LOG_D("Switching RTC Source to 32.768Khz succeeded, using 32K XTAL");
        calibrate_one(RTC_CAL_RTC_MUX);
        calibrate_one(RTC_CAL_32K_XTAL);
    }
    calibrate_one(RTC_CAL_RTC_MUX);
    calibrate_one(RTC_CAL_32K_XTAL);
    if (rtc_clk_slow_freq_get() != RTC_SLOW_FREQ_32K_XTAL) {
        LILYGO_LOG_E("Failed to switch 32K XTAL RTC source to 32.768Khz !!! ");
        return false;
    }
    return true;
}

void setGroupBitsFromISR(EventGroupHandle_t xEventGroup,
                         const EventBits_t uxBitsToSet)
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(xEventGroup, uxBitsToSet, &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}
