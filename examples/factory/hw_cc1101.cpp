/**
 * @file      hw_cc1101.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 * Implements interrupt-driven CC1101 configuration, transmit, and receive support.
 */

#include <LilyGoLog.h>
#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_CC1101


#include <LilyGoLib.h>
#include <string.h>

static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;
static uint32_t last_rx_diagnostic_millis = 0;
static int16_t last_tx_state = 0;
static volatile uint32_t rx_irq_count = 0;
static volatile uint32_t tx_irq_count = 0;

#define LORA_RX_ISR_FLAG               _BV(0)
#define LORA_TX_ISR_FLAG               _BV(1)
#define LORA_ISR_FLAGS                 (LORA_RX_ISR_FLAG | LORA_TX_ISR_FLAG)

#define RADIO_DEFAULT_BIT_RATE       1.2    // kbps
#define RADIO_DEFAULT_DEV_FREQ       5.2    // kHz

static float hw_get_cc1101_bit_rate(uint16_t value)
{
    if (value == 0 || value == 1) {
        return RADIO_DEFAULT_BIT_RATE;
    }
    return value == 38 ? 38.4f : static_cast<float>(value);
}

static void hw_log_cc1101_step(const char *step, int16_t state)
{
    LILYGO_LOG_PRINTF("[CC1101][CFG] %-22s state=%d\n", step, state);
}

static void IRAM_ATTR hw_radio_rx_isr()
{
    if (!radioEvent) {
        return;
    }
    __atomic_add_fetch(&rx_irq_count, 1U, __ATOMIC_RELAXED);
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  LORA_RX_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

static void IRAM_ATTR hw_radio_tx_isr()
{
    if (!radioEvent) {
        return;
    }
    __atomic_add_fetch(&tx_irq_count, 1U, __ATOMIC_RELAXED);
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  LORA_TX_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if (xResult == pdPASS) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void hw_radio_begin()
{
    radioEvent = xEventGroupCreate();

    // T-Embed-Plus routes GDO0 to the packet IRQ and GDO2 to the packet
    // sent IRQ. RadioLib attaches the callbacks to these pins below.
    pinMode(LORA_IRQ, INPUT);
    pinMode(LORA_BUSY, INPUT);
    radio.setPacketSentAction(hw_radio_tx_isr);
    radio.setPacketReceivedAction(hw_radio_rx_isr);
    LILYGO_LOG_PRINTF("[CC1101][IRQ] attached GDO0=GPIO%d RX, GDO2=GPIO%d TX\n",
                      LORA_IRQ, LORA_BUSY);
    LILYGO_LOG_PRINTF("[CC1101][IRQ] initial levels GDO0=%d GDO2=%d\n",
                      digitalRead(LORA_IRQ), digitalRead(LORA_BUSY));
}

int16_t hw_set_radio_params(radio_params_t &params)
{
    const float bit_rate = hw_get_cc1101_bit_rate(params.cr);
    LILYGO_LOG_PRINTF("Set radio params:\n");
    LILYGO_LOG_PRINTF("Frequency:%.2f MHz\n", params.freq);
    LILYGO_LOG_PRINTF("Bandwidth:%.2f KHz\n", params.bandwidth);
    LILYGO_LOG_PRINTF("TxPower:%u dBm\n", params.power);
    LILYGO_LOG_PRINTF("Interval:%u ms\n", params.interval);
    LILYGO_LOG_PRINTF("BitRate:%.1f kbps\n", bit_rate);
    LILYGO_LOG_PRINTF("Modulation:%s\n", params.sf ? "OOK" : "2-FSK");
    LILYGO_LOG_PRINTF("SyncWord:0x%02X 0x23\n", params.syncWord);
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
#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
    state = instance.setRFFrequencyBand(params.freq);
    hw_log_cc1101_step("set frequency", state);
#if defined(ARDUINO_T_EMBED_PLUS_V2)
    LILYGO_LOG_ONLY({
        const uint16_t rf_switch_state = instance.io.digitalReadPort();
        LILYGO_LOG_PRINTF("[CC1101][CFG] RF path SW1=%d SW0=%d\n",
                          (rf_switch_state >> BOARD_XL9555_CC_SW1) & 1U,
                          (rf_switch_state >> BOARD_XL9555_CC_SW0) & 1U);
    });
#else
    LILYGO_LOG_PRINTF("[CC1101][CFG] RF path SW1=%d SW0=%d\n",
                      digitalRead(BOARD_LORA_SW1), digitalRead(BOARD_LORA_SW0));
#endif
#else
    state = radio.setFrequency(params.freq);
    hw_log_cc1101_step("set frequency", state);
#endif
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        LILYGO_LOG_PRINTLN(F("Selected frequency is invalid for this module!"));
    }
    // set modulation before output power so the PA table matches ASK/OOK or FSK.
    state = radio.setOOK(params.sf != 0);
    hw_log_cc1101_step(params.sf ? "set modulation OOK" : "set modulation 2-FSK", state);
    if (state != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Unable to set modulation!"));
    }
    // set bandwidth
    state = radio.setRxBandwidth(params.bandwidth);
    hw_log_cc1101_step("set RX bandwidth", state);
    if (state == RADIOLIB_ERR_INVALID_RX_BANDWIDTH) {
        LILYGO_LOG_PRINTLN(F("Selected bandwidth is invalid for this module!"));
    }
    // set CC1101 sync word
    state = radio.setSyncWord(params.syncWord, 0x23);
    hw_log_cc1101_step("set sync word", state);
    if (state  != RADIOLIB_ERR_NONE) {
        LILYGO_LOG_PRINTLN(F("Unable to set sync word!"));
    }
    // set output power
    state = radio.setOutputPower(params.power);
    hw_log_cc1101_step("set output power", state);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        LILYGO_LOG_PRINTLN(F("Selected output power is invalid for this module!"));
    }
    // set bit rate
    state = radio.setBitRate(bit_rate);
    hw_log_cc1101_step("set bit rate", state);
    if (state == RADIOLIB_ERR_INVALID_BIT_RATE) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Selected bit rate is invalid for this module!"));
    } else if (state == RADIOLIB_ERR_INVALID_BIT_RATE_BW_RATIO) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Increase receiver bandwidth for this bit rate."));
    }
    // Keep the packet profile aligned for both OOK and 2-FSK modes.
    state = radio.setFrequencyDeviation(RADIO_DEFAULT_DEV_FREQ);
    hw_log_cc1101_step("set frequency dev", state);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY_DEVIATION) {
        LILYGO_LOG_PRINTLN(F("[CC1101] Selected frequency deviation is invalid for this module!"));
    }

    LILYGO_LOG_PRINTF("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        LILYGO_LOG_PRINTF("RADIO_DISABLE\n");
        if (radioEvent) {
            xEventGroupClearBits(radioEvent, LORA_ISR_FLAGS);
        }
        state =  radio.standby();
        LILYGO_LOG_PRINTF("[CC1101][MODE] standby state=%d GDO0=%d GDO2=%d\n",
                          state, digitalRead(LORA_IRQ), digitalRead(LORA_BUSY));
        break;
    case RADIO_TX:
        state =  radio.standby();
        if (radioEvent) {
            xEventGroupClearBits(radioEvent, LORA_RX_ISR_FLAG);
            xEventGroupSetBits(radioEvent, LORA_TX_ISR_FLAG);
        }
        LILYGO_LOG_PRINTF("[CC1101][MODE] TX ready state=%d GDO0=%d GDO2=%d\n",
                          state, digitalRead(LORA_IRQ), digitalRead(LORA_BUSY));
        break;
    case RADIO_RX:
        if (radioEvent) {
            xEventGroupClearBits(radioEvent, LORA_ISR_FLAGS);
        }
        state =  radio.startReceive();
        last_rx_diagnostic_millis = millis();
        LILYGO_LOG_PRINTF("[CC1101][MODE] start RX state=%d GDO0=%d GDO2=%d\n",
                          state,
                          digitalRead(LORA_IRQ), digitalRead(LORA_BUSY));
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
    params.bandwidth = 58.0;
    params.freq = 433.92;
    params.cr = 1;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 1;
    params.power = 10;
    params.interval = 3000;
    params.syncWord = 0x01;
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
                                 LORA_TX_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
        if ((eventBits & LORA_TX_ISR_FLAG) != LORA_TX_ISR_FLAG) {
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
    xEventGroupClearBits(radioEvent, LORA_TX_ISR_FLAG);
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
                            LORA_TX_ISR_FLAG,
                            pdTRUE,
                            pdTRUE,
                            0);
    if ((eventBits & LORA_TX_ISR_FLAG) != LORA_TX_ISR_FLAG) {
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
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, LORA_RX_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
    if ((eventBits & LORA_RX_ISR_FLAG) != LORA_RX_ISR_FLAG) {
        params.state = -1;
        const uint32_t now = millis();
        if (now - last_rx_diagnostic_millis >= 2000) {
            last_rx_diagnostic_millis = now;
            instance.lockSPI();
            const float live_rssi = radio.getRSSI();
            instance.unlockSPI();
            LILYGO_LOG_PRINTF("[CC1101][RX] waiting irq=%lu RSSI=%.1f GDO0=%d GDO2=%d",
                              static_cast<unsigned long>(rx_irq_count), live_rssi,
                              digitalRead(LORA_IRQ), digitalRead(LORA_BUSY));
#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
#if defined(ARDUINO_T_EMBED_PLUS_V2)
            LILYGO_LOG_ONLY({
                const uint16_t rf_switch_state = instance.io.digitalReadPort();
                LILYGO_LOG_PRINTF(" SW1=%d SW0=%d",
                                  (rf_switch_state >> BOARD_XL9555_CC_SW1) & 1U,
                                  (rf_switch_state >> BOARD_XL9555_CC_SW0) & 1U);
            });
#else
            LILYGO_LOG_PRINTF(" SW1=%d SW0=%d", digitalRead(BOARD_LORA_SW1),
                              digitalRead(BOARD_LORA_SW0));
#endif
#endif
            LILYGO_LOG_PRINTLN();
        }
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
    LILYGO_LOG_PRINTF("[CC1101][IRQ] RX event #%lu GDO0=%d\n",
                      static_cast<unsigned long>(rx_irq_count), digitalRead(LORA_IRQ));
    size_t packet_len = radio.getPacketLength();
    LILYGO_LOG_PRINTF("[CC1101][RX] packet length=%u capacity=%u\n",
                      static_cast<unsigned>(packet_len), static_cast<unsigned>(capacity));
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
    const int16_t restart_state = radio.startReceive();
    instance.unlockSPI();
    LILYGO_LOG_PRINTF("[CC1101][RX] read state=%d restart state=%d RSSI=%d\n",
                      params.state, restart_state, params.rssi);


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
    if (!radioEvent || !data || length == 0) {
        last_tx_state = -1;
        return false;
    }

    LILYGO_LOG_PRINTF("[CC1101][TX] begin length=%u txIrq=%lu GDO2=%d\n",
                      static_cast<unsigned>(length), static_cast<unsigned long>(tx_irq_count),
                      digitalRead(LORA_BUSY));
    instance.lockSPI();
    int state = radio.standby();
    LILYGO_LOG_PRINTF("[CC1101][TX] standby state=%d\n", state);
    if (state == RADIOLIB_ERR_NONE) {
        xEventGroupClearBits(radioEvent, LORA_TX_ISR_FLAG);
        state = radio.startTransmit(data, length);
        LILYGO_LOG_PRINTF("[CC1101][TX] startTransmit state=%d GDO2=%d\n",
                          state, digitalRead(LORA_BUSY));
    }
    instance.unlockSPI();

    if (state == RADIOLIB_ERR_NONE) {
        // Wait for the CC1101 GDO2 packet-sent IRQ. This keeps the direct
        // Factory tool path interrupt-driven instead of polling RadioLib.
        const EventBits_t eventBits = xEventGroupWaitBits(
            radioEvent, LORA_TX_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2000));
        LILYGO_LOG_PRINTF("[CC1101][IRQ] TX wait bits=0x%02X count=%lu GDO2=%d\n",
                          static_cast<unsigned>(eventBits), static_cast<unsigned long>(tx_irq_count),
                          digitalRead(LORA_BUSY));
        if ((eventBits & LORA_TX_ISR_FLAG) == 0) {
            state = RADIOLIB_ERR_TX_TIMEOUT;
        }
    }

    instance.lockSPI();
    const int16_t finishState = radio.finishTransmit();
    instance.unlockSPI();
    if (state == RADIOLIB_ERR_NONE) {
        state = finishState;
    }
    last_tx_state = state;
    last_send_millis = millis();
    LILYGO_LOG_PRINTF("[CC1101][TX] finish state=%d final=%d\n", finishState, state);
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
