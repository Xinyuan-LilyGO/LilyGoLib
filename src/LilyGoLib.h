/**
 * @file      LilyGoLib.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-09
 *
 */

#pragma once

#include "LilyGoLog.h"

// #define USING_SPIFFS
#define USING_FATFS
// #define USING_SDCARD

#include <esp_arduino_version.h>

#if (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,3,0)) && !defined(PLATFORMIO)
#error "Please manually update and install Arduino Core ESP32 to the latest version. The version must be greater than or equal to V3.3.0-alpha1. For how to update, please refer to https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html#installing-using-arduino-ide"
#endif

#if !defined(ARDUINO_T_WATCH_S3_ULTRA) && !defined(ARDUINO_T_WATCH_S3) && !defined(ARDUINO_T_LORA_PAGER) && !defined(ARDUINO_TWATCH_BASE) && !defined(ARDUINO_T_DECK) && !defined(ARDUINO_T_DECK_V2) && !defined(ARDUINO_TWATCH_2020_V3)
#error "Please update arduino-esp32-core to version 3.3.0 or above, select the correct board and then compile"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#if ARDUINO_USB_CDC_ON_BOOT != 1
#pragma message("WARNING: If you need to monitor printed data, be sure to set USB CDC On boot to ENABLE, otherwise you will not see any data in the serial monitor")
#endif

#endif //CONFIG_IDF_TARGET_ESP32S3

// LilyGo 2019 smartwatch is based on ESP32.
#include "LilyGoWatch.h"
// LilyGo 2020 v3 smartwatch is based on ESP32.
#include "LilyGoWatchV3.h"
// LilyGo 2022 smartwatch is based on the ESP32S3 + OPI
#include "LilyGoWatchS3.h"
// LilyGo 2025 smartwatch is based on ESP32S3 + QSPI RAM.
#include "LilyGoWatchUltra.h"
// LilyGo 2025 Keyboard Pager is based on ESP32S3 + QSPI RAM.
#include "LilyGo_LoRa_Pager.h"
#include "LilyGoLib_Version.h"
#include "usb/USB_Service.h"

#ifdef ARDUINO_T_DECK_V2
#include "LilyGo_T_Deck_V2.h"
#endif

#ifdef ARDUINO_T_DECK
#include "LilyGo_T_Deck.h"
#endif

#if defined(ARDUINO) && \
    (defined(USING_ST25R3916) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA))
#include "nfc/LilyGoNfcService.h"
#endif
