/**
 * @file      LilyGoNfcType2Rf.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Declares Type 2 acquisition and a dedicated low-level RFAL listener worker.
 */
#pragma once
#include "nfc/LilyGoNfcType2.h"
#include "nfc/LilyGoNfcType2Write.h"
#include "LilyGoLogConfig.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)
#include "nfc/nfc_include.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>

/** @brief Read available Type 2 pages and optional identity commands without writes. */
bool lilygoReadNfcType2(RfalNfcClass &reader, const rfalNfcaListenDevice &card, LilyGoNfcType2Dump &dump);
/** @brief Capture the selected target, preflight user memory, write and verify changed pages. */
LilyGoNfcType2WriteResult lilygoWriteNfcType2(RfalNfcClass &reader,
        const rfalNfcaListenDevice &card, const LilyGoNfcType2Dump &source, LilyGoNfcType2Dump &target);

#if LILYGO_DEBUG_ENABLED
/** Diagnostic snapshots never read IRQ status registers or contain card payloads. */
struct LilyGoNfcType2RfDiagnostics {
    uint32_t samples, polls, stateChanges, statesSeen, maxPollGapUs, maxFieldPollGapUs, maxPrepareUs, dropped;
    uint32_t hardwareStatesSeen;
    uint16_t registerError;
    uint8_t operation, mode, passiveControl, passiveState, auxMod, modulation, auxiliary, iso14443a;
    bool fieldOn, memoryOk, memoryRepaired;
};

/** Bounded worker trace: 1 = listen state, 2 = command, 3 = error, 4 = selection hardware. */
struct LilyGoNfcType2RfTrace {
    uint32_t atUs, prepareUs;
    uint16_t rxBits, txBits, error;
    uint8_t kind, state, command, reply;
    uint8_t hardwareState, operation, mode, auxiliary, iso14443a;
    bool authenticated;
};
#endif

/** Thread-safe snapshot of listener activity and its most recent exchange. */
struct LilyGoNfcType2RfStatus {
    uint32_t exchanges;          /**< Completed command dispatches. */
    uint32_t reads;              /**< Successful memory read commands. */
    uint32_t writes;             /**< Successful writes to RAM. */
    uint32_t errors;             /**< RF transport failures. */
    uint16_t lastError;          /**< Most recent RFAL error. */
    uint16_t rxBits;             /**< Last command length after CRC removal. */
    uint16_t txBits;             /**< Last response length before CRC insertion. */
    uint8_t command;             /**< Last command byte. */
    uint8_t listenState;         /**< Current low-level RFAL listen state. */
    bool readerActive;           /**< Anticollision has selected this card. */
    bool failed;                 /**< Listener could not recover from a transport failure. */
#if LILYGO_DEBUG_ENABLED
    LilyGoNfcType2RfDiagnostics diagnostics;
#endif
};

/**
 * @brief Owns a volatile card image and the timing-sensitive listener task.
 *
 * The task never calls application callbacks or logs command contents. Status
 * is copied under a short critical section and consumed on the caller thread.
 * stop() joins the task before the caller may power off or reuse the NFC chip.
 */
class LilyGoNfcType2Listener {
public:
    LilyGoNfcType2Listener();
    ~LilyGoNfcType2Listener();
    /** @brief Start listening with a private image copy and optional RAM writes. */
    bool begin(RfalRfClass *rf, const LilyGoNfcType2Dump &dump, bool allowUserWrites);
    /** @brief Stop and join the worker before releasing RF hardware. */
    void stop();
    /** @brief Copy transport statistics without exposing mutable card data. */
    LilyGoNfcType2RfStatus status();
#if LILYGO_DEBUG_ENABLED
    /** @brief Consume one buffered event on the non-realtime service thread. */
    bool takeTrace(LilyGoNfcType2RfTrace &event);
#endif
private:
    LilyGoNfcType2Listener(const LilyGoNfcType2Listener &) = delete;
    LilyGoNfcType2Listener &operator=(const LilyGoNfcType2Listener &) = delete;
    static void taskEntry(void *argument);
    void run();
    ReturnCode restartListen();
    ReturnCode exchange(uint16_t txBits);
    void dispatch();
    void recordError(ReturnCode error);
#if LILYGO_DEBUG_ENABLED
    void recordTrace(const LilyGoNfcType2RfTrace &event);
    void observeListenState(rfalLmState state);
    void captureSelection(rfalLmState state);
    void captureDiagnostics();
    static constexpr uint8_t kTraceCapacity = 32;
    LilyGoNfcType2RfTrace _trace[kTraceCapacity] = {};
    uint8_t _traceHead = 0, _traceCount = 0, _diagnosticLastState = 0xFF, _diagnosticLastHardware = 0xFF;
    uint32_t _diagnosticAtMs = 0, _diagnosticPollUs = 0, _diagnosticSelectionUs = 0;
    bool _diagnosticFieldActive = false;
#endif
    RfalRfClass *_rf;
    TaskHandle_t _task;
    SemaphoreHandle_t _stopped;
    std::atomic<bool> _stopRequested;
    portMUX_TYPE _statusMux = portMUX_INITIALIZER_UNLOCKED;
    LilyGoNfcType2RfStatus _status;
    LilyGoNfcType2Dump _dump;
    LilyGoNfcType2Session _session;
    uint8_t _rx[32];
    uint8_t _tx[LILYGO_NFC_TYPE2_MAX_PAGES * LILYGO_NFC_TYPE2_PAGE_SIZE];
    uint16_t _rxBits;
    bool _exchangePending;
    bool _recoverPending;
    bool _allowUserWrites;
};
#endif
