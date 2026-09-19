/**
 * @file      hw_nrf2401.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */

#include <LilyGoLog.h>
#include "hal_interface.h"

#if defined(USING_EXTERN_NRF2401)

#include <string.h>

static uint8_t nrf24_pipe_addr[5] = {0x01, 0x23, 0x45, 0x67, 0x89};
static const int8_t nrf24_power_table[] = {-18, -12, -6, 0};

#ifdef ARDUINO

#include <LilyGoLib.h>

static EventGroupHandle_t    radioEvent = NULL;
static int16_t               last_tx_state = 0;

#define NRF24_ISR_FLAG              _BV(1)

static void hw_nrf24_isr()
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
    if (!radioEvent) {
        return;
    }
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  NRF24_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

void hw_nrf24_begin()
{
    radioEvent = xEventGroupCreate();
    LILYGO_LOG_PRINTF(" init NRF2401 \n");
    bool rlst = instance.initNRF24();
    if (!rlst) {
        LILYGO_LOG_PRINTF("nRF2401 option Model not detected\n");
        return;
    }
    nrf24.setPacketSentAction(hw_nrf24_isr);

    // Set PA control IO to output function
    instance.io.pinMode(EXPANDS_GPIO_EN, OUTPUT);
}
#endif

bool hw_has_nrf24()
{
    if (hw_get_device_online() & HW_NRF24_ONLINE) {
        return true;
    }
    return false;
}

void hw_get_nrf24_params(radio_params_t &params)
{
    params.freq = 2400.0;
    params.cr = 1000;   //bit rate
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.power = 3;    // Index into nrf24_power_table: 0 dBm
    params.interval = 3000;
}

void hw_set_nrf24_address(const uint8_t *address, size_t length)
{
    if (!address || length < sizeof(nrf24_pipe_addr)) {
        return;
    }
    memcpy(nrf24_pipe_addr, address, sizeof(nrf24_pipe_addr));
}

void hw_get_nrf24_address(uint8_t *address, size_t length)
{
    if (!address || length < sizeof(nrf24_pipe_addr)) {
        return;
    }
    memcpy(address, nrf24_pipe_addr, sizeof(nrf24_pipe_addr));
}

int16_t hw_set_nrf24_params(radio_params_t &params)
{
#ifdef ARDUINO
    int state = RADIOLIB_ERR_NONE;
    uint8_t power_index = params.power;
    if (power_index >= (sizeof(nrf24_power_table) / sizeof(nrf24_power_table[0]))) {
        power_index = (sizeof(nrf24_power_table) / sizeof(nrf24_power_table[0])) - 1;
    }

    instance.lockSPI();

    state = nrf24.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        LILYGO_LOG_PRINTLN(F("Selected frequency is invalid for this module!"));
    }
    // Sets bit rate
    state = nrf24.setBitRate(params.cr);
    if (state == RADIOLIB_ERR_INVALID_CODING_RATE) {
        LILYGO_LOG_PRINTLN(F("Selected coding rate is invalid for this module!"));
    }
    // set output power
    state = nrf24.setOutputPower(nrf24_power_table[power_index]);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        LILYGO_LOG_PRINTLN(F("Selected output power is invalid for this module!"));
    }

    switch (params.mode) {
    case RADIO_DISABLE:
        state =  nrf24.standby();
        // Receiving function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, LOW);
        break;
    case RADIO_TX:
        // Transmit function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, HIGH);
        state = nrf24.setTransmitPipe(nrf24_pipe_addr);
        if (state == RADIOLIB_ERR_NONE) {
            LILYGO_LOG_PRINTLN(F("TX pipe configured"));
            if (radioEvent) {
                xEventGroupSetBits(radioEvent, NRF24_ISR_FLAG);
            }
        } else {
            LILYGO_LOG_PRINT(F("failed, code "));
            LILYGO_LOG_PRINTLN(state);
        }
        break;
    case RADIO_RX:
        // Receiving function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, LOW);
        state = nrf24.setReceivePipe(0, nrf24_pipe_addr);
        if (state == RADIOLIB_ERR_NONE) {
            LILYGO_LOG_PRINTLN(F("RX pipe configured"));
            state = nrf24.startReceive();
        } else {
            LILYGO_LOG_PRINT(F("failed, code "));
            LILYGO_LOG_PRINTLN(state);
        }
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

void hw_set_nrf24_listening()
{
}

void hw_clear_nrf24_flag()
{
#ifdef ARDUINO
    if (radioEvent) {
        xEventGroupSetBits(radioEvent, NRF24_ISR_FLAG);
    }
#endif
}

bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    if (!radioEvent) {
        params.state = -1;
        last_tx_state = params.state;
        return false;
    }

    if (continuous) {
        EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
        if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
            params.state = -1;
            last_tx_state = params.state;
            return false;
        }
    }

    if (!params.data) {
        params.state = -1;
        last_tx_state = params.state;
        LILYGO_LOG_PRINTF("rx data buffer is empty");
        return false;
    }
    if (params.length > 32) {
        params.length = 32;
    }

    LILYGO_LOG_PRINT("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        LILYGO_LOG_PRINTF("%02X,", params.data[i]);
    }
    LILYGO_LOG_PRINTLN();
    LILYGO_LOG_PRINT("[TX LEN:]");
    LILYGO_LOG_PRINTLN(params.length);

    instance.lockSPI();
    nrf24.finishTransmit();
    xEventGroupClearBits(radioEvent, NRF24_ISR_FLAG);
    params.state = nrf24.startTransmit((const uint8_t*)params.data, params.length, 0);
    instance.unlockSPI();
    last_tx_state = params.state;

    if (params.state == RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("transmission started!"));
    } else {
        LILYGO_LOG_PRINT(F("failed, code "));
        LILYGO_LOG_PRINTLN(params.state);
    }
    return params.state == RADIOLIB_ERR_NONE;
#else
    params.state = 0;
    return true;
#endif
}

bool hw_get_nrf24_tx_done(int16_t &state)
{
#ifdef ARDUINO
    if (!radioEvent) {
        state = -1;
        last_tx_state = state;
        return true;
    }

    EventBits_t eventBits = xEventGroupWaitBits(radioEvent,
                            NRF24_ISR_FLAG,
                            pdTRUE,
                            pdTRUE,
                            0);
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        return false;
    }

    instance.lockSPI();
    state = nrf24.finishTransmit();
    instance.unlockSPI();
    last_tx_state = state;
    return true;
#else
    state = 0;
    return true;
#endif
}

void hw_get_nrf24_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        LILYGO_LOG_PRINTF("rx data buffer is empty\n");
        return;
    }

    instance.lockSPI();
    size_t  length = nrf24.getPacketLength();
    params.length = length > params.length ? params.length : length;
    params.state = nrf24.readData(params.data, params.length);
    // Start next packet recv
    nrf24.startReceive();
    instance.unlockSPI();


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
    }
#endif
}

#endif
