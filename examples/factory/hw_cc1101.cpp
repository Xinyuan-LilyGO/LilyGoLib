/**
 * @file      hw_cc1101.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */

#include <LilyGoLog.h>
#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_CC1101


#include <LilyGoLib.h>
#include <string.h>

static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;
static int16_t last_tx_state = 0;

#define LORA_ISR_FLAG                  _BV(0)

#define RADIO_DEFAULT_BIT_RATE      38.4    //kbps
#define RADIO_DEFAULT_DEV_FREQ      20.0

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

    // CC1101 uses GDO2 for packet sent and GDO0 for packet received.
    radio.setPacketSentAction(hw_radio_isr);
    radio.setPacketReceivedAction(hw_radio_isr);
}

int16_t hw_set_radio_params(radio_params_t &params)
{
    LILYGO_LOG_PRINTF("Set radio params:\n");
    LILYGO_LOG_PRINTF("Frequency:%.2f MHz\n", params.freq);
    LILYGO_LOG_PRINTF("Bandwidth:%.2f KHz\n", params.bandwidth);
    LILYGO_LOG_PRINTF("TxPower:%u dBm\n", params.power);
    LILYGO_LOG_PRINTF("Interval:%u ms\n", params.interval);
    LILYGO_LOG_PRINTF("BitRate:%u kbps\n", params.cr);
    LILYGO_LOG_PRINTF("Modulation:%s\n", params.sf ? "OOK" : "2-FSK");
    LILYGO_LOG_PRINTF("SyncWord:%u \n", params.syncWord);
    LILYGO_LOG_PRINTF("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        LILYGO_LOG_PRINTF("RADIO_DISABLE\n");
        break;
    case RADIO_TX:
        LILYGO_LOG_PRINTF("RADIO_TX\n");
        break;
    case RADIO_RX:
        LILYGO_LOG_PRINTF("RADIO_RX\n");
        break;
    case RADIO_CW:
        LILYGO_LOG_PRINTF("RADIO_CW\n");
        break;
    default:
        break;
    }

#ifdef ARDUINO
    int16_t state = 0;
    instance.lockSPI();
    state = radio.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        LILYGO_LOG_PRINTLN(F("Selected frequency is invalid for this module!"));
    }
    // set modulation before output power so the PA table matches ASK/OOK or FSK.
    state = radio.setOOK(params.sf != 0);
    if (state != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Unable to set modulation!"));
    }
    // set bandwidth
    state = radio.setRxBandwidth(params.bandwidth);
    if (state == RADIOLIB_ERR_INVALID_RX_BANDWIDTH) {
        LILYGO_LOG_PRINTLN(F("Selected bandwidth is invalid for this module!"));
    }
    // set LoRa sync word
    state = radio.setSyncWord(params.syncWord, 0x23);
    if (state  != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("Unable to set sync word!"));
    }
    // set output power
    state = radio.setOutputPower(params.power);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        LILYGO_LOG_PRINTLN(F("Selected output power is invalid for this module!"));
    }
    // set bit rate
    float bit_rate = (params.cr == 0) ? RADIO_DEFAULT_BIT_RATE : (float)params.cr;
    if (params.cr == 38) {
        bit_rate = 38.4;
    }
    state = radio.setBitRate(bit_rate);
    if (state == RADIOLIB_ERR_INVALID_BIT_RATE) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Selected bit rate is invalid for this module!"));
    } else if (state == RADIOLIB_ERR_INVALID_BIT_RATE_BW_RATIO) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Increase receiver bandwidth for this bit rate."));
    }
    // set allowed frequency deviation
    if (params.sf == 0) {
        if (radio.setFrequencyDeviation(RADIO_DEFAULT_DEV_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY_DEVIATION) {
            LILYGO_LOG_PRINTLN(F("[CC1101] Selected frequency deviation is invalid for this module!"));
        }
    }

    LILYGO_LOG_PRINTF("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        LILYGO_LOG_PRINTF("RADIO_DISABLE\n");
        if (radioEvent) {
            xEventGroupClearBits(radioEvent, LORA_ISR_FLAG);
        }
        state =  radio.standby();
        LILYGO_LOG_PRINTF("RADIO_DISABLE state:%d\n", state);
        break;
    case RADIO_TX:
        state =  radio.standby();
        if (radioEvent) {
            xEventGroupSetBits(radioEvent, LORA_ISR_FLAG);
        }
        LILYGO_LOG_PRINTF("RADIO_TX state:%d\n", state);
        break;
    case RADIO_RX:
        if (radioEvent) {
            xEventGroupClearBits(radioEvent, LORA_ISR_FLAG);
        }
        state =  radio.startReceive();
        LILYGO_LOG_PRINTF("RADIO_RX state:%d\n", state);
        break;
    case RADIO_CW:
        state = radio.transmitDirect();
        LILYGO_LOG_PRINTF("RADIO_CW state:%d\n", state);
        break;
    default:
        break;
    }
    instance.unlockSPI();
    return state;
#else
    return 0;
#endif
}

void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 102.0;
    params.freq = 433.0;
    params.cr = 0;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 1;
    params.power = 10;
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
    radio.standby();
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
    if (!radioEvent) {
        params.state = -1;
        return;
    }
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        LILYGO_LOG_PRINTF("rx data buffer is empty");
        return;
    }

    uint8_t overflow_buf[255];
    size_t capacity = params.length;
    instance.lockSPI();
    size_t packet_len = radio.getPacketLength();
    size_t read_len = packet_len > sizeof(overflow_buf) ? sizeof(overflow_buf) : packet_len;
    uint8_t *read_buf = packet_len > capacity ? overflow_buf : params.data;
    params.state = radio.readData(read_buf, read_len);
    params.length = (packet_len > capacity) ? capacity : packet_len;
    if (read_buf != params.data && params.length > 0) {
        memcpy(params.data, read_buf, params.length);
    }
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


static const float bandwidth_list[] = {58, 68, 81, 102, 116, 135, 162, 203, 232, 270, 325, 406, 464, 541, 650};
static const float power_level_list[] = {-30, -20, -15, -10, 0, 5, 7, 10};
static const float freq_list[] = {315.0, 390.0, 433.0,
                                  433.92, 434.0, 868.0,
                                  868.35, 915.0
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
    return "315MHz\n"
           "390MHz\n"
           "433MHz\n"
           "433.92MHz\n"
           "434MHz\n"
           "868MHz\n"
           "868.35MHz\n"
           "915MHz";
}

float radio_get_freq_from_index(uint8_t index)
{
    if (index >= radio_get_freq_length()) {
        return 433.0;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return   "58KHz\n"
             "68KHz\n"
             "81KHz\n"
             "102KHz\n"
             "116KHz\n"
             "135KHz\n"
             "162KHz\n"
             "203KHz\n"
             "232KHz\n"
             "270KHz\n"
             "325KHz\n"
             "406KHz\n"
             "464KHz\n"
             "541KHz\n"
             "650KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index >= radio_get_bandwidth_length()) {
        return 102.0;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "-30dBm\n"
            "-20dBm\n"
            "-15dBm\n"
            "-10dBm\n"
            "0dBm\n"
            "5dBm\n"
            "7dBm\n"
            "10dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index >= radio_get_tx_power_length()) {
        return 10;
    }
    return power_level_list[index];
}

/* Spectral scan not supported on CC1101 */
bool hw_has_spectral_scan() { return false; }
int16_t hw_radio_spectral_scan_init() { return -1; }
int16_t hw_radio_spectral_scan_start(uint16_t) { return -1; }
int16_t hw_radio_spectral_scan_status() { return -1; }
int16_t hw_radio_spectral_scan_result(uint16_t*) { return -1; }
void hw_radio_spectral_scan_abort() {}
void hw_radio_spectral_scan_deinit() {}

#endif
