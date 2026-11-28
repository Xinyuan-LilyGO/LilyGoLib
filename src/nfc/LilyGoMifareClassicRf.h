/**
 * @file      LilyGoMifareClassicRf.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Owns the experimental Classic listener and its transparent-mode RMT transmitter.
 */
#pragma once
#include "nfc/LilyGoMifareClassicEmulation.h"
#if defined(ARDUINO) && defined(USING_ST25R3916)
#include "LilyGoLog.h"
#include "nfc/nfc_include.h"
#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

#if LILYGO_NFC_DEBUG_ENABLED
/** Diagnostic-only snapshots; no register access is performed by the UI thread. */
struct LilyGoMifareClassicRfDiagnostics {
    uint32_t samples, sampledAtMs, polls, irqHighPolls, stateChanges, statesSeen;
    uint32_t prepareUs, maxPrepareUs, transmitUs;
    uint16_t registerError;
    uint8_t listenState, operation, mode, passiveControl, passiveState, auxMod;
    bool fieldOn, irqHigh, memoryOk;
};
#endif

/** Snapshot copied to the service thread; no callbacks or printing in the RF worker. */
struct LilyGoMifareClassicRfStatus {
    uint32_t exchanges, reads, authAttempts, authSuccesses, rejected, errors;
    uint16_t lastError, rxBits, txBits;
    uint8_t command, reason;
    bool readerActive, failed;
#if LILYGO_NFC_DEBUG_ENABLED
    LilyGoMifareClassicRfDiagnostics diagnostics;
#endif
};

/** Read-only listener; stop() joins the worker before restoring shared hardware. */
class LilyGoMifareClassicListener {
public:
    LilyGoMifareClassicListener();
    ~LilyGoMifareClassicListener();
    /** @brief Copy a supported image, allocate the transmitter and start the worker. */
    bool begin(RfalRfClass *rf, const LilyGoMifareClassicDump &dump);
    /** @brief Join the worker and release RMT without detaching SPI pins. */
    void stop();
    /** @brief Get a coherent activity snapshot. */
    LilyGoMifareClassicRfStatus status();
private:
    static void taskEntry(void *argument);
    void run();
    ReturnCode restart();
    bool dispatch(bool initial);
    bool transmit(const LilyGoMifareClassicResponse &response);
    void fail(ReturnCode error);
    void releaseTransmitter();
#if LILYGO_NFC_DEBUG_ENABLED
    void logConfigurationState(const char *stage);
    void observeListenState(rfalLmState state);
    void captureDiagnostics();
    uint32_t _diagnosticAtMs = 0, _diagnosticPolls = 0, _diagnosticIrqPolls = 0;
    uint8_t _diagnosticLastState = 0xFF;
#endif
    RfalRfST25R3916Class *_rf;
    rmt_channel_handle_t _channel;
    rmt_encoder_handle_t _encoder;
    TaskHandle_t _task;
    SemaphoreHandle_t _stopped;
    std::atomic<bool> _stopRequested;
    portMUX_TYPE _statusMux = portMUX_INITIALIZER_UNLOCKED;
    LilyGoMifareClassicRfStatus _status;
    LilyGoMifareClassicDump _dump;
    LilyGoMifareClassicSession _session;
    uint8_t _rx[32];
    uint16_t _rxBits;
    bool _transmitterEnabled, _sleeping;
    uint32_t _rmtMatrix, _rmtMux;
    LilyGoMifareClassicPulse _pulses[1700];
    rmt_symbol_word_t _symbols[850];
};
#endif
