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

static uint16_t xiaomi_lightbar_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFE;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void build_xiaomi_lightbar_packet(uint8_t packet[17], uint32_t remote_id,
        uint16_t command, uint8_t sequence)
{
    static const uint8_t preamble[8] = {
        0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12
    };

    memcpy(packet, preamble, sizeof(preamble));
    packet[8] = (remote_id >> 16) & 0xFF;
    packet[9] = (remote_id >> 8) & 0xFF;
    packet[10] = remote_id & 0xFF;
    packet[11] = 0xFF;
    packet[12] = sequence;
    packet[13] = (command >> 8) & 0xFF;
    packet[14] = command & 0xFF;

    const uint16_t crc = xiaomi_lightbar_crc16(packet, 15);
    packet[15] = (crc >> 8) & 0xFF;
    packet[16] = crc & 0xFF;
}

#ifdef ARDUINO

#include <LilyGoLib.h>

static EventGroupHandle_t    radioEvent = NULL;
static int16_t               last_tx_state = 0;
static bool                  xiaomi_capture_active = false;
static uint8_t               xiaomi_capture_channel_index = 0;
static uint32_t              xiaomi_capture_next_channel_at = 0;

#define NRF24_ISR_FLAG              _BV(1)

static constexpr uint8_t xiaomi_capture_channels[] = {6, 7, 15, 16, 43, 44, 68, 69};
static constexpr uint8_t xiaomi_capture_address[] = {0x1C, 0xDD, 0x14, 0x39, 0x53};
static constexpr uint8_t xiaomi_lightbar_preamble[] = {
    0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12
};
static constexpr size_t xiaomi_raw_packet_length = 12;
static constexpr size_t xiaomi_decoded_packet_length = 9;
static constexpr uint32_t xiaomi_channel_dwell_ms = 80;

#if defined(NRF24_CS)
static constexpr int xiaomi_nrf24_cs_pin = NRF24_CS;
#elif defined(BOARD_NRF24_CS)
static constexpr int xiaomi_nrf24_cs_pin = BOARD_NRF24_CS;
#else
static constexpr int xiaomi_nrf24_cs_pin = 44;
#endif

static void set_nrf24_pa_transmit(bool enable)
{
#if defined(ARDUINO_T_LORA_PAGER)
    instance.io.digitalWrite(EXPANDS_GPIO_EN, enable ? HIGH : LOW);
#else
    (void)enable;
#endif
}

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

// Caller must hold the shared SPI lock.
static uint8_t xiaomi_nrf24_spi_transfer(uint8_t command, const uint8_t *out,
        uint8_t *in, size_t length)
{
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    digitalWrite(xiaomi_nrf24_cs_pin, LOW);
    const uint8_t status = SPI.transfer(command);
    for (size_t i = 0; i < length; ++i) {
        const uint8_t value = out ? out[i] : 0xFF;
        const uint8_t result = SPI.transfer(value);
        if (in) in[i] = result;
    }
    digitalWrite(xiaomi_nrf24_cs_pin, HIGH);
    SPI.endTransaction();
    return status;
}

static void xiaomi_nrf24_write_register(uint8_t reg, uint8_t value)
{
    xiaomi_nrf24_spi_transfer(RADIOLIB_NRF24_CMD_WRITE | (reg & 0x1F),
                              &value, nullptr, 1);
}

static uint8_t xiaomi_nrf24_read_register(uint8_t reg)
{
    uint8_t value = 0;
    xiaomi_nrf24_spi_transfer(reg & 0x1F, nullptr, &value, 1);
    return value;
}

static int16_t xiaomi_listen_on_channel(uint8_t channel)
{
    int16_t state = nrf24.standby();
    if (state == RADIOLIB_ERR_NONE) {
        state = nrf24.setFrequency(2400.0f + channel);
    }
    if (state == RADIOLIB_ERR_NONE) state = nrf24.startReceive();
    return state;
}

static int16_t xiaomi_configure_remote_capture()
{
    int16_t state = nrf24.standby();
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setBitRate(2000);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAddressWidth(5);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAutoAck(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setCrcFiltering(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.disablePipe(0);
    for (uint8_t pipe = 2; state == RADIOLIB_ERR_NONE && pipe <= 5; ++pipe) {
        state = nrf24.disablePipe(pipe);
    }
    if (state == RADIOLIB_ERR_NONE) {
        state = nrf24.setReceivePipe(1, xiaomi_capture_address);
    }
    if (state != RADIOLIB_ERR_NONE) return state;

    // Proprietary Telink frames do not contain a valid dynamic-length field.
    xiaomi_nrf24_write_register(RADIOLIB_NRF24_REG_FEATURE, 0x00);
    xiaomi_nrf24_write_register(RADIOLIB_NRF24_REG_DYNPD, 0x00);
    xiaomi_nrf24_write_register(RADIOLIB_NRF24_REG_RX_PW_P1,
                                xiaomi_raw_packet_length);
    xiaomi_nrf24_spi_transfer(RADIOLIB_NRF24_CMD_FLUSH_RX, nullptr, nullptr, 0);
    xiaomi_nrf24_write_register(
        RADIOLIB_NRF24_REG_STATUS,
        RADIOLIB_NRF24_RX_DR | RADIOLIB_NRF24_TX_DS | RADIOLIB_NRF24_MAX_RT);
    if (xiaomi_nrf24_read_register(RADIOLIB_NRF24_REG_EN_AA) != 0x00 ||
            xiaomi_nrf24_read_register(RADIOLIB_NRF24_REG_DYNPD) != 0x00 ||
            xiaomi_nrf24_read_register(RADIOLIB_NRF24_REG_FEATURE) != 0x00 ||
            xiaomi_nrf24_read_register(RADIOLIB_NRF24_REG_RX_PW_P1) !=
            xiaomi_raw_packet_length) {
        return -1;
    }
    return RADIOLIB_ERR_NONE;
}

static int16_t xiaomi_transmit_packet(const uint8_t *packet, size_t length)
{
#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
    // The Embed expansion header has no nRF24 IRQ line.
    nrf24.finishTransmit();
    int16_t state = nrf24.startTransmit(packet, length, 0);
    const uint32_t started = millis();
    while (state == RADIOLIB_ERR_NONE) {
        const int16_t status = nrf24.getStatus(
                                   RADIOLIB_NRF24_TX_DS | RADIOLIB_NRF24_MAX_RT);
        if (status & RADIOLIB_NRF24_TX_DS) {
            return nrf24.finishTransmit();
        }
        if (status & RADIOLIB_NRF24_MAX_RT) {
            nrf24.finishTransmit();
            return RADIOLIB_ERR_ACK_NOT_RECEIVED;
        }
        if (millis() - started >= 70) {
            nrf24.finishTransmit();
            return RADIOLIB_ERR_TX_TIMEOUT;
        }
        delay(1);
    }
    return state;
#else
    return nrf24.transmit(packet, length, 0);
#endif
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
#if defined(ARDUINO_T_LORA_PAGER)
    nrf24.setPacketSentAction(hw_nrf24_isr);
    instance.io.pinMode(EXPANDS_GPIO_EN, OUTPUT);
#endif
}
#endif

bool hw_has_nrf24()
{
    if (!(hw_get_device_online() & HW_NRF24_ONLINE)) {
        return false;
    }
#if defined(ARDUINO_T_EMBED_PLUS_V2)
    // V2 shares the nRF24 CE/CS pins with the C5 UART. Keep this transition in
    // the board adapter so every Factory nRF24 entry uses the correct GPIO
    // matrix and XL9555 route without duplicating pin setup in each page.
    if (!instance.selectNRF24Path()) {
        LILYGO_LOG_E("[NRF24] failed to select V2 nRF24 path");
        return false;
    }
#endif
    return true;
}

int16_t hw_start_xiaomi_lightbar_remote_capture()
{
#ifdef ARDUINO
    if (!radioEvent || !hw_has_nrf24()) return -1;
    if (xiaomi_capture_active) return RADIOLIB_ERR_NONE;

    set_nrf24_pa_transmit(false);
    instance.lockSPI();
    int16_t state = xiaomi_configure_remote_capture();
    if (state == RADIOLIB_ERR_NONE) {
        xiaomi_capture_channel_index = 0;
        state = xiaomi_listen_on_channel(
                    xiaomi_capture_channels[xiaomi_capture_channel_index]);
    }
    if (state != RADIOLIB_ERR_NONE) nrf24.begin();
    instance.unlockSPI();

    xiaomi_capture_active = state == RADIOLIB_ERR_NONE;
    xiaomi_capture_next_channel_at = millis() + xiaomi_channel_dwell_ms;
    xEventGroupClearBits(radioEvent, NRF24_ISR_FLAG);
    return state;
#else
    return 0;
#endif
}

bool hw_poll_xiaomi_lightbar_remote(uint32_t &remote_id, uint8_t &channel)
{
#ifdef ARDUINO
    if (!xiaomi_capture_active) return false;

    uint8_t raw[xiaomi_raw_packet_length] = {};
    bool received = false;
    const uint32_t now = millis();

    instance.lockSPI();
    const int16_t status = nrf24.getStatus(RADIOLIB_NRF24_RX_DR);
    if (status >= 0 && (status & RADIOLIB_NRF24_RX_DR)) {
        const uint8_t active_channel =
            xiaomi_capture_channels[xiaomi_capture_channel_index];
        if (nrf24.standby() == RADIOLIB_ERR_NONE) {
            xiaomi_nrf24_spi_transfer(RADIOLIB_NRF24_CMD_READ_RX_PAYLOAD,
                                      nullptr, raw, sizeof(raw));
            xiaomi_nrf24_write_register(
                RADIOLIB_NRF24_REG_STATUS,
                RADIOLIB_NRF24_RX_DR | RADIOLIB_NRF24_TX_DS |
                RADIOLIB_NRF24_MAX_RT);
            received = true;
            channel = active_channel;
        }
        xiaomi_listen_on_channel(active_channel);
        xiaomi_capture_next_channel_at = now + xiaomi_channel_dwell_ms;
    } else if ((int32_t)(now - xiaomi_capture_next_channel_at) >= 0) {
        xiaomi_capture_channel_index =
            (xiaomi_capture_channel_index + 1U) %
            (sizeof(xiaomi_capture_channels) / sizeof(xiaomi_capture_channels[0]));
        xiaomi_listen_on_channel(
            xiaomi_capture_channels[xiaomi_capture_channel_index]);
        xiaomi_capture_next_channel_at = now + xiaomi_channel_dwell_ms;
    }
    instance.unlockSPI();

    if (!received) return false;

    uint8_t decoded[xiaomi_decoded_packet_length];
    memcpy(decoded, raw + 3, sizeof(decoded));
    if (decoded[3] != 0xFF) return false;

    uint8_t crc_input[15];
    memcpy(crc_input, xiaomi_lightbar_preamble,
           sizeof(xiaomi_lightbar_preamble));
    memcpy(crc_input + sizeof(xiaomi_lightbar_preamble), decoded, 7);
    const uint16_t received_crc = ((uint16_t)decoded[7] << 8) | decoded[8];
    if (received_crc != xiaomi_lightbar_crc16(crc_input, sizeof(crc_input))) {
        return false;
    }

    remote_id = ((uint32_t)decoded[0] << 16) |
                ((uint32_t)decoded[1] << 8) | decoded[2];
    return remote_id != 0 && remote_id != 0xFFFFFF;
#else
    (void)remote_id;
    (void)channel;
    return false;
#endif
}

void hw_stop_xiaomi_lightbar_remote_capture()
{
#ifdef ARDUINO
    if (!xiaomi_capture_active) return;
    xiaomi_capture_active = false;

    instance.lockSPI();
    nrf24.standby();
    const int16_t state = nrf24.begin();
#if defined(ARDUINO_T_LORA_PAGER)
    if (state == RADIOLIB_ERR_NONE) nrf24.setPacketSentAction(hw_nrf24_isr);
#else
    (void)state;
#endif
    instance.unlockSPI();
    set_nrf24_pa_transmit(false);
    if (radioEvent) xEventGroupClearBits(radioEvent, NRF24_ISR_FLAG);
#endif
}

int16_t hw_send_xiaomi_lightbar_command(uint32_t remote_id, uint16_t command,
                                        uint8_t sequence)
{
#ifdef ARDUINO
    if (!radioEvent || !hw_has_nrf24()) {
        return -1;
    }

    // MJGJD01YL protocol details:
    // https://github.com/lamperez/xiaomi-lightbar-nrf24
    // ESP32 reference implementation:
    // https://github.com/ebinf/lightbar2mqtt
    static const uint8_t sync_address[5] = {0x55, 0x55, 0x55, 0x55, 0x55};
    static const uint8_t channels[] = {6, 15, 43, 68};
    static const uint8_t repetitions_per_channel = 5;
    uint8_t packet[17];
    build_xiaomi_lightbar_packet(packet, remote_id & 0xFFFFFF, command, sequence);

    int16_t state = RADIOLIB_ERR_NONE;
    set_nrf24_pa_transmit(true);
    instance.lockSPI();

    state = nrf24.standby();
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setBitRate(2000);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setOutputPower(0);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setAddressWidth(5);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setCrcFiltering(false);
    if (state == RADIOLIB_ERR_NONE) state = nrf24.setTransmitPipe(sync_address);

    for (uint8_t repeat = 0;
            state == RADIOLIB_ERR_NONE && repeat < repetitions_per_channel;
            ++repeat) {
        for (uint8_t i = 0; i < sizeof(channels); ++i) {
            state = nrf24.setFrequency(2400.0f + channels[i]);
            if (state != RADIOLIB_ERR_NONE) break;
            state = xiaomi_transmit_packet(packet, sizeof(packet));
            if (state != RADIOLIB_ERR_NONE) break;
            delay(2);
        }
    }

    nrf24.standby();
    nrf24.setFrequency(2400.0f);
    nrf24.setCrcFiltering(true);
    nrf24.setAutoAck(true);
    instance.unlockSPI();
    set_nrf24_pa_transmit(false);
    xEventGroupClearBits(radioEvent, NRF24_ISR_FLAG);

    LILYGO_LOG_PRINTF("MJGJD01YL id=%06lX command=%04X seq=%u state=%d\n",
                      (unsigned long)(remote_id & 0xFFFFFF), command, sequence, state);
    return state;
#else
    (void)remote_id;
    (void)command;
    (void)sequence;
    return 0;
#endif
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
        set_nrf24_pa_transmit(false);
        break;
    case RADIO_TX:
        // Transmit function
        set_nrf24_pa_transmit(true);
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
        set_nrf24_pa_transmit(false);
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

#if !defined(ARDUINO_T_EMBED_PLUS) && !defined(ARDUINO_T_EMBED_PLUS_V2)
    if (continuous) {
        EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdMS_TO_TICKS(2));
        if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
            params.state = -1;
            last_tx_state = params.state;
            return false;
        }
    }
#else
    (void)continuous;
#endif

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

#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
    // The T-Embed expansion header has no nRF24 IRQ line, so poll the status
    // register over SPI instead of waiting for a GPIO interrupt.
    instance.lockSPI();
    nrf24.finishTransmit();
    params.state = nrf24.startTransmit((const uint8_t *)params.data, params.length, 0);
    const uint32_t started = millis();
    while (params.state == RADIOLIB_ERR_NONE) {
        const int16_t status = nrf24.getStatus(RADIOLIB_NRF24_TX_DS | RADIOLIB_NRF24_MAX_RT);
        if (status & RADIOLIB_NRF24_TX_DS) {
            params.state = nrf24.finishTransmit();
            break;
        }
        if (status & RADIOLIB_NRF24_MAX_RT) {
            nrf24.finishTransmit();
            params.state = RADIOLIB_ERR_ACK_NOT_RECEIVED;
            break;
        }
        if (millis() - started >= 70) {
            nrf24.finishTransmit();
            params.state = RADIOLIB_ERR_TX_TIMEOUT;
            break;
        }
        delay(1);
    }
    instance.unlockSPI();
#else
    instance.lockSPI();
    nrf24.finishTransmit();
    xEventGroupClearBits(radioEvent, NRF24_ISR_FLAG);
    params.state = nrf24.startTransmit((const uint8_t*)params.data, params.length, 0);
    instance.unlockSPI();
#endif
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
#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
    state = last_tx_state;
    return true;
#else
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
#endif
#else
    state = 0;
    return true;
#endif
}

void hw_get_nrf24_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
#if !defined(ARDUINO_T_EMBED_PLUS) && !defined(ARDUINO_T_EMBED_PLUS_V2)
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return;
    }
#endif

    if (!params.data) {
        params.state = -1;
        LILYGO_LOG_PRINTF("rx data buffer is empty\n");
        return;
    }

    instance.lockSPI();
#if defined(ARDUINO_T_EMBED_PLUS) || defined(ARDUINO_T_EMBED_PLUS_V2)
    if (!(nrf24.getStatus(RADIOLIB_NRF24_RX_DR) & RADIOLIB_NRF24_RX_DR)) {
        instance.unlockSPI();
        params.length = 0;
        params.state = -1;
        return;
    }
#endif
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
