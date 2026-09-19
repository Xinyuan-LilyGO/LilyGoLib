/**
 * @file      LilyGoRotaryInput.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-20
 *
 */
#include "LilyGoLog.h"
#include "LilyGoRotaryInput.h"

#include <limits.h>
#include "esp_err.h"
#include "esp_idf_version.h"

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#ifndef SOC_PCNT_SUPPORTED
#define SOC_PCNT_SUPPORTED 0
#endif

#if SOC_PCNT_SUPPORTED && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0) && __has_include("driver/pulse_cnt.h")
#define LILYGO_ROTARY_CAN_PCNT_NEW 1
#else
#define LILYGO_ROTARY_CAN_PCNT_NEW 0
#endif

#if SOC_PCNT_SUPPORTED && __has_include("driver/pcnt.h")
#define LILYGO_ROTARY_CAN_PCNT_LEGACY 1
#else
#define LILYGO_ROTARY_CAN_PCNT_LEGACY 0
#endif

#if LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_NEW && !LILYGO_ROTARY_CAN_PCNT_NEW
#error "LILYGO_ROTARY_BACKEND_PCNT_NEW requires ESP-IDF >= 5.0 and driver/pulse_cnt.h"
#endif

#if LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_OLD && !LILYGO_ROTARY_CAN_PCNT_LEGACY
#error "LILYGO_ROTARY_BACKEND_PCNT_OLD requires legacy driver/pcnt.h"
#endif

#if (LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_NEW) || \
    (LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_AUTO && LILYGO_ROTARY_CAN_PCNT_NEW)
#include "driver/pulse_cnt.h"
#define LILYGO_ROTARY_HAS_PCNT_NEW 1
#else
#define LILYGO_ROTARY_HAS_PCNT_NEW 0
#endif

#if (LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_OLD) || \
    (LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_AUTO && !LILYGO_ROTARY_HAS_PCNT_NEW && LILYGO_ROTARY_CAN_PCNT_LEGACY)
#include "driver/pcnt.h"
#define LILYGO_ROTARY_HAS_PCNT_LEGACY 1
#else
#define LILYGO_ROTARY_HAS_PCNT_LEGACY 0
#endif

#ifndef LILYGO_ROTARY_PCNT_HIGH_LIMIT
#define LILYGO_ROTARY_PCNT_HIGH_LIMIT 30000
#endif

#ifndef LILYGO_ROTARY_PCNT_LOW_LIMIT
#define LILYGO_ROTARY_PCNT_LOW_LIMIT -30000
#endif

#if LILYGO_ROTARY_HAS_PCNT_LEGACY
#ifndef LILYGO_ROTARY_PCNT_UNIT
#define LILYGO_ROTARY_PCNT_UNIT PCNT_UNIT_0
#endif
#endif

namespace
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
pcnt_unit_handle_t pcntUnit = nullptr;
pcnt_channel_handle_t pcntChannelA = nullptr;
pcnt_channel_handle_t pcntChannelB = nullptr;
bool pcntUnitEnabled = false;
bool pcntUnitStarted = false;
#endif

#if LILYGO_ROTARY_HAS_PCNT_LEGACY
constexpr pcnt_unit_t legacyPcntUnit = LILYGO_ROTARY_PCNT_UNIT;
#endif

#if LILYGO_ROTARY_DEBUG_LOG
void logRotaryConfig(const LilyGoRotaryInputConfig &config, const char *backend)
{
    LILYGO_LOG_I("rotary begin backend=%s pins A=%u B=%u C=%u enc_pullup=%u btn_pullup=%u counts_per_step=%u dir=%d poll_ms=%u btn_debounce_ms=%u glitch_ns=%lu legacy_filter=%u initial A/B/C=%u/%u/%u",
          backend,
          config.pinA,
          config.pinB,
          config.pinButton,
          config.encoderUseInternalPullup ? 1 : 0,
          config.buttonUseInternalPullup ? 1 : 0,
          config.countsPerStep,
          config.directionSign,
          config.pollIntervalMs,
          config.buttonDebounceMs,
          static_cast<unsigned long>(config.glitchFilterNs),
          config.legacyFilterCycles,
          digitalRead(config.pinA),
          digitalRead(config.pinB),
          digitalRead(config.pinButton));
}
#endif

int16_t clampToInt16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return static_cast<int16_t>(value);
}
}

LilyGoRotaryInput::LilyGoRotaryInput()
{
    portMUX_INITIALIZE(&_mux);
}

bool LilyGoRotaryInput::begin(const LilyGoRotaryInputConfig &config)
{
    end();

    _config = config;
    _config.countsPerStep = clampCountsPerStep(_config.countsPerStep);
    _config.directionSign = (_config.directionSign < 0) ? -1 : 1;
    if (_config.buttonDebounceMs == 0) {
        _config.buttonDebounceMs = 1;
    }
    if (_config.pollIntervalMs == 0) {
        _config.pollIntervalMs = 1;
    }

    pinMode(_config.pinA, _config.encoderUseInternalPullup ? INPUT_PULLUP : INPUT);
    pinMode(_config.pinB, _config.encoderUseInternalPullup ? INPUT_PULLUP : INPUT);
    pinMode(_config.pinButton, _config.buttonUseInternalPullup ? INPUT_PULLUP : INPUT);

    _taskShouldStop = false;
    _suspended = false;
    _rawRemainder = 0;
    _softwareRawDelta = 0;
    _lastPcntEventMs = 0;
    _lastPcntEventSign = 0;
    _pendingPcntReverseMs = 0;
    _pendingPcntReverseSign = 0;
    _pendingPcntReverseCount = 0;
    _softwareLastState = (digitalRead(_config.pinA) ? 1 : 0) | (digitalRead(_config.pinB) ? 2 : 0);

    if (!beginBackend()) {
        endBackend();
        _backend = BACKEND_NONE;
#if LILYGO_ROTARY_DEBUG_LOG
        LILYGO_LOG_E("rotary begin failed target_backend=%d pins A=%u B=%u C=%u",
              LILYGO_ROTARY_BACKEND,
              _config.pinA,
              _config.pinB,
              _config.pinButton);
#endif
        return false;
    }

    resetButtonState(_config.ignorePressedOnStart);

    BaseType_t ret = xTaskCreate(taskEntry, "rotary", 2 * 1024, this, 10, &_task);
    if (ret != pdPASS) {
        endBackend();
        _backend = BACKEND_NONE;
        _task = nullptr;
        return false;
    }

    _running = true;
#if LILYGO_ROTARY_DEBUG_LOG
    logRotaryConfig(_config, backendName());
#endif
    return true;
}

void LilyGoRotaryInput::end()
{
    _running = false;
    _suspended = true;
    if (_task) {
        _taskShouldStop = true;
        uint32_t start = millis();
        while (_task && (millis() - start) < 50) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (_task) {
            vTaskDelete(_task);
            _task = nullptr;
        }
    }

    endBackend();

    portENTER_CRITICAL(&_mux);
    _buttonPressed = false;
    _buttonClicked = false;
    _buttonReleased = false;
    _softwareRawDelta = 0;
    portEXIT_CRITICAL(&_mux);

    _rawRemainder = 0;
    _lastPcntEventMs = 0;
    _lastPcntEventSign = 0;
    _pendingPcntReverseMs = 0;
    _pendingPcntReverseSign = 0;
    _pendingPcntReverseCount = 0;
    _backend = BACKEND_NONE;
    _suspended = false;
    _taskShouldStop = false;
}

void LilyGoRotaryInput::suspend()
{
    if (!_running) {
        return;
    }

    _suspended = true;
    pauseBackend();
    clear();
}

void LilyGoRotaryInput::resume()
{
    if (!_running) {
        return;
    }

    clear();
    resetButtonState(true);
    resumeBackend();
    _suspended = false;
}

void LilyGoRotaryInput::clear()
{
    clearBackend();
    clearSoftware();

    portENTER_CRITICAL(&_mux);
    _rawRemainder = 0;
    _lastPcntEventMs = 0;
    _lastPcntEventSign = 0;
    _pendingPcntReverseMs = 0;
    _pendingPcntReverseSign = 0;
    _pendingPcntReverseCount = 0;
    _buttonClicked = false;
    _buttonReleased = false;
    portEXIT_CRITICAL(&_mux);
}

RotaryMsg_t LilyGoRotaryInput::read()
{
    RotaryMsg_t msg = {};
    if (!_running || _suspended) {
        return msg;
    }

    int32_t rawDelta = readRawDelta();
    bool suppressedByButton = false;

#if LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS > 0
    uint32_t now = millis();
    portENTER_CRITICAL(&_mux);
    suppressedByButton = _buttonPressed || (static_cast<int32_t>(_rotarySuppressUntilMs - now) > 0);
    portEXIT_CRITICAL(&_mux);

    if (suppressedByButton && rawDelta != 0) {
#if LILYGO_ROTARY_DEBUG_LOG
        LILYGO_LOG_I("rotary discard raw=%ld while button active pins A/B/C=%u/%u/%u",
              static_cast<long>(rawDelta),
              digitalRead(_config.pinA),
              digitalRead(_config.pinB),
              digitalRead(_config.pinButton));
#endif
        rawDelta = 0;
    }
#endif

    portENTER_CRITICAL(&_mux);
    _rawRemainder += rawDelta * _config.directionSign;
    int32_t diff = _rawRemainder / _config.countsPerStep;
    _rawRemainder %= _config.countsPerStep;
    int32_t remainder = _rawRemainder;
    portEXIT_CRITICAL(&_mux);

    msg.enc_diff = clampToInt16(diff);
    if (msg.enc_diff > 0) {
        msg.dir = ROTARY_DIR_UP;
    } else if (msg.enc_diff < 0) {
        msg.dir = ROTARY_DIR_DOWN;
    } else {
        msg.dir = ROTARY_DIR_NONE;
    }

    portENTER_CRITICAL(&_mux);
    msg.centerBtnPressed = _buttonPressed;
    msg.centerBtnClicked = _buttonClicked;
    msg.centerBtnReleased = _buttonReleased;
    _buttonClicked = false;
    _buttonReleased = false;
    portEXIT_CRITICAL(&_mux);

#if LILYGO_ROTARY_DEBUG_LOG
    if (rawDelta != 0 || msg.enc_diff != 0 || msg.centerBtnClicked || msg.centerBtnReleased) {
        LILYGO_LOG_I("rotary event backend=%s raw=%ld diff=%d rem=%ld dir=%d pins A/B/C=%u/%u/%u btn pressed/click/release=%u/%u/%u",
              backendName(),
              static_cast<long>(rawDelta),
              msg.enc_diff,
              static_cast<long>(remainder),
              msg.dir,
              digitalRead(_config.pinA),
              digitalRead(_config.pinB),
              digitalRead(_config.pinButton),
              msg.centerBtnPressed ? 1 : 0,
              msg.centerBtnClicked ? 1 : 0,
              msg.centerBtnReleased ? 1 : 0);
    }
#endif

    return msg;
}

void LilyGoRotaryInput::setCountsPerStep(uint8_t countsPerStep)
{
    countsPerStep = clampCountsPerStep(countsPerStep);

    portENTER_CRITICAL(&_mux);
    _config.countsPerStep = countsPerStep;
    _rawRemainder = 0;
    portEXIT_CRITICAL(&_mux);
}

uint8_t LilyGoRotaryInput::getCountsPerStep() const
{
    return clampCountsPerStep(_config.countsPerStep);
}

bool LilyGoRotaryInput::isRunning() const
{
    return _running;
}

const char *LilyGoRotaryInput::backendName() const
{
    switch (_backend) {
    case BACKEND_PCNT_NEW:
        return "pcnt_new";
    case BACKEND_PCNT_LEGACY:
        return "pcnt_legacy";
    case BACKEND_SOFTWARE:
        return "software";
    default:
        return "none";
    }
}

void LilyGoRotaryInput::taskEntry(void *arg)
{
    static_cast<LilyGoRotaryInput *>(arg)->taskLoop();
}

void LilyGoRotaryInput::taskLoop()
{
    while (!_taskShouldStop) {
        if (!_suspended) {
            updateButton();
            if (_backend == BACKEND_SOFTWARE) {
                updateSoftwareDecoder();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(_config.pollIntervalMs));
    }
    _task = nullptr;
    vTaskDelete(nullptr);
}

void LilyGoRotaryInput::resetButtonState(bool ignoreCurrentPress)
{
    uint8_t raw = digitalRead(_config.pinButton);
    uint32_t now = millis();

    _buttonStableRaw = raw;
    _buttonLastRaw = raw;
    _buttonLastChangeMs = now;
    _ignorePressedUntilRelease = ignoreCurrentPress && (raw == LOW);

    portENTER_CRITICAL(&_mux);
    _buttonPressed = false;
    _buttonClicked = false;
    _buttonReleased = false;
    portEXIT_CRITICAL(&_mux);
}

void LilyGoRotaryInput::setButtonPressed(bool pressed)
{
    bool changed = false;
    uint32_t suppressUntilMs = millis() + LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS;

    portENTER_CRITICAL(&_mux);
    if (_buttonPressed != pressed) {
        _buttonPressed = pressed;
        changed = true;
#if LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS > 0
        _rotarySuppressUntilMs = suppressUntilMs;
#endif
        if (pressed) {
            _buttonClicked = true;
        } else {
            _buttonReleased = true;
        }
    }
    portEXIT_CRITICAL(&_mux);

#if LILYGO_ROTARY_DEBUG_LOG
    if (changed) {
        LILYGO_LOG_I("rotary button %s raw=%u pin=%u",
              pressed ? "pressed" : "released",
              digitalRead(_config.pinButton),
              _config.pinButton);
    }
#endif
}

void LilyGoRotaryInput::updateButton()
{
    uint8_t raw = digitalRead(_config.pinButton);
    uint32_t now = millis();

    if (raw != _buttonLastRaw) {
        _buttonLastRaw = raw;
        _buttonLastChangeMs = now;
        return;
    }

    if ((now - _buttonLastChangeMs) < _config.buttonDebounceMs) {
        return;
    }

    if (raw == _buttonStableRaw) {
        return;
    }

    _buttonStableRaw = raw;
    bool pressed = (raw == LOW);

    if (_ignorePressedUntilRelease) {
        if (!pressed) {
            _ignorePressedUntilRelease = false;
            setButtonPressed(false);
        }
        return;
    }

    setButtonPressed(pressed);
}

void LilyGoRotaryInput::updateSoftwareDecoder()
{
    static const int8_t transitionTable[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0
    };

    uint8_t currentState = (digitalRead(_config.pinA) ? 1 : 0) | (digitalRead(_config.pinB) ? 2 : 0);
    uint8_t transition = (_softwareLastState << 2) | currentState;
    int8_t delta = transitionTable[transition & 0x0F];
    _softwareLastState = currentState;

    if (delta != 0) {
        portENTER_CRITICAL(&_mux);
        _softwareRawDelta += delta;
        portEXIT_CRITICAL(&_mux);
    }
}

bool LilyGoRotaryInput::beginBackend()
{
#if LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_SOFTWARE
    return beginSoftware();
#elif LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_NEW
    return beginNewPcnt();
#elif LILYGO_ROTARY_BACKEND == LILYGO_ROTARY_BACKEND_PCNT_OLD
    return beginLegacyPcnt();
#else
    if (beginNewPcnt()) {
        return true;
    }
    if (beginLegacyPcnt()) {
        return true;
    }
    return beginSoftware();
#endif
}

void LilyGoRotaryInput::endBackend()
{
    endNewPcnt();
    endLegacyPcnt();
    endSoftware();
    _backend = BACKEND_NONE;
}

void LilyGoRotaryInput::pauseBackend()
{
    pauseNewPcnt();
    pauseLegacyPcnt();
}

void LilyGoRotaryInput::resumeBackend()
{
    resumeNewPcnt();
    resumeLegacyPcnt();
}

void LilyGoRotaryInput::clearBackend()
{
    clearNewPcnt();
    clearLegacyPcnt();
}

int32_t LilyGoRotaryInput::readRawDelta()
{
    switch (_backend) {
    case BACKEND_PCNT_NEW:
        return filterPcntRawDelta(readNewPcnt());
    case BACKEND_PCNT_LEGACY:
        return filterPcntRawDelta(readLegacyPcnt());
    case BACKEND_SOFTWARE:
        return readSoftwareDelta();
    default:
        return 0;
    }
}

int32_t LilyGoRotaryInput::filterPcntRawDelta(int32_t rawDelta)
{
#if LILYGO_ROTARY_PCNT_NORMALIZE_EVENT
    if (rawDelta == 0) {
        return 0;
    }

    uint32_t now = millis();
#if LILYGO_ROTARY_SUPPRESS_WHILE_BUTTON_MS > 0
    bool suppressedByButton = false;
    portENTER_CRITICAL(&_mux);
    suppressedByButton = _buttonPressed || (static_cast<int32_t>(_rotarySuppressUntilMs - now) > 0);
    portEXIT_CRITICAL(&_mux);

    if (suppressedByButton) {
#if LILYGO_ROTARY_DEBUG_LOG
        LILYGO_LOG_I("rotary filter discard raw=%ld while button active pins A/B/C=%u/%u/%u",
              static_cast<long>(rawDelta),
              digitalRead(_config.pinA),
              digitalRead(_config.pinB),
              digitalRead(_config.pinButton));
#endif
        return 0;
    }
#endif

    int8_t sign = rawDelta > 0 ? 1 : -1;
    int32_t normalizedDelta = sign;
    uint8_t countsPerStep = getCountsPerStep();
    bool accept = true;
    const char *reason = nullptr;
    uint32_t elapsed = 0;
    uint8_t pendingReverseCount = 0;

    portENTER_CRITICAL(&_mux);
    elapsed = now - _lastPcntEventMs;
    if (_lastPcntEventMs != 0 &&
        LILYGO_ROTARY_PCNT_EVENT_DEBOUNCE_MS > 0 &&
        elapsed < LILYGO_ROTARY_PCNT_EVENT_DEBOUNCE_MS) {
        accept = false;
        reason = "debounce";
    } else if (_lastPcntEventMs != 0 &&
               LILYGO_ROTARY_PCNT_REVERSE_REJECT_MS > 0 &&
               _lastPcntEventSign != 0 &&
               sign != _lastPcntEventSign &&
               elapsed < LILYGO_ROTARY_PCNT_REVERSE_REJECT_MS) {
        accept = false;
        reason = "reverse";
#if LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_COUNT > 1
    } else if (_lastPcntEventSign != 0 && sign != _lastPcntEventSign) {
        if (_pendingPcntReverseSign == sign &&
            (now - _pendingPcntReverseMs) <= LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_MS) {
            if (_pendingPcntReverseCount < 255) {
                _pendingPcntReverseCount++;
            }
        } else {
            _pendingPcntReverseSign = sign;
            _pendingPcntReverseCount = 1;
        }
        _pendingPcntReverseMs = now;
        pendingReverseCount = _pendingPcntReverseCount;

        if (_pendingPcntReverseCount < LILYGO_ROTARY_PCNT_REVERSE_CONFIRM_COUNT) {
            accept = false;
            reason = "reverse_confirm";
        } else {
            _lastPcntEventMs = now;
            _lastPcntEventSign = sign;
            _pendingPcntReverseSign = 0;
            _pendingPcntReverseCount = 0;
            _pendingPcntReverseMs = 0;
        }
#endif
    } else {
        _lastPcntEventMs = now;
        _lastPcntEventSign = sign;
        _pendingPcntReverseSign = 0;
        _pendingPcntReverseCount = 0;
        _pendingPcntReverseMs = 0;
    }
    portEXIT_CRITICAL(&_mux);

#if LILYGO_ROTARY_DEBUG_LOG
    if (!accept) {
        LILYGO_LOG_I("rotary filter drop raw=%ld reason=%s elapsed=%lu last_sign=%d pending=%d/%u pins A/B/C=%u/%u/%u",
              static_cast<long>(rawDelta),
              reason ? reason : "unknown",
              static_cast<unsigned long>(elapsed),
              _lastPcntEventSign,
              sign,
              pendingReverseCount,
              digitalRead(_config.pinA),
              digitalRead(_config.pinB),
              digitalRead(_config.pinButton));
    } else if (rawDelta != normalizedDelta) {
        LILYGO_LOG_I("rotary filter raw=%ld -> %ld counts_per_step=%u elapsed=%lu pins A/B/C=%u/%u/%u",
              static_cast<long>(rawDelta),
              static_cast<long>(normalizedDelta),
              countsPerStep,
              static_cast<unsigned long>(elapsed),
              digitalRead(_config.pinA),
              digitalRead(_config.pinB),
              digitalRead(_config.pinButton));
    }
#endif

    return accept ? normalizedDelta : 0;
#else
    return rawDelta;
#endif
}

bool LilyGoRotaryInput::beginNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    pcnt_unit_config_t unitConfig = {};
    unitConfig.high_limit = LILYGO_ROTARY_PCNT_HIGH_LIMIT;
    unitConfig.low_limit = LILYGO_ROTARY_PCNT_LOW_LIMIT;
    if (pcnt_new_unit(&unitConfig, &pcntUnit) != ESP_OK) {
        return false;
    }

    pcnt_glitch_filter_config_t filterConfig = {};
    filterConfig.max_glitch_ns = _config.glitchFilterNs;
    if (pcnt_unit_set_glitch_filter(pcntUnit, &filterConfig) != ESP_OK) {
        endNewPcnt();
        return false;
    }

    pcnt_chan_config_t channelAConfig = {};
    channelAConfig.edge_gpio_num = _config.pinA;
    channelAConfig.level_gpio_num = _config.pinB;
    if (pcnt_new_channel(pcntUnit, &channelAConfig, &pcntChannelA) != ESP_OK) {
        endNewPcnt();
        return false;
    }

    pcnt_chan_config_t channelBConfig = {};
    channelBConfig.edge_gpio_num = _config.pinB;
    channelBConfig.level_gpio_num = _config.pinA;
    if (pcnt_new_channel(pcntUnit, &channelBConfig, &pcntChannelB) != ESP_OK) {
        endNewPcnt();
        return false;
    }

    if (pcnt_channel_set_edge_action(pcntChannelA,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE) != ESP_OK ||
        pcnt_channel_set_level_action(pcntChannelA,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK ||
        pcnt_channel_set_edge_action(pcntChannelB,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE) != ESP_OK ||
        pcnt_channel_set_level_action(pcntChannelB,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE) != ESP_OK) {
        endNewPcnt();
        return false;
    }

    if (pcnt_unit_enable(pcntUnit) != ESP_OK) {
        endNewPcnt();
        return false;
    }
    pcntUnitEnabled = true;

    if (pcnt_unit_clear_count(pcntUnit) != ESP_OK ||
        pcnt_unit_start(pcntUnit) != ESP_OK) {
        endNewPcnt();
        return false;
    }
    pcntUnitStarted = true;

    _backend = BACKEND_PCNT_NEW;
#if LILYGO_ROTARY_DEBUG_LOG
    LILYGO_LOG_I("rotary pcnt_new configured high=%d low=%d glitch_ns=%lu",
          LILYGO_ROTARY_PCNT_HIGH_LIMIT,
          LILYGO_ROTARY_PCNT_LOW_LIMIT,
          static_cast<unsigned long>(_config.glitchFilterNs));
#endif
    return true;
#else
    return false;
#endif
}

void LilyGoRotaryInput::endNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    if (pcntUnit) {
        if (pcntUnitStarted) {
            pcnt_unit_stop(pcntUnit);
            pcntUnitStarted = false;
        }
        if (pcntUnitEnabled) {
            pcnt_unit_disable(pcntUnit);
            pcntUnitEnabled = false;
        }
    }
    if (pcntChannelA) {
        pcnt_del_channel(pcntChannelA);
        pcntChannelA = nullptr;
    }
    if (pcntChannelB) {
        pcnt_del_channel(pcntChannelB);
        pcntChannelB = nullptr;
    }
    if (pcntUnit) {
        pcnt_del_unit(pcntUnit);
        pcntUnit = nullptr;
    }
#endif
}

void LilyGoRotaryInput::pauseNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    if (_backend == BACKEND_PCNT_NEW && pcntUnit && pcntUnitStarted) {
        pcnt_unit_stop(pcntUnit);
        pcntUnitStarted = false;
    }
#endif
}

void LilyGoRotaryInput::resumeNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    if (_backend == BACKEND_PCNT_NEW && pcntUnit && !pcntUnitStarted) {
        pcnt_unit_clear_count(pcntUnit);
        if (pcnt_unit_start(pcntUnit) == ESP_OK) {
            pcntUnitStarted = true;
        }
    }
#endif
}

void LilyGoRotaryInput::clearNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    if (_backend == BACKEND_PCNT_NEW && pcntUnit) {
        pcnt_unit_clear_count(pcntUnit);
    }
#endif
}

int32_t LilyGoRotaryInput::readNewPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_NEW
    if (_backend != BACKEND_PCNT_NEW || !pcntUnit) {
        return 0;
    }
    int value = 0;
    if (pcnt_unit_get_count(pcntUnit, &value) != ESP_OK) {
        return 0;
    }
    if (value != 0) {
        pcnt_unit_clear_count(pcntUnit);
    }
    return value;
#else
    return 0;
#endif
}

bool LilyGoRotaryInput::beginLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    pcnt_config_t channelAConfig = {};
    channelAConfig.pulse_gpio_num = _config.pinA;
    channelAConfig.ctrl_gpio_num = _config.pinB;
    channelAConfig.lctrl_mode = PCNT_MODE_REVERSE;
    channelAConfig.hctrl_mode = PCNT_MODE_KEEP;
    channelAConfig.pos_mode = PCNT_COUNT_DEC;
    channelAConfig.neg_mode = PCNT_COUNT_INC;
    channelAConfig.counter_h_lim = LILYGO_ROTARY_PCNT_HIGH_LIMIT;
    channelAConfig.counter_l_lim = LILYGO_ROTARY_PCNT_LOW_LIMIT;
    channelAConfig.unit = legacyPcntUnit;
    channelAConfig.channel = PCNT_CHANNEL_0;

#if LILYGO_ROTARY_PCNT_LEGACY_SINGLE_EDGE
    channelAConfig.neg_mode = PCNT_COUNT_DIS;

    if (pcnt_unit_config(&channelAConfig) != ESP_OK) {
        return false;
    }
#else
    pcnt_config_t channelBConfig = {};
    channelBConfig.pulse_gpio_num = _config.pinB;
    channelBConfig.ctrl_gpio_num = _config.pinA;
    channelBConfig.lctrl_mode = PCNT_MODE_REVERSE;
    channelBConfig.hctrl_mode = PCNT_MODE_KEEP;
    channelBConfig.pos_mode = PCNT_COUNT_INC;
    channelBConfig.neg_mode = PCNT_COUNT_DEC;
    channelBConfig.counter_h_lim = LILYGO_ROTARY_PCNT_HIGH_LIMIT;
    channelBConfig.counter_l_lim = LILYGO_ROTARY_PCNT_LOW_LIMIT;
    channelBConfig.unit = legacyPcntUnit;
    channelBConfig.channel = PCNT_CHANNEL_1;

    if (pcnt_unit_config(&channelAConfig) != ESP_OK ||
        pcnt_unit_config(&channelBConfig) != ESP_OK) {
        return false;
    }
#endif

    _backend = BACKEND_PCNT_LEGACY;
    if (pcnt_set_filter_value(legacyPcntUnit, _config.legacyFilterCycles) != ESP_OK ||
        pcnt_filter_enable(legacyPcntUnit) != ESP_OK ||
        pcnt_counter_clear(legacyPcntUnit) != ESP_OK) {
        endLegacyPcnt();
        return false;
    }
    if (pcnt_counter_resume(legacyPcntUnit) != ESP_OK) {
        endLegacyPcnt();
        return false;
    }

#if LILYGO_ROTARY_DEBUG_LOG
    LILYGO_LOG_I("rotary pcnt_legacy configured unit=%d high=%d low=%d filter_cycles=%u single_edge=%u",
          static_cast<int>(legacyPcntUnit),
          LILYGO_ROTARY_PCNT_HIGH_LIMIT,
          LILYGO_ROTARY_PCNT_LOW_LIMIT,
          _config.legacyFilterCycles,
          LILYGO_ROTARY_PCNT_LEGACY_SINGLE_EDGE ? 1 : 0);
#endif
    return true;
#else
    return false;
#endif
}

void LilyGoRotaryInput::endLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    if (_backend == BACKEND_PCNT_LEGACY) {
        pcnt_counter_pause(legacyPcntUnit);
        pcnt_counter_clear(legacyPcntUnit);
        pcnt_filter_disable(legacyPcntUnit);
        pcnt_set_pin(legacyPcntUnit, PCNT_CHANNEL_0, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED);
        pcnt_set_pin(legacyPcntUnit, PCNT_CHANNEL_1, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED);
        _backend = BACKEND_NONE;
    }
#endif
}

void LilyGoRotaryInput::pauseLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    if (_backend == BACKEND_PCNT_LEGACY) {
        pcnt_counter_pause(legacyPcntUnit);
    }
#endif
}

void LilyGoRotaryInput::resumeLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    if (_backend == BACKEND_PCNT_LEGACY) {
        pcnt_counter_clear(legacyPcntUnit);
        pcnt_counter_resume(legacyPcntUnit);
    }
#endif
}

void LilyGoRotaryInput::clearLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    if (_backend == BACKEND_PCNT_LEGACY) {
        pcnt_counter_clear(legacyPcntUnit);
    }
#endif
}

int32_t LilyGoRotaryInput::readLegacyPcnt()
{
#if LILYGO_ROTARY_HAS_PCNT_LEGACY
    if (_backend != BACKEND_PCNT_LEGACY) {
        return 0;
    }
    int16_t value = 0;
    if (pcnt_get_counter_value(legacyPcntUnit, &value) != ESP_OK) {
        return 0;
    }
    if (value != 0) {
        pcnt_counter_clear(legacyPcntUnit);
    }
    return value;
#else
    return 0;
#endif
}

bool LilyGoRotaryInput::beginSoftware()
{
    _backend = BACKEND_SOFTWARE;
    _softwareLastState = (digitalRead(_config.pinA) ? 1 : 0) | (digitalRead(_config.pinB) ? 2 : 0);
    clearSoftware();
    return true;
}

void LilyGoRotaryInput::endSoftware()
{
    clearSoftware();
}

void LilyGoRotaryInput::clearSoftware()
{
    portENTER_CRITICAL(&_mux);
    _softwareRawDelta = 0;
    portEXIT_CRITICAL(&_mux);
}

int32_t LilyGoRotaryInput::readSoftwareDelta()
{
    portENTER_CRITICAL(&_mux);
    int32_t delta = _softwareRawDelta;
    _softwareRawDelta = 0;
    portEXIT_CRITICAL(&_mux);
    return delta;
}
