/**
 * @file      BrightnessController.h
 * @brief     Defines a CRTP helper for synchronous and timer-based brightness fades.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-11-10
 *
 */
#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/**
 * @brief CRTP helper that fades display brightness up or down.
 *
 * @tparam T Device class implementing getBrightness(), setBrightness(), and
 *           getInstance() for asynchronous operation.
 * @tparam MIN_BRIGHTNESS Lowest accepted brightness level.
 * @tparam MAX_BRIGHTNESS Highest accepted brightness level.
 * @tparam DEFAULT_DELAY_MS Default delay between fade steps in milliseconds.
 */
template<typename T, uint8_t MIN_BRIGHTNESS, uint8_t MAX_BRIGHTNESS, uint32_t DEFAULT_DELAY_MS>
class BrightnessController
{
protected:
    /** FreeRTOS timer used by asynchronous brightness transitions. */
    TimerHandle_t timerHandler = nullptr;

public:
    /**
     * @brief Decrease the display brightness to the target level.
     *
     * This function gradually decreases the display brightness to the 'target_level'. The 'delay_ms' parameter
     * specifies the delay between each decrement step, and the 'async' parameter indicates whether the operation
     * should be performed asynchronously.
     *
     * @param target_level The target brightness level to reach.
     * @param delay_ms Delay between each brightness decrement step
     * @param async Whether to perform the operation asynchronously
     */
    void decrementBrightness(uint8_t target_level, uint32_t delay_ms = DEFAULT_DELAY_MS, bool async = false)
    {
        if (target_level < MIN_BRIGHTNESS) target_level = MIN_BRIGHTNESS;
        if (target_level > MAX_BRIGHTNESS) target_level = MAX_BRIGHTNESS;

        if (!async) {
            uint8_t brightness = static_cast<T *>(this)->getBrightness();
            if (target_level >= brightness)
                return;
            for (int i = brightness; i > target_level; i--) {
                static_cast<T *>(this)->setBrightness(i);
                delay(delay_ms);
            }
            static_cast<T * >(this)->setBrightness(target_level);
        } else {
            static uint8_t pvTimerParams;
            pvTimerParams = target_level;
            if (!timerHandler) {
                timerHandler = xTimerCreate("bri", pdMS_TO_TICKS(delay_ms), pdTRUE, &pvTimerParams, [](TimerHandle_t xTimer) {
                    uint8_t *target_level_ptr = (uint8_t *)pvTimerGetTimerID(xTimer);
                    T* inst = T::getInstance();
                    uint8_t brightness = inst->getBrightness();

                    if (brightness > *target_level_ptr) {
                        brightness--;
                        inst->setBrightness(brightness);
                    }

                    if (brightness <= *target_level_ptr) {
                        BrightnessController* controller = static_cast<BrightnessController *>(inst);
                        if (controller->timerHandler == xTimer) {
                            if (xTimerDelete(xTimer, 0) == pdPASS) {
                                controller->timerHandler = NULL;
                            }
                        }
                    }
                });
            }

            if (!timerHandler) {
                return;
            }

            uint8_t current_brightness = static_cast<T *>(this)->getBrightness();
            if (current_brightness <= target_level) {
                return;
            }

            if (xTimerIsTimerActive(timerHandler) == pdTRUE) {
                return;
            }
            xTimerStart(timerHandler, portMAX_DELAY);
        }
    }


    /**
     * @brief Increase the display brightness to the target level.
     *
     * This function gradually increases the display brightness to the 'target_level'. The 'delay_ms' parameter
     * specifies the delay between each increment step, and the 'async' parameter indicates whether the operation
     * should be performed asynchronously.
     *
     * @param target_level The target brightness level to reach.
     * @param delay_ms Delay between each brightness increment step
     * @param async Whether to perform the operation asynchronously
     */
    void incrementalBrightness(uint8_t target_level, uint32_t delay_ms = DEFAULT_DELAY_MS, bool async = false)
    {
        if (target_level < MIN_BRIGHTNESS) target_level = MIN_BRIGHTNESS;
        if (target_level > MAX_BRIGHTNESS) target_level = MAX_BRIGHTNESS;

        if (!async) {
            uint8_t brightness = static_cast<T *>(this)->getBrightness();
            if (target_level <= brightness)
                return;
            for (int i = brightness + 1; i <= target_level; i++) {
                static_cast<T *>(this)->setBrightness(i);
                delay(delay_ms);
            }
        } else {
            static uint8_t pvTimerParams;
            pvTimerParams = target_level;
            if (!timerHandler) {
                timerHandler = xTimerCreate("bri", pdMS_TO_TICKS(delay_ms), pdTRUE, &pvTimerParams, [](TimerHandle_t xTimer) {
                    uint8_t *target_level_ptr = (uint8_t *)pvTimerGetTimerID(xTimer);
                    T* inst = T::getInstance();
                    uint8_t brightness = inst->getBrightness();
                    if (brightness < *target_level_ptr) {
                        brightness++;
                        inst->setBrightness(brightness);
                    }
                    if (brightness >= *target_level_ptr) {
                        BrightnessController* controller = static_cast<BrightnessController *>(inst);
                        if (controller->timerHandler == xTimer) {
                            if (xTimerDelete(xTimer, 0) == pdPASS) {
                                controller->timerHandler = NULL;
                            }
                        }
                    }
                });
            }

            if (!timerHandler) {
                return;
            }

            uint8_t current_brightness = static_cast<T *>(this)->getBrightness();
            if (current_brightness >= target_level) {
                return;
            }

            if (xTimerIsTimerActive(timerHandler) == pdTRUE) {
                return;
            }
            xTimerStart(timerHandler, portMAX_DELAY);
        }
    }
};
