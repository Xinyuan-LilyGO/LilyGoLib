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
#include <string>
#include <iostream>
#include <vector>
#include "event_define.h"

using namespace std;



typedef enum {
    MEDIA_VOLUME_UP,
    MEDIA_VOLUME_DOWN,
    MEDIA_PLAY_PAUSE,
    MEDIA_NEXT,
    MEDIA_PREVIOUS
} media_key_value_t;


// Check if not compiling for Arduino environment
// If not, define the wl_status_t enumeration
#ifndef ARDUINO

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
#define HW_RADIO_ONLINE          _BV(0)
#define HW_TOUCH_ONLINE          _BV(1)
#define HW_DRV_ONLINE            _BV(2)
#define HW_PMU_ONLINE            _BV(3)
#define HW_RTC_ONLINE            _BV(4)
#define HW_PSRAM_ONLINE          _BV(5)
#define HW_GPS_ONLINE            _BV(6)
#define HW_SD_ONLINE             _BV(7)
#define HW_NFC_ONLINE            _BV(8)
#define HW_SENSOR_ONLINE         _BV(9)
#define HW_KEYBOARD_ONLINE       _BV(10)
#define HW_GAUGE_ONLINE          _BV(11)
#define HW_EXPAND_ONLINE         _BV(12)
#define HW_CODEC_ONLINE          _BV(13)
#define HW_NRF24_ONLINE          _BV(14)

#else
// If compiling for Arduino, include the WiFi library
#include <WiFi.h>
#endif


// Define the GMT offset in seconds (for 8 hours ahead)
#define GMT_OFFSET_SECOND       (8*3600)

/**
 * @brief Structure to hold GPS parameters.
 *
 * This structure stores information related to GPS, such as model,
 * latitude, longitude, date and time, speed, received data size,
 * number of satellites, and PPS status.
 */
typedef struct  {
    string model;
    double lat;
    double lng;
    struct tm datetime;
    double speed;
    uint32_t rx_size;
    uint16_t satellite;
    bool pps;
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
    string ssid;
} wifi_scan_params_t;

/**
 * @brief Structure to hold WiFi connection parameters.
 *
 * This structure stores the SSID and password required for a WiFi connection.
 */
typedef struct {
    string ssid;
    string password;
} wifi_conn_params_t;



typedef enum {
    MONITOR_PMU,
    MONITOR_PPM,
} monitor_params_type_t;

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
    monitor_params_type_t type;
    string   charge_state;      // string
    uint16_t sys_voltage;       // mv
    uint16_t battery_voltage;   // mv
    uint16_t usb_voltage;       // mv
    int      battery_percent;   // %
    float    temperature;       // Celsius
    uint16_t remainingCapacity; // mAh
    uint16_t fullChargeCapacity;// mAh
    uint16_t designCapacity;    //mAh
    int16_t  instantaneousCurrent;   // mA
    int16_t  standbyCurrent;     // mA
    int16_t  averagePower;      //mW
    int16_t  maxLoadCurrent;    //mA
    uint16_t timeToEmpty;       // minute
    uint16_t timeToFull;        // minute
} monitor_params_t;

/**
 * @brief Structure to hold user setting parameters.
 *
 * This structure stores user-defined settings, such as display brightness level,
 * keyboard backlight level, display timeout in seconds, charger current, and charger enable status.
 */
typedef struct {
    uint8_t brightness_level;
    uint8_t keyboard_bl_level;
    uint8_t disp_timeout_second;
    uint16_t charger_current;
    uint8_t charger_enable;
} user_setting_params_t;

/**
 * @brief Structure to hold audio parameters.
 *
 * This structure stores information related to audio events and the filename of the audio file.
 */
typedef struct {
    enum app_event event;
    const char *filename ;
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
} imu_params_t;

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
 * @param param A reference to a string where the SSID will be stored.
 */
void hw_get_wifi_ssid(string &param);

/**
 * @brief Get the current date and time as a string.
 *
 * @param param A reference to a string where the date and time will be stored.
 */
void hw_get_date_time(string &param);

/**
 * @brief Get the current date and time as a struct tm.
 *
 * @param timeinfo A reference to a struct tm where the date and time will be stored.
 */
void hw_get_date_time(struct tm &timeinfo);

/**
 * @brief Get the current WiFi status.
 *
 * @return The current WiFi status as defined in the wl_status_t enumeration.
 */
wl_status_t hw_get_wifi_status();

/**
 * @brief Get the current IP address.
 *
 * @param param A reference to a string where the IP address will be stored.
 */
void hw_get_ip_address(string &param);

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
 * @param param A reference to a string where the Arduino version will be stored.
 */
void hw_get_arduino_version(string &param);

/**
 * @brief Get the GPS information.
 *
 * @param param A reference to a gps_params_t structure where the GPS information will be stored.
 */
bool hw_get_gps_info(gps_params_t &param);


void hw_gps_attach_pps();

void hw_gps_detach_pps();

/**
 * @brief Get the online status of the hardware devices.
 *
 * @return A 32-bit unsigned integer representing the online status of the hardware devices.
 */
uint32_t hw_get_device_online();

/**
 * @brief Set the display backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_disp_backlight(uint8_t level);

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
 * @brief Set the keyboard backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_kb_backlight(uint8_t level);

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
void hw_get_wifi_scan_result(vector <wifi_scan_params_t> &list);

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
 * @brief Get the received radio data.
 *
 * @param params A reference to a radio_rx_params_t structure where the received data will be stored.
 */
void hw_get_radio_rx(radio_rx_params_t &params);

/**
 * @brief Mount the SD card.
 */
void hw_mount_sd();

/**
 * @brief Get the list of music files on the SD card.
 *
 * @param list A reference to a vector where the music file names will be stored.
 */
void hw_get_sd_music(vector <string> &list);

/**
 * @brief Start playing a music file from the SD card.
 *
 * @param filename A pointer to the name of the music file to play.
 */
void hw_set_sd_music_play(const char *filename);

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


void hw_set_play_stop();

/**
 * @brief Shutdown the hardware.
 */
void hw_shutdown();

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
 * @brief Set the charger current.
 *
 * @param milliampere The charger current to be set in milliamperes.
 */
void hw_set_charger_current(uint16_t milliampere);

/**
 * @brief Get the microphone pressure level.
 *
 * @return The microphone pressure level.
 */
int16_t hw_get_microphone_pressure_level();

/**
 * @brief Get the monitor parameters.
 *
 * @param params A reference to a monitor_params_t structure where the monitor parameters will be stored.
 */
void hw_get_monitor_params(monitor_params_t &params);

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
 * @brief Enable the BLE module.
 *
 * @param devName A pointer to the device name for BLE advertising.
 */
void hw_enable_ble(const char *devName);

/**
 * @brief Disable the BLE module.
 */
void hw_disable_ble();

/**
 * @brief Get the BLE message.
 *
 * @param buffer A pointer to a buffer where the BLE message will be stored.
 * @param buffer_size The size of the buffer.
 * @return The number of bytes read from the BLE message.
 */
size_t hw_get_ble_message(char*buffer, size_t buffer_size);

/**
 * @brief Deinitialize the BLE module.
 */
void hw_deinit_ble();

/**
 * @brief Enable the BLE keyboard function.
 */
void hw_set_ble_kb_enable();

/**
 * @brief Disable the BLE keyboard function.
 */
void hw_set_ble_kb_disable();

/**
 * @brief Send a character via the BLE keyboard.
 *
 * @param c A pointer to the character to send.
 */
void hw_set_ble_kb_char(const char * c);

/**
 * @brief Send a key code via the BLE keyboard.
 *
 * @param key The key code to send.
 */
void hw_set_ble_kb_key(uint8_t key);

/**
 * @brief Release the keys on the BLE keyboard.
 */
void hw_set_ble_kb_release();

/**
 * @brief Check if the BLE keyboard is connected.
 *
 * @return True if connected, false otherwise.
 */
bool hw_get_ble_kb_connected();


void hw_set_ble_key(media_key_value_t key);

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
 */
void hw_inc_brightness(uint8_t level);

/**
 * @brief Decrease the display brightness level.
 *
 * This function decreases the display brightness by the specified level.
 *
 * @param level The amount by which to decrease the brightness.
 */
void hw_dec_brightness(uint8_t level);

/**
 * @brief Set the CPU frequency.
 *
 * This function sets the CPU frequency to the specified value in megahertz.
 *
 * @param mhz The desired CPU frequency in megahertz.
 */
void hw_set_cpu_freq(uint32_t mhz);

/**
 * @brief Start the microphone.
 *
 * This function initializes and starts the microphone for audio input.
 */
void hw_set_mic_start();

/**
 * @brief Stop the microphone.
 *
 * This function stops the microphone and releases any associated resources.
 */
void hw_set_mic_stop();


void hw_disable_input_devices();


void hw_enable_input_devices();


void hw_flush_keyboard();

bool hw_has_keyboard();

bool hw_has_otg_function();

uint8_t hw_get_disp_min_brightness();
uint16_t hw_get_disp_max_brightness();
uint8_t hw_get_min_charge_current();
uint16_t hw_get_max_charge_current();
uint8_t hw_get_charge_level_nums();
uint8_t hw_get_charge_steps();
uint16_t hw_set_charger_current_level(uint8_t level);
uint8_t hw_get_charger_current_level();

void hw_print_mem_info();

void hw_get_nrf24_params(radio_params_t &params);
void hw_get_nrf24_params(radio_params_t &params);
int16_t hw_set_nrf24_params(radio_params_t &params);
void hw_set_nrf24_listening();
bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous = true);
void hw_get_nrf24_rx(radio_rx_params_t &params);
bool hw_has_nrf24();
void hw_clear_nrf24_flag();


const char *radio_get_freq_list();
float radio_get_freq_from_index(uint8_t index);
const char *radio_get_bandwidth_list();
float radio_get_bandwidth_from_index(uint8_t index);
const char *radio_get_tx_power_list();
float radio_get_tx_power_from_index(uint8_t index);
uint16_t radio_get_freq_length();
uint16_t radio_get_bandwidth_length();
uint16_t radio_get_tx_power_length();

#if defined(ARDUINO_T_LORA_PAGER)
#define USING_BLE_KEYBOARD
#define  FLOAT_BUTTON_WIDTH  40
#define  FLOAT_BUTTON_HEIGHT 40
#ifndef USING_BHI260_SENSOR
#define USING_BHI260_SENSOR
#endif

#ifndef RADIOLIB_EXCLUDE_NRF24
#define USING_EXTERN_NRF2401
#endif

#elif defined(ARDUINO_T_WATCH_S3_ULTRA)
#define USING_TOUCHPAD
#define  FLOAT_BUTTON_WIDTH  60
#define  FLOAT_BUTTON_HEIGHT 60
#define USING_BLE_KEYBOARD
#ifndef USING_BHI260_SENSOR
#define USING_BHI260_SENSOR
#endif
// #define USING_BLE_CONTROL

#elif defined(ARDUINO_T_WATCH_S3)
#define USING_TOUCHPAD
#define  FLOAT_BUTTON_WIDTH  40
#define  FLOAT_BUTTON_HEIGHT 40
#ifndef USING_BMA423_SENSOR
#define USING_BMA423_SENSOR
#define USING_BLE_KEYBOARD
#endif
// #define USING_BLE_CONTROL

#endif

