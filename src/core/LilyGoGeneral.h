/**
 * @file      LilyGoGeneral.h
 * @brief     Declares shared low-level helper functions used by LilyGo boards.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-11
 *
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @brief Enable the ESP32 slow crystal clock when supported by the target.
 * @return true if the slow crystal was enabled successfully.
 */
bool esp_enable_slow_crystal();

/**
 * @brief Set FreeRTOS event group bits from an interrupt context.
 * @param xEventGroup Event group handle to update.
 * @param uxBitsToSet Bit mask to set in the event group.
 */
void setGroupBitsFromISR(EventGroupHandle_t xEventGroup,
                         const EventBits_t uxBitsToSet);
