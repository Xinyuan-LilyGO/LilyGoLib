/**
 * @file      hal_interface.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 */

#pragma once
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <vector>
#include "event_define.h"

using std::vector;

#define GPS_MODEL_MAX_LEN                 24
#define WIFI_SSID_MAX_LEN                 64
#define WIFI_PASSWORD_MAX_LEN             128
#define AUDIO_FILE_NAME_MAX_LEN           128
#define NETWORK_AUDIO_NAME_MAX_LEN        48
#define NETWORK_AUDIO_URL_MAX_LEN         192
#define POWER_LABEL_MAX_LEN               32

typedef enum {
    MEDIA_VOLUME_UP,
    MEDIA_VOLUME_DOWN,
    MEDIA_PLAY_PAUSE,
    MEDIA_NEXT,
    MEDIA_PREVIOUS
} media_key_value_t;

typedef enum {
    KEYBOARD_TYPE_NONE,
    KEYBOARD_TYPE_1,
    KEYBOARD_TYPE_2,
} keyboard_type_t;

typedef enum {
    SENSOR_TYPE_IMU,
    SENSOR_TYPE_ACCEL
} sensor_type_t;


/* Radio frequency constants */
#define RADIO_FIXED_FREQUENCY  920.0
#define RADIO_FIXED_FREQUENCY_STRING "920MHZ"
#define RADIO_DEFAULT_FREQUENCY  RADIO_FIXED_FREQUENCY
#define RADIO_DEFAULT_TX_POWER 13

// #define RADIO_DEFAULT_FREQUENCY  868.0
// #define RADIO_DEFAULT_TX_POWER 22

// Check if not compiling for Arduino environment
// If not, define the wl_status_t enumeration
#ifndef ARDUINO

#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

#ifndef _BV
#define _BV(x)                      (1UL<<x)
#endif

/**
 * @brief Enumeration representing different WiFi statuses.
 *
 * This enumeration is used to represent various WiFi connection statuses,
 * which is compatible with the WiFi Shield library.
 */
typedef enum {
    WL_NO_SHIELD = 255,  // for compatibility with WiFi Shield library
    WL_STOPPED = 254,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
} wl_status_t;

#define DEVICE_MAX_BRIGHTNESS_LEVEL 255
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT   1000
#define DEVICE_MIN_CHARGE_CURRENT   100
#define DEVICE_CHARGE_LEVEL_NUMS    12
#define DEVICE_CHARGE_STEPS         1
#define USING_RADIO_NAME            "SX12XX"


// Hardware online status bit definitions
// Each bit represents the online status of a specific hardware component
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
#define NO_HW_MAG                   (_BV(17))
#define HW_QMI8658_ONLINE           (_BV(18))
#define HW_LED_INDIC_ONLINE         (_BV(19))
#define HW_SD_UNAVAILABLE           (_BV(22))

#else
// If compiling for Arduino, include the WiFi library
#include <WiFi.h>
#if defined(USING_ST25R3916) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA)
#include <nfc/LilyGoNfcService.h>
#endif
#endif


// Define the GMT offset in seconds (for 8 hours ahead)
#define GMT_OFFSET_SECOND       (8*3600)

#define GPS_SIGNAL_MAX_SATELLITES      96
#define GPS_SIGNAL_MAX_CONSTELLATIONS  5

typedef enum {
    GPS_SAT_SYSTEM_UNKNOWN = 0,
    GPS_SAT_SYSTEM_GPS,
    GPS_SAT_SYSTEM_GLONASS,
    GPS_SAT_SYSTEM_BEIDOU,
    GPS_SAT_SYSTEM_GALILEO,
    GPS_SAT_SYSTEM_QZSS
} gps_satellite_system_t;

typedef enum {
    GPS_ANTENNA_UNKNOWN = 0,
    GPS_ANTENNA_OK,
    GPS_ANTENNA_OPEN,
    GPS_ANTENNA_SHORT
} gps_antenna_state_t;

typedef struct {
    gps_satellite_system_t system;
    uint16_t prn;
    int16_t elevation;
    int16_t azimuth;
    int16_t cn0;
    bool has_cn0;
    bool used;
} gps_signal_satellite_t;

typedef struct {
    gps_satellite_system_t system;
    uint16_t visible;
    uint16_t tracking;
    uint16_t used;
    int16_t max_cn0;
    int16_t avg_cn0;
} gps_signal_constellation_t;

/**
 * @brief Structure to hold GPS parameters.
 *
 * This structure stores information related to GPS, such as model,
 * latitude, longitude, date and time, speed, received data size,
 * number of satellites, and PPS status.
 */
typedef struct  {
    char model[GPS_MODEL_MAX_LEN];
    double lat;
    double lng;
    struct tm datetime;
    double speed;
    double altitude;
    bool location_valid;
    bool datetime_valid;
    bool speed_valid;
    bool altitude_valid;
    uint32_t rx_size;
    uint16_t satellite;
    bool pps;
    bool nmea_to_serial;
    gps_antenna_state_t antenna_state;
    uint32_t antenna_age;
    uint16_t signal_satellite_count;
    uint16_t constellation_count;
    gps_signal_satellite_t signal_satellites[GPS_SIGNAL_MAX_SATELLITES];
    gps_signal_constellation_t constellations[GPS_SIGNAL_MAX_CONSTELLATIONS];
    uint32_t ttff_ms;
    bool ttff_valid;
} gps_params_t;

/**
 * @brief Enumeration representing different radio modes.
 *
 * This enumeration defines the possible operating modes of the radio.
 */
enum RadioMode {
    RADIO_DISABLE,
    RADIO_TX,
    RADIO_RX,
    RADIO_CW,
};

/**
 * @brief Structure to hold radio parameters.
 *
 * This structure stores information about the radio's configuration,
 * such as running status, frequency, bandwidth, power, spreading factor,
 * coding rate, mode, sync word, and interval.
 */
typedef struct {
    bool isRunning;
    float freq;
    float bandwidth;
    uint16_t cr;
    uint8_t power;
    uint8_t sf;
    uint8_t mode;
    uint8_t syncWord;
    uint32_t interval;
} radio_params_t;

/**
 * @brief Structure to hold WiFi scan parameters.
 *
 * This structure stores information obtained from a WiFi scan,
 * including the BSSID, authentication mode, RSSI, channel, and SSID.
 */
typedef struct {
    uint8_t bssid[6];                     /**< MAC address of AP */
    uint8_t authmode;
    int8_t  rssi;
    int32_t channel;
    char ssid[WIFI_SSID_MAX_LEN];
} wifi_scan_params_t;

/**
 * @brief Structure to hold WiFi connection parameters.
 *
 * This structure stores the SSID and password required for a WiFi connection.
 */
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASSWORD_MAX_LEN];
} wifi_conn_params_t;

/**
 * @brief  Enumeration representing different audio source types.
 * @note   This enumeration is used to specify the source of audio data.
 */
typedef enum {
    AUDIO_SOURCE_FATFS,
    AUDIO_SOURCE_SDCARD,
} audio_source_type_t;

typedef enum {
    MIC_INPUT_SOURCE_INTERNAL = 0,
    MIC_INPUT_SOURCE_JACK = 1,
} mic_input_source_t;

/**
 * @brief  Structure to hold audio parameters.
 * @note   This structure is used to specify the audio source and filename.
 */
typedef struct {
    audio_source_type_t source_type;
    char file_name[AUDIO_FILE_NAME_MAX_LEN];
} AudioParams_t;

typedef struct {
    char name[NETWORK_AUDIO_NAME_MAX_LEN];
    char url[NETWORK_AUDIO_URL_MAX_LEN];
} NetworkAudioStreamParams_t;

/**
 * @brief Structure to hold monitor parameters.
 *
 * This structure stores information about the device's battery and power status,
 * such as battery voltage, USB voltage, battery percentage, charge state,
 * temperature, remaining capacity, full charge capacity, design capacity,
 * instantaneous current, standby current, average power, max load current,
 * time to empty, and time to full.
 */
typedef struct {
    bool     has_gauge;
    bool     charging;          // true while charging is active
    char     charge_state[POWER_LABEL_MAX_LEN];
    uint16_t sys_voltage;       // mv
    uint16_t battery_voltage;   // mv
    uint16_t usb_voltage;       // mv
    int      battery_percent;   // %
    float    temperature;       // Celsius
    uint16_t remainingCapacity; // mAh
    uint16_t fullChargeCapacity;// mAh
    uint16_t designCapacity;    //mAh
    int16_t  instantaneousCurrent;   // mA
    float    instantaneousPower;   // W
    int16_t  standbyCurrent;     // mA
    int16_t  averagePower;      //mW
    int16_t  maxLoadCurrent;    //mA
    uint16_t timeToEmpty;       // minute
    uint16_t timeToFull;        // minute
    char ntc_state[POWER_LABEL_MAX_LEN];
} monitor_params_t;

typedef enum {
    POWER_MONITOR_SRC_NONE = 0,
    POWER_MONITOR_SRC_PMU,
    POWER_MONITOR_SRC_EXTERNAL_GAUGE,
    POWER_MONITOR_SRC_INTERNAL_GAUGE,
    POWER_MONITOR_SRC_ESTIMATED,
    POWER_MONITOR_SRC_ADC,
} power_monitor_source_t;

typedef struct {
    bool valid;
    float value;
    power_monitor_source_t source;
} power_monitor_metric_t;

typedef struct {
    uint32_t online_mask;
    char pmic_name[POWER_LABEL_MAX_LEN];
    char gauge_name[POWER_LABEL_MAX_LEN];
    bool pmu_present;
    bool external_gauge_present;
    bool fuel_gauge_present;
    bool battery_present_valid;
    bool battery_present;
    bool vbus_present_valid;
    bool vbus_present;
    bool charging;
    bool charge_done;
    bool charge_fault;
    bool charge_enabled_valid;
    bool charge_enabled;
    bool otg_supported;
    bool otg_enabled;
    char charge_state[POWER_LABEL_MAX_LEN];
    char ntc_state[POWER_LABEL_MAX_LEN];
    power_monitor_metric_t vbus_mv;
    power_monitor_metric_t vbus_ma;
    power_monitor_metric_t sys_mv;
    power_monitor_metric_t battery_mv;
    power_monitor_metric_t battery_ma;
    power_monitor_metric_t battery_percent;
    power_monitor_metric_t temperature_c;
    power_monitor_metric_t battery_temperature_c;
    power_monitor_metric_t instantaneous_power_w;
    power_monitor_metric_t average_power_mw;
    power_monitor_metric_t remaining_capacity_mah;
    power_monitor_metric_t full_charge_capacity_mah;
    power_monitor_metric_t design_capacity_mah;
    power_monitor_metric_t standby_current_ma;
    power_monitor_metric_t max_load_current_ma;
    power_monitor_metric_t time_to_empty_min;
    power_monitor_metric_t time_to_full_min;
} power_monitor_snapshot_t;

/**
 * @brief Structure to hold user setting parameters.
 *
 * This structure stores user-defined settings, such as display brightness level,
 * keyboard backlight level, display timeout in seconds, charger current, and charger enable status.
 */
typedef struct {
    uint8_t brightness_level;
    uint8_t keyboard_bl_level;
    uint8_t led_indicator_level;
    uint8_t disp_timeout_second;
    uint16_t charger_current;
    uint8_t charger_enable;
    uint8_t theme_preset_idx;
} user_setting_params_t;

typedef enum {
    AUDIO_JACK_MODE_CTIA = 0,
    AUDIO_JACK_MODE_OMTP = 1,
} audio_jack_mode_t;

/**
 * @brief Structure to hold audio parameters.
 *
 * This structure stores information related to audio events and the filename of the audio file.
 */
typedef struct {
    enum app_event event;
    const char *filename ;
    const char *url;
    audio_source_type_t source_type;
    uint16_t frequency_hz;
    uint16_t duration_ms;
} audio_params_t;

/**
 * @brief Structure to hold radio transmit parameters.
 *
 * This structure stores information required for radio transmission,
 * such as the data buffer, data length, and transmission state.
 */
typedef struct {
    uint8_t *data;
    size_t  length;
    int state;
} radio_tx_params_t;

/**
 * @brief Structure to hold radio receive parameters.
 *
 * This structure stores information obtained from radio reception,
 * such as the received data buffer, data length, RSSI, SNR, and reception state.
 */
typedef struct {
    uint8_t *data;
    size_t  length;
    int16_t rssi;
    int16_t snr;
    int state;
} radio_rx_params_t;

/**
 * @brief Structure to hold IMU parameters.
 *
 * This structure stores information related to the Inertial Measurement Unit (IMU),
 * such as roll, pitch, and heading.
 */
typedef struct {
    float roll;
    float pitch ;
    float heading;
    uint8_t orientation;
    uint8_t reverse ;
    bool accel_valid;
    float accel_x;
    float accel_y;
    float accel_z;
    float accel_magnitude;
    uint32_t accel_sequence;
} imu_params_t;

typedef enum {
    BMA_ACTIVITY_STATIONARY,
    BMA_ACTIVITY_WALKING,
    BMA_ACTIVITY_RUNNING,
    BMA_ACTIVITY_UNKNOWN,
} bma_activity_t;

typedef enum {
    BMA_SENSOR_EVENT_NONE,
    BMA_SENSOR_EVENT_STEP,
    BMA_SENSOR_EVENT_SINGLE_TAP,
    BMA_SENSOR_EVENT_DOUBLE_TAP,
    BMA_SENSOR_EVENT_TRIPLE_TAP,
    BMA_SENSOR_EVENT_ACTIVITY,
    BMA_SENSOR_EVENT_TILT,
    BMA_SENSOR_EVENT_ANY_MOTION,
    BMA_SENSOR_EVENT_NO_MOTION,
    BMA_SENSOR_EVENT_DATA_READY,
} bma_sensor_event_t;

typedef struct {
    bool valid;
    bool accel_valid;
    float accel_x;
    float accel_y;
    float accel_z;
    float magnitude;
    float peak_magnitude;
    uint8_t orientation;
    uint8_t reverse;
    uint32_t step_count;
    uint32_t step_events;
    uint32_t single_taps;
    uint32_t double_taps;
    uint32_t triple_taps;
    uint32_t activity_events;
    uint32_t tilt_events;
    uint32_t motion_events;
    uint32_t no_motion_events;
    bma_activity_t activity;
    bma_sensor_event_t last_event;
} bma_sensor_snapshot_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
    bool valid;
} mag_calibration_t;

typedef struct {
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
    float field_x;
    float field_y;
    float field_z;
    float heading_degrees;
    float strength_ut;
    bool overflow;
} mag_data_t;

typedef enum {
    HW_TRACKBALL_DIR_NONE,
    HW_TRACKBALL_DIR_UP,
    HW_TRACKBALL_DIR_DOWN,
    HW_TRACKBALL_DIR_LEFT,
    HW_TRACKBALL_DIR_RIGHT
} hw_trackball_dir;

using TrackballEventCallback = void(*)(int8_t delta_x, int8_t delta_y);
using ButtonEventCallback = void(*)(uint8_t idx, uint8_t state);

typedef enum : uint8_t {
    HW_POINTER_BUTTON_LEFT = 0x01,
    HW_POINTER_BUTTON_RIGHT = 0x02,
    HW_POINTER_BUTTON_MIDDLE = 0x04,
} hw_pointer_button_t;

using PointerButtonEventCallback = void(*)(uint8_t button_mask);

/**
 * @brief Initialize the hardware.
 *
 * This function is used to perform the initial setup of the hardware components.
 */
void hw_init();

/**
 * @brief Get the number of connected hardware devices.
 *
 * @return The number of connected hardware devices.
 */
uint16_t hw_get_devices_nums();

/**
 * @brief Get the name of a specific hardware device.
 *
 * @param index The index of the hardware device.
 * @return A pointer to the name of the hardware device.
 */
const char *hw_get_devices_name(int index);

/**
 * @brief Get the variant name of the device.
 *
 * @return A pointer to the variant name string.
 */
const char *hw_get_variant_name();

/**
 * @brief Get the MAC address of the device.
 *
 * @param mac A pointer to an array where the MAC address will be stored.
 * @return True if the MAC address is successfully retrieved, false otherwise.
 */
bool hw_get_mac(uint8_t *mac);

/**
 * @brief Get the current WiFi SSID.
 *
 * @param param Buffer where the SSID will be stored.
 * @param param_size Buffer size in bytes.
 */
void hw_get_wifi_ssid(char *param, size_t param_size);

/**
 * @brief Get the current date and time as a string.
 *
 * @param param Buffer where the date and time will be stored.
 * @param param_size Buffer size in bytes.
 */
void hw_get_date_time(char *param, size_t param_size);

/**
 * @brief Get the current date and time as a struct tm.
 *
 * @param timeinfo A reference to a struct tm where the date and time will be stored.
 */
void hw_get_date_time(struct tm &timeinfo);

/**
 * @brief Set the current device time.
 *
 * On Arduino builds this updates the system clock and writes the RTC when it is present.
 *
 * @param timeinfo Local date/time to apply.
 * @return True if the time was accepted and applied.
 */
bool hw_set_date_time(const struct tm &timeinfo);

/**
 * @brief Write the current system time to the hardware RTC when it is online.
 *
 * @return True if a hardware RTC is online and the write command was issued.
 */
bool hw_write_rtc_from_system_time();

/**
 * @brief Get the persisted fixed timezone offset in seconds.
 *
 * @return GMT offset in seconds.
 */
int32_t hw_get_timezone_offset();

/**
 * @brief Get the persisted daylight saving offset in seconds.
 *
 * @return Daylight saving offset in seconds.
 */
int32_t hw_get_daylight_offset();

/**
 * @brief Persist and apply the fixed timezone offset used for NTP sync.
 *
 * @param gmt_offset_sec GMT offset in seconds.
 * @param daylight_offset_sec Daylight saving offset in seconds.
 */
void hw_set_timezone_offset(int32_t gmt_offset_sec, int32_t daylight_offset_sec);

/**
 * @brief Sync the system clock from NTP using a fixed timezone offset.
 *
 * @param gmt_offset_sec GMT offset in seconds.
 * @param daylight_offset_sec Daylight saving offset in seconds.
 * @param server1 Primary NTP server.
 * @param server2 Secondary NTP server.
 * @param wait_ms Maximum wait time for SNTP to produce a local time.
 * @return True if a local time was obtained.
 */
bool hw_sync_time_from_ntp(int32_t gmt_offset_sec, int32_t daylight_offset_sec,
                           const char *server1, const char *server2, uint32_t wait_ms);

/**
 * @brief Get the current WiFi status.
 *
 * @return The current WiFi status as defined in the wl_status_t enumeration.
 */
wl_status_t hw_get_wifi_status();

/**
 * @brief Get the current IP address.
 *
 * @param param Buffer where the IP address will be stored.
 * @param param_size Buffer size in bytes.
 */
void hw_get_ip_address(char *param, size_t param_size);

/**
 * @brief Get the current WiFi RSSI.
 *
 * @return The current WiFi RSSI value.
 */
int16_t hw_get_wifi_rssi();

/**
 * @brief Get the current battery voltage.
 *
 * @return The current battery voltage in millivolts.
 */
int16_t hw_get_battery_voltage();

/**
 * @brief Get the size of the SD card.
 *
 * @return The size of the SD card in floating-point format.
 */
float hw_get_sd_size();

/**
 * @brief Get the Arduino version.
 *
 * @param param Buffer where the Arduino version will be stored.
 * @param param_size Buffer size in bytes.
 */
void hw_get_arduino_version(char *param, size_t param_size);

/**
 * @brief Get the GPS information.
 *
 * @param param A reference to a gps_params_t structure where the GPS information will be stored.
 */
bool hw_get_gps_info(gps_params_t &param);

/**
 * @brief Enable or disable raw GNSS NMEA output on Serial.
 */
void hw_set_gps_nmea_serial(bool enable);

/**
 * @brief Attach the PPS signal to the GPS.
 */
void hw_gps_attach_pps();

/**
 * @brief Detach the PPS signal from the GPS.
 */
void hw_gps_detach_pps();

/**
 * @brief Get the online status of the hardware devices.
 *
 * @return A 32-bit unsigned integer representing the online status of the hardware devices.
 */
uint32_t hw_get_device_online();

/**
 * @brief Get the runtime LoRa hardware presence result.
 *
 * @return true if LoRa hardware is present. Boards without runtime detection return their static radio capability.
 */
bool hw_has_lora_hardware();

/**
 * @brief Set the display backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_disp_backlight(uint8_t level);

/**
 * @brief  Enable or disable the display blacklight.
 * @param  enable: true to enable, false to disable.
 * @retval None
 */
void hw_disp_enable_backlight(bool enable);

/**
 * @brief Get the current display backlight level.
 *
 * @return The current display backlight level.
 */
uint8_t hw_get_disp_backlight();

/**
 * @brief Check if the display is on.
 *
 * @return True if the display is on, false otherwise.
 */
bool hw_get_disp_is_on();

/**
 * @brief  Enable or disable the keyboard backlight.
 * @param  enable: true to enable, false to disable.
 * @retval None
 */
void hw_kb_enable_backlight(bool enable);

/**
 * @brief Set the keyboard backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_kb_backlight(uint8_t level);

/**
 * @brief Set the indicator LED backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_led_backlight(uint8_t level);

/**
 * @brief Get the current keyboard backlight level.
 *
 * @return The current keyboard backlight level.
 */
uint8_t hw_get_kb_backlight();

/**
 * @brief Start a WiFi scan.
 *
 * @return The result of the WiFi scan operation.
 */
int16_t hw_set_wifi_scan();

/**
 * @brief Get the WiFi scanning ?
 * @return true is running,false is stop.
 */
bool hw_get_wifi_scanning();

/**
 * @brief Get the results of the WiFi scan.
 *
 * @param list A reference to a vector where the WiFi scan results will be stored.
 */
void hw_get_wifi_scan_result(vector < wifi_scan_params_t > &list);

/**
 * @brief Set up a WiFi connection.
 *
 * @param params A reference to a wifi_conn_params_t structure containing the SSID and password.
 */
void hw_set_wifi_connect(wifi_conn_params_t &params);

/**
 * @brief Check if the device is connected to a WiFi network.
 *
 * @return True if connected, false otherwise.
 */
bool hw_get_wifi_connected();

/**
 * @brief Set the radio parameters.
 *
 * @param params A reference to a radio_params_t structure containing the radio configuration.
 * @return The result of the radio parameter setting operation.
 */
int16_t hw_set_radio_params(radio_params_t &params);

/**
 * @brief Get the current radio parameters.
 *
 * @param params A reference to a radio_params_t structure where the radio parameters will be stored.
 */
void hw_get_radio_params(radio_params_t &params);

/**
 * @brief Set the radio to listening mode.
 */
void hw_set_radio_listening();

/**
 * @brief Set the radio to default configuration.
 */
void hw_set_radio_default();

/**
 * @brief Start radio transmission.
 *
 * @param params A reference to a radio_tx_params_t structure containing the transmission data.
 * @param continuous Whether the transmission should be continuous. Default is true.
 */
void hw_set_radio_tx(radio_tx_params_t &params, bool continuous = true);

/**
 * @brief Check whether a non-blocking radio transmission is complete.
 *
 * @param state The final transmit state when the function returns true.
 * @return true when TX has completed or failed, false when it is still running.
 */
bool hw_get_radio_tx_done(int16_t &state);

/**
 * @brief Get the received radio data.
 *
 * @param params A reference to a radio_rx_params_t structure where the received data will be stored.
 */
void hw_get_radio_rx(radio_rx_params_t &params);

/**
 * @brief Check if the radio supports spectral scan (SX126x only).
 */
bool hw_has_spectral_scan();

/**
 * @brief Initialize spectral scan mode (uploads patch, configures FSK modem).
 * @return 0 on success, negative on error.
 */
int16_t hw_radio_spectral_scan_init();

/**
 * @brief Start a spectral scan.
 * @param numSamples Number of samples (e.g., 2048).
 * @return 0 on success, negative on error.
 */
int16_t hw_radio_spectral_scan_start(uint16_t numSamples);

/**
 * @brief Check if spectral scan is complete.
 * @return 0 if complete, negative if still running or error.
 */
int16_t hw_radio_spectral_scan_status();

/**
 * @brief Get spectral scan results.
 * @param results Array of RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE (33) uint16_t values.
 * @return 0 on success, negative on error.
 */
int16_t hw_radio_spectral_scan_result(uint16_t *results);

/**
 * @brief Abort spectral scan.
 */
void hw_radio_spectral_scan_abort();

/**
 * @brief Exit spectral scan mode (restore radio defaults).
 */
void hw_radio_spectral_scan_deinit();

/*
* @brief Check if the SD card is inserted.
* @return True if the SD card is inserted, false otherwise.
*/
bool hw_is_sd_insert();

/*
* @brief Check whether this board has a physical SD card detect pin.
* @return True if the board can detect SD card insertion without mounting.
*/
bool hw_has_sd_detect_pin();

/*
* @brief Check the physical SD card insertion state when a detect pin exists.
* @return True if the SD card is physically inserted or already ready on boards without detect pin.
*/
bool hw_is_sd_card_inserted();

/*
* @brief Get the board default SD SPI frequency.
* @return The default SD SPI frequency in Hz, or 0 if not supported.
*/
uint32_t hw_get_sd_default_spi_freq();

/*
* @brief Build the SD mount SPI frequency retry list. The board default is first.
* @param freqs Output buffer for frequencies in Hz.
* @param max_count Maximum number of entries in freqs.
* @return Number of frequencies written.
*/
uint8_t hw_get_sd_mount_freq_list(uint32_t *freqs, uint8_t max_count);

/**
 * @brief Mount the SD card.
 * @param spi_freq SD SPI frequency in Hz. Pass 0 to use the board default.
 */
bool hw_mount_sd(uint32_t spi_freq = 0);

/**
 * @brief Unmount the SD card and clear cached SD state.
 */
void hw_unmount_sd();

/**
 * @brief Get the list of music files from the SD card.
 *
 * @param list A reference to an AudioParams_t structure where the music file list will be stored.
 */
void hw_get_filesystem_music(vector < AudioParams_t >  &list);

/**
 * @brief Get network audio streams from SD config, or built-in defaults.
 *
 * Config path: /radio_streams.txt
 * Format: Name|http://host/path
 */
void hw_get_network_audio_streams(vector < NetworkAudioStreamParams_t > &list);

/*
* @brief Start playing a music file from the SD card.
*
* @param source_type The source type of the audio (e.g., SD card, FFAT).
* @param filename A pointer to the name of the music file to play.
*/
void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename);

/**
 * @brief Start playing an HTTP MP3/ICY audio stream.
 */
void hw_set_network_audio_stream_play(const char *name, const char *url);

/**
 * @brief Play the boot PCM WAV from FFat asynchronously.
 *
 * Expected file path in the uploaded FFat image: /boot.wav
 */
void hw_play_boot_sound_async();

/**
 * @brief Pause the music playback.
 */
void hw_set_sd_music_pause();

/**
 * @brief Resume the music playback.
 */
void hw_set_sd_music_resume();

/**
 * @brief Check if the music player is running.
 *
 * @return True if the music player is running, false otherwise.
 */
bool hw_player_running();

/**
 * @brief Set the volume level.
 *
 * @param volume The volume level to set (0-100).
 */
void hw_set_volume(uint8_t volume);

/**
 * @brief  Get the current volume level.
 * @retval Current volume level
 */
uint8_t hw_get_volume();

/**
 * @brief Play a short beep through the audio output device.
 *
 * @param frequency_hz Tone frequency in Hz.
 * @param duration_ms Tone duration in milliseconds.
 */
void hw_audio_beep(uint16_t frequency_hz, uint16_t duration_ms);

/**
 * @brief  Get the number of microphone input channels.
 * @retval Number of channels (1 = mono, 2 = stereo)
 */
uint8_t hw_get_codec_input_channels();

/**
 * @brief  Get the current microphone gain.
 * @retval Current gain
 */
float hw_get_mic_gain();

/**
 * @brief  Set the microphone gain.
 * @param  gain: The gain value to set.
 * @retval None
 */
void hw_set_mic_gain(float gain);

/**
 * @brief Check whether microphone input source selection is available.
 *
 * @return True when the board can switch between internal and 3.5mm jack microphone inputs.
 */
bool hw_has_mic_input_source_setting();

/**
 * @brief Set the microphone input source.
 *
 * @param source One of mic_input_source_t.
 */
void hw_set_mic_input_source(uint8_t source);

/**
 * @brief Get the current microphone input source.
 *
 * @return One of mic_input_source_t.
 */
uint8_t hw_get_mic_input_source();

/**
 * @brief Get the display name for the current microphone input source.
 *
 * @return Human-readable input source name.
 */
const char *hw_get_mic_input_source_name();

/**
 * @brief Re-apply the current microphone input source to the codec without changing settings.
 *
 * @return True if the codec register write succeeds.
 */
bool hw_apply_mic_input_source();

/**
 * @brief Stop the music playback.
 */
void hw_set_play_stop();

/**
 * @brief Request playback stop without waiting for the player task to exit.
 */
void hw_set_play_stop_async();

/**
 * @brief Shutdown the hardware.
 *
 * Devices without a PMIC enter deep sleep to emulate power off.
 */
void hw_shutdown();

/**
 * @brief Hardware behavior used for the primary power-off action.
 */
typedef enum {
    HW_POWER_OFF_SHUTDOWN = 0,
    HW_POWER_OFF_SHIP_MODE,
    HW_POWER_OFF_DEEP_SLEEP,
} hw_power_off_mode_t;

/**
 * @brief Get the power-off behavior supported by this device.
 */
hw_power_off_mode_t hw_get_power_off_mode();

/**
 * @brief Get the PMIC or charger name used by the device.
 */
const char *hw_get_power_controller_name();

/**
 * @brief Check whether the primary power-off action is currently available.
 * @return False only when ship mode is blocked by external USB power.
 */
bool hw_can_shutdown();

/*
* @brief Check if the hardware adapter is connected.
* @return True if the hardware adapter is connected, false otherwise.
*/
bool hw_adapter_is_connected();

/**
 * @brief Put the hardware into sleep mode.
 */
void hw_sleep();

/**
 * @brief Check if the OTG function is enabled.
 *
 * @return True if the OTG function is enabled, false otherwise.
 */
bool hw_get_otg_enable();

/**
 * @brief Enable or disable the OTG function.
 *
 * @param enable True to enable, false to disable.
 * @return True if the operation is successful, false otherwise.
 */
bool hw_set_otg(bool enable);

/**
 * @brief Check if the charging function is enabled.
 *
 * @return True if the charging function is enabled, false otherwise.
 */
bool hw_get_charge_enable();

/**
 * @brief Enable or disable the charger.
 *
 * @param enable True to enable, false to disable.
 */
void hw_set_charger(bool enable);

/**
 * @brief Get the current charger current.
 *
 * @return The current charger current in milliamperes.
 */
uint16_t hw_get_charger_current();


/**
 * @brief Get the monitor parameters.
 *
 * @param params A reference to a monitor_params_t structure where the monitor parameters will be stored.
 */
void hw_get_monitor_params(monitor_params_t &params);

/**
 * @brief Get normalized power monitor parameters with per-field validity.
 */
void hw_get_power_monitor_snapshot(power_monitor_snapshot_t &snapshot);

/**
 * @brief Register the IMU processing function.
 */
void hw_register_imu_process();

/**
 * @brief Unregister the IMU processing function.
 */
void hw_unregister_imu_process();

/**
 * @brief Get the IMU parameters.
 *
 * @param params A reference to an imu_params_t structure where the IMU parameters will be stored.
 */
void hw_get_imu_params(imu_params_t &params);

/**
 * @brief Get cached BMA accelerometer runtime data.
 */
void hw_get_bma_sensor_snapshot(bma_sensor_snapshot_t &snapshot);

/**
 * @brief Reset cached BMA counters and peak values.
 */
void hw_reset_bma_sensor_stats();

/**
 * @brief  Get the sensor type.
 * @retval see sensor_type_t
 */
sensor_type_t hw_get_sensor_type();


/**
 * @brief Set the callback function for keyboard reading.
 *
 * This function allows you to register a callback function that will be called
 * when there is a keyboard input event. The callback function should accept an
 * integer representing the input state and a reference to a character to store
 * the input character.
 *
 * @param read A pointer to the callback function.
 */
void hw_set_keyboard_read_callback(void(*read)(int state, char &c));

/**
 * @brief Provide hardware feedback.
 *
 * This function is used to trigger some form of hardware feedback, such as a
 * vibration or a sound, depending on the hardware implementation.
 */
void hw_feedback();

/** Get the persisted DRV2605 ROM waveform effect (1-117). */
uint8_t hw_get_haptic_effect();

/** Set and persist the DRV2605 ROM waveform effect (1-117). */
bool hw_set_haptic_effect(uint8_t effect);

/**
 * @brief Disable hardware feedback.
 */
void hw_disable_feedback();

/**
 * @brief Enable hardware feedback.
 */
void hw_enable_feedback();

/**
 * @brief Show the WiFi connection process bar on the UI.
 *
 * This function is responsible for displaying a progress bar on the user interface
 * to indicate the status of the WiFi connection process.
 */
void ui_show_wifi_process_bar();

/**
 * @brief Pop up a message box on the UI.
 *
 * This function displays a message box on the user interface with a given title
 * and message text.
 *
 * @param title_txt A pointer to the title text of the message box.
 * @param msg_txt A pointer to the message text to be displayed.
 */
void ui_msg_pop_up(const char *title_txt, const char *msg_txt);

/**
 * @brief Check if the application is currently in the menu.
 *
 * This function determines whether the application is currently in a menu state.
 *
 * @return True if the application is in the menu, false otherwise.
 */
bool isinMenu();

/**
 * @brief Get the user settings.
 *
 * This function retrieves the current user settings and stores them in the provided
 * user_setting_params_t structure.
 *
 * @param param A reference to a user_setting_params_t structure where the settings will be stored.
 */
void hw_get_user_setting(user_setting_params_t &param);

/**
 * @brief Set the user settings.
 *
 * This function updates the user settings with the values provided in the given
 * user_setting_params_t structure.
 *
 * @param param A reference to a user_setting_params_t structure containing the new settings.
 */
void hw_set_user_setting(user_setting_params_t &param);

bool hw_get_nav_auto_hide_enabled(void);
void hw_set_nav_auto_hide_enabled(bool enabled);
bool hw_get_keyboard_navigation_enabled(void);
void hw_set_keyboard_navigation_enabled(bool enabled);
bool hw_get_touch_guide_dismissed(void);
void hw_set_touch_guide_dismissed(bool dismissed);

/**
 * @brief Get the display timeout in milliseconds.
 *
 * This function returns the current display timeout value in milliseconds.
 *
 * @return The display timeout value in milliseconds.
 */
const uint32_t hw_get_disp_timeout_ms();

/**
 * @brief Enter the low - power loop mode.
 *
 * This function puts the hardware into a low - power loop state to conserve energy.
 */
void hw_low_power_loop();

/**
 * @brief Increase the display brightness level.
 *
 * This function increases the display brightness by the specified level.
 *
 * @param level The amount by which to increase the brightness.
 * @param async If true, the brightness change will be applied asynchronously; otherwise, it will be applied synchronously.
 */
void hw_inc_brightness(uint8_t level, bool async = false);

/**
 * @brief Decrease the display brightness level.
 *
 * This function decreases the display brightness by the specified level.
 *
 * @param level The amount by which to decrease the brightness.
 * @param async If true, the brightness change will be applied asynchronously; otherwise, it will be applied synchronously.
 */
void hw_dec_brightness(uint8_t level, bool async = false);

/**
 * @brief Set the CPU frequency.
 *
 * This function sets the CPU frequency to the specified value in megahertz.
 *
 * @param mhz The desired CPU frequency in megahertz.
 */
void hw_set_cpu_freq(uint32_t mhz);

/**
 * @brief Disable all input devices.
 *
 * This function disables all input devices, such as the microphone and touchpad.
 */
void hw_disable_input_devices();

/**
 * @brief Enable all input devices.
 *
 * This function enables all input devices, such as the microphone and touchpad.
 */
void hw_enable_input_devices();

/**
 * @brief Enable the keyboard.
 *
 * This function enables the keyboard input.
 */
void hw_enable_keyboard();

/**
 * @brief Disable the keyboard.
 *
 * This function disables the keyboard input.
 */
void hw_disable_keyboard();

/**
 * @brief Flush the keyboard input buffer.
 *
 * This function clears the keyboard input buffer.
 */
void hw_flush_keyboard();

/**
 * @brief Check if the keyboard is available.
 *
 * This function checks if the keyboard is available for input.
 *
 * @return True if the keyboard is available, false otherwise.
 */
bool hw_has_keyboard();

/**
 * @brief Check if the rotary encoder is available.
 *
 * @return True if the rotary encoder is available, false otherwise.
 */
bool hw_has_encoder();

/**
 * @brief Set rotary encoder step divider.
 *
 * Larger values reduce sensitivity because more raw encoder counts are required
 * before one LVGL encoder step is emitted.
 *
 * @param divider Number of raw encoder counts per output step.
 */
void hw_set_rotary_step_divider(uint8_t divider);

/**
 * @brief Save rotary encoder step divider to non-volatile storage.
 *
 * The value is also applied immediately.
 *
 * @param divider Number of raw encoder counts per output step.
 */
void hw_save_rotary_step_divider(uint8_t divider);

/**
 * @brief Get rotary encoder step divider.
 *
 * @return Number of raw encoder counts per output step.
 */
uint8_t hw_get_rotary_step_divider();

/**
 * @brief Get the minimum supported rotary encoder step divider.
 *
 * @return Minimum supported step divider.
 */
uint8_t hw_get_rotary_step_divider_min();

/**
 * @brief Get the maximum supported rotary encoder step divider.
 *
 * @return Maximum supported step divider.
 */
uint8_t hw_get_rotary_step_divider_max();

/**
 * @brief Check if the board supports audio jack mode switching.
 *
 * @return True when CTIA/TRRS switching is available.
 */
bool hw_has_audio_jack_mode_setting();

/**
 * @brief Apply and persist the audio jack mode.
 *
 * CTIA drives EXPANDS_AUDIO_JACK_SEL low. TRRS drives it high.
 *
 * @param mode One of audio_jack_mode_t.
 */
void hw_set_audio_jack_mode(uint8_t mode);

/**
 * @brief Get the current persisted audio jack mode.
 *
 * @return One of audio_jack_mode_t.
 */
uint8_t hw_get_audio_jack_mode();

/**
 * @brief Check if the indicator LED is available.
 * @retval True if the indicator LED is available, false otherwise.
 */
bool hw_has_indicator_led();

/**
 * @brief  Get the firmware version of the expansion module.
 * @note   This function retrieves the firmware version from the expansion module.
 * @retval The firmware version as a string.
 */
const char *hw_get_expands_fw_version();

/**
 * @brief Check if the OTG function is available.
 *
 * This function checks if the OTG (On-The-Go) function is available.
 *
 * @return True if the OTG function is available, false otherwise.
 */
bool hw_has_otg_function();

/**
 * @brief Get the minimum display brightness level.
 *
 * This function retrieves the minimum display brightness level.
 *
 * @return The minimum display brightness level.
 */
uint8_t hw_get_disp_min_brightness();

/**
 * @brief Get the maximum display brightness level.
 *
 * This function retrieves the maximum display brightness level.
 *
 * @return The maximum display brightness level.
 */
uint16_t hw_get_disp_max_brightness();

/**
 * @brief Get the minimum charging current level.
 *
 * This function retrieves the minimum charging current level.
 *
 * @return The minimum charging current level.
 */
uint8_t hw_get_min_charge_current();

/**
 * @brief Get the maximum charging current level.
 *
 * This function retrieves the maximum charging current level.
 *
 * @return The maximum charging current level.
 */
uint16_t hw_get_max_charge_current();

/**
 * @brief Get the number of charging levels.
 *
 * This function retrieves the number of charging levels available.
 *
 * @return The number of charging levels.
 */
uint8_t hw_get_charge_level_nums();

/**
 * @brief Get the charging steps.
 *
 * This function retrieves the charging steps.
 *
 * @return The charging steps.
 */
uint8_t hw_get_charge_steps();

/**
 * @brief Set the charger current level.
 *
 * This function sets the charger current level to the specified level.
 *
 * @param level The desired charger current level.
 * @return The actual charger current level set.
 */
uint16_t hw_set_charger_current_level(uint8_t level);

/**
 * @brief Get the current charger current level.
 *
 * This function retrieves the current charger current level.
 *
 * @return The current charger current level.
 */
uint8_t hw_get_charger_current_level();

/**
 * @brief Print memory information.
 *
 * This function prints the current memory usage information to the console.
 */
void hw_print_mem_info();

/**
 * @brief Get the NRF24 parameters.
 *
 * This function retrieves the NRF24 radio parameters.
 *
 * @param params The radio parameters structure to fill.
 */
void hw_get_nrf24_params(radio_params_t &params);

/**
 * @brief Set the NRF24 parameters.
 *
 * This function sets the NRF24 radio parameters.
 *
 * @param params The radio parameters structure containing the new settings.
 * @return The result of the operation (0 for success, negative for error).
 */
int16_t hw_set_nrf24_params(radio_params_t &params);

/**
 * @brief Set the NRF24 5-byte pipe address used for TX and RX.
 *
 * @param address Pointer to a 5-byte address.
 * @param length Address buffer length. Values shorter than 5 are ignored.
 */
void hw_set_nrf24_address(const uint8_t *address, size_t length);

/**
 * @brief Get the NRF24 5-byte pipe address used for TX and RX.
 *
 * @param address Destination buffer.
 * @param length Destination buffer length. Values shorter than 5 are ignored.
 */
void hw_get_nrf24_address(uint8_t *address, size_t length);

/**
 * @brief Set the NRF24 listening mode.
 *
 * This function sets the NRF24 radio to listening mode.
 */
void hw_set_nrf24_listening();

/**
 * @brief Set the NRF24 transmission mode.
 *
 * This function sets the NRF24 radio to transmission mode.
 *
 * @param params The transmission parameters to use.
 * @param continuous If true, the transmission will be continuous.
 * @return True if the operation was successful, false otherwise.
 */
bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous = true);

/**
 * @brief Check whether a non-blocking NRF24 transmission is complete.
 *
 * @param state The final transmit state when the function returns true.
 * @return true when TX has completed or failed, false when it is still running.
 */
bool hw_get_nrf24_tx_done(int16_t &state);

/**
 * @brief Get the NRF24 reception parameters.
 *
 * This function retrieves the NRF24 radio reception parameters.
 *
 * @param params The reception parameters structure to fill.
 */
void hw_get_nrf24_rx(radio_rx_params_t &params);

/**
 * @brief Check if NRF24 is available.
 *
 * This function checks if the NRF24 radio is available.
 *
 * @return True if the NRF24 radio is available, false otherwise.
 */
bool hw_has_nrf24();

/**
 * @brief Clear the NRF24 flag.
 *
 * This function clears the NRF24 radio flag.
 */
void hw_clear_nrf24_flag();

/**
 * @brief Get the radio frequency list.
 *
 * This function retrieves the list of available radio frequencies.
 *
 * @return A pointer to the frequency list string.
 */
const char *radio_get_freq_list();

/**
 * @brief Get the radio frequency from the index.
 *
 * This function retrieves the radio frequency corresponding to the given index.
 *
 * @param index The index of the desired frequency.
 * @return The radio frequency at the specified index.
 */
float radio_get_freq_from_index(uint8_t index);

/**
 * @brief Get the radio bandwidth from the index.
 *
 * This function retrieves the radio bandwidth corresponding to the given index.
 *
 * @param index The index of the desired bandwidth.
 * @return The radio bandwidth at the specified index.
 */
float radio_get_bandwidth_from_index(uint8_t index);

/**
 * @brief Get the radio bandwidth list.
 *
 * This function retrieves the list of available radio bandwidths.
 *
 * @param high_freq If true, retrieves the high frequency bandwidths.
 * @return A pointer to the bandwidth list string.
 */
const char *radio_get_bandwidth_list(bool high_freq = false);

/**
 * @brief Get the radio transmission power list.
 *
 * This function retrieves the list of available radio transmission power levels.
 *
 * @param high_freq If true, retrieves the high frequency power levels.
 * @return A pointer to the transmission power list string.
 */
const char *radio_get_tx_power_list(bool high_freq = false);

/**
 * @brief Get the radio transmission power from the index.
 *
 * This function retrieves the radio transmission power corresponding to the given index.
 *
 * @param index The index of the desired transmission power.
 * @return The radio transmission power at the specified index.
 */
float radio_get_tx_power_from_index(uint8_t index);

/**
 * @brief Transmit data via radio.
 *
 * This function transmits the given data using the radio.
 *
 * @param data A pointer to the data to be transmitted.
 * @param length The length of the data to be transmitted.
 * @return True if the transmission was successful, false otherwise.
 */
bool radio_transmit(const uint8_t *data, size_t length);
int16_t radio_get_last_transmit_state();

/**
 * @brief Get the radio frequency length.
 *
 * This function retrieves the length of the radio frequency list.
 *
 * @return The length of the frequency list.
 */
uint16_t radio_get_freq_length();

/**
 * @brief Get the radio bandwidth length.
 *
 * This function retrieves the length of the radio bandwidth list.
 *
 * @return The length of the bandwidth list.
 */
uint16_t radio_get_bandwidth_length();

/**
 * @brief Get the radio transmission power length.
 *
 * This function retrieves the length of the radio transmission power list.
 *
 * @return The length of the transmission power list.
 */
uint16_t radio_get_tx_power_length();

#if defined(USING_IR_REMOTE)
/**
 * @brief Set the remote control code.
 *
 * This function sets the remote control code for the IR transmitter.
 *
 * @param nec_code The NEC code to set.
 */
void hw_set_remote_code(uint32_t nec_code);
#endif


/**
 * @brief Select the IR function (send/receive).
 *
 * This function selects whether to enable sending or receiving for the IR function.
 *
 * @param enableSend True to enable sending, false to enable receiving.
 */
void hw_ir_function_select(bool enableSend);

/**
 * @brief Get the remote control code.
 *
 * This function retrieves the remote control code received by the IR receiver.
 *
 * @param result A reference to a uint64_t variable where the received code will be stored.
 */
void hw_get_remote_code(uint64_t &result);

enum Si4735Mode {
    FM,
    LSB,
    USB,
    AM,
};

/**
 * @brief Set the power state of the Si4735.
 *
 * This function sets the power state of the Si4735.
 *
 * @param powerOn True to turn on the power, false to turn it off.
 */
void hw_si4735_set_power(bool powerOn);

/**
 * @brief Set the volume of the Si4735.
 *
 * This function sets the volume of the Si4735.
 *
 * @param vol The volume level to set (0-63).
 */
void hw_si4735_set_volume(uint8_t vol);

/**
 * @brief Get the volume of the Si4735.
 *
 * This function retrieves the current volume level of the Si4735.
 *
 * @return The current volume level (0-63).
 */
uint8_t hw_si4735_get_volume(void);

/**
 * @brief Get the RSSI of the Si4735.
 *
 * This function retrieves the current RSSI (Received Signal Strength Indicator) level of the Si4735.
 *
 * @return The current RSSI level.
 */
uint8_t hw_si4735_get_rssi();

/**
 * @brief Get the SNR of the Si4735.
 *
 * This function retrieves the current Signal-to-Noise Ratio.
 *
 * @return The current SNR in dB.
 */
uint8_t hw_si4735_get_snr();

/**
 * @brief Get the frequency of the Si4735.
 *
 * This function retrieves the current frequency of the Si4735.
 *
 * @return The current frequency.
 */
uint16_t hw_si4735_get_freq();

/**
 * @brief Check if the current mode is FM.
 *
 * This function checks if the Si4735 is currently in FM mode.
 *
 * @return True if in FM mode, false otherwise.
 */
bool hw_si4735_is_fm();

/**
 * @brief Set the mode of the Si4735.
 *
 * This function sets the mode of the Si4735.
 *
 * @param bandType The mode to set.
 */
void hw_si4735_set_mode(Si4735Mode bandType);

/**
 * @brief Update the Si4735 steps.
 *
 * This function updates the steps of the Si4735.
 *
 * @return The number of steps updated.
 */
uint16_t hw_si4735_update_steps();

/**
 * @brief Set the AGC (Automatic Gain Control) state.
 *
 * This function sets the AGC state of the Si4735.
 *
 * @param on True to enable AGC, false to disable it.
 */
void hw_si4735_set_agc(bool on);

/**
 * @brief Set the BFO (Beat Frequency Oscillator) state.
 *
 * This function sets the BFO state of the Si4735.
 *
 * @param on True to enable BFO, false to disable it.
 */
void hw_si4735_set_bfo(bool on);

/**
 * @brief Set the frequency up.
 *
 * This function increases the frequency of the Si4735.
 */
void hw_si4735_set_freq_up();

/**
 * @brief Set the frequency down.
 *
 * This function decreases the frequency of the Si4735.
 */
void hw_si4735_set_freq_down();

/**
 * @brief Set the band up.
 *
 * This function increases the band of the Si4735.
 */
void hw_si4735_band_up();

/*
* @brief Set the seek up.
*
* This function increases the seek frequency of the Si4735.
*/
void hw_si4735_seek_up();

/*
* @brief Set the seek down.
*
* This function decreases the seek frequency of the Si4735.
*/
void hw_si4735_seek_down();

/**
 * @brief Set the band down.
 *
 * This function decreases the band of the Si4735.
 */
void hw_si4735_band_down();

/**
 * @brief Get the current mode of the Si4735.
 *
 * This function retrieves the current mode of the Si4735.
 *
 * @return The current mode.
 */
Si4735Mode hw_si4735_get_mode();

/**
 * @brief Get the current band name of the Si4735.
 *
 * This function retrieves the current band name of the Si4735.
 *
 * @return The current band name.
 */
const char *hw_si4735_get_band_name();

/**
 * @brief Get the current step of the Si4735.
 *
 * This function retrieves the current step of the Si4735.
 *
 * @return The current step.
 */
uint16_t hw_si4735_get_current_step();


/**
 * @brief  Set the frequency of the Si4735.
 * @note   This function sets the frequency of the Si4735.
 * @param  freq: The frequency to set (in kHz).
 * @retval None
 */
void hw_si4735_set_freq(uint16_t freq);


/**
 * @brief Enable or disable the magnetometer.
 *
 * This function enables or disables the magnetometer.
 *
 * @param enable True to enable the magnetometer, false to disable it.
 */
void hw_mag_enable(bool enable);

/**
 * @brief Get the current magnetic field strength.
 *
 * This function retrieves the current magnetic field strength from the magnetometer.
 *
 * @return The current magnetic field strength.
 */
float hw_mag_get_polar();

/**
 * @brief Read magnetometer raw, field strength, and heading data.
 *
 * @param data A reference to a mag_data_t structure where data will be stored.
 * @return true if a new sample was read.
 */
bool hw_mag_read(mag_data_t &data);

/**
 * @brief Apply a magnetometer calibration offset for the current runtime.
 *
 * This does not persist the offset.
 *
 * @param cal Calibration offset.
 */
void hw_mag_apply_calibration(const mag_calibration_t &cal);

/**
 * @brief Save and apply magnetometer calibration to NVS.
 *
 * @param cal Calibration offset.
 * @return true if saved.
 */
bool hw_mag_save_calibration(const mag_calibration_t &cal);

/**
 * @brief Load and apply magnetometer calibration from NVS.
 *
 * @param cal Optional output calibration pointer.
 * @return true if a valid calibration was loaded.
 */
bool hw_mag_load_calibration(mag_calibration_t *cal = nullptr);

/**
 * @brief Get the active magnetometer calibration.
 *
 * @param cal Active calibration.
 */
void hw_mag_get_calibration(mag_calibration_t &cal);

/**
 * @brief Clear saved magnetometer calibration and reset runtime offset.
 */
void hw_mag_clear_calibration();


/**
 * @brief  Get the current environmental data.
 * @note   This function retrieves the current temperature, humidity, pressure, and altitude from the BME sensor.
 * @param  &temp: A reference to a float where the temperature will be stored.
 * @param  &humi: A reference to a float where the humidity will be stored.
 * @param  &press: A reference to a float where the pressure will be stored.
 * @param  &alt: A reference to a float where the altitude will be stored.
 * @retval None
 */
void hw_bme_get_data(float &temp, float &humi, float &press, float &alt);

/**
 * @brief Set the trackball callback.
 *
 * This function sets the callback function for trackball events.
 *
 * @param callback The callback function to set.
 */
void hw_set_trackball_callback(TrackballEventCallback callback);

/**
 * @brief Check whether the board's pointer device was initialized.
 *
 * Directional GPIO trackballs and continuous XY sensors such as PAW350 are
 * exposed through the same pointer interface.
 *
 * @return True when pointer movement events are available.
 */
bool hw_pointer_available();

/**
 * @brief Set a normalized pointer-button callback.
 *
 * The callback receives one of HW_POINTER_BUTTON_LEFT,
 * HW_POINTER_BUTTON_RIGHT, or HW_POINTER_BUTTON_MIDDLE. Board-specific button
 * layouts are mapped by the HAL.
 *
 * @param callback Callback to register, or NULL to unregister it.
 */
void hw_set_pointer_button_callback(PointerButtonEventCallback callback);

/**
 * @brief Set the button callback.
 *
 * This function sets the callback function for button events.
 *
 * @param callback The callback function to set.
 */
void hw_set_button_callback(ButtonEventCallback callback);

/**
 * @brief Check whether board-level button event monitoring is available.
 *
 * @return True when the device can report BUTTON_EVENT events.
 */
bool hw_has_button_monitor();

/**
 * @brief Check whether PMU power-key event monitoring is available.
 *
 * @return True when the device can report PMU power-key events.
 */
bool hw_has_pmu_button_monitor();

/**
 * @brief Get the latest board-level button event text.
 *
 * @param buffer Destination text buffer.
 * @param size Destination buffer size.
 */
void hw_get_button_monitor_status(char *buffer, size_t size);

/**
 * @brief Get the latest PMU power-key event text.
 *
 * @param buffer Destination text buffer.
 * @param size Destination buffer size.
 */
void hw_get_pmu_button_monitor_status(char *buffer, size_t size);


/**
 * @brief Start NFC discovery.
 *
 * This function starts NFC discovery.
 *
 * @return True if NFC discovery is successfully started, false otherwise.
 */
bool hw_start_nfc_discovery();

#if defined(ARDUINO) && defined(USING_ST25R3916)
/**
 * @brief Start NFC card emulation.
 *
 * This function starts NFC card emulation with an NDEF template.
 *
 * @param config NFC emulation configuration.
 * @return True if NFC emulation is successfully started, false otherwise.
 */
bool hw_start_nfc_emulation(const LilyGoNfcEmulationConfig &config);
#endif

/**
 * @brief Run NFC service loop.
 *
 * This function should be called from the main loop while NFC is available.
 */
void hw_loop_nfc();

/**
 * @brief Stop NFC service.
 *
 * This function stops reader discovery or card emulation.
 */
void hw_stop_nfc();

/**
 * @brief Stop NFC discovery.
 *
 * This function stops NFC discovery.
 */
void hw_stop_nfc_discovery();

/**
 * @brief Get the device power tips string.
 *
 * This function retrieves the device power tips string.
 *
 * @return The device power tips string.
 */
const char *hw_get_device_power_tips_string();


/**
 * @brief Check if the screen is small.
 *
 * This function checks if the screen is small (e.g., 240x240 or smaller).
 *
 * @return True if the screen is small, false otherwise.
 */
bool is_screen_small();

/**
 * @brief Get the firmware hash string.
 *
 * This function retrieves the firmware hash string.
 *
 * @return The firmware hash string.
 */
const char *hw_get_firmware_hash_string();

/**
 * @brief Get the chip ID string.
 *
 * This function retrieves the chip ID string.
 *
 * @return The chip ID string.
 */
const char *hw_get_chip_id_string();

/**
* @brief Sets the RF switch to either a USB interface or the built-in antenna.
* * This function sets the RF switch to either a USB LoRa interface or the built-in LoRa antenna based on the 'to_usb' parameter.
* * @param to_usb If True, the RF switch is set to a USB LoRa interface; if false, it is set to the built-in LoRa antenna.
*/
void hw_set_usb_rf_switch(bool to_usb);


/*
 * Factory capability map.
 *
 * Upstream Arduino-ESP32 variants are the source of truth for released boards.
 * This layer adds factory-specific compatibility aliases and derives UI
 * EXCLUDE_* switches from explicit capabilities.
 */

#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280) || \
    defined(ARDUINO_LILYGO_LORA_CC1101) || defined(ARDUINO_LILYGO_LORA_LR1121) || \
    defined(ARDUINO_LILYGO_LORA_SI4432)
#define FACTORY_HAS_RADIO_MODULE       1
#else
#define FACTORY_HAS_RADIO_MODULE       0
#endif

#if defined(ARDUINO_T_LORA_PAGER)
#define FLOAT_BUTTON_WIDTH             40
#define FLOAT_BUTTON_HEIGHT            40
#define MAIN_FONT                      &lv_font_montserrat_16
#define NFC_TIPS_STRING                "Place the NFC card close to the center of the arrow on the back. It will vibrate when the card is detected; otherwise, it will not display anything if it cannot be resolved."
#define DEVICE_KEYBOARD_TYPE           KEYBOARD_TYPE_1
#define DISP_BACKLIGHT_DELAY_MS        50

#elif defined(ARDUINO_T_WATCH_S3_ULTRA)
#define FLOAT_BUTTON_WIDTH             60
#define FLOAT_BUTTON_HEIGHT            60
#define MAIN_FONT                      &lv_font_montserrat_22
#define NFC_TIPS_STRING                "Hold the NFC card close to the front of the screen. It will vibrate when the card is detected; otherwise, it will not display anything if it cannot be resolved."
#define DISP_BACKLIGHT_DELAY_MS        5

#elif defined(ARDUINO_T_WATCH_S3)
#define FLOAT_BUTTON_WIDTH             40
#define FLOAT_BUTTON_HEIGHT            40
#define MAIN_FONT                      &lv_font_montserrat_12
#define NFC_TIPS_STRING                "No NFC devices"
#define DISP_BACKLIGHT_DELAY_MS        5

#elif defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3)
#define FLOAT_BUTTON_WIDTH             40
#define FLOAT_BUTTON_HEIGHT            40
#define MAIN_FONT                      &lv_font_montserrat_12
#define NFC_TIPS_STRING                "No NFC devices"
#define DISP_BACKLIGHT_DELAY_MS        5

#elif defined(ARDUINO_T_DECK_V2)
#define FLOAT_BUTTON_WIDTH             40
#define FLOAT_BUTTON_HEIGHT            40
#define MAIN_FONT                      &lv_font_montserrat_18
#define NFC_TIPS_STRING                "No NFC devices"
#define DEVICE_KEYBOARD_TYPE           KEYBOARD_TYPE_2
#define DISP_BACKLIGHT_DELAY_MS        50
#define HAS_EFFECT_BUTTONS
#endif

#ifndef FLOAT_BUTTON_WIDTH
#define FLOAT_BUTTON_WIDTH             40
#endif
#ifndef FLOAT_BUTTON_HEIGHT
#define FLOAT_BUTTON_HEIGHT            40
#endif
#ifndef MAIN_FONT
#define MAIN_FONT                      &lv_font_montserrat_16
#endif
#ifndef NFC_TIPS_STRING
#define NFC_TIPS_STRING                "No NFC devices"
#endif
#ifndef DEVICE_KEYBOARD_TYPE
#define DEVICE_KEYBOARD_TYPE           KEYBOARD_TYPE_NONE
#endif
#ifndef DISP_BACKLIGHT_DELAY_MS
#define DISP_BACKLIGHT_DELAY_MS        50
#endif

#ifndef FACTORY_HAS_AUDIO_CODEC
#if defined(USING_AUDIO_CODEC) || defined(USING_ES7210)
#define FACTORY_HAS_AUDIO_CODEC        1
#else
#define FACTORY_HAS_AUDIO_CODEC        0
#endif
#endif

#ifndef FACTORY_HAS_AUDIO_OUT
#if defined(USING_AUDIO_CODEC) || defined(USING_PCM_AMPLIFIER) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK)
#define FACTORY_HAS_AUDIO_OUT          1
#else
#define FACTORY_HAS_AUDIO_OUT          0
#endif
#endif

#ifndef FACTORY_HAS_AUDIO_IN
#if defined(USING_AUDIO_CODEC) || defined(USING_PDM_MICROPHONE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK)
#define FACTORY_HAS_AUDIO_IN           1
#else
#define FACTORY_HAS_AUDIO_IN           0
#endif
#endif

#ifndef FACTORY_HAS_MICROPHONE
#define FACTORY_HAS_MICROPHONE         FACTORY_HAS_AUDIO_IN
#endif

#ifndef FACTORY_HAS_RTC
#if defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || \
    defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_RTC                1
#else
#define FACTORY_HAS_RTC                0
#endif
#endif

#ifndef FACTORY_HAS_POWER_MANAGE
#if defined(USING_PMU_MANAGE) || defined(USING_PPM_MANAGE) || defined(ARDUINO_TWATCH_BASE) || \
    defined(ARDUINO_TWATCH_2020_V3) || defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || \
    defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_POWER_MANAGE       1
#else
#define FACTORY_HAS_POWER_MANAGE       0
#endif
#endif

#ifndef FACTORY_HAS_BQ_GAUGE
#if defined(ARDUINO_T_LORA_PAGER) || defined(GAUGE_CHIP_BQ27220) || \
    (defined(USING_BQ_GAUGE) && !defined(ARDUINO_T_DECK_V2))
#define FACTORY_HAS_BQ_GAUGE           1
#else
#define FACTORY_HAS_BQ_GAUGE           0
#endif
#endif

#ifndef FACTORY_HAS_AXP2602_GUAGE
#if defined(USING_AXP2602_GUAGE) || defined(T_DECK_V2_REV06) || defined(T_DECK_V2_REV07)
#define FACTORY_HAS_AXP2602_GUAGE      1
#else
#define FACTORY_HAS_AXP2602_GUAGE      0
#endif
#endif

#ifndef FACTORY_HAS_GAUGE
#if FACTORY_HAS_BQ_GAUGE || FACTORY_HAS_AXP2602_GUAGE
#define FACTORY_HAS_GAUGE              1
#else
#define FACTORY_HAS_GAUGE              0
#endif
#endif

#ifndef FACTORY_HAS_TOUCH_INPUT
#if defined(USING_TOUCHPAD) || defined(USING_INPUT_DEV_TOUCHPAD) || defined(HAS_TOUCHSCREEN) || \
    defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || defined(ARDUINO_T_WATCH_S3) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_TOUCH_INPUT        1
#else
#define FACTORY_HAS_TOUCH_INPUT        0
#endif
#endif

#ifndef FACTORY_HAS_TOUCHSCREEN
#if defined(HAS_TOUCHSCREEN)
#define FACTORY_HAS_TOUCHSCREEN        1
#else
#define FACTORY_HAS_TOUCHSCREEN        0
#endif
#endif

#ifndef FACTORY_HAS_KEYBOARD
#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD) || \
    defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_KEYBOARD           1
#else
#define FACTORY_HAS_KEYBOARD           0
#endif
#endif

#ifndef FACTORY_HAS_ROTARY
#if defined(USING_INPUT_DEV_ROTARY) || defined(ARDUINO_T_LORA_PAGER)
#define FACTORY_HAS_ROTARY             1
#else
#define FACTORY_HAS_ROTARY             0
#endif
#endif

#ifndef FACTORY_HAS_TRACKBALL
#if defined(USING_TRACKBALL) || defined(USING_TRACKBALL_V2) || defined(USING_TDECK_TRACKBALL)
#define FACTORY_HAS_TRACKBALL          1
#else
#define FACTORY_HAS_TRACKBALL          0
#endif
#endif

#ifndef FACTORY_HAS_SD
#if defined(HAS_SD_CARD_SOCKET) || defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_T_WATCH_S3_ULTRA) || \
    defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_SD                 1
#else
#define FACTORY_HAS_SD                 0
#endif
#endif

#ifndef FACTORY_HAS_GPS
#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK)
#define FACTORY_HAS_GPS                1
#else
#define FACTORY_HAS_GPS                0
#endif
#endif

#ifndef FACTORY_GPS_RUNTIME_PROBE
#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_DECK)
#define FACTORY_GPS_RUNTIME_PROBE      1
#else
#define FACTORY_GPS_RUNTIME_PROBE      0
#endif
#endif

#ifndef FACTORY_HAS_RADIO
#if FACTORY_HAS_RADIO_MODULE
#define FACTORY_HAS_RADIO              1
#else
#define FACTORY_HAS_RADIO              0
#endif
#endif

#ifndef FACTORY_HAS_LORAWAN
#if (defined(ARDUINO_LILYGO_LORA_LR1121) && !defined(EXCLUDE_LORAWAN_LR1121)) || \
    (defined(ARDUINO_LILYGO_LORA_SX1262) && !defined(EXCLUDE_LORAWAN_SX1262))
#define FACTORY_HAS_LORAWAN            1
#else
#define FACTORY_HAS_LORAWAN            0
#endif
#endif

#ifndef FACTORY_HAS_CC1101_TOOL
#if defined(ARDUINO_LILYGO_LORA_CC1101)
#define FACTORY_HAS_CC1101_TOOL        1
#else
#define FACTORY_HAS_CC1101_TOOL        0
#endif
#endif

#ifndef FACTORY_HAS_NRF24
#if defined(ARDUINO_T_LORA_PAGER) && !defined(RADIOLIB_EXCLUDE_NRF24)
#define FACTORY_HAS_NRF24              1
#else
#define FACTORY_HAS_NRF24              0
#endif
#endif

#ifndef FACTORY_HAS_NFC
#if defined(USING_ST25R3916) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER)
#define FACTORY_HAS_NFC                1
#else
#define FACTORY_HAS_NFC                0
#endif
#endif

#ifndef FACTORY_HAS_MOTION_SENSOR
#if defined(USING_BHI260_SENSOR) || defined(USING_BMA423_SENSOR) || defined(USING_QMI8658_SENSOR) || \
    defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || defined(ARDUINO_T_WATCH_S3) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_MOTION_SENSOR      1
#else
#define FACTORY_HAS_MOTION_SENSOR      0
#endif
#endif

#ifndef FACTORY_HAS_BHI260
#if defined(USING_BHI260_SENSOR) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_BHI260             1
#else
#define FACTORY_HAS_BHI260             0
#endif
#endif

#ifndef FACTORY_HAS_BMA423
#if defined(USING_BMA423_SENSOR) || defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3)
#define FACTORY_HAS_BMA423             1
#else
#define FACTORY_HAS_BMA423             0
#endif
#endif

#ifndef FACTORY_HAS_QMI8658
#if defined(USING_QMI8658_SENSOR)
#define FACTORY_HAS_QMI8658            1
#else
#define FACTORY_HAS_QMI8658            0
#endif
#endif

#ifndef FACTORY_HAS_ENV_SENSOR
#if defined(USING_BME280)
#define FACTORY_HAS_ENV_SENSOR         1
#else
#define FACTORY_HAS_ENV_SENSOR         0
#endif
#endif

#ifndef FACTORY_HAS_COMPASS
#if defined(USING_MAG_COMPASS) || defined(USING_MAG_QMC5883) || defined(USING_MAG_QMC6309)
#define FACTORY_HAS_COMPASS            1
#else
#define FACTORY_HAS_COMPASS            0
#endif
#endif

#ifndef FACTORY_HAS_I2C
#if defined(HAS_I2C_INTERFACE) || defined(SDA) || defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
    defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK)
#define FACTORY_HAS_I2C                1
#else
#define FACTORY_HAS_I2C                0
#endif
#endif

#ifndef FACTORY_HAS_SPI
#if defined(HAS_SPI_INTERFACE) || FACTORY_HAS_RADIO || FACTORY_HAS_SD || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_SPI                1
#else
#define FACTORY_HAS_SPI                0
#endif
#endif

#ifndef FACTORY_HAS_IR_TX
#if defined(USING_IR_REMOTE) || defined(ARDUINO_TWATCH_2020_V3) || defined(ARDUINO_T_WATCH_S3) || \
    defined(USING_IR_TRANSMITTER)
#define FACTORY_HAS_IR_TX              1
#else
#define FACTORY_HAS_IR_TX              0
#endif
#endif

#ifndef FACTORY_HAS_IR_RX
#if defined(USING_IR_RECEIVER)
#define FACTORY_HAS_IR_RX              1
#else
#define FACTORY_HAS_IR_RX              0
#endif
#endif

#ifndef FACTORY_HAS_SI4735
#if defined(USING_SI473X_RADIO)
#define FACTORY_HAS_SI4735             1
#else
#define FACTORY_HAS_SI4735             0
#endif
#endif

#ifndef FACTORY_HAS_EXPANDER
#if defined(USING_XL9555_EXPANDS) || defined(USING_MCU_EXPANDS) || defined(USING_TCA8418_EXPANDS)
#define FACTORY_HAS_EXPANDER           1
#else
#define FACTORY_HAS_EXPANDER           0
#endif
#endif

#ifndef FACTORY_HAS_USB_RF_SWITCH
#if defined(HAS_USB_RF_SWITCH) || defined(ARDUINO_T_WATCH_S3_ULTRA)
#define FACTORY_HAS_USB_RF_SWITCH      1
#else
#define FACTORY_HAS_USB_RF_SWITCH      0
#endif
#endif

#ifndef FACTORY_HAS_LED_INDICATOR
#if defined(USING_LED_INDICATOR)
#define FACTORY_HAS_LED_INDICATOR      1
#else
#define FACTORY_HAS_LED_INDICATOR      0
#endif
#endif

#ifndef FACTORY_HAS_USB_MSC
#if FACTORY_HAS_SD && (defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || \
    defined(ARDUINO_T_DECK_V2) || defined(ARDUINO_T_DECK))
#define FACTORY_HAS_USB_MSC            1
#else
#define FACTORY_HAS_USB_MSC            0
#endif
#endif

#ifndef FACTORY_HAS_HAPTIC_DRV
#if defined(USING_DRV2605) || defined(ARDUINO_T_WATCH_S3) || \
    defined(ARDUINO_T_WATCH_S3_ULTRA) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_HAPTIC_DRV         1
#else
#define FACTORY_HAS_HAPTIC_DRV         0
#endif
#endif

#ifndef FACTORY_HAS_NES
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)
#define FACTORY_HAS_NES                1
#else
#define FACTORY_HAS_NES                0
#endif
#endif

#if FACTORY_HAS_TOUCH_INPUT
#ifndef USING_TOUCHPAD
#define USING_TOUCHPAD
#endif
#ifndef USING_INPUT_DEV_TOUCHPAD
#define USING_INPUT_DEV_TOUCHPAD
#endif
#endif

#if FACTORY_HAS_BHI260
#ifndef USING_BHI260_SENSOR
#define USING_BHI260_SENSOR
#endif
#endif

#if FACTORY_HAS_BMA423
#ifndef USING_BMA423_SENSOR
#define USING_BMA423_SENSOR
#endif
#endif

#if FACTORY_HAS_NFC
#ifndef USING_ST25R3916
#define USING_ST25R3916
#endif
#endif

#if FACTORY_HAS_NRF24
#ifndef USING_EXTERN_NRF2401
#define USING_EXTERN_NRF2401
#endif
#endif

#if FACTORY_HAS_SD
#ifndef HAS_SD_CARD_SOCKET
#define HAS_SD_CARD_SOCKET
#endif
#endif

#if FACTORY_HAS_I2C
#ifndef HAS_I2C_INTERFACE
#define HAS_I2C_INTERFACE
#endif
#endif

#if FACTORY_HAS_SPI
#ifndef HAS_SPI_INTERFACE
#define HAS_SPI_INTERFACE
#endif
#endif

#if FACTORY_HAS_IR_TX
#ifndef USING_IR_REMOTE
#define USING_IR_REMOTE
#endif
#endif

#if FACTORY_HAS_IR_RX
#ifndef USING_IR_RECEIVER
#define USING_IR_RECEIVER
#endif
#endif

#if FACTORY_HAS_IR_TX && FACTORY_HAS_IR_RX
#ifndef HAS_IR_RX_TX
#define HAS_IR_RX_TX
#endif
#endif

#if FACTORY_HAS_BQ_GAUGE
#ifndef USING_BQ_GAUGE
#define USING_BQ_GAUGE
#endif
#endif

#if FACTORY_HAS_AXP2602_GUAGE
#ifndef USING_AXP2602_GUAGE
#define USING_AXP2602_GUAGE
#endif
#endif

#if defined(ARDUINO_T_DECK_V2) && defined(GAUGE_CHIP_AXP2602)
#error "T-Deck V2 uses USING_AXP2602_GUAGE; do not define GAUGE_CHIP_AXP2602."
#endif

#if defined(ARDUINO_T_DECK_V2) && defined(USING_BQ_GAUGE) && defined(USING_AXP2602_GUAGE)
#error "T-Deck V2 uses an AXP2602 gauge; do not define USING_BQ_GAUGE for this board."
#endif

#if FACTORY_HAS_COMPASS
#ifndef USING_MAG_COMPASS
#define USING_MAG_COMPASS
#endif
#endif

#if FACTORY_HAS_USB_RF_SWITCH
#ifndef HAS_USB_RF_SWITCH
#define HAS_USB_RF_SWITCH
#endif
#endif

#if !FACTORY_HAS_RADIO
#ifndef USING_RADIO_NAME
#define USING_RADIO_NAME               "None"
#endif
#endif

#if !FACTORY_HAS_AUDIO_OUT
#ifndef EXCLUDE_AUDIO_PLAYER
#define EXCLUDE_AUDIO_PLAYER
#endif
#endif

#if !FACTORY_HAS_AUDIO_IN
#ifndef EXCLUDE_AUDIO_RECORDER
#define EXCLUDE_AUDIO_RECORDER
#endif
#endif

#if !FACTORY_HAS_MICROPHONE
#ifndef EXCLUDE_MICROPHONE
#define EXCLUDE_MICROPHONE
#endif
#endif

#if !FACTORY_HAS_SD
#ifndef EXCLUDE_SD_APPS
#define EXCLUDE_SD_APPS
#endif
#ifndef EXCLUDE_SD_MANAGER
#define EXCLUDE_SD_MANAGER
#endif
#endif

#if !FACTORY_HAS_GPS
#ifndef EXCLUDE_GPS
#define EXCLUDE_GPS
#endif
#endif

#if !FACTORY_HAS_RADIO
#ifndef EXCLUDE_LORA
#define EXCLUDE_LORA
#endif
#endif

#if !FACTORY_HAS_LORAWAN
#ifndef EXCLUDE_LORAWAN
#define EXCLUDE_LORAWAN
#endif
#endif

#if !FACTORY_HAS_CC1101_TOOL
#ifndef EXCLUDE_CC1101_TOOL
#define EXCLUDE_CC1101_TOOL
#endif
#endif

#if !FACTORY_HAS_NFC
#ifndef EXCLUDE_NFC
#define EXCLUDE_NFC
#endif
#ifndef EXCLUDE_NFC_EMULATION
#define EXCLUDE_NFC_EMULATION
#endif
#endif

#if !FACTORY_HAS_IR_TX
#ifndef EXCLUDE_IR_REMOTE
#define EXCLUDE_IR_REMOTE
#endif
#endif

#if !FACTORY_HAS_IR_RX || !FACTORY_HAS_SD
#ifndef EXCLUDE_IR_RECORDER
#define EXCLUDE_IR_RECORDER
#endif
#endif

#if !FACTORY_HAS_IR_TX || !FACTORY_HAS_SD
#ifndef EXCLUDE_IR_PLAYER
#define EXCLUDE_IR_PLAYER
#endif
#endif

#if !FACTORY_HAS_IR_TX || !FACTORY_HAS_IR_RX || !FACTORY_HAS_SD
#ifndef EXCLUDE_IR_UNIVERSAL
#define EXCLUDE_IR_UNIVERSAL
#endif
#endif

#if !FACTORY_HAS_AUDIO_CODEC
#ifndef EXCLUDE_I2S_TEST
#define EXCLUDE_I2S_TEST
#endif
#endif

#if !FACTORY_HAS_NRF24 || !FACTORY_HAS_SPI
#ifndef EXCLUDE_NRF24
#define EXCLUDE_NRF24
#endif
#endif

#if !FACTORY_HAS_SI4735
#ifndef EXCLUDE_SI4735_RADIO_WF
#define EXCLUDE_SI4735_RADIO_WF
#endif
#endif

#if !FACTORY_HAS_COMPASS
#ifndef EXCLUDE_COMPASS
#define EXCLUDE_COMPASS
#endif
#endif

#if !FACTORY_HAS_KEYBOARD
#ifndef EXCLUDE_KEYBOARD
#define EXCLUDE_KEYBOARD
#endif
#endif

#if !FACTORY_HAS_TRACKBALL
#ifndef EXCLUDE_TRACKBALL
#define EXCLUDE_TRACKBALL
#endif
#endif

#if !FACTORY_HAS_KEYBOARD && !FACTORY_HAS_TRACKBALL
#ifndef EXCLUDE_BLE_HID
#define EXCLUDE_BLE_HID
#endif
#endif

#if !FACTORY_HAS_MOTION_SENSOR
#ifndef EXCLUDE_IMU
#define EXCLUDE_IMU
#endif
#endif

#if !FACTORY_HAS_MOTION_SENSOR || !FACTORY_HAS_SD
#ifndef EXCLUDE_SENSOR_LOGGER
#define EXCLUDE_SENSOR_LOGGER
#endif
#endif

#if !FACTORY_HAS_GPS || !FACTORY_HAS_SD
#ifndef EXCLUDE_TRACK_LOGGER
#define EXCLUDE_TRACK_LOGGER
#endif
#endif

#if !FACTORY_HAS_I2C
#ifndef EXCLUDE_INA219
#define EXCLUDE_INA219
#endif
#ifndef EXCLUDE_THERMAL
#define EXCLUDE_THERMAL
#endif
#endif

#if !FACTORY_HAS_HAPTIC_DRV
#ifndef EXCLUDE_DRV2605
#define EXCLUDE_DRV2605
#endif
#endif

#if !FACTORY_HAS_USB_MSC
#ifndef EXCLUDE_USB_DISK
#define EXCLUDE_USB_DISK
#endif
#ifndef EXCLUDE_BAD_USB
#define EXCLUDE_BAD_USB
#endif
#endif

#if !FACTORY_HAS_NES
#ifndef EXCLUDE_NES
#define EXCLUDE_NES
#endif
#endif

#if defined(ARDUINO_T_DECK)
#ifndef EXCLUDE_WALKIE
#define EXCLUDE_WALKIE
#endif
#endif
