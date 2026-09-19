/**
 * @file      LilyGoTypedef.h
 * @brief     Defines shared board capability, power, wakeup, and init option types.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-19
 *
 */

#pragma once

#include <Arduino.h>
#include <string.h>

/**
 * @brief Mark a public API as deprecated with a compiler-specific attribute.
 */
#ifndef LILYGO_DEPRECATED
#if defined(__cplusplus) && (__cplusplus >= 201402L)
#define LILYGO_DEPRECATED(message) [[deprecated(message)]]
#elif defined(__GNUC__) || defined(__clang__)
#define LILYGO_DEPRECATED(message) __attribute__((deprecated(message)))
#endif
#endif

#ifndef LILYGO_DEPRECATED
#define LILYGO_DEPRECATED(message)
#endif

/**
 * @name Hardware presence mask bits
 * @brief Bits returned by device probing to report available peripherals.
 * @{
 */
#define HW_RADIO_ONLINE             (_BV(0))
#define HW_TOUCH_ONLINE             (_BV(1))
#define HW_DRV_ONLINE               (_BV(2))
#define HW_PMU_ONLINE               (_BV(3))
#define HW_RTC_ONLINE               (_BV(4))
#define HW_PSRAM_ONLINE             (_BV(5))
#define HW_GPS_ONLINE               (_BV(6))
#define HW_SD_ONLINE                (_BV(7))
#define HW_NFC_ONLINE               (_BV(8))
#define HW_BHI260AP_ONLINE          (_BV(9))
#define HW_KEYBOARD_ONLINE          (_BV(10))
#define HW_GAUGE_ONLINE             (_BV(11))
#define HW_EXPAND_ONLINE            (_BV(12))
#define HW_CODEC_ONLINE             (_BV(13))
#define HW_NRF24_ONLINE             (_BV(14))
#define HW_SI473X_ONLINE            (_BV(15))
#define HW_BME280_ONLINE            (_BV(16))
#define HW_MAGNETOMETER_ONLINE      (_BV(17))
#define HW_BMA_ONLINE               (_BV(18))
#define HW_QMI8658_ONLINE           (_BV(19))
#define HW_LED_INDIC_ONLINE         (_BV(20))
#define HW_PAW_A350_ONLINE          (_BV(21))
#define HW_SD_UNAVAILABLE           (_BV(22)) /**< SD is blocked by the detected hardware configuration. */
/** @} */


/**
 * @name Hardware initialization disable mask bits
 * @brief Bits passed to board begin() helpers to skip selected init stages.
 * @{
 */
#define NO_HW_RTC                   (_BV(0))
#define NO_HW_I2C_SCAN              (_BV(1))
#define NO_SCAN_I2C_DEV             (_BV(2))
// #define NO_HW_TFT                (_BV(1))
#define NO_HW_TOUCH                 (_BV(3))
#define NO_HW_SENSOR                (_BV(4))
#define NO_HW_NFC                   (_BV(5))
#define NO_HW_DRV                   (_BV(6))
#define NO_HW_GPS                   (_BV(7))
#define NO_HW_SD                    (_BV(8))
#define NO_HW_MIC                   (_BV(9))
#define NO_INIT_DELAY               (_BV(10))
#define NO_HW_LORA                  (_BV(11))
#define NO_HW_KEYBOARD              (_BV(12))
#define NO_INIT_FATFS               (_BV(13))
#define NO_HW_SI4735                (_BV(14))
#define NO_HW_BME280                (_BV(15))
#define NO_HW_MAG                   (_BV(16))
#define NO_HW_CODEC                 (_BV(17))
#define NO_HW_QMI8658               (_BV(18))
#define NO_HW_ROTARY                (_BV(19))
#define NO_HW_PAW_A350              (_BV(20))
/** @} */

/**
 * @name Hardware interrupt mask bits
 * @brief Bits used to report pending board-level interrupt sources.
 * @{
 */
#define HW_IRQ_TOUCHPAD             (_BV(0))
#define HW_IRQ_RTC                  (_BV(1))
#define HW_IRQ_POWER                (_BV(2))
#define HW_IRQ_SENSOR               (_BV(3))
#define HW_IRQ_EXPAND               (_BV(4))
/** @} */

/**
 * @brief Board-level controllable power rails and functional domains.
 */
typedef enum PowerCtrlChannel {
    POWER_DISPLAY,           /**< Display and touch power supply. */
    POWER_DISPLAY_BACKLIGHT, /**< Display backlight power supply. */
    POWER_RADIO,             /**< LoRa or sub-GHz radio power supply. */
    POWER_HAPTIC_DRIVER,     /**< Touch feedback driver power supply. */
    POWER_GPS,               /**< GNSS receiver power supply. */
    POWER_NFC,               /**< NFC reader power supply. */
    POWER_SD_CARD,           /**< SD card power supply. */
    POWER_SPEAK,             /**< Audio power amplifier supply. */
    POWER_SENSOR,            /**< Sensor power supply. */
    POWER_KEYBOARD,          /**< Keyboard power supply. */
    POWER_EXT_GPIO,          /**< External GPIO power domain. */
    POWER_SI4735_RADIO,      /**< SI4735 receiver power supply. */
    POWER_CODEC,             /**< External audio codec power supply. */
    POWER_RTC,               /**< RTC power supply. */

} PowerCtrlChannel_t;

/**
 * @brief Wakeup source mask bits reported after sleep.
 */
typedef enum WakeupSource {
    WAKEUP_SRC_POWER_KEY      = _BV(0), /**< Power key wakeup. */
    WAKEUP_SRC_TOUCH_PANEL    = _BV(1), /**< Touch panel wakeup. */
    WAKEUP_SRC_BOOT_BUTTON    = _BV(2), /**< Boot button wakeup. */
    WAKEUP_SRC_ROTARY_BUTTON  = _BV(3), /**< Rotary center button wakeup. */
    WAKEUP_SRC_TIMER          = _BV(4), /**< Timer wakeup. */
    WAKEUP_SRC_SENSOR         = _BV(5), /**< Sensor interrupt wakeup. */
    WAKEUP_SRC_BUTTON_LEFT    = _BV(6), /**< T-Deck V2 left button wakeup. */
    WAKEUP_SRC_BUTTON_RIGHT   = _BV(7), /**< T-Deck V2 right button wakeup. */
    WAKEUP_SRC_BUTTON         = _BV(8)  /**< Generic button wakeup. */
} WakeupSource_t;

/**
 * @brief Callback used by drivers that expose explicit lock/unlock hooks.
 * @return true if the lock was acquired.
 */
typedef bool (*lock_callback_t)(void);

/**
 * @brief Supported PMIC device families.
 */
enum PmicType {
    PMIC_TYPE_UNKNOWN, /**< Unknown or unsupported PMIC. */
    PMIC_TYPE_AXP2101, /**< X-Powers AXP2101 PMIC. */
    PMIC_TYPE_AXP202,  /**< X-Powers AXP202 PMIC. */
    PMIC_TYPE_BQ25896, /**< TI BQ25896 charger. */
};

/**
 * @brief Static hardware capability description for a board variant.
 */
struct LilyGoDeviceCapability {
    const char *boardName; /**< Human-readable board name. */
    const char *radioName; /**< Human-readable radio name, or "None". */
    const char *pmicName;  /**< Human-readable PMIC name, or "None". */
    const char *gaugeName; /**< Human-readable fuel gauge name, or "None". */
    bool hasSd;           /**< true when an SD card interface is present. */
    bool hasGps;          /**< true when a GNSS receiver is present. */
    bool gpsRuntimeProbe; /**< true when GNSS presence should be probed at runtime. */
    bool hasTouch;        /**< true when a touch panel is present. */
    bool hasKeyboard;     /**< true when a keyboard is present. */
    bool hasTrackball;    /**< true when a trackball input device is present. */
    bool hasRotary;       /**< true when a rotary encoder is present. */
    bool hasBma423;       /**< true when a BMA423 motion sensor is present. */
    bool hasBhi260;       /**< true when a BHI260 motion sensor is present. */
    bool hasNfc;          /**< true when an NFC controller is present. */
    bool hasIrTx;         /**< true when IR transmit hardware is present. */
    bool hasIrRx;         /**< true when IR receive hardware is present. */
    bool hasEnvSensor;    /**< true when an environmental sensor is present. */
    bool hasCompass;      /**< true when a magnetometer is present. */
    bool hasAudioOut;     /**< true when audio output is present. */
    bool hasAudioIn;      /**< true when audio input is present. */
    bool hasHaptic;       /**< true when haptic feedback hardware is present. */
    bool hasExternalI2c;  /**< true when an external I2C interface is available. */
    bool hasExternalSpi;  /**< true when an external SPI interface is available. */
    bool hasExternalUart; /**< true when an external UART interface is available. */
    bool hasExternalGpio; /**< true when external GPIO expansion/control is available. */
    PmicType pmicType;     /**< PMIC type used for runtime logic. */
    bool hasButton;        /**< true when board-level button events are available. */
    bool hasPmuButton;     /**< true when PMIC power-key events are available. */
};

/**
 * @brief Per-feature initialization switches used by board begin() methods.
 */
struct LilyGoDeviceInitOptions {
    bool scanI2c = true;      /**< Scan and print I2C devices during begin(). */
    bool initFatfs = true;    /**< Mount USB MSC / FATFS service when the board uses it. */
    bool initDelay = true;    /**< Keep legacy delayed init steps enabled. */
    bool initRtc = true;      /**< Initialize the RTC chip. */
    bool initTouch = true;    /**< Initialize the touch panel. */
    bool initSensor = true;   /**< Initialize the primary motion sensor. */
    bool initNfc = true;      /**< Initialize the NFC reader. */
    bool initHaptic = true;   /**< Initialize the haptic driver. */
    bool initGps = true;      /**< Initialize the GNSS receiver. */
    bool initSd = true;       /**< Initialize the SD card interface. */
    bool initAudio = true;    /**< Initialize microphone and amplifier paths. */
    bool initRadio = true;    /**< Initialize LoRa / sub-GHz radio. */
    bool initKeyboard = true; /**< Initialize keyboard input. */
    bool initSi4735 = true;   /**< Initialize SI4735 receiver. */
    bool initBme280 = true;   /**< Initialize BME280 environmental sensor. */
    bool initCompass = true;  /**< Initialize magnetometer / compass. */
    bool initCodec = true;    /**< Initialize external audio codec. */
    bool initQmi8658 = true;  /**< Initialize QMI8658 IMU. */
    bool initRotary = true;   /**< Initialize rotary encoder. */
    bool initPawA350 = true;  /**< Initialize PAW A350 trackball sensor. */
    bool initGauge = true;    /**< Initialize external fuel gauge. */
    bool initPmu = true;      /**< Initialize PMU / charger device. */
};

/**
 * @brief Check whether a capability name represents an available device.
 * @param name Capability name string.
 * @return true when the name is non-empty and not "None".
 */
inline bool lilygo_capability_name_is_available(const char *name)
{
    return name && name[0] != '\0' && strcmp(name, "None") != 0;
}

/**
 * @brief Check whether a gauge capability refers to an external gauge IC.
 * @param name Gauge name string.
 * @return true when the gauge is available and not PMU internal.
 */
inline bool lilygo_capability_has_external_gauge(const char *name)
{
    return lilygo_capability_name_is_available(name) && strcmp(name, "PMU internal") != 0;
}

/**
 * @brief Check whether a capability advertises a PMIC.
 * @param capability Board capability description.
 * @return true when the board has a known PMIC / charger IC.
 */
inline bool lilygo_capability_has_pmic(const LilyGoDeviceCapability &capability)
{
    return capability.pmicType != PMIC_TYPE_UNKNOWN || lilygo_capability_name_is_available(capability.pmicName);
}

/**
 * @brief Build default initialization options from a capability table entry.
 * @param capability Board capability description.
 * @return Initialization options matching the advertised board features.
 */
inline LilyGoDeviceInitOptions lilygo_init_options_from_capability(const LilyGoDeviceCapability &capability)
{
    LilyGoDeviceInitOptions options;
    options.initFatfs = false;
    options.initRtc = false;
    options.initTouch = capability.hasTouch;
    options.initSensor = capability.hasBma423 || capability.hasBhi260;
    options.initNfc = capability.hasNfc;
    options.initHaptic = capability.hasHaptic;
    options.initGps = capability.hasGps || capability.gpsRuntimeProbe;
    options.initSd = capability.hasSd;
    options.initAudio = capability.hasAudioOut || capability.hasAudioIn;
    options.initRadio = lilygo_capability_name_is_available(capability.radioName);
    options.initKeyboard = capability.hasKeyboard;
    options.initRotary = capability.hasRotary;
    options.initBme280 = capability.hasEnvSensor;
    options.initCompass = capability.hasCompass;
    options.initSi4735 = false;
    options.initQmi8658 = false;
    options.initPawA350 = capability.hasTrackball;
    options.initCodec = capability.hasAudioOut || capability.hasAudioIn;
    options.initGauge = lilygo_capability_has_external_gauge(capability.gaugeName);
    options.initPmu = lilygo_capability_has_pmic(capability);
    return options;
}

/**
 * @brief Apply legacy disable-mask bits to a default initialization option set.
 * @param defaults Default option set to modify.
 * @param disable_hw_init Bit mask made from NO_HW_* and NO_INIT_* values.
 * @return Updated initialization options.
 */
inline LilyGoDeviceInitOptions lilygo_init_options_from_disable_mask(
    const LilyGoDeviceInitOptions &defaults,
    uint32_t disable_hw_init)
{
    LilyGoDeviceInitOptions options = defaults;
    if (disable_hw_init & (NO_HW_I2C_SCAN | NO_SCAN_I2C_DEV)) {
        options.scanI2c = false;
    }
    if (disable_hw_init & NO_INIT_FATFS) {
        options.initFatfs = false;
    }
    if (disable_hw_init & NO_INIT_DELAY) {
        options.initDelay = false;
    }
    if (disable_hw_init & NO_HW_RTC) {
        options.initRtc = false;
    }
    if (disable_hw_init & NO_HW_TOUCH) {
        options.initTouch = false;
    }
    if (disable_hw_init & NO_HW_SENSOR) {
        options.initSensor = false;
    }
    if (disable_hw_init & NO_HW_NFC) {
        options.initNfc = false;
    }
    if (disable_hw_init & NO_HW_DRV) {
        options.initHaptic = false;
    }
    if (disable_hw_init & NO_HW_GPS) {
        options.initGps = false;
    }
    if (disable_hw_init & NO_HW_SD) {
        options.initSd = false;
    }
    if (disable_hw_init & NO_HW_MIC) {
        options.initAudio = false;
    }
    if (disable_hw_init & NO_HW_LORA) {
        options.initRadio = false;
    }
    if (disable_hw_init & NO_HW_KEYBOARD) {
        options.initKeyboard = false;
    }
    if (disable_hw_init & NO_HW_SI4735) {
        options.initSi4735 = false;
    }
    if (disable_hw_init & NO_HW_BME280) {
        options.initBme280 = false;
    }
    if (disable_hw_init & NO_HW_MAG) {
        options.initCompass = false;
    }
    if (disable_hw_init & NO_HW_CODEC) {
        options.initCodec = false;
    }
    if (disable_hw_init & NO_HW_QMI8658) {
        options.initQmi8658 = false;
    }
    if (disable_hw_init & NO_HW_ROTARY) {
        options.initRotary = false;
    }
    if (disable_hw_init & NO_HW_PAW_A350) {
        options.initPawA350 = false;
    }
    return options;
}
