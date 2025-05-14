/**
 * @file      LilyGoLib.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-09
 *
 */

#pragma once

// #define USING_SPIFFS
#define USING_FATFS
// #define USING_SDCARD

#include <esp_arduino_version.h>

#if (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,3,0)) && !defined(PLATFORMIO)
#error "Please manually update and install Arduino Core ESP32 to the latest version. The version must be greater than or equal to V3.3.0-alpha1. For how to update, please refer to https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html#installing-using-arduino-ide"
#endif

#if !defined(ARDUINO_T_WATCH_S3_ULTRA) && !defined(ARDUINO_T_WATCH_S3) && !defined(ARDUINO_T_LORA_PAGER)
#error "Please update arduino-esp32-core to version 3.3.0 or above, select the correct board and then compile"
#endif

#if   defined(CONFIG_IDF_TARGET_ESP32S3)

#if ARDUINO_USB_CDC_ON_BOOT != 1
#warning "If you need to monitor printed data, be sure to set USB CDC On boot to ENABLE, otherwise you will not see any data in the serial monitor"
#endif

#elif defined(CONFIG_IDF_TARGET_ESP32)

#error "This library does not support ESP32 version variants"

#endif //CONFIG_IDF_TARGET_ESP32S3

#include "LilyGoWatchS3.h"
#include "LilyGoWatchUltra.h"
#include "LilyGo_LoRa_Pager.h"
