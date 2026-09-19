/**
 * @file      hal_es7210_microphone.cpp
 * @brief     ES7210 four-channel microphone monitor support for the original T-Deck.
 */
#include "hal_es7210_microphone.h"

#include <string.h>

#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
#include <LilyGoLib.h>
#include <LilyGoLog.h>
#include <math.h>

static int16_t s_tdm_buffer[ES7210_CAPTURE_FRAMES * ES7210_MIC_CHANNELS];
static bool s_running = false;
static uint8_t s_mic_mask = HW_ES7210_DEFAULT_MIC_MASK;
static uint32_t s_read_count = 0;

// In ES7210 TDM I2S mode the physical channel order is MIC1, MIC3, MIC2, MIC4.
static const uint8_t s_mic_slot[ES7210_MIC_CHANNELS] = {0, 2, 1, 3};

static void extract_waveform(uint8_t slot, int16_t *samples, uint16_t *level)
{
    int64_t sum = 0;
    for (int frame = 0; frame < ES7210_CAPTURE_FRAMES; frame++) {
        sum += s_tdm_buffer[frame * ES7210_MIC_CHANNELS + slot];
    }
    int32_t mean = (int32_t)(sum / ES7210_CAPTURE_FRAMES);

    int64_t sum_sq = 0;
    for (int frame = 0; frame < ES7210_CAPTURE_FRAMES; frame++) {
        int32_t value = s_tdm_buffer[frame * ES7210_MIC_CHANNELS + slot] - mean;
        sum_sq += (int64_t)value * value;
    }
    *level = (uint16_t)constrain((int32_t)sqrt((double)sum_sq / ES7210_CAPTURE_FRAMES), 0, 32767);

    const int samples_per_pair = ES7210_CAPTURE_FRAMES / (ES7210_WAVEFORM_SAMPLES / 2);
    for (int pair = 0; pair < ES7210_WAVEFORM_SAMPLES / 2; pair++) {
        int32_t min_value = 32767;
        int32_t max_value = -32768;
        int min_frame = 0;
        int max_frame = 0;
        for (int offset = 0; offset < samples_per_pair; offset++) {
            int frame = pair * samples_per_pair + offset;
            int32_t value = s_tdm_buffer[frame * ES7210_MIC_CHANNELS + slot] - mean;
            value = constrain(value, -32768, 32767);
            if (value < min_value) {
                min_value = value;
                min_frame = offset;
            }
            if (value > max_value) {
                max_value = value;
                max_frame = offset;
            }
        }
        int point = pair * 2;
        samples[point] = (int16_t)(min_frame < max_frame ? min_value : max_value);
        samples[point + 1] = (int16_t)(min_frame < max_frame ? max_value : min_value);
    }
}

static int32_t channel_rms(uint8_t slot)
{
    int64_t sum_sq = 0;
    for (int frame = 0; frame < ES7210_CAPTURE_FRAMES; frame++) {
        int32_t value = s_tdm_buffer[frame * ES7210_MIC_CHANNELS + slot];
        sum_sq += (int64_t)value * value;
    }
    return (int32_t)sqrt((double)sum_sq / ES7210_CAPTURE_FRAMES);
}
#endif

bool hw_has_es7210_mic_array()
{
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    return true;
#else
    return false;
#endif
}

bool hw_set_es7210_mic_mask(uint8_t mic_mask)
{
    mic_mask &= HW_ES7210_MIC1 | HW_ES7210_MIC2 | HW_ES7210_MIC3 | HW_ES7210_MIC4;
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    if (mic_mask == 0 || !instance.configureEs7210Microphones(mic_mask, true)) {
        return false;
    }
    s_mic_mask = mic_mask;
    return true;
#else
    (void)mic_mask;
    return false;
#endif
}

uint8_t hw_get_es7210_mic_mask()
{
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    return s_mic_mask;
#else
    return 0;
#endif
}

bool hw_set_es7210_mic_start(uint8_t mic_mask)
{
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    AudioInputIf *mic = instance.getAudioInput();
    if (!mic || !hw_set_es7210_mic_mask(mic_mask) ||
            !mic->open(16, ES7210_MIC_CHANNELS, ES7210_SAMPLE_RATE)) {
        instance.configureEs7210Microphones(HW_ES7210_DEFAULT_MIC_MASK, false);
        return false;
    }

    s_running = true;
    LILYGO_LOG_PRINTF("start ES7210 four-channel mic function mask=0x%02X\n", s_mic_mask);
    return true;
#else
    (void)mic_mask;
    return false;
#endif
}

void hw_set_es7210_mic_stop()
{
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    if (!s_running) {
        return;
    }
    AudioInputIf *mic = instance.getAudioInput();
    if (mic) {
        mic->close();
    }
    instance.configureEs7210Microphones(HW_ES7210_DEFAULT_MIC_MASK, false);
    s_mic_mask = HW_ES7210_DEFAULT_MIC_MASK;
    s_running = false;
#endif
}

bool hw_audio_get_es7210_waveform_data(ES7210WaveformData *waveform_data)
{
    if (!waveform_data) {
        return false;
    }
    memset(waveform_data, 0, sizeof(*waveform_data));
#if defined(ARDUINO_T_DECK) && !defined(EXCLUDE_MICROPHONE)
    AudioInputIf *mic = instance.getAudioInput();
    if (!mic || !s_running ||
            mic->read((uint8_t *)s_tdm_buffer, sizeof(s_tdm_buffer)) != (int)sizeof(s_tdm_buffer)) {
        return false;
    }

    s_read_count++;
    for (uint8_t mic_index = 0; mic_index < ES7210_MIC_CHANNELS; mic_index++) {
        uint8_t slot = s_mic_slot[mic_index];
        extract_waveform(slot, waveform_data->mic_samples[mic_index],
                         &waveform_data->mic_level[mic_index]);
    }

    if (LILYGO_DEBUG_ENABLED && s_read_count % 20 == 0) {
        LILYGO_LOG_PRINTF("[MIC] ES7210 mask=0x%02X", s_mic_mask);
        for (uint8_t mic_index = 0; mic_index < ES7210_MIC_CHANNELS; mic_index++) {
            LILYGO_LOG_PRINTF(" MIC%u rms=%ld", mic_index + 1,
                              (long)channel_rms(s_mic_slot[mic_index]));
        }
        LILYGO_LOG_PRINTLN();
    }
    return true;
#else
    return false;
#endif
}
