/**
 * @file      hw_lr1121.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */

#include <LilyGoLog.h>
#include "hal_interface.h"
#include "hw_radio_log.h"

#ifdef ARDUINO_LILYGO_LORA_LR1121


static bool _high_freq = false;

#ifdef ARDUINO
#include <LilyGoLib.h>

static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;
static int16_t last_tx_state = 0;

#define LORA_ISR_FLAG                  _BV(0)

static TickType_t radio_tx_timeout_ticks(size_t length)
{
    RadioLibTime_t toa_us = radio.getTimeOnAir(length);
    uint32_t timeout_ms = 1000;
    if (toa_us > 0) {
        timeout_ms = (uint32_t)((toa_us * 3UL) / 2000UL) + 250;
    }
    if (timeout_ms < 500) {
        timeout_ms = 500;
    } else if (timeout_ms > 15000) {
        timeout_ms = 15000;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

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

#endif /*ARDUINO*/

int16_t hw_set_radio_params(radio_params_t &params)
{
    const uint8_t requested_power = params.power;
    const char *step = "Initialize";
    if (params.freq > 960.0) {
        _high_freq = true;
    } else {
        _high_freq = false;
    }

#ifdef ARDUINO
    // Lock SPI bus
    instance.lockSPI();
    // Return the first configuration failure while always releasing the SPI lock.
    int16_t state = [&]() -> int16_t {
        int16_t state = 0;

        /*
        *  Re-initialize LoRa
        * */
        if (!instance.initLoRa()) return RADIOLIB_ERR_CHIP_NOT_FOUND;
        radio.setPacketSentAction(hw_radio_isr);

#ifdef ARDUINO_T_DECK_V2
        instance.setRFFrequencyBand(params.freq);
#endif

        step = "Frequency";
        state = radio.setFrequency(params.freq);
        RADIOLIB_ASSERT(state);

        // set bandwidth
        step = "Bandwidth";
        state = radio.setBandwidth(params.bandwidth, _high_freq);
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

        bool forceHighPower = params.freq < 1000.0f;

#ifdef LILYGO_RADIO_2G4_TX_POWER_LIMIT
        const uint8_t power_limit = LILYGO_RADIO_2G4_TX_POWER_LIMIT;
#else
        const uint8_t power_limit = 13;
#endif

        if (params.freq >= 2400 && params.power > power_limit) {
            params.power = power_limit;
            forceHighPower = false;
        }
        // set output power
        step = "TX power";
        state = radio.setOutputPower(params.power, forceHighPower);
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
    hw_log_lora_config("LR1121", params, state, step, requested_power);
    return state;
}

void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 125.0;
    params.freq = RADIO_DEFAULT_FREQUENCY;
    params.cr = 5;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 12;
    params.power = 22;
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

    xEventGroupClearBits(radioEvent, LORA_ISR_FLAG);

    LILYGO_LOG_PRINT("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        LILYGO_LOG_PRINTF("%02X,", params.data[i]);
    }
    LILYGO_LOG_PRINTLN();
    LILYGO_LOG_PRINT("[TX LEN:]");
    LILYGO_LOG_PRINTLN(params.length);

    instance.lockSPI();
    radio.finishTransmit();
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
    if (!data || length == 0) {
        last_tx_state = -1;
        return false;
    }

    if (!radioEvent) {
        last_tx_state = -1;
        return false;
    }

    xEventGroupClearBits(radioEvent, LORA_ISR_FLAG);

    TickType_t timeout_ticks;

    instance.lockSPI();
    radio.finishTransmit();
    timeout_ticks = radio_tx_timeout_ticks(length);
    int state = radio.startTransmit(data, length);
    instance.unlockSPI();

    if (state != RADIOLIB_ERR_NONE) {
        last_tx_state = state;
        return false;
    }

    EventBits_t eventBits = xEventGroupWaitBits(
                                radioEvent,
                                LORA_ISR_FLAG,
                                pdTRUE,
                                pdTRUE,
                                timeout_ticks);

    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        instance.lockSPI();
        radio.finishTransmit();
        instance.unlockSPI();
        last_tx_state = RADIOLIB_ERR_TX_TIMEOUT;
        return false;
    }

    instance.lockSPI();
    state = radio.finishTransmit();
    instance.unlockSPI();

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

#ifdef RADIO_FIXED_FREQUENCY
static const float freq_list[] = {RADIO_FIXED_FREQUENCY,
                                  2400.0, 2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0, 2490.0, 2500.0
                                 };
#else
static const float freq_list[] = {315.0, 433.0, 434.0, 470.0, 842.0, 850, 868.0, 915.0, 923.0, 945.0,
                                  2400.0, 2410.0, 2420.0, 2430.0, 2440.0, 2450.0, 2460.0, 2470.0, 2480.0, 2490.0, 2500.0
                                 };
#endif

static const float bandwidth_list[] = {62.5, 125.0, 250.0, 500.0};
static const float bandwidth_high_freq_list[] = {203.125, 406.25, 812.5};

static const float power_level_list[] = {2, 5, 10, 12, 17, 20, 22};
static const float power_level_high_freq_list[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};

uint16_t radio_get_freq_length()
{
    return (sizeof(freq_list) / sizeof(freq_list[0]));
}

uint16_t radio_get_bandwidth_length()
{
    if (_high_freq) {
        return (sizeof(bandwidth_high_freq_list) / sizeof(bandwidth_high_freq_list[0]));
    }
    return (sizeof(bandwidth_list) / sizeof(bandwidth_list[0]));
}

uint16_t radio_get_tx_power_length()
{
    if (_high_freq) {
#ifdef LILYGO_RADIO_2G4_TX_POWER_LIMIT
        return LILYGO_RADIO_2G4_TX_POWER_LIMIT + 1;
#else
        return (sizeof(power_level_high_freq_list) / sizeof(power_level_high_freq_list[0]));
#endif
    }
    return (sizeof(power_level_list) / sizeof(power_level_list[0]));
}

const char *radio_get_freq_list()
{
#ifdef RADIO_FIXED_FREQUENCY
    return RADIO_FIXED_FREQUENCY_STRING"\n2400MHz\n""2410MHz\n""2420MHz\n""2430MHz\n""2440MHz\n""2450MHz\n""2460MHz\n""2470MHz\n""2480MHz\n""2490MHz\n""2500MHz";
#else
    return "315MHz\n""433MHz\n""434MHz\n""470MHz\n""842MHZ\n""850MHZ\n""868MHz\n""915MHz\n""923MHz\n""945MHz\n"
           "2400MHz\n""2410MHz\n""2420MHz\n""2430MHz\n""2440MHz\n""2450MHz\n""2460MHz\n""2470MHz\n""2480MHz\n""2490MHz\n""2500MHz";
#endif
}

float radio_get_freq_from_index(uint8_t index)
{
    if (index >= radio_get_freq_length()) {
        _high_freq = false;
        return RADIO_DEFAULT_FREQUENCY;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    _high_freq = high_freq;
    if (high_freq) {
        return "203.125KHz\n""406.25KHz\n""812.5KHz";
    } else {
        return "62.5KHz\n""125KHz\n""250KHz\n""500KHz";
    }
}

const char *radio_get_tx_power_list(bool high_freq)
{
    _high_freq = high_freq;
    if (high_freq) {
        static char options[96];
        size_t used = 0;
        for (uint16_t i = 0; i < radio_get_tx_power_length(); ++i) {
            used += snprintf(options + used, sizeof(options) - used, "%s%udBm", i ? "\n" : "", (unsigned)i);
        }
        return options;
    }
    return  "2dBm\n""5dBm\n""10dBm\n""12dBm\n""17dBm\n""20dBm\n""22dBm";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (_high_freq) {
        if (index >= (sizeof(bandwidth_high_freq_list) / sizeof(bandwidth_high_freq_list[0]))) {
            index = 0;
        }
        return bandwidth_high_freq_list[index];
    }
    if (index >= (sizeof(bandwidth_list) / sizeof(bandwidth_list[0]))) {
        index = 0;
    }
    return bandwidth_list[index];
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (_high_freq) {
        if (index >= radio_get_tx_power_length()) {
            index = radio_get_tx_power_length() - 1;
        }
        return power_level_high_freq_list[index];
    }
    if (index >= (sizeof(power_level_list) / sizeof(power_level_list[0]))) {
        return 22.0;
    }
    return power_level_list[index];
}

/* Spectral scan not supported on LR1121 */
bool hw_has_spectral_scan() { return false; }
int16_t hw_radio_spectral_scan_init() { return -1; }
int16_t hw_radio_spectral_scan_start(uint16_t) { return -1; }
int16_t hw_radio_spectral_scan_status() { return -1; }
int16_t hw_radio_spectral_scan_result(uint16_t*) { return -1; }
void hw_radio_spectral_scan_abort() {}
void hw_radio_spectral_scan_deinit() {}

#endif
