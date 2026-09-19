/**
 * @file      hw_sx1280.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */
#include <LilyGoLog.h>
#include "hal_interface.h"
#include "hw_radio_log.h"

#ifdef ARDUINO_LILYGO_LORA_SX1280

#include <LilyGoLib.h>

static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;
static int16_t last_tx_state = 0;

#define LORA_ISR_FLAG                  _BV(0)

static void hw_radio_isr()
{
    if (!radioEvent) {
        return;
    }
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  LORA_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

void hw_radio_begin()
{
    radioEvent = xEventGroupCreate();

    // Radio  register isr event
    radio.setPacketSentAction(hw_radio_isr);
}

int16_t hw_set_radio_params(radio_params_t &params)
{
    const uint8_t requested_power = params.power;
    const char *step = "Initialize";
#ifdef ARDUINO
    instance.lockSPI();
    // Return the first configuration failure while always releasing the SPI lock.
    int16_t state = [&]() -> int16_t {
        step = "Standby";
        int16_t state = radio.standby();
        RADIOLIB_ASSERT(state);
        step = "Frequency";
        state = radio.setFrequency(params.freq);
        RADIOLIB_ASSERT(state);
        // set bandwidth
        step = "Bandwidth";
        state = radio.setBandwidth(params.bandwidth);
        RADIOLIB_ASSERT(state);
        // set spreading factor
        step = "Spreading factor";
        state = radio.setSpreadingFactor(params.sf);
        RADIOLIB_ASSERT(state);
        // set coding rate
        step = "Coding rate";
        state = radio.setCodingRate(params.cr);
        RADIOLIB_ASSERT(state);
        // set LoRa sync word
        step = "Sync word";
        state = radio.setSyncWord(params.syncWord);
        RADIOLIB_ASSERT(state);
        // set output power
        step = "TX power";
        state = radio.setOutputPower(params.power);
        RADIOLIB_ASSERT(state);

        switch (params.mode) {
        case RADIO_DISABLE:
            step = "Standby";
            state =  radio.standby();
            break;
        case RADIO_TX:
            step = "Start TX";
            state =  radio.startTransmit("");
            break;
        case RADIO_RX:
            step = "Start RX";
            state =  radio.startReceive();
            break;
        case RADIO_CW:
            step = "Standby";
            state = radio.standby();
            RADIOLIB_ASSERT(state);
            delay(5);
            step = "Start CW";
            state = radio.transmitDirect();
            break;
        default:
            break;
        }
        return state;
    }();
    instance.unlockSPI();
#else
    int16_t state = 0;
#endif
    hw_log_lora_config("SX1280", params, state, step, requested_power);
    return state;
}

void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 203.125;
    params.freq = 2400.0;
    params.cr = 5;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 12;
    params.power = 13;
    params.interval = 3000;
    params.syncWord = 0xCD;
}

void hw_set_radio_default()
{
    radio_params_t params ;
    hw_get_radio_params(params);
    hw_set_radio_params(params);
}

void hw_set_radio_listening()
{
#ifdef ARDUINO
    instance.lockSPI();
    // Start next packet recv
    radio.startReceive();
    instance.unlockSPI();
#endif
}

void hw_set_radio_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    if (!radioEvent) {
        params.state = -1;
        last_tx_state = params.state;
        return;
    }

    if (continuous) {
        EventBits_t  eventBits = xEventGroupWaitBits(radioEvent,
                                 LORA_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
        if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
            params.state = -1;
            last_tx_state = params.state;
            return;
        }
    }

    if (!params.data) {
        LILYGO_LOG_PRINTF("tx data buffer is empty");
        params.state = -1;
        last_tx_state = params.state;
        return;
    }

    LILYGO_LOG_PRINT("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        LILYGO_LOG_PRINTF("%02X,", params.data[i]);
    }
    LILYGO_LOG_PRINTLN();
    LILYGO_LOG_PRINT("[TX LEN:]");
    LILYGO_LOG_PRINTLN(params.length);

    instance.lockSPI();
    radio.finishTransmit();
    xEventGroupClearBits(radioEvent, LORA_ISR_FLAG);
    params.state = radio.startTransmit(params.data, params.length);
    instance.unlockSPI();
    last_tx_state = params.state;

    if (params.state == RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("transmission started!"));
    } else {
        LILYGO_LOG_PRINT(F("failed, code "));
        LILYGO_LOG_PRINTLN(params.state);
    }
#endif
}

bool hw_get_radio_tx_done(int16_t &state)
{
#ifdef ARDUINO
    if (!radioEvent) {
        state = -1;
        last_tx_state = state;
        return true;
    }

    EventBits_t eventBits = xEventGroupWaitBits(radioEvent,
                            LORA_ISR_FLAG,
                            pdTRUE,
                            pdTRUE,
                            0);
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        return false;
    }

    instance.lockSPI();
    state = radio.finishTransmit();
    instance.unlockSPI();
    last_tx_state = state;
    last_send_millis = millis();
    return true;
#else
    state = 0;
    return true;
#endif
}

void hw_get_radio_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        LILYGO_LOG_PRINTF("rx data buffer is empty");
        return;
    }

    instance.lockSPI();
    params.length = radio.getPacketLength();
    params.state = radio.readData(params.data, params.length);
    params.rssi = radio.getRSSI();
    params.snr = radio.getSNR();
    // Start next packet recv
    radio.startReceive();
    instance.unlockSPI();


    if (last_send_millis + 200 > millis()) {
        // avoid showing own sent messages
        params.length = 0;
        return;
    }

    params.data[params.length] = '\0';

    LILYGO_LOG_PRINT("[RX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        LILYGO_LOG_PRINTF("%02X,", params.data[i]);
    }
    LILYGO_LOG_PRINTLN();
    LILYGO_LOG_PRINT("[RX LEN:]");
    LILYGO_LOG_PRINTLN(params.length);

    if (params.state == RADIOLIB_ERR_NONE && params.length != 0) {
        // packet was successfully received
        LILYGO_LOG_PRINTLN(F("[Radio] Received packet!"));
        LILYGO_LOG_PRINT("[LEN]:");
        LILYGO_LOG_PRINTLN(params.length);
        LILYGO_LOG_PRINT("[PAYLOAD]:");
        LILYGO_LOG_PRINTLN((char *)params.data);
        // print RSSI (Received Signal Strength Indicator)
        LILYGO_LOG_PRINT(F("[Radio] RSSI:\t\t"));
        LILYGO_LOG_PRINT(params.rssi);
        LILYGO_LOG_PRINTLN(F(" dBm"));
        // print SNR (Signal-to-Noise Ratio)
        LILYGO_LOG_PRINT(F("[Radio] SNR:\t\t"));
        LILYGO_LOG_PRINT(params.snr);
        LILYGO_LOG_PRINTLN(F(" dB"));
    }
#else
    params.length = 0;
#endif
}

bool radio_transmit(const uint8_t *data, size_t length)
{
#ifdef ARDUINO
    int state = radio.transmit(data, length);
    last_tx_state = state;
    last_send_millis = millis();
    return (state == RADIOLIB_ERR_NONE);
#else
    return true;
#endif
}

int16_t radio_get_last_transmit_state()
{
#ifdef ARDUINO
    return last_tx_state;
#else
    return 0;
#endif
}

static const float bandwidth_list[] = {203.125, 406.25, 812.5, 1625.0};
static const float power_level_list[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const float freq_list[] = {2400.0,
                                  2412.0,
                                  2422.0,
                                  2432.0,
                                  2442.0,
                                  2452.0,
                                  2462.0,
                                  2472.0,
                                  2482.0,
                                  2492.0,
                                  2498.0,
                                  2500.0
                                 };

uint16_t radio_get_freq_length()
{
    return (sizeof(freq_list) / sizeof(freq_list[0]));
}

uint16_t radio_get_bandwidth_length()
{
    return (sizeof(bandwidth_list) / sizeof(bandwidth_list[0]));
}

uint16_t radio_get_tx_power_length()
{
    return (sizeof(power_level_list) / sizeof(power_level_list[0]));
}

const char *radio_get_freq_list()
{
    return "2400MHz\n"
           "2412MHz\n"
           "2422MHz\n"
           "2432MHz\n"
           "2442MHz\n"
           "2452MHz\n"
           "2462MHz\n"
           "2472MHz\n"
           "2482MHz\n"
           "2492MHz\n"
           "2498MHz\n"
           "2500MHz";
}

float radio_get_freq_from_index(uint8_t index)
{

    if (index >= radio_get_freq_length()) {
        return 2400.0;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return "203.125KHz\n"
           "406.25KHz\n"
           "812.5KHz\n"
           "1625.0KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index >= radio_get_bandwidth_length()) {
        return 203.125;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "0dBm\n"
            "1dBm\n"
            "2dBm\n"
            "3dBm\n"
            "4dBm\n"
            "5dBm\n"
            "6dBm\n"
            "7dBm\n"
            "8dBm\n"
            "9dBm\n"
            "10dBm\n"
            "11dBm\n"
            "12dBm\n"
            "13dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index >= radio_get_tx_power_length()) {
        return 13;
    }
    return power_level_list[index];
}

/* Spectral scan not supported on SX1280 */
bool hw_has_spectral_scan() { return false; }
int16_t hw_radio_spectral_scan_init() { return -1; }
int16_t hw_radio_spectral_scan_start(uint16_t) { return -1; }
int16_t hw_radio_spectral_scan_status() { return -1; }
int16_t hw_radio_spectral_scan_result(uint16_t*) { return -1; }
void hw_radio_spectral_scan_abort() {}
void hw_radio_spectral_scan_deinit() {}

#endif
