/**
 * @file      hw_nrf2401.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 */

#include "hal_interface.h"

#if defined(USING_EXTERN_NRF2401)

#include <LilyGoLib.h>

static EventGroupHandle_t    radioEvent = NULL;

#define NRF24_ISR_FLAG              _BV(1)

static void hw_nrf24_isr()
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
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
    printf(" init NRF2401 \n");
    bool rlst = instance.initNRF24();
    if (!rlst) {
        printf("nRF2401 option Model not detected\n");
        return;
    }
    nrf24.setPacketSentAction(hw_nrf24_isr);
}

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
    params.cr = 1000;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.power = 0;
    params.interval = 3000;
}

int16_t hw_set_nrf24_params(radio_params_t &params)
{
    static uint8_t addr[] = {0x01, 0x23, 0x45, 0x67, 0x89};
    int state = RADIOLIB_ERR_NONE;

    instance.lockSPI();

    state = nrf24.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
    }
    // set coding rate
    state = nrf24.setBitRate(params.cr);
    if (state == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
    }
    // set output power
    state = nrf24.setOutputPower(params.power);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
    }

    switch (params.mode) {
    case RADIO_DISABLE:
        state =  nrf24.standby();
        instance.powerControl(POWER_EXT_GPIO, false);
        break;
    case RADIO_TX:
        instance.powerControl(POWER_EXT_GPIO, true);
        state = nrf24.setTransmitPipe(addr);
        if (state == RADIOLIB_ERR_NONE) {
            Serial.println(F("success!"));
            nrf24.startTransmit("Hello World!");
        } else {
            Serial.print(F("failed, code "));
            Serial.println(state);
        }
        break;
    case RADIO_RX:
        instance.powerControl(POWER_EXT_GPIO, true);
        state = nrf24.setReceivePipe(0, addr);
        if (state == RADIOLIB_ERR_NONE) {
            Serial.println(F("success!"));
            state = nrf24.startReceive();
        } else {
            Serial.print(F("failed, code "));
            Serial.println(state);
        }
        break;
    default:
        break;
    }
    instance.unlockSPI();

    return state;
}

void hw_set_nrf24_listening()
{
}

void hw_clear_nrf24_flag()
{
#ifdef ARDUINO
    xEventGroupSetBits(radioEvent, NRF24_ISR_FLAG);
#endif
}

bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous)
{
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return false;
    }

    if (!params.data) {
        params.state = -1;
        printf("rx data buffer is empty");
        return false;
    }

    Serial.print("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        Serial.printf("%02X,", params.data[i]);
    }
    Serial.println();
    Serial.print("[TX LEN:]");
    Serial.println(params.length);

    instance.lockSPI();
    params.state = nrf24.startTransmit((const uint8_t*)params.data, params.length, 0);
    instance.unlockSPI();

    if (params.state == RADIOLIB_ERR_NONE) {
        // packet was successfully sent
        Serial.println(F("transmission finished!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(params.state);
    }
    return true;
}

void hw_get_nrf24_rx(radio_rx_params_t &params)
{
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        printf("rx data buffer is empty");
        return;
    }

    instance.lockSPI();
    params.length = nrf24.getPacketLength();
    params.state = nrf24.readData(params.data, params.length);
    // Start next packet recv
    nrf24.startReceive();
    instance.unlockSPI();


    Serial.print("[RX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        Serial.printf("%02X,", params.data[i]);
    }
    Serial.println();
    Serial.print("[RX LEN:]");
    Serial.println(params.length);

    if (params.state == RADIOLIB_ERR_NONE && params.length != 0) {
        // packet was successfully received
        Serial.println(F("[Radio] Received packet!"));
        Serial.print("[LEN]:");
        Serial.println(params.length);
        Serial.print("[PAYLOAD]:");
        Serial.println((char *)params.data);
    }
}

#endif
