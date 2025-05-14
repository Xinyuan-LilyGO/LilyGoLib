/**
 * @file      LilyGoTypedef.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-19
 *
 */

#pragma once

#include <Arduino.h>

/* Hardware presence mask */
#define HW_RADIO_ONLINE             (_BV(0))
#define HW_TOUCH_ONLINE             (_BV(1))
#define HW_DRV_ONLINE               (_BV(2))
#define HW_PMU_ONLINE               (_BV(3))
#define HW_RTC_ONLINE               (_BV(4))
#define HW_PSRAM_ONLINE             (_BV(5))
#define HW_GPS_ONLINE               (_BV(6))
#define HW_SD_ONLINE                (_BV(7))
#define HW_NFC_ONLINE               (_BV(8))
#define HW_SENSOR_ONLINE            (_BV(9))
#define HW_KEYBOARD_ONLINE          (_BV(10))
#define HW_GAUGE_ONLINE             (_BV(11))
#define HW_EXPAND_ONLINE            (_BV(12))
#define HW_CODEC_ONLINE             (_BV(13))
#define HW_NRF24_ONLINE             (_BV(14))


/* Selectively disable some initialisation */
#define NO_HW_RTC                   (_BV(0))
#define NO_HW_I2C_SCAN              (_BV(1))
#define NO_SCAN_I2C_DEV             (_BV(2))
// #define NO_HW_TFT                (_BV(1))
#define NO_HW_TOUCH                 (_BV(12))
#define NO_HW_SENSOR                (_BV(3))
#define NO_HW_NFC                   (_BV(4))
#define NO_HW_DRV                   (_BV(5))
#define NO_HW_GPS                   (_BV(6))
#define NO_HW_SD                    (_BV(7))
#define NO_HW_MIC                   (_BV(8))
#define NO_INIT_DELAY               (_BV(9))
#define NO_HW_LORA                  (_BV(10))
#define NO_HW_KEYBOARD              (_BV(11))
#define NO_INIT_FATFS               (_BV(12))

/* Hardware interrupt mask */
#define HW_IRQ_TOUCHPAD             (_BV(0))
#define HW_IRQ_RTC                  (_BV(1))
#define HW_IRQ_POWER                (_BV(2))
#define HW_IRQ_SENSOR               (_BV(3))


typedef enum PowerCtrlChannel {
    // Display and touch power supply
    POWER_DISPLAY,
    // Display backlight power supply
    POWER_DISPLAY_BACKLIGHT,
    // LoRa power supply
    POWER_RADIO,
    // Touch feedback driver power supply
    POWER_HAPTIC_DRIVER,
    // Global Positioning GPS power supply
    POWER_GPS,
    // NFC power supply
    POWER_NFC,
    // SD Card power supply
    POWER_SD_CARD,
    // Audio Power Amplifier Power Supply
    POWER_SPEAK,
    // Sensor power supply
    POWER_SENSOR,
    // Keyboard power supply
    POWER_KEYBOARD,
    // Extern gpio
    POWER_EXT_GPIO,
} PowerCtrlChannel_t;


typedef enum WakeupSource {
    WAKEUP_SRC_POWER_KEY   = _BV(0),
    WAKEUP_SRC_TOUCH_PANEL = _BV(1),
    WAKEUP_SRC_BOOT_BUTTON = _BV(2),
    WAKEUP_SRC_ROTARY_BUTTON = _BV(3),
    WAKEUP_SRC_TIMER = _BV(4),
    WAKEUP_SRC_SENSOR = _BV(5),
} WakeupSource_t;


typedef bool (*lock_callback_t)(void);
