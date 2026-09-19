/**
 * @file      AudioInputIf.h
 * @brief     Defines the common audio input interface and WAV recording helper.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-11
 *
 */
#pragma once
#include "LilyGoLog.h"
#include <Arduino.h>
#include "_wav_header.h"

/**
 * @brief Common interface for microphone and codec input devices.
 *
 * Implementations provide device initialization, PCM capture, optional runtime
 * stream configuration, and optional microphone gain control.
 */
class AudioInputIf
{
public:
    /**
     * @brief Destroy the audio input interface.
     */
    virtual ~AudioInputIf() = default;

    /**
     * @brief Return the concrete audio input instance when one is exposed.
     * @return Pointer to the concrete input object, or nullptr by default.
     */
    virtual AudioInputIf *getAudioInput()
    {
        return nullptr;
    }

    /**
     * @brief Initialize the input hardware.
     * @return true if the input device is ready to use.
     */
    virtual bool begin() = 0;

    /**
     * @brief Release the input hardware resources.
     */
    virtual void end() {}

    /**
     * @brief Read raw PCM bytes from the input stream.
     * @param buffer Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @return Number of bytes read, or a device-specific error value.
     */
    virtual int read(uint8_t *buffer, size_t size) = 0;

    /**
     * @brief Open the input stream with the requested PCM format.
     * @param bits Bits per sample, usually 16.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @return true if the stream was opened successfully.
     */
    virtual bool open(uint8_t bits, uint8_t channels, uint32_t sample_rate)
    {
        return true;
    }

    /**
     * @brief Set the microphone gain level.
     * @param gain Gain value in the implementation-defined unit.
     */
    virtual void setMicGain(float gain) {}

    /**
     * @brief Get the current microphone gain level.
     * @return Current gain value, or 0.0f when unsupported.
     */
    virtual float getMicGain() const { return 0.0f; }

    /**
     * @brief Close the active input stream.
     */
    virtual void close() {}

    /**
     * @brief Record PCM audio into an in-memory WAV file.
     * @param rec_seconds Recording duration in seconds.
     * @param output Receives the allocated WAV buffer on success.
     * @param out_size Receives the total WAV buffer size in bytes.
     * @param sample_rate Requested sample rate in Hz.
     * @param num_channels Requested channel count.
     * @return true if recording completed and output data is valid.
     *
     * @note The returned buffer is allocated with ps_malloc() and must be
     *       released by the caller with free().
     */
    bool recordWAV(size_t rec_seconds, uint8_t**output, size_t *out_size, uint16_t sample_rate = 16000, uint8_t num_channels = 1)
    {
        uint16_t sample_width = 16;
        size_t rec_size = rec_seconds * ((sample_rate * (sample_width / 8)) * num_channels);
        const pcm_wav_header_t wav_header = PCM_WAV_HEADER_DEFAULT(rec_size, sample_width, sample_rate, num_channels);
        *out_size = 0;

        LILYGO_LOG_D("Record WAV: rate:%lu, bits:%u, channels:%u, size:%lu", sample_rate, sample_width, num_channels, rec_size);

        uint8_t *wav_buf = (uint8_t *)ps_malloc(rec_size + PCM_WAV_HEADER_SIZE);
        if (wav_buf == NULL) {
            LILYGO_LOG_E("Failed to allocate WAV buffer with size %u", rec_size + PCM_WAV_HEADER_SIZE);
            return false;
        }
        memcpy(wav_buf, &wav_header, PCM_WAV_HEADER_SIZE);

        bool rlst = open(sample_width, num_channels, sample_rate);
        if (!rlst) {
            free(wav_buf);
            LILYGO_LOG_E("Open audio device failed");
            return false;
        }
        rlst = read(wav_buf + PCM_WAV_HEADER_SIZE, rec_size);
        if (rlst == 0 ) {
            LILYGO_LOG_E("Recorded failed");
            free(wav_buf);
            close();
            return false;
        }
        *out_size = rec_size + PCM_WAV_HEADER_SIZE;
        close();
        *output = wav_buf;
        return true;
    }
};
