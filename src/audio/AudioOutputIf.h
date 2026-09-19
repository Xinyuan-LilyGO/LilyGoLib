/**
 * @file      AudioOutputIf.h
 * @brief     Defines the common audio output interface and WAV playback helper.
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
 * @brief Callback used to notify board-specific audio output state changes.
 *
 * Implementations call the callback with true when an output stream is opened
 * and false when the stream is closed.
 */
using mute_callback_t = void (*)(bool);

/**
 * @brief Common interface for speaker, amplifier, and codec output devices.
 *
 * Implementations provide device initialization, PCM playback, optional runtime
 * stream configuration, and optional volume control.
 */
class AudioOutputIf
{
protected:
    /** Callback used by board support code to control external audio routing. */
    mute_callback_t _mute_callback = nullptr;
public:
    /**
     * @brief Destroy the audio output interface.
     */
    virtual ~AudioOutputIf() = default;

    /**
     * @brief Return the concrete audio output instance when one is exposed.
     * @return Pointer to the concrete output object, or nullptr by default.
     */
    virtual AudioOutputIf *getAudioOutput()
    {
        return nullptr;
    }

    /**
     * @brief Initialize the output hardware.
     * @return true if the output device is ready to use.
     */
    virtual bool begin() = 0;

    /**
     * @brief Release the output hardware resources.
     */
    virtual void end()
    {
    }

    /**
     * @brief Write raw PCM bytes to the output stream.
     * @param buffer Source buffer containing PCM data.
     * @param size Number of bytes to write.
     * @return Number of bytes written, or a device-specific error value.
     */
    virtual int write(const uint8_t *buffer, size_t size) = 0;

    /**
     * @brief Open the output stream with the requested PCM format.
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
     * @brief Close the active output stream.
     */
    virtual void close() {}

    /**
     * @brief Set the playback volume.
     * @param level Volume level in the implementation-defined range.
     */
    virtual void setVolume(uint8_t level) {}

    /**
     * @brief Get the current playback volume.
     * @return Current volume level, or 100 when unsupported.
     */
    virtual int getVolume()
    {
        return 100;
    }

    /**
     * @brief Register the board-level audio output state callback.
     * @param callback Callback to invoke when playback output opens or closes.
     */
    void setMuteCallback(mute_callback_t callback)
    {
        _mute_callback = callback;
    }

    /**
     * @brief Play an in-memory PCM WAV file.
     * @param data Pointer to the WAV file buffer.
     * @param len Total WAV buffer length in bytes.
     * @return true if the WAV buffer was accepted and playback was attempted.
     *
     * @note Only PCM WAV data is accepted by the default implementation.
     */
    virtual bool playWAV(uint8_t *data, size_t len)
    {
        const int WAVE_HEADER_SIZE = PCM_WAV_HEADER_SIZE;
        pcm_wav_header_t *header = (pcm_wav_header_t *)data;
        if (header->fmt_chunk.audio_format != 1) {
            LILYGO_LOG_E("Audio format is not PCM!");
            return false;
        }
        wav_data_chunk_t *data_chunk = &header->data_chunk;
        size_t data_offset = 0;
        while (memcmp(data_chunk->subchunk_id, "data", 4) != 0) {
            LILYGO_LOG_D(
                "Skip chunk: %c%c%c%c, len: %lu", data_chunk->subchunk_id[0], data_chunk->subchunk_id[1], data_chunk->subchunk_id[2], data_chunk->subchunk_id[3],
                data_chunk->subchunk_size + 8
            );
            data_offset += data_chunk->subchunk_size + 8;
            data_chunk = (wav_data_chunk_t *)(data + WAVE_HEADER_SIZE + data_offset - 8);
        }
        LILYGO_LOG_D(
            "Play WAV: rate:%lu, bits:%d, channels:%d, size:%lu", header->fmt_chunk.sample_rate, header->fmt_chunk.bits_per_sample, header->fmt_chunk.num_of_channels,
            data_chunk->subchunk_size
        );

        int ret = open(header->fmt_chunk.bits_per_sample, header->fmt_chunk.num_of_channels, header->fmt_chunk.sample_rate);
        if (ret < 0) {
            LILYGO_LOG_E("Open audio device failed");
            return false;
        }
        write(data + WAVE_HEADER_SIZE + data_offset, data_chunk->subchunk_size);
        close();
        return true;
    }
};
