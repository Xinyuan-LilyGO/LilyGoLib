/**
 * @file      LilyGoRotaryInput.h
 * @brief     Declares the rotary encoder input service and backend configuration.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-20
 *
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../display/LilyGoDispInterface.h"

/** Select the rotary backend automatically. */
#define LILYGO_ROTARY_BACKEND_AUTO       0
/** Use the ESP-IDF 5.x PCNT driver backend. */
#define LILYGO_ROTARY_BACKEND_PCNT_NEW   1
/** Use the legacy ESP-IDF PCNT driver backend. */
#define LILYGO_ROTARY_BACKEND_PCNT_OLD   2
/** Use the software quadrature decoder backend. */
#define LILYGO_ROTARY_BACKEND_SOFTWARE   3

/**
 * @brief Default rotary backend selection for the current board.
 */
#ifndef LILYGO_ROTARY_BACKEND
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_BACKEND LILYGO_ROTARY_BACKEND_PCNT_OLD
#else
#define LILYGO_ROTARY_BACKEND LILYGO_ROTARY_BACKEND_AUTO
#endif
#endif

/**
 * @brief Enable verbose rotary debug logging when set to 1.
 */
#ifndef LILYGO_ROTARY_DEBUG_LOG
#define LILYGO_ROTARY_DEBUG_LOG 0
#endif

/**
 * @brief Default raw encoder counts consumed for one logical step.
 */
#ifndef LILYGO_ROTARY_COUNTS_PER_STEP
#define LILYGO_ROTARY_COUNTS_PER_STEP 4
#endif

/**
 * @brief Minimum allowed counts-per-step setting.
 */
#ifndef LILYGO_ROTARY_COUNTS_PER_STEP_MIN
#define LILYGO_ROTARY_COUNTS_PER_STEP_MIN 1
#endif

/**
 * @brief Maximum allowed counts-per-step setting.
 */
#ifndef LILYGO_ROTARY_COUNTS_PER_STEP_MAX
#define LILYGO_ROTARY_COUNTS_PER_STEP_MAX 4
#endif

/**
 * @brief Direction multiplier applied to decoded rotary deltas.
 */
#ifndef LILYGO_ROTARY_DIRECTION_SIGN
#define LILYGO_ROTARY_DIRECTION_SIGN 1
#endif

/**
 * @brief Enable internal pullups for encoder A/B pins when set to 1.
 */
#ifndef LILYGO_ROTARY_ENCODER_USE_INTERNAL_PULLUP
#define LILYGO_ROTARY_ENCODER_USE_INTERNAL_PULLUP 0
#endif

/**
 * @brief Button debounce interval in milliseconds.
 */
#ifndef LILYGO_ROTARY_BUTTON_DEBOUNCE_MS
#define LILYGO_ROTARY_BUTTON_DEBOUNCE_MS 20
#endif

/**
 * @brief Rotary service polling interval in milliseconds.
 */
#ifndef LILYGO_ROTARY_BUTTON_POLL_MS
#define LILYGO_ROTARY_BUTTON_POLL_MS 5
#endif

/**
 * @brief Glitch filter duration for the ESP-IDF 5.x PCNT backend.
 */
#ifndef LILYGO_ROTARY_PCNT_GLITCH_FILTER_NS
#define LILYGO_ROTARY_PCNT_GLITCH_FILTER_NS 1000
#endif

/**
 * @brief Glitch filter duration in APB cycles for the legacy PCNT backend.
 */
#ifndef LILYGO_ROTARY_PCNT_LEGACY_FILTER_CYCLES
#define LILYGO_ROTARY_PCNT_LEGACY_FILTER_CYCLES 1023
#endif

/**
 * @brief Count only one edge per quadrature phase when enabled for legacy PCNT.
 */
#ifndef LILYGO_ROTARY_PCNT_LEGACY_SINGLE_EDGE
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_LEGACY_SINGLE_EDGE 0
#else
#define LILYGO_ROTARY_PCNT_LEGACY_SINGLE_EDGE 0
#endif
#endif

/**
 * @brief Suppress rotary movement for a short time while the button is active.
 */
#ifndef LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS 150
#else
#define LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS 0
#endif
#endif

/**
 * @brief Normalize raw PCNT events to one logical raw step when enabled.
 */
#ifndef LILYGO_ROTARY_PCNT_NORMALIZE_EVENT
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_NORMALIZE_EVENT 1
#else
#define LILYGO_ROTARY_PCNT_NORMALIZE_EVENT 0
#endif
#endif

/**
 * @brief Minimum interval between accepted normalized PCNT events.
 */
#ifndef LILYGO_ROTARY_PCNT_EVENT_DEBOUNCE_MS
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_EVENT_DEBOUNCE_MS 150
#else
#define LILYGO_ROTARY_PCNT_EVENT_DEBOUNCE_MS 0
#endif
#endif

/**
 * @brief Interval used to reject immediate reverse-direction PCNT noise.
 */
#ifndef LILYGO_ROTARY_PCNT_REVERSE_REJECT_MS
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_REVERSE_REJECT_MS 120
#else
#define LILYGO_ROTARY_PCNT_REVERSE_REJECT_MS 0
#endif
#endif

/**
 * @brief Number of reverse events required before accepting a direction change.
 */
#ifndef LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_COUNT
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_COUNT 3
#else
#define LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_COUNT 1
#endif
#endif

/**
 * @brief Time window for confirming reverse-direction PCNT events.
 */
#ifndef LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_MS
#if defined(ARDUINO_T_LORA_PAGER)
#define LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_MS 1500
#else
#define LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_MS 0
#endif
#endif

/**
 * @brief Pin and timing configuration for LilyGoRotaryInput.
 */
struct LilyGoRotaryInputConfig {
    uint8_t pinA = 0;              /**< Encoder phase A pin. */
    uint8_t pinB = 0;              /**< Encoder phase B pin. */
    uint8_t pinButton = 0;         /**< Encoder center button pin. */
    int8_t directionSign = LILYGO_ROTARY_DIRECTION_SIGN;              /**< Direction multiplier, usually 1 or -1. */
    uint8_t countsPerStep = LILYGO_ROTARY_COUNTS_PER_STEP;            /**< Raw counts consumed for one logical step. */
    uint32_t glitchFilterNs = LILYGO_ROTARY_PCNT_GLITCH_FILTER_NS;    /**< ESP-IDF 5.x PCNT glitch filter duration. */
    uint16_t legacyFilterCycles = LILYGO_ROTARY_PCNT_LEGACY_FILTER_CYCLES; /**< Legacy PCNT filter duration. */
    uint16_t buttonDebounceMs = LILYGO_ROTARY_BUTTON_DEBOUNCE_MS;     /**< Button debounce interval in milliseconds. */
    uint8_t pollIntervalMs = LILYGO_ROTARY_BUTTON_POLL_MS;            /**< Polling interval in milliseconds. */
    bool encoderUseInternalPullup = LILYGO_ROTARY_ENCODER_USE_INTERNAL_PULLUP; /**< true to enable encoder pin pullups. */
    bool buttonUseInternalPullup = false;                             /**< true to enable the button pin pullup. */
    bool ignorePressedOnStart = true;                                 /**< true to ignore an already-held button until release. */
};

/**
 * @brief Background rotary encoder and center-button reader.
 */
class LilyGoRotaryInput
{
public:
    /**
     * @brief Construct a rotary input service.
     */
    LilyGoRotaryInput();

    /**
     * @brief Start the rotary input backend and polling task.
     * @param config Pin and timing configuration.
     * @return true if the backend and task were started.
     */
    bool begin(const LilyGoRotaryInputConfig &config);

    /**
     * @brief Stop the rotary input service and release backend resources.
     */
    void end();

    /**
     * @brief Temporarily stop reporting rotary and button events.
     */
    void suspend();

    /**
     * @brief Resume event reporting after suspend().
     */
    void resume();

    /**
     * @brief Clear accumulated movement and button edge state.
     */
    void clear();

    /**
     * @brief Read and clear the latest rotary message.
     * @return Rotary direction, delta, and button edge state.
     */
    RotaryMsg_t read();

    /**
     * @brief Set the number of raw counts required for one logical step.
     * @param countsPerStep Requested counts per step; clamped to the allowed range.
     */
    void setCountsPerStep(uint8_t countsPerStep);

    /**
     * @brief Get the current counts-per-step setting.
     * @return Current counts per logical step.
     */
    uint8_t getCountsPerStep() const;

    /**
     * @brief Get the minimum allowed counts-per-step value.
     * @return Minimum counts per logical step.
     */
    static constexpr uint8_t getCountsPerStepMin()
    {
        return LILYGO_ROTARY_COUNTS_PER_STEP_MIN;
    }

    /**
     * @brief Get the maximum allowed counts-per-step value.
     * @return Maximum counts per logical step.
     */
    static constexpr uint8_t getCountsPerStepMax()
    {
        return LILYGO_ROTARY_COUNTS_PER_STEP_MAX < LILYGO_ROTARY_COUNTS_PER_STEP_MIN
                   ? LILYGO_ROTARY_COUNTS_PER_STEP_MIN
                   : LILYGO_ROTARY_COUNTS_PER_STEP_MAX;
    }

    /**
     * @brief Clamp a counts-per-step value to the supported range.
     * @param countsPerStep Requested counts per logical step.
     * @return Clamped counts-per-step value.
     */
    static constexpr uint8_t clampCountsPerStep(uint8_t countsPerStep)
    {
        return countsPerStep < getCountsPerStepMin()
                   ? getCountsPerStepMin()
                   : (countsPerStep > getCountsPerStepMax() ? getCountsPerStepMax() : countsPerStep);
    }

    /**
     * @brief Check whether the rotary service is running.
     * @return true if begin() succeeded and end() has not stopped the service.
     */
    bool isRunning() const;

    /**
     * @brief Get the active backend name.
     * @return Static backend name string.
     */
    const char *backendName() const;

private:
    /**
     * @brief Internal rotary backend identifiers.
     */
    enum Backend {
        BACKEND_NONE,
        BACKEND_PCNT_NEW,
        BACKEND_PCNT_LEGACY,
        BACKEND_SOFTWARE,
    };

    /**
     * @brief FreeRTOS task entry point.
     * @param arg LilyGoRotaryInput instance pointer.
     */
    static void taskEntry(void *arg);

    /**
     * @brief Poll rotary and button state until the task is stopped.
     */
    void taskLoop();

    /**
     * @brief Sample and debounce the center button.
     */
    void updateButton();

    /**
     * @brief Update the software quadrature decoder from GPIO state.
     */
    void updateSoftwareDecoder();

    /**
     * @brief Reset debounced button state.
     * @param ignoreCurrentPress true to ignore a button already held at startup.
     */
    void resetButtonState(bool ignoreCurrentPress);

    /**
     * @brief Store a debounced button pressed state and edge flags.
     * @param pressed true when the button is pressed.
     */
    void setButtonPressed(bool pressed);

    /**
     * @brief Start the configured rotary backend.
     * @return true if a backend was started.
     */
    bool beginBackend();

    /**
     * @brief Stop and release the active backend.
     */
    void endBackend();

    /**
     * @brief Pause event generation for the active backend.
     */
    void pauseBackend();

    /**
     * @brief Resume event generation for the active backend.
     */
    void resumeBackend();

    /**
     * @brief Clear accumulated state for the active backend.
     */
    void clearBackend();

    /**
     * @brief Read raw movement from the active backend.
     * @return Signed raw movement delta.
     */
    int32_t readRawDelta();

    /**
     * @brief Apply PCNT event filtering and normalization.
     * @param rawDelta Raw PCNT movement delta.
     * @return Filtered raw movement delta.
     */
    int32_t filterPcntRawDelta(int32_t rawDelta);

    /**
     * @brief Start the ESP-IDF 5.x PCNT backend.
     * @return true if the backend was started.
     */
    bool beginNewPcnt();

    /** @brief Stop the ESP-IDF 5.x PCNT backend. */
    void endNewPcnt();

    /** @brief Pause the ESP-IDF 5.x PCNT backend. */
    void pauseNewPcnt();

    /** @brief Resume the ESP-IDF 5.x PCNT backend. */
    void resumeNewPcnt();

    /** @brief Clear the ESP-IDF 5.x PCNT counter. */
    void clearNewPcnt();

    /**
     * @brief Read and clear the ESP-IDF 5.x PCNT counter.
     * @return Signed raw movement delta.
     */
    int32_t readNewPcnt();

    /**
     * @brief Start the legacy ESP-IDF PCNT backend.
     * @return true if the backend was started.
     */
    bool beginLegacyPcnt();

    /** @brief Stop the legacy ESP-IDF PCNT backend. */
    void endLegacyPcnt();

    /** @brief Pause the legacy ESP-IDF PCNT backend. */
    void pauseLegacyPcnt();

    /** @brief Resume the legacy ESP-IDF PCNT backend. */
    void resumeLegacyPcnt();

    /** @brief Clear the legacy ESP-IDF PCNT counter. */
    void clearLegacyPcnt();

    /**
     * @brief Read and clear the legacy ESP-IDF PCNT counter.
     * @return Signed raw movement delta.
     */
    int32_t readLegacyPcnt();

    /**
     * @brief Start the software decoder backend.
     * @return true if the backend was started.
     */
    bool beginSoftware();

    /** @brief Stop the software decoder backend. */
    void endSoftware();

    /** @brief Clear accumulated software decoder movement. */
    void clearSoftware();

    /**
     * @brief Read and clear software decoder movement.
     * @return Signed raw movement delta.
     */
    int32_t readSoftwareDelta();

    /** Runtime configuration. */
    LilyGoRotaryInputConfig _config;
    /** Active backend. */
    Backend _backend = BACKEND_NONE;
    /** Rotary polling task handle. */
    TaskHandle_t _task = nullptr;
    /** Spinlock for shared task state. */
    portMUX_TYPE _mux;
    /** true when the polling task should exit. */
    volatile bool _taskShouldStop = false;
    /** true when event generation is suspended. */
    volatile bool _suspended = false;
    /** true when the service has started successfully. */
    bool _running = false;
    /** Raw movement remainder carried between reads. */
    int32_t _rawRemainder = 0;
    /** Accumulated raw software-decoder delta. */
    int32_t _softwareRawDelta = 0;
    /** Previous two-bit software decoder state. */
    uint8_t _softwareLastState = 0;

    /** Stable debounced raw button level. */
    uint8_t _buttonStableRaw = HIGH;
    /** Last sampled raw button level. */
    uint8_t _buttonLastRaw = HIGH;
    /** millis() timestamp of the last raw button change. */
    uint32_t _buttonLastChangeMs = 0;
    /** millis() deadline before rotary movement is accepted after button activity. */
    uint32_t _rotarySuppressUntilMs = 0;
    /** millis() timestamp of the last accepted PCNT event. */
    uint32_t _lastPcntEventMs = 0;
    /** Direction sign of the last accepted PCNT event. */
    int8_t _lastPcntEventSign = 0;
    /** millis() timestamp for pending reverse-direction confirmation. */
    uint32_t _pendingPcntReverseMs = 0;
    /** Direction sign being confirmed as a reverse event. */
    int8_t _pendingPcntReverseSign = 0;
    /** Number of pending reverse events observed in the confirmation window. */
    uint8_t _pendingPcntReverseCount = 0;
    /** true while the center button is pressed. */
    bool _buttonPressed = false;
    /** One-shot center-button click flag. */
    bool _buttonClicked = false;
    /** One-shot center-button release flag. */
    bool _buttonReleased = false;
    /** true while startup press suppression is active. */
    bool _ignorePressedUntilRelease = false;
};
