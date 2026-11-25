/**
 * @file      ui_c5_scan_transport.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Advances C5 boot and recurring acknowledged scans without blocking the UI.
 */
#pragma once
#include "ui_define.h"
#if defined(ARDUINO_T_EMBED_PLUS_V2)
class C5ScanTransport {
public:
    HardwareSerial serial{1};
    bool active = false, finished = false, failed = false;
    void begin(bool ble) { _ble = ble; _phase = 0; _step = 0; _attempts = 0; _length = 0; _discardLine = false; active = true; finished = failed = false; }
    void close() {
        if (!active && !_serialOpen) return;
        active = false;
        if (_serialOpen) { serial.end(); _serialOpen = false; }
        instance.setC5Reset(true);
        instance.selectNRF24Path();
    }
    void tick(void (*line)(const char *), void (*scanStarted)() = nullptr) {
        if (!active) return;
        _scanStarted = scanStarted;
        const uint32_t now = millis();
        unsigned budget = 0;
        if (_phase == 5 && int32_t(now - _deadline) >= 0) {
            // Discard late replies before sending a new command. A delayed UI
            // tick must not mistake the preceding round's OK for the next one.
            for (; _serialOpen && serial.available() && budget < 768; ++budget) serial.read();
            if (_serialOpen && serial.available()) return;
            const bool reboot = failed;
            finished = failed = false; _length = 0; _discardLine = false;
            if (reboot) {
                if (_serialOpen) { serial.end(); _serialOpen = false; }
                _phase = 0; _step = 0; _attempts = 0;
            } else {
                _phase = 3; _step = 3; send();
            }
        }
        if (_phase == 0) {
            if (!instance.selectC5Path() || !instance.setC5Reset(true)) { complete(true); return; }
            _phase = 1; _deadline = now + 30; return;
        }
        if (_phase == 1 && int32_t(now - _deadline) >= 0) {
            serial.setRxBufferSize(4096); serial.begin(115200, SERIAL_8N1, C5_UART_RX, C5_UART_TX); _serialOpen = true;
            if (!instance.setC5Reset(false)) { complete(true); return; }
            _phase = 2; _deadline = now + 1500;
        }
        // Bound each UI tick even if advertisements arrive continuously.
        for (; _serialOpen && serial.available() && budget < 768; ++budget) {
            char c = serial.read();
            if (c == '\r' || c == '\n') {
                if (_discardLine) { _discardLine = false; _length = 0; continue; }
                if (!_length) continue;
                _input[_length] = 0; _length = 0;
                if (!strcmp(_input, "ready") && _phase == 2) _deadline = now;
                if (_phase == 3 && !strcmp(_input, "OK")) {
                    if (_step == 3) { if (!_ble) complete(false); else { _phase = 4; _deadline = now + 12000; } }
                    else { ++_step; send(); }
                } else if ((_phase == 3 || _phase == 4) && (!strcmp(_input, "ERROR") || !strcmp(_input, "FAIL"))) complete(true);
                if (_ble && (_phase == 3 || _phase == 4) && _step == 3 && !strncmp(_input, "+BLESCANDONE", 12)) complete(false);
                if (_phase == 3 || _phase == 4) line(_input);
            } else if (!_discardLine) {
                if (_length + 1 < sizeof(_input)) _input[_length++] = c;
                else {
                    // Do not reinterpret an oversized packet's tail as a new AT line.
                    _length = 0; _discardLine = true;
                    if (!_ble) LILYGO_LOG_W("[C5Scan] mode=WiFi discard=overlong-line limit=%u", unsigned(sizeof(_input) - 1));
                }
            }
        }
        if (_phase == 2 && int32_t(now - _deadline) >= 0) { _phase = 3; send(); }
        else if ((_phase == 3 || _phase == 4) && int32_t(now - _deadline) >= 0) {
            if (_phase == 3 && _step == 0 && ++_attempts < 3) send();
            else { complete(true); if (!_ble) LILYGO_LOG_W("[C5Scan] timeout mode=WiFi step=%u", _step); }
        }
    }
    bool scanning() const { return _phase >= 3 && _step == 3 && !finished && !failed; }
private:
    bool _ble = false, _serialOpen = false, _discardLine = false;
    unsigned _phase = 0, _step = 0, _attempts = 0;
    uint32_t _deadline = 0;
    char _input[640] = {};
    size_t _length = 0;
    void (*_scanStarted)() = nullptr;
    void complete(bool error) {
        failed = error; finished = !error;
        _phase = 5; _deadline = millis() + 5000;
    }
    void send() {
        static const char *wifi[] = {"AT", "ATE0", "AT+CWMODE=1", "AT+CWLAP"};
        static const char *ble[] = {"AT", "ATE0", "AT+BLEINIT=1", "AT+BLESCAN=1,3"};
        const char *command = (_ble ? ble : wifi)[_step];
        // Reset round bookkeeping before any replies to the scan are parsed,
        // including when several AT replies arrive in one UI tick.
        if (_step == 3 && _scanStarted) _scanStarted();
        serial.print(command); serial.print("\r\n");
        _deadline = millis() + (_step == 3 ? 20000 : 2000);
        if (!_ble) LILYGO_LOG_D("[C5Scan] mode=WiFi command=%s", command);
    }
};
#endif
