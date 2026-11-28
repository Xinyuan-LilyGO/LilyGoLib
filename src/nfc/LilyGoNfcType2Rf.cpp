/**
 * @file      LilyGoNfcType2Rf.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Captures Type 2 cards and serves their commands through low-level RFAL listen mode.
 */
#include "nfc/LilyGoNfcService.h"
#include "LilyGoLog.h"
#if defined(ARDUINO) && defined(USING_ST25R3916)
#include "nfc/LilyGoNfcType2Rf.h"
#include <string.h>

namespace {
static bool selectAgain(RfalNfcClass &reader, const rfalNfcaListenDevice &card)
{
    RfalRfClass *rf = reader.getRfalRf();
    if (!rf) return false;
    rf->rfalFieldOff();
    delay(5);
    if (reader.rfalNfcaPollerInitialize() != ST_ERR_NONE || rf->rfalFieldOnAndStartGT() != ST_ERR_NONE) return false;
    rfalNfcaSensRes atqa = {};
    rfalNfcaSelRes sak = {};
    return reader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &atqa) == ST_ERR_NONE &&
           atqa.anticollisionInfo == card.sensRes.anticollisionInfo &&
           atqa.platformInfo == card.sensRes.platformInfo &&
           reader.rfalNfcaPollerSelect(card.nfcId1, card.nfcId1Len, &sak) == ST_ERR_NONE &&
           sak.sak == card.selRes.sak;
}
static bool command(RfalNfcClass &reader, uint8_t *tx, uint16_t length, uint8_t *rx, uint16_t expected)
{
    RfalRfClass *rf = reader.getRfalRf();
    if (!rf) return false;
    uint16_t received = 0;
    const ReturnCode error = rf->rfalTransceiveBlockingTxRx(tx, length, rx, expected, &received,
                            RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(10));
    return error == ST_ERR_NONE && received == expected;
}
static void storePage(LilyGoNfcType2Dump &dump, uint16_t page, const uint8_t *bytes)
{
    if (page >= dump.pageCount) return;
    memcpy(dump.pages[page], bytes, 4);
    if (!dump.pageRead[page]) { dump.pageRead[page] = true; ++dump.pagesRead; }
}
} // namespace

bool lilygoReadNfcType2(RfalNfcClass &reader, const rfalNfcaListenDevice &card, LilyGoNfcType2Dump &dump)
{
    memset(&dump, 0, sizeof(dump));
    if ((card.nfcId1Len != 4 && card.nfcId1Len != 7) || card.selRes.sak != 0) return false;
    dump.uidLen = card.nfcId1Len;
    memcpy(dump.uid, card.nfcId1, dump.uidLen);
    dump.atqa[0] = card.sensRes.anticollisionInfo;
    dump.atqa[1] = card.sensRes.platformInfo;
    dump.sak = card.selRes.sak;
    uint8_t versionCommand[] = {0x60};
    dump.versionValid = command(reader, versionCommand, sizeof(versionCommand), dump.version, sizeof(dump.version));
    if (dump.versionValid) dump.model = lilygoNfcType2Identify(dump.version, &dump.pageCount);
    if (dump.uidLen != 7) dump.model = LILYGO_NFC_TYPE2_GENERIC;
    dump.geometryKnown = dump.model != LILYGO_NFC_TYPE2_GENERIC;
    if (!selectAgain(reader, card)) return false;
    uint8_t read0[] = {0x30, 0};
    uint8_t block[16] = {};
    if (!command(reader, read0, sizeof(read0), block, sizeof(block))) return false;
    uint8_t firstPage[4];
    memcpy(firstPage, block, sizeof(firstPage));
    if (!dump.geometryKnown) {
        // CC bounds cover the advertised data area, not unknown vendor pages.
        uint8_t readCC[] = {0x30, 3};
        const bool ccRead = command(reader, readCC, sizeof(readCC), block, sizeof(block));
        dump.pageCount = ccRead && block[0] == 0xE1 ? uint16_t(4U + uint16_t(block[2]) * 2U) : 16U;
        if (dump.pageCount > LILYGO_NFC_TYPE2_MAX_PAGES) dump.pageCount = LILYGO_NFC_TYPE2_MAX_PAGES;
        if (!ccRead && !selectAgain(reader, card)) return false;
    }
    storePage(dump, 0, firstPage);
    LILYGO_NFC_LOG_I("[ST25][T2][READ] model=%s pages=%u geometry_known=%u version=%u",
                     lilygoNfcType2ModelName(dump.model), dump.pageCount, dump.geometryKnown, dump.versionValid);
    for (uint16_t page = 1; page < dump.pageCount; ++page) {
        uint8_t read[] = {0x30, uint8_t(page)};
        if (command(reader, read, sizeof(read), block, sizeof(block))) {
            // Only the addressed page is unambiguous: a protected card may
            // roll the remaining bytes over before its AUTH0 boundary.
            storePage(dump, page, block);
        } else {
            // Retry the first page after re-selection. A READ can roll over
            // before AUTH0; single-page retries must not invent later pages.
            if (!selectAgain(reader, card)) break;
            if (command(reader, read, sizeof(read), block, sizeof(block))) storePage(dump, page, block);
            if (!selectAgain(reader, card)) break;
        }
    }
    if (dump.versionValid && selectAgain(reader, card)) {
        uint8_t signature[] = {0x3C, 0};
        dump.signatureValid = command(reader, signature, sizeof(signature), dump.signature, sizeof(dump.signature));
        if (selectAgain(reader, card)) {
            uint8_t counter[] = {0x39, 2};
            dump.counterValid = command(reader, counter, sizeof(counter), dump.counter, sizeof(dump.counter));
        }
    }
    // PWD/PACK pages return masked zeros, not the actual credentials.
    dump.credentialsKnown = false;
    LILYGO_NFC_LOG_I("[ST25][T2][READ] pages=%u/%u complete=%u signature=%u counter=%u credentials=unknown",
                     dump.pagesRead, dump.pageCount, lilygoNfcType2DumpComplete(dump), dump.signatureValid, dump.counterValid);
    LILYGO_NFC_LOG_ONLY(for (uint16_t p = 0; p < dump.pageCount; ++p) {
        if (dump.pageRead[p]) LILYGO_NFC_LOG_D("[ST25][T2][READ] page=%03u data=%02X%02X%02X%02X", p,
                    dump.pages[p][0], dump.pages[p][1], dump.pages[p][2], dump.pages[p][3]);
        else LILYGO_NFC_LOG_W("[ST25][T2][READ] page=%03u unavailable", p);
    });
    (void)selectAgain(reader, card);
    return lilygoNfcType2DumpValid(dump);
}

LilyGoNfcType2WriteResult lilygoWriteNfcType2(RfalNfcClass &reader,
        const rfalNfcaListenDevice &card, const LilyGoNfcType2Dump &source, LilyGoNfcType2Dump &target)
{
    LilyGoNfcType2WriteResult result = {};
    result.failedPage = UINT16_MAX;
    result.status = lilygoNfcType2CheckWriteSource(source, &result.failedPage);
    if (result.status != LILYGO_NFC_TYPE2_WRITE_READY) return result;
    if (!lilygoReadNfcType2(reader, card, target)) {
        result.status = LILYGO_NFC_TYPE2_WRITE_INVALID_TARGET;
        return result;
    }
    LilyGoNfcType2WriteIo io = {};
    io.context = &reader;
    io.readPage = [](void *context, uint8_t page, uint8_t data[4]) -> uint16_t {
        auto &device = *static_cast<RfalNfcClass *>(context);
        RfalRfClass *rf = device.getRfalRf();
        if (!rf) return ST_ERR_WRONG_STATE;
        uint8_t request[] = {0x30, page};
        uint8_t response[16];
        uint16_t received = 0;
        const ReturnCode error = rf->rfalTransceiveBlockingTxRx(request, sizeof(request), response,
                sizeof(response), &received, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(10));
        if (error != ST_ERR_NONE) return error;
        if (received != sizeof(response)) return ST_ERR_PROTO;
        memcpy(data, response, 4);
        return ST_ERR_NONE;
    };
    io.writePage = [](void *context, uint8_t page, const uint8_t data[4]) -> uint16_t {
        const ReturnCode error = static_cast<RfalNfcClass *>(context)->rfalT2TPollerWrite(page, data);
        LILYGO_NFC_LOG_D("[ST25][T2][WRITE] page=%u ack_code=%u", page, error);
        return error;
    };
    result = lilygoWriteNfcType2UserPages(source, target, io);
    LILYGO_NFC_LOG_I("[ST25][T2][WRITE] status=%u planned=%u unchanged=%u attempted=%u acked=%u verified=%u failed_page=%u transport=%u",
            result.status, result.plannedPages, result.unchangedPages, result.attemptedPages,
            result.writtenPages, result.verifiedPages, result.failedPage, result.transportError);
    return result;
}

LilyGoNfcType2Listener::LilyGoNfcType2Listener() : _rf(nullptr), _task(nullptr), _stopped(nullptr),
    _stopRequested(false), _status{}, _dump{}, _session{}, _rx{}, _tx{}, _rxBits(0),
    _exchangePending(false), _recoverPending(false), _allowUserWrites(false) {}

LilyGoNfcType2Listener::~LilyGoNfcType2Listener() { stop(); }

bool LilyGoNfcType2Listener::begin(RfalRfClass *rf, const LilyGoNfcType2Dump &dump, bool allowUserWrites)
{
    stop();
    _status = {};
    if (!rf || !lilygoNfcType2CanEmulate(dump)) {
        _status.failed = true; _status.lastError = ST_ERR_PARAM;
        return false;
    }
    _rf = rf; _dump = dump; _allowUserWrites = allowUserWrites;
    _session = {}; _status = {}; _stopRequested.store(false);
    _recoverPending = false;
#if LILYGO_DEBUG_ENABLED
    _traceHead = _traceCount = 0;
    _diagnosticLastState = _diagnosticLastHardware = 0xFF;
    _diagnosticAtMs = millis(); _diagnosticPollUs = micros();
    _diagnosticSelectionUs = 0; _diagnosticFieldActive = false;
#endif
    _stopped = xSemaphoreCreateBinary();
    if (!_stopped) { _status.failed = true; _status.lastError = ST_ERR_NOMEM; return false; }
    const ReturnCode error = restartListen();
    if (error != ST_ERR_NONE) {
        portENTER_CRITICAL(&_statusMux);
        _status.failed = true; _status.lastError = error;
        portEXIT_CRITICAL(&_statusMux);
        LILYGO_LOG_E("[ST25][T2][CONFIG] listen/identity verification failed code=%u", error);
        _rf->rfalListenStop();
        vSemaphoreDelete(_stopped); _stopped = nullptr; _task = nullptr;
        return false;
    }
#if LILYGO_DEBUG_ENABLED
    captureDiagnostics();
    const auto &d = _status.diagnostics;
    LILYGO_LOG_I("[ST25][T2][CONFIG] memory_ok=%u repaired=%u uid_len=%u atqa=0x%02X%02X sak_cl1=0x%02X sak_cl2=0x%02X uid_cfg=0x%02X iso_a=0x%02X",
                 d.memoryOk, d.memoryRepaired, _dump.uidLen, _dump.atqa[1], _dump.atqa[0],
                 _dump.sak | (_dump.uidLen == 7 ? 4U : 0U), _dump.sak & ~4U,
                 d.auxiliary & ST25R3916_REG_AUX_nfc_id_mask, d.iso14443a);
#endif
    if (xTaskCreate(taskEntry, "nfc_type2", 4096, this, 3, &_task) != pdPASS) {
        _status.failed = true; _status.lastError = ST_ERR_NOMEM;
        _rf->rfalListenStop();
        vSemaphoreDelete(_stopped); _stopped = nullptr; _task = nullptr;
        return false;
    }
    return true;
}

void LilyGoNfcType2Listener::stop()
{
    if (_task) {
        _stopRequested.store(true);
        xSemaphoreTake(_stopped, portMAX_DELAY);
        _task = nullptr;
    }
    if (_stopped) { vSemaphoreDelete(_stopped); _stopped = nullptr; }
    _rf = nullptr;
}

LilyGoNfcType2RfStatus LilyGoNfcType2Listener::status()
{
    portENTER_CRITICAL(&_statusMux);
    const LilyGoNfcType2RfStatus result = _status;
    portEXIT_CRITICAL(&_statusMux);
    return result;
}

#if LILYGO_DEBUG_ENABLED
bool LilyGoNfcType2Listener::takeTrace(LilyGoNfcType2RfTrace &event)
{
    portENTER_CRITICAL(&_statusMux);
    const bool available = _traceCount != 0;
    if (available) {
        event = _trace[_traceHead];
        _traceHead = uint8_t((_traceHead + 1U) % kTraceCapacity);
        --_traceCount;
    }
    portEXIT_CRITICAL(&_statusMux);
    return available;
}

void LilyGoNfcType2Listener::recordTrace(const LilyGoNfcType2RfTrace &event)
{
    portENTER_CRITICAL(&_statusMux);
    if (_traceCount < kTraceCapacity) {
        _trace[(_traceHead + _traceCount) % kTraceCapacity] = event;
        ++_traceCount;
    } else ++_status.diagnostics.dropped;
    portEXIT_CRITICAL(&_statusMux);
}

void LilyGoNfcType2Listener::observeListenState(rfalLmState state)
{
    if (_diagnosticLastState == uint8_t(state)) return;
    _diagnosticLastState = uint8_t(state);
    // Take the first hardware sample immediately on each waiting-state change.
    _diagnosticSelectionUs = micros() - 1000U;
    if (state == RFAL_LM_STATE_POWER_OFF) _diagnosticLastHardware = 0xFF;
    portENTER_CRITICAL(&_statusMux);
    ++_status.diagnostics.stateChanges;
    if (unsigned(state) < 32U) _status.diagnostics.statesSeen |= 1UL << unsigned(state);
    portEXIT_CRITICAL(&_statusMux);
    LilyGoNfcType2RfTrace event = {};
    event.kind = 1; event.atUs = micros(); event.state = uint8_t(state);
    recordTrace(event);
}

void LilyGoNfcType2Listener::captureSelection(rfalLmState state)
{
    // RFAL's READY_A covers both UID cascade levels. A quiet-state heartbeat
    // loses that distinction after the reader removes its field. Sample only
    // while selecting, at most once per millisecond; never consume IRQ status.
    const uint32_t nowUs = micros();
    if (nowUs - _diagnosticSelectionUs < 1000U) return;
    _diagnosticSelectionUs = nowUs;
    auto *chip = static_cast<RfalRfST25R3916Class *>(_rf);
    LilyGoNfcType2RfTrace event = {};
    event.kind = 4; event.atUs = nowUs; event.state = uint8_t(state);
    event.error = chip->st25r3916ReadRegister(ST25R3916_REG_PASSIVE_TARGET_STATUS, &event.hardwareState);
    if (event.error == ST_ERR_NONE && event.hardwareState == _diagnosticLastHardware) return;
    _diagnosticLastHardware = event.error == ST_ERR_NONE ? event.hardwareState : 0xFF;
    const uint8_t registers[] = {ST25R3916_REG_OP_CONTROL, ST25R3916_REG_MODE,
        ST25R3916_REG_AUX, ST25R3916_REG_ISO14443A_NFC};
    uint8_t *values[] = {&event.operation, &event.mode, &event.auxiliary, &event.iso14443a};
    for (unsigned i = 0; i < sizeof(registers); ++i) {
        const ReturnCode error = chip->st25r3916ReadRegister(registers[i], values[i]);
        if (event.error == ST_ERR_NONE) event.error = error;
    }
    portENTER_CRITICAL(&_statusMux);
    if (event.error == ST_ERR_NONE && event.hardwareState < 32U)
        _status.diagnostics.hardwareStatesSeen |= 1UL << event.hardwareState;
    portEXIT_CRITICAL(&_statusMux);
    recordTrace(event);
}

void LilyGoNfcType2Listener::captureDiagnostics()
{
    // Only before task startup or in quiet waiting states, on the RF owner.
    // Reading IRQ status here would consume the events RFAL still needs.
    auto *chip = static_cast<RfalRfST25R3916Class *>(_rf);
    uint8_t values[8] = {};
    const uint8_t registers[] = {ST25R3916_REG_OP_CONTROL, ST25R3916_REG_MODE,
        ST25R3916_REG_PASSIVE_TARGET, ST25R3916_REG_PASSIVE_TARGET_STATUS,
        ST25R3916_REG_AUX_MOD, ST25R3916_REG_PT_MOD, ST25R3916_REG_AUX, ST25R3916_REG_ISO14443A_NFC};
    ReturnCode error = ST_ERR_NONE;
    for (unsigned i = 0; i < sizeof(registers); ++i) {
        const ReturnCode current = chip->st25r3916ReadRegister(registers[i], &values[i]);
        if (error == ST_ERR_NONE) error = current;
    }
    const bool fieldOn = _rf->rfalIsExtFieldOn();
    portENTER_CRITICAL(&_statusMux);
    auto &d = _status.diagnostics;
    ++d.samples; d.fieldOn = fieldOn; d.registerError = error;
    d.operation = values[0]; d.mode = values[1]; d.passiveControl = values[2];
    d.passiveState = values[3]; d.auxMod = values[4]; d.modulation = values[5];
    d.auxiliary = values[6]; d.iso14443a = values[7];
    portEXIT_CRITICAL(&_statusMux);
}
#endif

void LilyGoNfcType2Listener::taskEntry(void *argument)
{
    auto *listener = static_cast<LilyGoNfcType2Listener *>(argument);
    listener->run();
    xSemaphoreGive(listener->_stopped);
    vTaskDelete(nullptr);
}

void LilyGoNfcType2Listener::recordError(ReturnCode error)
{
    // A brief field loss may already have ended by the time recovery samples
    // the field detector. The transport event still terminates that session.
    if (error == ST_ERR_LINK_LOSS) _session = {};
    portENTER_CRITICAL(&_statusMux);
    ++_status.errors; _status.lastError = error;
    portEXIT_CRITICAL(&_statusMux);
#if LILYGO_DEBUG_ENABLED
    LilyGoNfcType2RfTrace event = {};
    event.kind = 3; event.atUs = micros(); event.error = error;
    event.state = uint8_t(_rf->rfalListenGetState(nullptr, nullptr));
    recordTrace(event);
#endif
}

ReturnCode LilyGoNfcType2Listener::restartListen()
{
    auto *chip = static_cast<RfalRfST25R3916Class *>(_rf);
    ReturnCode error = _rf->rfalListenStop();
    if (error != ST_ERR_NONE) return error;
    // Reader anticollision framing must not leak into target operation.
    error = chip->st25r3916ClrRegisterBits(ST25R3916_REG_ISO14443A_NFC, ST25R3916_REG_ISO14443A_NFC_antcl);
    if (error != ST_ERR_NONE) return error;
    rfalLmConfPA config = {};
    config.nfcidLen = _dump.uidLen == 7 ? RFAL_LM_NFCID_LEN_07 : RFAL_LM_NFCID_LEN_04;
    memcpy(config.nfcid, _dump.uid, _dump.uidLen);
    memcpy(config.SENS_RES, _dump.atqa, 2);
    config.SEL_RES = _dump.sak;
    _exchangePending = false; _rxBits = 0;
    error = _rf->rfalListenStart(RFAL_LM_MASK_NFCA, &config, nullptr, nullptr,
                                _rx, uint16_t(sizeof(_rx) * 8U), &_rxBits);
    if (error != ST_ERR_NONE) return error;
    // ListenStart can disable the oscillator in POWER_OFF. PT memory readback
    // requires en=1 and a stable crystal even if the SPI helper reports success.
    chip->st25r3916OscOn();
    uint8_t operation = 0, display = 0, auxiliary = 0, iso14443a = 0;
    error = chip->st25r3916ReadRegister(ST25R3916_REG_OP_CONTROL, &operation);
    if (error == ST_ERR_NONE) error = chip->st25r3916ReadRegister(ST25R3916_REG_AUX_DISPLAY, &display);
    if (error != ST_ERR_NONE) return error;
    if (!(operation & ST25R3916_REG_OP_CONTROL_en) || !(display & ST25R3916_REG_AUX_DISPLAY_osc_ok)) return ST_ERR_IO;
    uint8_t expected[ST25R3916_PTM_A_LEN] = {}, memory[ST25R3916_PTM_A_LEN] = {};
    memcpy(expected, config.nfcid, sizeof(config.nfcid));
    memcpy(expected + 10, config.SENS_RES, 2);
    expected[12] = config.SEL_RES | (_dump.uidLen == 7 ? 4U : 0U);
    expected[13] = expected[14] = config.SEL_RES & ~4U;
    error = chip->st25r3916ReadPTMem(memory, sizeof(memory));
    const bool repaired = error == ST_ERR_NONE && memcmp(expected, memory, sizeof(memory));
    if (repaired) {
        error = chip->st25r3916WritePTMem(expected, sizeof(expected));
        if (error == ST_ERR_NONE) error = chip->st25r3916ReadPTMem(memory, sizeof(memory));
    }
    if (error != ST_ERR_NONE) return error;
    if (memcmp(expected, memory, sizeof(memory))) return ST_ERR_IO;
    error = chip->st25r3916ReadRegister(ST25R3916_REG_AUX, &auxiliary);
    if (error == ST_ERR_NONE) error = chip->st25r3916ReadRegister(ST25R3916_REG_ISO14443A_NFC, &iso14443a);
    if (error != ST_ERR_NONE) return error;
    const uint8_t uidConfig = _dump.uidLen == 7 ? ST25R3916_REG_AUX_nfc_id_7bytes : ST25R3916_REG_AUX_nfc_id_4bytes;
    if ((auxiliary & ST25R3916_REG_AUX_nfc_id_mask) != uidConfig ||
            (iso14443a & ST25R3916_REG_ISO14443A_NFC_antcl)) return ST_ERR_IO;
#if LILYGO_DEBUG_ENABLED
    portENTER_CRITICAL(&_statusMux);
    _status.diagnostics.memoryOk = true;
    _status.diagnostics.memoryRepaired |= repaired;
    portEXIT_CRITICAL(&_statusMux);
#endif
    // Restore RFAL field detection only after the actual hardware identity is
    // verified. A field already present is handled by RFAL's IDLE path.
    return _rf->rfalListenSetState(RFAL_LM_STATE_POWER_OFF);
}

ReturnCode LilyGoNfcType2Listener::exchange(uint16_t txBits)
{
    rfalTransceiveContext context = {};
    context.txBuf = txBits ? _tx : nullptr;
    context.txBufLen = txBits;
    context.rxBuf = _rx; context.rxBufLen = sizeof(_rx) * 8U;
    context.rxRcvdLen = &_rxBits;
    context.flags = RFAL_TXRX_FLAGS_DEFAULT;
    if (txBits == 4) context.flags |= RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_PAR_TX_NONE;
    context.fwt = RFAL_FWT_NONE;
    _rxBits = 0;
    const ReturnCode error = _rf->rfalStartTransceive(&context);
    _exchangePending = error == ST_ERR_NONE;
    // Push TX immediately instead of waiting for the UI or the next RTOS tick.
    for (unsigned i = 0; _exchangePending && i < 8 && _rf->rfalIsTransceiveInTx(); ++i) _rf->rfalWorker();
    return error;
}

void LilyGoNfcType2Listener::dispatch()
{
#if LILYGO_DEBUG_ENABLED
    const uint32_t started = micros();
#endif
    const uint16_t bits = _rxBits;
    if (!bits || bits % 8U || bits > sizeof(_rx) * 8U) {
        const ReturnCode error = exchange(0);
        if (error != ST_ERR_NONE) { recordError(error); _recoverPending = true; }
        return;
    }
    const uint8_t opcode = _rx[0];
    const LilyGoNfcType2Response result = lilygoNfcType2Command(_dump, _session, _rx,
                    bits / 8U, _tx, sizeof(_tx), _allowUserWrites);
    ReturnCode error;
    if (result.sleep) {
        _exchangePending = false;
        error = _rf->rfalListenSleepStart(RFAL_LM_STATE_SLEEP_A, _rx, sizeof(_rx) * 8U, &_rxBits);
    } else error = exchange(result.bits);
    portENTER_CRITICAL(&_statusMux);
    ++_status.exchanges; _status.command = opcode;
    _status.rxBits = bits; _status.txBits = result.bits;
    _status.readerActive = !result.sleep;
    if (result.read) ++_status.reads;
    if (result.modified) ++_status.writes;
    portEXIT_CRITICAL(&_statusMux);
#if LILYGO_DEBUG_ENABLED
    LilyGoNfcType2RfTrace event = {};
    event.kind = 2; event.atUs = started; event.prepareUs = micros() - started;
    event.command = opcode; event.rxBits = bits; event.txBits = result.bits;
    event.reply = result.bits == 4 ? (_tx[0] & 0x0FU) : 0xFF;
    event.authenticated = _session.authenticated;
    portENTER_CRITICAL(&_statusMux);
    if (event.prepareUs > _status.diagnostics.maxPrepareUs) _status.diagnostics.maxPrepareUs = event.prepareUs;
    portEXIT_CRITICAL(&_statusMux);
    recordTrace(event);
#endif
    if (error != ST_ERR_NONE) { recordError(error); _recoverPending = true; }
}

void LilyGoNfcType2Listener::run()
{
    uint32_t yielded = millis();
    while (!_stopRequested.load()) {
#if LILYGO_DEBUG_ENABLED
        const uint32_t nowUs = micros();
        const uint32_t gapUs = nowUs - _diagnosticPollUs;
        _diagnosticPollUs = nowUs;
        portENTER_CRITICAL(&_statusMux);
        ++_status.diagnostics.polls;
        if (gapUs > _status.diagnostics.maxPollGapUs) _status.diagnostics.maxPollGapUs = gapUs;
        if (_diagnosticFieldActive && gapUs > _status.diagnostics.maxFieldPollGapUs)
            _status.diagnostics.maxFieldPollGapUs = gapUs;
        portEXIT_CRITICAL(&_statusMux);
#endif
        if (_recoverPending) {
            // Protocol errors do not start a new RF-field session.
            const bool counted = _session.counterIncremented;
            _session = {};
            if (_rf->rfalIsExtFieldOn()) _session.counterIncremented = counted;
            const ReturnCode restart = restartListen();
            portENTER_CRITICAL(&_statusMux);
            _status.readerActive = false;
            _status.failed = restart != ST_ERR_NONE;
            portEXIT_CRITICAL(&_statusMux);
            if (restart != ST_ERR_NONE) { recordError(restart); break; }
            _recoverPending = false;
        }
        _rf->rfalWorker();
        if (_exchangePending) {
            const ReturnCode error = _rf->rfalGetTransceiveStatus();
            if (error == ST_ERR_NONE) { _exchangePending = false; dispatch(); }
            else if (error != ST_ERR_BUSY) {
                _exchangePending = false;
                recordError(error);
                _recoverPending = true;
            }
        } else {
            bool dataReady = false;
            const rfalLmState state = _rf->rfalListenGetState(&dataReady, nullptr);
            portENTER_CRITICAL(&_statusMux); _status.listenState = uint8_t(state); portEXIT_CRITICAL(&_statusMux);
#if LILYGO_DEBUG_ENABLED
            observeListenState(state);
            if (!dataReady && (state == RFAL_LM_STATE_IDLE || state == RFAL_LM_STATE_READY_A || state == RFAL_LM_STATE_READY_Ax))
                captureSelection(state);
            if (!dataReady && (state == RFAL_LM_STATE_POWER_OFF || state == RFAL_LM_STATE_IDLE) &&
                    millis() - _diagnosticAtMs >= 1000U) {
                captureDiagnostics(); _diagnosticAtMs = millis();
            }
#endif
            if (state == RFAL_LM_STATE_POWER_OFF) {
                if (!_rf->rfalIsExtFieldOn()) _session = {};
                portENTER_CRITICAL(&_statusMux); _status.readerActive = false; portEXIT_CRITICAL(&_statusMux);
            }
            if ((state == RFAL_LM_STATE_ACTIVE_A || state == RFAL_LM_STATE_ACTIVE_Ax) && dataReady) {
                // The high-level RFAL activation path rejects Type 2 commands.
                // Enter raw target mode after hardware anticollision instead.
                const ReturnCode error = _rf->rfalListenSetState(RFAL_LM_STATE_TARGET_A);
                if (error == ST_ERR_NONE) dispatch();
                else { recordError(error); _recoverPending = true; }
            }
        }
        // A tick between every RFAL step delays selection and each reply.
        // Poll continuously during a bounded field-active burst, then yield
        // so the UI, stop request and idle/watchdog tasks remain serviceable.
        const rfalLmState state = _rf->rfalListenGetState(nullptr, nullptr);
#if LILYGO_DEBUG_ENABLED
        observeListenState(state);
        _diagnosticFieldActive = state != RFAL_LM_STATE_POWER_OFF;
#endif
        if (state == RFAL_LM_STATE_POWER_OFF || millis() - yielded >= 20U) {
            vTaskDelay(1); yielded = millis();
        }
    }
    _rf->rfalListenStop();
}
#endif
