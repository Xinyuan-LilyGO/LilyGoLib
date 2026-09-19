/**
 * @file      hw_sx1262.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */

#include <LilyGoLog.h>
#include "hal_interface.h"
#include "hw_radio_log.h"

#ifdef ARDUINO_LILYGO_LORA_SX1262

#ifdef ARDUINO
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
#endif

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

        // Set Current Limit
        step = "Current limit";
        state = radio.setCurrentLimit(140);
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
    hw_log_lora_config("SX1262", params, state, step, requested_power);
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
    params.power = RADIO_DEFAULT_TX_POWER;
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
            LILYGO_LOG_PRINTF("bits : %u\n", eventBits);
            params.state = -1;
            last_tx_state = params.state;
            return;
        }
    }

    if (!params.data) {
        LILYGO_LOG_PRINTLN("tx data buffer is empty");
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
        LILYGO_LOG_PRINTF("Rx data buffer is empty\n");
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


static const float bandwidth_list[] = {41.7, 62.5, 125.0, 250.0, 500.0};
static const float power_level_list[] = {2, 5, 10, 12, 17, 20, 22};
#ifdef RADIO_FIXED_FREQUENCY
static const float freq_list[] = {RADIO_FIXED_FREQUENCY};
#else
static const float freq_list[] = {433.0, 470.0, 842.0, 850, 868.0, 915.0, 923.0, 945.0};
#endif

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
#ifdef RADIO_FIXED_FREQUENCY
    return RADIO_FIXED_FREQUENCY_STRING;
#else
    return "433MHz\n""470MHz\n""842MHZ\n""850MHZ\n""868MHz\n""915MHz\n""923MHz\n""945MHz";
#endif
}

float radio_get_freq_from_index(uint8_t index)
{
    if (index > radio_get_freq_length()) {
        return RADIO_DEFAULT_FREQUENCY;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return "41.7KHz\n""62.5KHz\n""125KHz\n""250KHz\n""500KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index > radio_get_bandwidth_length()) {
        return 125.0;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "2dBm\n""5dBm\n""10dBm\n""12dBm\n""17dBm\n""20dBm\n""22dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index > radio_get_tx_power_length()) {
        return 22;
    }
    return power_level_list[index];
}

/* ── Spectral Scan (SX126x only) ── */
#include <modules/SX126x/patches/SX126x_patch_scan.h>

static bool spectral_scan_active = false;

bool hw_has_spectral_scan()
{
    return true;
}

int16_t hw_radio_spectral_scan_init()
{
    instance.lockSPI();

    /* Initialize FSK modem at default frequency */
    ConfigFSK_t config;
    config.frequency = RADIO_DEFAULT_FREQUENCY;
    int16_t state = radio.beginFSK(config);
    if (state != RADIOLIB_ERR_NONE) {
        instance.unlockSPI();
        return state;
    }

    /* Upload spectral scan patch */
    state = radio.uploadPatch(sx126x_patch_scan, sizeof(sx126x_patch_scan));
    if (state != RADIOLIB_ERR_NONE) {
        instance.unlockSPI();
        return state;
    }

    /* Configure scan parameters */
    state = radio.setRxBandwidth(234.3);
    if (state != RADIOLIB_ERR_NONE) {
        instance.unlockSPI();
        return state;
    }
    state = radio.setDataShaping(RADIOLIB_SHAPING_NONE);
    if (state != RADIOLIB_ERR_NONE) {
        instance.unlockSPI();
        return state;
    }

    spectral_scan_active = true;
    instance.unlockSPI();
    return RADIOLIB_ERR_NONE;
}

int16_t hw_radio_spectral_scan_start(uint16_t numSamples)
{
    if (!spectral_scan_active) return -1;
    instance.lockSPI();
    int16_t state = radio.spectralScanStart(numSamples);
    instance.unlockSPI();
    return state;
}

int16_t hw_radio_spectral_scan_status()
{
    if (!spectral_scan_active) return -1;
    instance.lockSPI();
    int16_t state = radio.spectralScanGetStatus();
    instance.unlockSPI();
    return state;
}

int16_t hw_radio_spectral_scan_result(uint16_t *results)
{
    if (!spectral_scan_active) return -1;
    instance.lockSPI();
    int16_t state = radio.spectralScanGetResult(results);
    instance.unlockSPI();
    return state;
}

void hw_radio_spectral_scan_abort()
{
    if (!spectral_scan_active) return;
    instance.lockSPI();
    radio.spectralScanAbort();
    instance.unlockSPI();
}

void hw_radio_spectral_scan_deinit()
{
    if (!spectral_scan_active) return;
    instance.lockSPI();
    radio.standby();
    instance.unlockSPI();
    spectral_scan_active = false;
    hw_set_radio_default();
}

#endif
