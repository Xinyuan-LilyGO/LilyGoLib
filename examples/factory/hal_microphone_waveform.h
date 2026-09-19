/**
 * @file      hal_microphone_waveform.h
 * @brief     Common PCM microphone waveform capture interface.
 */
#pragma once

#include <stdint.h>

#define MIC_WAVEFORM_CAPTURE_FRAMES 512
#define MIC_WAVEFORM_SAMPLE_RATE 16000
#define MIC_WAVEFORM_MAX_CHANNELS 2
#define MIC_WAVEFORM_SAMPLES 16

typedef struct {
    int16_t samples[MIC_WAVEFORM_MAX_CHANNELS][MIC_WAVEFORM_SAMPLES];
    uint16_t level[MIC_WAVEFORM_MAX_CHANNELS];
    uint8_t channels;
} MicrophoneWaveformData;

bool hw_set_mic_waveform_start();
void hw_set_mic_waveform_stop();
bool hw_audio_get_waveform_data(MicrophoneWaveformData *waveform_data);

