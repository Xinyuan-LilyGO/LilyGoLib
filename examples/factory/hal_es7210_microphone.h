/**
 * @file      hal_es7210_microphone.h
 * @brief     ES7210 four-channel microphone monitor interface.
 */
#pragma once

#include <stdint.h>

#define ES7210_CAPTURE_FRAMES 512
#define ES7210_SAMPLE_RATE 16000
#define ES7210_MIC_CHANNELS 4
#define ES7210_WAVEFORM_SAMPLES 16

typedef enum {
    HW_ES7210_MIC1 = (1U << 0),
    HW_ES7210_MIC2 = (1U << 1),
    HW_ES7210_MIC3 = (1U << 2),
    HW_ES7210_MIC4 = (1U << 3),
    HW_ES7210_DEFAULT_MIC_MASK = HW_ES7210_MIC1 | HW_ES7210_MIC3,
} hw_es7210_mic_mask_t;

typedef struct {
    int16_t mic_samples[ES7210_MIC_CHANNELS][ES7210_WAVEFORM_SAMPLES];
    uint16_t mic_level[ES7210_MIC_CHANNELS];
} ES7210WaveformData;

bool hw_has_es7210_mic_array();
bool hw_set_es7210_mic_mask(uint8_t mic_mask);
uint8_t hw_get_es7210_mic_mask();
bool hw_set_es7210_mic_start(uint8_t mic_mask);
void hw_set_es7210_mic_stop();
bool hw_audio_get_es7210_waveform_data(ES7210WaveformData *waveform_data);
