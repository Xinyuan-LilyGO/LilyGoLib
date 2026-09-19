#pragma once

#include <LilyGoLog.h>
#include <stdio.h>
#include <time.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include "hal_interface.h"

static inline void hw_log_lora_config(const char *model, const radio_params_t &params,
                                       int16_t state, const char *step, uint8_t requested_power)
{
#if LILYGO_DEBUG_ENABLED
    const char *mode = "Unknown";
    switch (params.mode) {
    case RADIO_DISABLE: mode = "Standby"; break;
    case RADIO_TX: mode = "TX"; break;
    case RADIO_RX: mode = "RX"; break;
    case RADIO_CW: mode = "CW"; break;
    default: break;
    }

    char power[64];
    if (params.power != requested_power) {
        snprintf(power, sizeof(power), "%u dBm (requested: %u dBm)",
                 (unsigned)params.power, (unsigned)requested_power);
    } else {
        snprintf(power, sizeof(power), "%u dBm", (unsigned)params.power);
    }

    char result[80];
    if (state != 0) {
        snprintf(result, sizeof(result), "ERROR %d at %s", (int)state, step);
    } else {
#ifdef ARDUINO
        snprintf(result, sizeof(result), "OK (0)");
#else
        snprintf(result, sizeof(result), "SIMULATED (0)");
#endif
    }

    LILYGO_LOG_PRINTF(
        "\n[LoRa] %s configuration\n"
        "  -----------------------------------------------\n"
        "  Mode          : %s\n"
        "  Frequency     : %.3f MHz\n"
        "  Bandwidth     : %.3f kHz\n"
        "  TX power      : %s\n"
        "  Spreading     : SF%u\n"
        "  Coding rate   : 4/%u\n"
        "  Sync word     : 0x%02X\n"
        "  Interval      : %lu ms\n"
        "  Result        : %s\n"
        "  -----------------------------------------------\n",
        model, mode, (double)params.freq, (double)params.bandwidth, power,
        (unsigned)params.sf, (unsigned)params.cr, (unsigned)params.syncWord,
        (unsigned long)params.interval, result);
#endif
}
