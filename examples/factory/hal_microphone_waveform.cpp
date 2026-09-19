/**
 * @file      hal_microphone_waveform.cpp
 * @brief     Common waveform capture for mono and stereo PCM microphones.
 */
#include "hal_microphone_waveform.h"
#include "hal_interface.h"

#include <string.h>

#if defined(ARDUINO) && !defined(EXCLUDE_MICROPHONE) && !defined(ARDUINO_T_DECK)
#include <LilyGoLib.h>
#include <LilyGoLog.h>
#include <math.h>

static int16_t s_pcm_buffer[MIC_WAVEFORM_CAPTURE_FRAMES * MIC_WAVEFORM_MAX_CHANNELS];
static uint8_t s_channels = 1;
static bool s_running = false;
static uint32_t s_read_count = 0;

static void extract_waveform(uint8_t slot, int16_t *samples, uint16_t *level)
{
    int64_t sum = 0;
    for (int frame = 0; frame < MIC_WAVEFORM_CAPTURE_FRAMES; frame++) {
        sum += s_pcm_buffer[frame * s_channels + slot];
    }
    int32_t mean = (int32_t)(sum / MIC_WAVEFORM_CAPTURE_FRAMES);

    int64_t sum_sq = 0;
    for (int frame = 0; frame < MIC_WAVEFORM_CAPTURE_FRAMES; frame++) {
        int32_t value = s_pcm_buffer[frame * s_channels + slot] - mean;
        sum_sq += (int64_t)value * value;
    }
    *level = (uint16_t)constrain(
                 (int32_t)sqrt((double)sum_sq / MIC_WAVEFORM_CAPTURE_FRAMES), 0, 32767);

    const int samples_per_pair =
        MIC_WAVEFORM_CAPTURE_FRAMES / (MIC_WAVEFORM_SAMPLES / 2);
    for (int pair = 0; pair < MIC_WAVEFORM_SAMPLES / 2; pair++) {
        int32_t min_value = 32767;
        int32_t max_value = -32768;
        int min_frame = 0;
        int max_frame = 0;
        for (int offset = 0; offset < samples_per_pair; offset++) {
            int frame = pair * samples_per_pair + offset;
            int32_t value = s_pcm_buffer[frame * s_channels + slot] - mean;
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
#endif

bool hw_set_mic_waveform_start()
{
#if defined(ARDUINO) && !defined(EXCLUDE_MICROPHONE) && !defined(ARDUINO_T_DECK)
    AudioInputIf *mic = instance.getAudioInput();
    if (!mic) {
        LILYGO_LOG_E("Audio input is unavailable");
        return false;
    }

    s_channels = constrain(hw_get_codec_input_channels(), 1, MIC_WAVEFORM_MAX_CHANNELS);
    if (!mic->open(16, s_channels, MIC_WAVEFORM_SAMPLE_RATE)) {
        LILYGO_LOG_E("Audio input open failed");
        return false;
    }
    hw_apply_mic_input_source();
    s_running = true;
    LILYGO_LOG_PRINTF("start %u-channel microphone waveform\n", s_channels);
    return true;
#else
    return false;
#endif
}

void hw_set_mic_waveform_stop()
{
#if defined(ARDUINO) && !defined(EXCLUDE_MICROPHONE) && !defined(ARDUINO_T_DECK)
    if (!s_running) return;
    AudioInputIf *mic = instance.getAudioInput();
    if (mic) mic->close();
    s_running = false;
#endif
}

bool hw_audio_get_waveform_data(MicrophoneWaveformData *waveform_data)
{
    if (!waveform_data) return false;
    memset(waveform_data, 0, sizeof(*waveform_data));

#if defined(ARDUINO) && !defined(EXCLUDE_MICROPHONE) && !defined(ARDUINO_T_DECK)
    AudioInputIf *mic = instance.getAudioInput();
    size_t read_size = MIC_WAVEFORM_CAPTURE_FRAMES * s_channels * sizeof(int16_t);
    if (!mic || !s_running ||
            mic->read((uint8_t *)s_pcm_buffer, read_size) != (int)read_size) {
        return false;
    }

    waveform_data->channels = s_channels;
    bool duplicate_mono_jack =
        s_channels == 2 && hw_get_mic_input_source() == MIC_INPUT_SOURCE_JACK;
    for (uint8_t channel = 0; channel < s_channels; channel++) {
        uint8_t slot = duplicate_mono_jack && channel == 1 ? 0 : channel;
        extract_waveform(slot, waveform_data->samples[channel],
                         &waveform_data->level[channel]);
    }

    if (LILYGO_DEBUG_ENABLED && ++s_read_count % 20 == 0) {
        LILYGO_LOG_PRINTF("[MIC] channels=%u", s_channels);
        for (uint8_t channel = 0; channel < s_channels; channel++) {
            LILYGO_LOG_PRINTF(" CH%u rms=%u", channel + 1,
                              waveform_data->level[channel]);
        }
        LILYGO_LOG_PRINTLN();
    }
    return true;
#else
    return false;
#endif
}

