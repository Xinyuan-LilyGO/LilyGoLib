/**
 * @file      LilyGoMifareClassicRf.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Receives Classic frames through FIFO and sends custom parity through transparent MOSI.
 */
#include "LilyGoLib.h"
#if defined(ARDUINO) && defined(USING_ST25R3916)
#include "nfc/LilyGoMifareClassicRf.h"
#include <SPI.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>
#include <hal/gpio_ll.h>
#include <esp_random.h>
#include <string.h>

namespace {
constexpr uint32_t kClockHz = 80000000;
#if LILYGO_NFC_DEBUG_ENABLED
void logTargetMemory(const char *stage, ReturnCode error, const uint8_t *expected, const uint8_t *actual)
{
    static const char hex[] = "0123456789ABCDEF";
    char wanted[ST25R3916_PTM_A_LEN * 3] = {}, received[ST25R3916_PTM_A_LEN * 3] = {};
    unsigned mismatches = 0;
    for (unsigned i = 0; i < ST25R3916_PTM_A_LEN; ++i) {
        wanted[i * 3] = hex[expected[i] >> 4]; wanted[i * 3 + 1] = hex[expected[i] & 15];
        received[i * 3] = hex[actual[i] >> 4]; received[i * 3 + 1] = hex[actual[i] & 15];
        if (i + 1 < ST25R3916_PTM_A_LEN) wanted[i * 3 + 2] = received[i * 3 + 2] = ' ';
        if (expected[i] != actual[i]) ++mismatches;
    }
    // These bytes contain only the target identity, never Classic sector keys.
    LILYGO_NFC_LOG_I("[ST25][MFC][EMUL][PTMEM] stage=%s code=%u mismatch_bytes=%u expected=%s actual=%s",
                    stage, error, mismatches, wanted, received);
}
#endif
struct MosiRoute {
    uint32_t matrix, mux;
    bool outputEnabled;
    MosiRoute() {
        matrix = GPIO.func_out_sel_cfg[MOSI].val;
        mux = REG_READ(GPIO_PIN_MUX_REG[MOSI]);
        outputEnabled = ((MOSI < 32 ? GPIO.enable : GPIO.enable1.val) >> (MOSI & 31)) & 1;
    }
    void restore() const {
        REG_WRITE(GPIO_PIN_MUX_REG[MOSI], mux);
        GPIO.func_out_sel_cfg[MOSI].val = matrix;
        if (outputEnabled) gpio_ll_output_enable(&GPIO, MOSI);
        else gpio_ll_output_disable(&GPIO, MOSI);
    }
};
} // namespace

LilyGoMifareClassicListener::LilyGoMifareClassicListener() : _rf(nullptr), _channel(nullptr),
    _encoder(nullptr), _task(nullptr), _stopped(nullptr), _stopRequested(false), _status{},
    _dump{}, _session{}, _rx{}, _rxBits(0), _transmitterEnabled(false), _sleeping(false),
    _rmtMatrix(0), _rmtMux(0), _pulses{}, _symbols{} {}
LilyGoMifareClassicListener::~LilyGoMifareClassicListener() { stop(); }

bool LilyGoMifareClassicListener::begin(RfalRfClass *rf, const LilyGoMifareClassicDump &dump)
{
    stop();
    _status = {};
    if (!rf || !lilygoMifareClassicCanEmulate(dump)) {
        fail(ST_ERR_PARAM);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][START] stage=image code=%u rf_present=%u uid_len=%u blocks=%u/%u",
                        ST_ERR_PARAM, rf != nullptr, dump.uidLen, dump.blocksRead, dump.totalBlocks);
        return false;
    }
    _rf = static_cast<RfalRfST25R3916Class *>(rf);
    _dump = dump; _session = {}; _stopRequested.store(false);
    LILYGO_NFC_LOG_I("[ST25][MFC][EMUL][START] stage=allocate mosi=%d cs=%d irq=%d rmt_hz=%lu",
                    MOSI, NFC_CS, NFC_INT, static_cast<unsigned long>(kClockHz));
    rmt_tx_channel_config_t config = {};
    config.gpio_num = gpio_num_t(MOSI);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = kClockHz;
    config.mem_block_symbols = 1024;
    config.trans_queue_depth = 1;
    config.flags.with_dma = true;
    rmt_copy_encoder_config_t encoderConfig = {};
    // Bind once while the bus is idle, then restore SPI immediately. The RMT
    // driver keeps the channel; frame transmission only changes the matrix.
    if (!instance.lockSPI(pdMS_TO_TICKS(100))) {
        fail(ST_ERR_BUSY);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][START] stage=spi_lock code=%u", ST_ERR_BUSY);
        stop(); return false;
    }
    SPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE1));
    const MosiRoute spiRoute;
    esp_err_t error = rmt_new_tx_channel(&config, &_channel);
    if (error == ESP_OK) error = rmt_new_copy_encoder(&encoderConfig, &_encoder);
    _rmtMatrix = GPIO.func_out_sel_cfg[MOSI].val;
    _rmtMux = REG_READ(GPIO_PIN_MUX_REG[MOSI]);
    spiRoute.restore();
    SPI.endTransaction();
    instance.unlockSPI();
    if (error != ESP_OK) {
        fail(ST_ERR_NOMEM);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL] transmitter allocation error=%ld", static_cast<long>(error));
        stop(); return false;
    }
    error = rmt_enable(_channel);
    if (error != ESP_OK) {
        fail(ST_ERR_IO);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL] transmitter enable error=%ld", static_cast<long>(error));
        stop(); return false;
    }
    _transmitterEnabled = true;
    _stopped = xSemaphoreCreateBinary();
    if (!_stopped) {
        fail(ST_ERR_NOMEM);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][START] stage=semaphore code=%u", ST_ERR_NOMEM);
        stop(); return false;
    }
    const ReturnCode listenError = restart();
    if (listenError != ST_ERR_NONE) {
        fail(listenError);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][START] stage=listen_config code=%u", listenError);
        stop(); return false;
    }
#if LILYGO_NFC_DEBUG_ENABLED
    _diagnosticAtMs = millis(); _diagnosticPolls = 0; _diagnosticIrqPolls = 0;
    _diagnosticLastState = 0xFF;
    captureDiagnostics();
#endif
    if (xTaskCreate(taskEntry, "nfc_classic", 6144, this, 3, &_task) != pdPASS) {
        fail(ST_ERR_NOMEM);
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][START] stage=task code=%u", ST_ERR_NOMEM);
        stop(); return false;
    }
    LILYGO_NFC_LOG_I("[ST25][MFC][EMUL][START] stage=ready code=%u", ST_ERR_NONE);
    return true;
}

void LilyGoMifareClassicListener::releaseTransmitter()
{
    if (_channel) {
        if (_transmitterEnabled) rmt_disable(_channel);
        _transmitterEnabled = false;
        // Deletion disables the bound GPIO. Keep both bus locks held and
        // restore its output-enable bit as well as its mux after deletion.
        if (instance.lockSPI(portMAX_DELAY)) {
            SPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE1));
            const MosiRoute spiRoute;
            rmt_del_channel(_channel); _channel = nullptr;
            spiRoute.restore();
            SPI.endTransaction();
            instance.unlockSPI();
        }
    }
    if (_encoder) { rmt_del_encoder(_encoder); _encoder = nullptr; }
}

void LilyGoMifareClassicListener::stop()
{
    if (_task) {
        _stopRequested.store(true);
        xSemaphoreTake(_stopped, portMAX_DELAY);
        _task = nullptr;
    }
    if (_rf) _rf->rfalListenStop();
    if (_stopped) { vSemaphoreDelete(_stopped); _stopped = nullptr; }
    releaseTransmitter();
    _rf = nullptr;
}

LilyGoMifareClassicRfStatus LilyGoMifareClassicListener::status()
{
    portENTER_CRITICAL(&_statusMux);
    const auto result = _status;
    portEXIT_CRITICAL(&_statusMux);
    return result;
}

void LilyGoMifareClassicListener::taskEntry(void *argument)
{
    auto *listener = static_cast<LilyGoMifareClassicListener *>(argument);
    listener->run();
    xSemaphoreGive(listener->_stopped);
    vTaskDelete(nullptr);
}

void LilyGoMifareClassicListener::fail(ReturnCode error)
{
    _session = {};
    portENTER_CRITICAL(&_statusMux);
    ++_status.errors; _status.lastError = error; _status.readerActive = false;
    portEXIT_CRITICAL(&_statusMux);
}

ReturnCode LilyGoMifareClassicListener::restart()
{
    _session = {}; _rxBits = 0; _sleeping = false;
    ReturnCode error = _rf->rfalListenStop();
    if (error != ST_ERR_NONE) {
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][CONFIG] stage=listen_stop code=%u", error);
        return error;
    }
    rfalLmConfPA config = {};
    config.nfcidLen = RFAL_LM_NFCID_LEN_04;
    memcpy(config.nfcid, _dump.uid, 4);
    memcpy(config.SENS_RES, _dump.atqa, 2);
    config.SEL_RES = _dump.sak;
    error = _rf->rfalListenStart(RFAL_LM_MASK_NFCA, &config, nullptr, nullptr,
                               _rx, sizeof(_rx) * 8, &_rxBits);
    if (error != ST_ERR_NONE) {
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][CONFIG] stage=listen_start code=%u", error);
        return error;
    }
    LILYGO_NFC_LOG_ONLY(if (!_task) logConfigurationState("after_listen_start"));
    // ListenStart may switch off the crystal in POWER_OFF. PT memory access
    // requires en=1 and a stable oscillator (DS12484, section 4.3.3), even
    // though register access and the SPI helpers can still report success.
    _rf->st25r3916OscOn();
    uint8_t operation = 0, auxiliary = 0;
    error = _rf->st25r3916ReadRegister(ST25R3916_REG_OP_CONTROL, &operation);
    if (error == ST_ERR_NONE) error = _rf->st25r3916ReadRegister(ST25R3916_REG_AUX_DISPLAY, &auxiliary);
    if (error != ST_ERR_NONE || !(operation & ST25R3916_REG_OP_CONTROL_en) ||
            !(auxiliary & ST25R3916_REG_AUX_DISPLAY_osc_ok)) {
        LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][CONFIG] stage=oscillator code=%u op=0x%02X aux_display=0x%02X",
                        error, operation, auxiliary);
        return error == ST_ERR_NONE ? ST_ERR_IO : error;
    }
    LILYGO_NFC_LOG_ONLY(if (!_task) logConfigurationState("before_ptmem"));
    uint8_t expected[ST25R3916_PTM_A_LEN] = {}, memory[ST25R3916_PTM_A_LEN] = {};
    memcpy(expected, _dump.uid, 4); memcpy(expected + 10, _dump.atqa, 2);
    expected[12] = expected[13] = expected[14] = _dump.sak;
    error = _rf->st25r3916ReadPTMem(memory, sizeof(memory));
    LILYGO_NFC_LOG_ONLY(if (!_task || error != ST_ERR_NONE || memcmp(expected, memory, sizeof(memory)))
        logTargetMemory("first_read", error, expected, memory));
    if (error == ST_ERR_NONE && memcmp(expected, memory, sizeof(memory))) {
        // Verify the actual hardware identity before advertising success.
        error = _rf->st25r3916WritePTMem(expected, sizeof(expected));
        LILYGO_NFC_LOG_W("[ST25][MFC][EMUL][CONFIG] stage=ptmem_rewrite code=%u", error);
        if (error == ST_ERR_NONE) error = _rf->st25r3916ReadPTMem(memory, sizeof(memory));
        LILYGO_NFC_LOG_ONLY(logTargetMemory("rewrite_read", error, expected, memory));
    }
    const bool identityOk = error == ST_ERR_NONE && !memcmp(expected, memory, sizeof(memory));
    LILYGO_NFC_LOG_ONLY(if (!_task || !identityOk) LILYGO_NFC_LOG_I("[ST25][MFC][EMUL][CONFIG] stage=verify uid_ok=%u atqa_ok=%u sak_ok=%u code=%u",
            !memcmp(expected, memory, 4), !memcmp(expected + 10, memory + 10, 2),
            !memcmp(expected + 12, memory + 12, 3), error));
#if LILYGO_NFC_DEBUG_ENABLED
    portENTER_CRITICAL(&_statusMux); _status.diagnostics.memoryOk = identityOk; portEXIT_CRITICAL(&_statusMux);
#endif
    if (!identityOk) return error == ST_ERR_NONE ? ST_ERR_IO : error;
    // Return to RFAL's field-detection path after memory access. RFAL keeps
    // IDLE if a reader field is already present; otherwise it waits for EON.
    error = _rf->rfalListenSetState(RFAL_LM_STATE_POWER_OFF);
    if (error != ST_ERR_NONE) LILYGO_NFC_LOG_E("[ST25][MFC][EMUL][CONFIG] stage=arm_field code=%u", error);
    LILYGO_NFC_LOG_ONLY(if (!_task || error != ST_ERR_NONE) logConfigurationState("armed"));
    return error;
}

#if LILYGO_NFC_DEBUG_ENABLED
void LilyGoMifareClassicListener::logConfigurationState(const char *stage)
{
    uint8_t operation = 0, auxiliary = 0, mode = 0, target = 0, targetState = 0;
    uint8_t mask = 0, activation = 0, deactivation = 0;
    const uint8_t registers[] = {ST25R3916_REG_OP_CONTROL, ST25R3916_REG_AUX_DISPLAY,
        ST25R3916_REG_MODE, ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_STATUS,
        ST25R3916_REG_IRQ_MASK_TIMER_NFC, ST25R3916_REG_FIELD_THRESHOLD_ACTV,
        ST25R3916_REG_FIELD_THRESHOLD_DEACTV};
    uint8_t *values[] = {&operation, &auxiliary, &mode, &target, &targetState, &mask, &activation, &deactivation};
    ReturnCode error = ST_ERR_NONE;
    for (unsigned i = 0; i < sizeof(registers); ++i) {
        const ReturnCode current = _rf->st25r3916ReadRegister(registers[i], values[i]);
        if (current != ST_ERR_NONE) error = current;
    }
    // Read configuration/display registers only: IRQ status reads would consume
    // the very EON/selection events needed by the listen worker.
    LILYGO_NFC_LOG_I("[ST25][MFC][EMUL][CONFIG] stage=%s state=%u en=%u osc_ok=%u rx=%u tx=%u "
                    "field=%u irq=%u op=0x%02X aux_display=0x%02X mode=0x%02X pt=0x%02X pta=0x%02X "
                    "mask_timer_nfc=0x%02X field_actv=0x%02X field_deactv=0x%02X reg_error=%u",
                    stage, _rf->rfalListenGetState(nullptr, nullptr),
                    bool(operation & ST25R3916_REG_OP_CONTROL_en), bool(auxiliary & ST25R3916_REG_AUX_DISPLAY_osc_ok),
                    bool(operation & ST25R3916_REG_OP_CONTROL_rx_en), bool(operation & ST25R3916_REG_OP_CONTROL_tx_en),
                    _rf->rfalIsExtFieldOn(), digitalRead(NFC_INT) == HIGH,
                    operation, auxiliary, mode, target, targetState, mask, activation, deactivation, error);
}

void LilyGoMifareClassicListener::observeListenState(rfalLmState state)
{
    if (_diagnosticLastState == uint8_t(state)) return;
    _diagnosticLastState = uint8_t(state);
    portENTER_CRITICAL(&_statusMux);
    auto &d = _status.diagnostics;
    ++d.stateChanges; d.listenState = uint8_t(state);
    if (unsigned(state) < 32) d.statesSeen |= 1UL << unsigned(state);
    portEXIT_CRITICAL(&_statusMux);
}

void LilyGoMifareClassicListener::captureDiagnostics()
{
    auto d = status().diagnostics;
    d.registerError = ST_ERR_NONE;
    const uint8_t registers[] = {ST25R3916_REG_OP_CONTROL, ST25R3916_REG_MODE,
            ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_STATUS, ST25R3916_REG_AUX_MOD};
    uint8_t *values[] = {&d.operation, &d.mode, &d.passiveControl, &d.passiveState, &d.auxMod};
    for (unsigned i = 0; i < sizeof(registers); ++i) {
        const ReturnCode error = _rf->st25r3916ReadRegister(registers[i], values[i]);
        if (error != ST_ERR_NONE) d.registerError = error;
    }
    d.fieldOn = _rf->rfalIsExtFieldOn();
    d.irqHigh = digitalRead(NFC_INT) == HIGH;
    d.polls = _diagnosticPolls; d.irqHighPolls = _diagnosticIrqPolls;
    d.sampledAtMs = millis();
    ++d.samples;
    portENTER_CRITICAL(&_statusMux); _status.diagnostics = d; portEXIT_CRITICAL(&_statusMux);
}
#endif

bool LilyGoMifareClassicListener::transmit(const LilyGoMifareClassicResponse &response)
{
#if LILYGO_NFC_DEBUG_ENABLED
    const uint32_t prepareStart = micros();
#endif
    const size_t pulses = lilygoMifareClassicEncodePulses(response, kClockHz, _pulses, 1700);
    if (!pulses) { fail(ST_ERR_PARAM); return false; }
    const size_t symbols = (pulses + 1) / 2;
    for (size_t i = 0; i < symbols; ++i) {
        const auto &a = _pulses[i * 2];
        _symbols[i].val = 0;
        _symbols[i].duration0 = a.ticks; _symbols[i].level0 = a.high;
        if (i * 2 + 1 < pulses) {
            const auto &b = _pulses[i * 2 + 1];
            _symbols[i].duration1 = b.ticks; _symbols[i].level1 = b.high;
        } else { _symbols[i].duration1 = 1; _symbols[i].level1 = 0; }
    }
    _rf->st25r3916ExecuteCommand(ST25R3916_CMD_MASK_RECEIVE_DATA);
    _rf->st25r3916ExecuteCommand(ST25R3916_CMD_CLEAR_FIFO);
    if (!instance.lockSPI(pdMS_TO_TICKS(20))) { fail(ST_ERR_BUSY); return false; }
    SPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE1));
    const MosiRoute spiRoute;
    digitalWrite(NFC_CS, LOW);
    SPI.transfer(ST25R3916_CMD_TRANSPARENT_MODE);
    digitalWrite(NFC_CS, HIGH);
    REG_WRITE(GPIO_PIN_MUX_REG[MOSI], _rmtMux);
    GPIO.func_out_sel_cfg[MOSI].val = _rmtMatrix;
    rmt_transmit_config_t config = {};
    config.flags.eot_level = 0;
#if LILYGO_NFC_DEBUG_ENABLED
    const uint32_t transmitStart = micros();
#endif
    esp_err_t error = rmt_transmit(_channel, _encoder, _symbols, symbols * sizeof(_symbols[0]), &config);
    if (error == ESP_OK) {
        const uint32_t started = micros();
        // Returning on the next scheduler tick can miss the reader's next
        // frame. Resume FIFO reception immediately after the DMA completion.
        do { error = rmt_tx_wait_all_done(_channel, 0); }
        while (error == ESP_ERR_TIMEOUT && micros() - started < 10000);
    }
    // Even failure must return MOSI and leave transparent mode before releasing
    // either shared-bus lock. Do not call an RFAL SPI helper while holding it.
    if (error != ESP_OK) { rmt_disable(_channel); _transmitterEnabled = false; }
    spiRoute.restore();
    digitalWrite(NFC_CS, LOW);
    SPI.transfer(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
    digitalWrite(NFC_CS, HIGH);
    SPI.endTransaction();
    instance.unlockSPI();
#if LILYGO_NFC_DEBUG_ENABLED
    const uint32_t transmitUs = micros() - transmitStart;
    portENTER_CRITICAL(&_statusMux);
    auto &d = _status.diagnostics;
    d.prepareUs = transmitStart - prepareStart;
    if (d.prepareUs > d.maxPrepareUs) d.maxPrepareUs = d.prepareUs;
    d.transmitUs = transmitUs;
    portEXIT_CRITICAL(&_statusMux);
#endif
    if (error != ESP_OK) {
        fail(ST_ERR_IO);
        portENTER_CRITICAL(&_statusMux); _status.failed = true; portEXIT_CRITICAL(&_statusMux);
        return false;
    }
    return true;
}

bool LilyGoMifareClassicListener::dispatch(bool initial)
{
    if (!_rxBits || _rxBits % 8 || _rxBits > sizeof(_rx) * 8) { fail(ST_ERR_PROTO); return false; }
    unsigned bytes = _rxBits / 8;
    if (initial) {
        // RFAL verified and removed the initial plaintext CRC. All subsequent
        // requests use the raw FIFO path, retaining their encrypted CRC bytes.
        if (bytes + 2 > sizeof(_rx)) return false;
        const uint16_t crc = lilygoMifareClassicCrcA(_rx, bytes);
        _rx[bytes++] = crc; _rx[bytes++] = crc >> 8;
        _rf->st25r3916ClearAndEnableInterrupts(ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_EOF |
                ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC | ST25R3916_IRQ_MASK_ERR1);
    }
    const auto response = lilygoMifareClassicCommand(_dump, _session, _rx, bytes, esp_random());
    const bool sent = !response.bits || transmit(response);
    portENTER_CRITICAL(&_statusMux);
    ++_status.exchanges;
    _status.rxBits = _rxBits; _status.txBits = response.bits;
    _status.command = response.command; _status.reason = response.rejected;
    if (!response.authenticated && (response.command == 0x60 || response.command == 0x61)) ++_status.authAttempts;
    if (response.authenticated) ++_status.authSuccesses;
    if (response.read) ++_status.reads;
    if (response.rejected) ++_status.rejected;
    _status.readerActive = sent && !response.sleep && response.bits;
    portEXIT_CRITICAL(&_statusMux);
    if (response.sleep) {
        // HALT must ignore REQA until WUPA or field removal, rather than
        // immediately advertising the same card again in the current field.
        _sleeping = _rf->rfalListenSleepStart(RFAL_LM_STATE_SLEEP_A, _rx,
                                             sizeof(_rx) * 8, &_rxBits) == ST_ERR_NONE;
        if (!_sleeping) fail(ST_ERR_IO);
    }
    return sent && !response.sleep && response.bits;
}

void LilyGoMifareClassicListener::run()
{
    bool raw = false;
    uint32_t yielded = millis();
    while (!_stopRequested.load()) {
#if LILYGO_NFC_DEBUG_ENABLED
        ++_diagnosticPolls;
        if (digitalRead(NFC_INT) == HIGH) ++_diagnosticIrqPolls;
#endif
        if (!raw) {
            _rf->rfalWorker();
            bool ready = false;
            auto state = _rf->rfalListenGetState(&ready, nullptr);
            // Let RFAL wait for EON and handle external-field transitions.
            // Do not override its state while a reader is being detected.
            if (state == RFAL_LM_STATE_POWER_OFF) _session = {};
#if LILYGO_NFC_DEBUG_ENABLED
            observeListenState(state);
            // Slow register snapshots are confined to waiting states. Never
            // insert diagnostic SPI reads between selection and authentication.
            if ((state == RFAL_LM_STATE_POWER_OFF || state == RFAL_LM_STATE_IDLE) &&
                    millis() - _diagnosticAtMs >= 1000) {
                captureDiagnostics(); _diagnosticAtMs = millis();
            }
#endif
            if ((state == RFAL_LM_STATE_ACTIVE_A || state == RFAL_LM_STATE_ACTIVE_Ax) && ready) {
                _sleeping = false;
                const ReturnCode error = _rf->rfalListenSetState(RFAL_LM_STATE_TARGET_A);
                if (error != ST_ERR_NONE) { fail(error); break; }
#if LILYGO_NFC_DEBUG_ENABLED
                observeListenState(RFAL_LM_STATE_TARGET_A);
#endif
                raw = dispatch(true);
                if (status().failed) break;
                if (!raw && !_sleeping) {
                    const ReturnCode restartError = restart();
                    if (restartError != ST_ERR_NONE) { fail(restartError); break; }
                }
            }
            // READY/ACTIVE are successive steps of the same reader exchange.
            // A tick between each step can accumulate milliseconds before Nt.
            if (state == RFAL_LM_STATE_POWER_OFF || millis() - yielded >= 20) {
                vTaskDelay(1); yielded = millis();
            }
            continue;
        }
        // Card mode strips parity in hardware. PAR/CRC IRQs are expected for
        // encrypted frames; validate the decrypted AR or CRC in the engine.
        // TARGET_A leaves frame handling to this listener, while the public
        // worker still collects and acknowledges pending chip interrupts.
        _rf->rfalWorker();
        const uint32_t flags = _rf->st25r3916GetInterrupt(ST25R3916_IRQ_MASK_RXE | ST25R3916_IRQ_MASK_EOF |
                ST25R3916_IRQ_MASK_PAR | ST25R3916_IRQ_MASK_CRC | ST25R3916_IRQ_MASK_ERR1);
        if (flags & (ST25R3916_IRQ_MASK_EOF | ST25R3916_IRQ_MASK_ERR1)) {
            fail(flags & ST25R3916_IRQ_MASK_EOF ? ST_ERR_LINK_LOSS : ST_ERR_FRAMING);
            raw = false;
        } else if (flags & ST25R3916_IRQ_MASK_RXE) {
            const uint16_t count = _rf->st25r3916GetNumFIFOBytes();
            const uint8_t last = _rf->st25r3916GetNumFIFOLastBits();
            if (!count || count > sizeof(_rx) || last) { fail(ST_ERR_PROTO); raw = false; }
            else {
                _rf->st25r3916ReadFifo(_rx, count);
                _rxBits = count * 8;
                raw = dispatch(false);
            }
        }
        if (status().failed) break;
        if (!raw && !_sleeping) {
            const ReturnCode restartError = restart();
            if (restartError != ST_ERR_NONE) { fail(restartError); break; }
        }
        // Yield periodically so a held reader field cannot starve the idle task.
        if (millis() - yielded >= 20) { vTaskDelay(1); yielded = millis(); }
    }
    _rf->rfalListenStop();
    portENTER_CRITICAL(&_statusMux);
    _status.failed = _status.failed || !_stopRequested.load(); _status.readerActive = false;
    portEXIT_CRITICAL(&_statusMux);
}
#endif
