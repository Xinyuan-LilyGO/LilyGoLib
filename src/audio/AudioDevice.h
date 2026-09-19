/**
 * @file      AudioDevice.h
 * @brief     Provides I2S and codec-backed audio input/output implementations.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-11
 *
 */
#pragma once

#include <Arduino.h>
#include "../bsp_codec/esp_codec.h"
#include "AudioInputIf.h"
#include "AudioOutputIf.h"



#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)

#include <driver/i2s.h>

/** Fixed sample rate for the legacy PDM microphone path. */
#ifndef MIC_I2S_SAMPLE_RATE
#define MIC_I2S_SAMPLE_RATE         16000
#endif

/** Fixed I2S port for the legacy PDM microphone path. */
#ifndef MIC_I2S_PORT
#define MIC_I2S_PORT                I2S_NUM_0
#endif

/** Fixed sample width for the legacy PDM microphone path. */
#ifndef MIC_I2S_BITS_PER_SAMPLE
#define MIC_I2S_BITS_PER_SAMPLE     I2S_BITS_PER_SAMPLE_16BIT
#endif

/** Default I2S port used by the legacy playback path. */
#ifndef PLAYER_IS2_PORT
#define PLAYER_IS2_PORT             I2S_NUM_1
#endif

/**
 * @brief PDM microphone input device backed by the legacy ESP-IDF I2S driver.
 */
class AudioInputDev : public AudioInputIf
{
private:
    int _sck_pin;
    int _data_pin;
    i2s_port_t _i2s_port;
    float _mic_gain_db = 20.0f;

    /* Apply software gain with soft-clipping limiter to prevent distortion */
    void _apply_gain(uint8_t *buffer, size_t size)
    {
        if (_mic_gain_db <= 0.0f) return;
        float factor = powf(10.0f, _mic_gain_db / 20.0f);
        int16_t *samples = (int16_t *)buffer;
        size_t count = size / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            float val = samples[i] * factor;
            /* Soft clipping: compress peaks instead of hard clipping */
            if (val > 24576.0f) {       /* ~75% of max */
                val = 24576.0f + (val - 24576.0f) * 0.3f;
            } else if (val < -24576.0f) {
                val = -24576.0f + (val + 24576.0f) * 0.3f;
            }
            /* Hard limit at absolute max */
            if (val > 32767.0f) val = 32767.0f;
            if (val < -32768.0f) val = -32768.0f;
            samples[i] = (int16_t)val;
        }
    }

public:
    /**
     * @brief Construct a legacy I2S PDM input device.
     * @param sck_pin PDM clock pin.
     * @param data_pin PDM data input pin.
     * @param i2s_port I2S peripheral used for capture.
     */
    AudioInputDev(int sck_pin, int data_pin, i2s_port_t i2s_port = MIC_I2S_PORT) :
        _sck_pin(sck_pin), _data_pin(data_pin), _i2s_port(i2s_port) {}

    /**
     * @brief Uninstall the I2S input driver.
     */
    ~AudioInputDev();

    /**
     * @brief Install and configure the I2S input driver.
     * @return true if the driver was installed and pins were configured.
     */
    bool begin();

    /**
     * @brief Stop the I2S input driver.
     */
    void end();

    /**
     * @brief Read PCM data through the native I2S driver API.
     * @param dest Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @param bytes_read Receives the number of bytes actually read.
     * @param ticks_to_wait Maximum time to wait for data.
     * @return true if the I2S read operation succeeded.
     */
    bool read(void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait = portMAX_DELAY);

    /**
     * @brief Read PCM data and return only the byte count.
     * @param dest Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @return Number of bytes read, or 0 on failure.
     */
    size_t readBytes(void *dest, size_t size);

    /**
     * @brief Read PCM bytes and apply software microphone gain.
     * @param buffer Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @return Number of bytes read.
     */
    int read(uint8_t *buffer, size_t size) override
    {
        size_t bytes_read = 0;
        read(buffer, size, &bytes_read, pdTICKS_TO_MS(100));
        if (bytes_read > 0) _apply_gain(buffer, bytes_read);
        return bytes_read;
    }

    void setMicGain(float gain) override { _mic_gain_db = gain; }
    float getMicGain() const override { return _mic_gain_db; }
};

/**
 * @brief PCM playback device backed by the legacy ESP-IDF I2S driver.
 */
class AudioOutputDev : public AudioOutputIf
{
private:
    int _bclk_pin;
    int _ws_pin;
    int _data_pin;
    i2s_port_t _i2s_port;
    uint8_t _volume = 100;

    /* Apply software volume to 16-bit PCM buffer */
    void _apply_volume(uint8_t *buffer, size_t size)
    {
        if (_volume >= 100) return;
        float gain = (float)_volume / 100.0f;
        int16_t *samples = (int16_t *)buffer;
        size_t count = size / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            samples[i] = (int16_t)(samples[i] * gain);
        }
    }

public:
    /**
     * @brief Construct a legacy I2S audio output device.
     * @param bclk Bit clock pin.
     * @param ws Word select pin.
     * @param data Serial data output pin.
     * @param i2s_port I2S peripheral used for playback.
     */
    AudioOutputDev(int bclk, int ws, int data, i2s_port_t i2s_port = PLAYER_IS2_PORT) :
        _bclk_pin(bclk), _ws_pin(ws), _data_pin(data), _i2s_port(i2s_port) {}

    /**
     * @brief Uninstall the I2S output driver.
     */
    ~AudioOutputDev();

    /**
     * @brief Install and configure the I2S output driver.
     * @return true if the output driver was installed.
     */
    bool begin();

    /**
     * @brief Stop the I2S output driver.
     */
    void end();

    /**
     * @brief Update the output sample rate.
     * @param rate Sample rate in Hz.
     * @return true if the I2S clock was updated.
     */
    bool setSampleRate(uint32_t rate);

    /**
     * @brief Configure the output stream format.
     * @param rate Sample rate in Hz.
     * @param bits_cfg Bits per sample.
     * @param ch I2S channel format.
     * @return true if the output clock configuration succeeded.
     */
    bool configureTX(uint32_t rate, uint32_t bits_cfg, i2s_channel_t ch);

    /**
     * @brief Write PCM bytes through the native I2S driver API.
     * @param dest Source buffer passed to the I2S driver.
     * @param size Number of bytes to write.
     * @param ticks_to_wait Maximum time to wait for DMA space.
     * @return Number of bytes written.
     */
    size_t write(void *dest, size_t size, TickType_t ticks_to_wait = portMAX_DELAY);

    /**
     * @brief Write PCM bytes and apply software volume when needed.
     * @param buffer Source buffer containing PCM data.
     * @param size Number of bytes to write.
     * @return Number of bytes written.
     */
    int write(const uint8_t *buffer, size_t size) override
    {
        /* Apply software volume before writing to I2S */
        if (_volume < 100 && size > 0) {
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (tmp) {
                memcpy(tmp, buffer, size);
                _apply_volume(tmp, size);
                int ret = this->write((void *)tmp, size, pdTICKS_TO_MS(100));
                free(tmp);
                return ret;
            }
        }
        return this->write((void *)buffer, size, pdTICKS_TO_MS(100));
    }

    void setVolume(uint8_t level) override { _volume = level; }
    int getVolume() override { return _volume; }
    bool open(uint8_t bits, uint8_t channels, uint32_t sample_rate) override
    {
        if (_mute_callback) {
            _mute_callback(true);
        }
        return configureTX(sample_rate, bits, static_cast<i2s_channel_t>(channels));
    }
    void close() override
    {
        if (_mute_callback) {
            _mute_callback(false);
        }
    }
    // void playWAV(uint8_t *data, size_t len);
};

#else

#include <ESP_I2S.h>

/**
 * @brief PDM microphone input device backed by the Arduino ESP_I2S API.
 */
class AudioInputDev : public AudioInputIf
{
private:
    int _sck_pin;
    int _data_pin;
    I2SClass i2s;
    float _mic_gain_db = 0.0f;

    /* Apply software gain with soft-clipping limiter to prevent distortion */
    void _apply_gain(uint8_t *buffer, size_t size)
    {
        if (_mic_gain_db <= 0.0f) return;
        float factor = powf(10.0f, _mic_gain_db / 20.0f);
        int16_t *samples = (int16_t *)buffer;
        size_t count = size / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            float val = samples[i] * factor;
            /* Soft clipping: compress peaks instead of hard clipping */
            if (val > 24576.0f) {       /* ~75% of max */
                val = 24576.0f + (val - 24576.0f) * 0.3f;
            } else if (val < -24576.0f) {
                val = -24576.0f + (val + 24576.0f) * 0.3f;
            }
            /* Hard limit at absolute max */
            if (val > 32767.0f) val = 32767.0f;
            if (val < -32768.0f) val = -32768.0f;
            samples[i] = (int16_t)val;
        }
    }

public:
    /**
     * @brief Construct a PDM input device.
     * @param sck_pin PDM clock pin.
     * @param data_pin PDM data input pin.
     */
    AudioInputDev(int sck_pin, int data_pin) : _sck_pin(sck_pin), _data_pin(data_pin) {}

    /**
     * @brief Configure and start the PDM input stream.
     * @return true if the PDM stream was started.
     */
    bool begin()
    {
        i2s.setPinsPdmRx(_sck_pin, _data_pin);
        return i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
    }

    /**
     * @brief Stop the PDM input stream.
     */
    void end() {}

    /**
     * @brief Read PCM bytes and apply software microphone gain.
     * @param buffer Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @return Number of bytes read.
     */
    int read(uint8_t *buffer, size_t size) override
    {
        size_t bytes_read = i2s.readBytes((char *)buffer, size);
        if (bytes_read > 0) _apply_gain(buffer, bytes_read);
        return bytes_read;
    }

    void setMicGain(float gain) override { _mic_gain_db = gain; }
    float getMicGain() const override { return _mic_gain_db; }
};

/**
 * @brief PCM playback device backed by the Arduino ESP_I2S API.
 */
class AudioOutputDev : public AudioOutputIf
{
private:
    int _bclk_pin;
    int _ws_pin;
    int _data_pin;
    I2SClass i2s;
    uint8_t _volume = 100;

    void _apply_volume(uint8_t *buffer, size_t size)
    {
        if (_volume >= 100) return;
        float gain = (float)_volume / 100.0f;
        int16_t *samples = (int16_t *)buffer;
        size_t count = size / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            samples[i] = (int16_t)(samples[i] * gain);
        }
    }

public:
    /**
     * @brief Construct an I2S audio output device.
     * @param bclk Bit clock pin.
     * @param ws Word select pin.
     * @param data Serial data output pin.
     */
    AudioOutputDev(int bclk, int ws, int data) : _bclk_pin(bclk), _ws_pin(ws), _data_pin(data) {}

    /**
     * @brief Configure and start the default I2S output stream.
     * @return true if the output stream was started.
     */
    bool begin()
    {
        i2s.setPins(_bclk_pin, _ws_pin, _data_pin);
        return i2s.begin(I2S_MODE_STD, 160000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    }

    /**
     * @brief Stop the I2S output stream.
     */
    void end() override
    {
        i2s.end();
    }

    /**
     * @brief Write PCM bytes and apply software volume when needed.
     * @param buffer Source buffer containing PCM data.
     * @param size Number of bytes to write.
     * @return Number of bytes written.
     */
    int write(const uint8_t *buffer, size_t size) override
    {
        if (_volume < 100 && size > 0) {
            uint8_t *tmp = (uint8_t *)malloc(size);
            if (tmp) {
                memcpy(tmp, buffer, size);
                _apply_volume(tmp, size);
                i2s.write(tmp, size);
                free(tmp);
                return size;
            }
        }
        i2s.write(buffer, size);
        return size;
    }

    void setVolume(uint8_t level) override { _volume = level; }
    int getVolume() override { return _volume; }

    /**
     * @brief Configure the active output stream format.
     * @param bits Bits per sample.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @return true if the stream was configured.
     */
    bool open(uint8_t bits, uint8_t channels, uint32_t sample_rate) override
    {
        if (_mute_callback) {
            _mute_callback(true);
        }
        i2s.configureTX(sample_rate, (i2s_data_bit_width_t)bits, (i2s_slot_mode_t)channels);
        return true;
    }

    void close() override
    {
        if (_mute_callback) {
            _mute_callback(false);
        }
    }
};

#endif


/**
 * @brief Audio input adapter for the shared EspCodec input path.
 */
class AudioInputCodecDev : public AudioInputIf
{
private:
    EspCodec *codec;
public:
    /**
     * @brief Construct a codec-backed audio input adapter.
     * @param c Codec instance used for capture.
     */
    AudioInputCodecDev(EspCodec *c) : codec(c) {}

    /**
     * @brief Destroy the codec-backed audio input adapter.
     */
    ~AudioInputCodecDev() = default;

    /**
     * @brief Report the codec input path as initialized.
     * @return Always true because the codec owner performs hardware setup.
     */
    bool begin() override
    {
        return true;
    }

    /**
     * @brief End the codec input path.
     */
    void end() override
    {
    }

    /**
     * @brief Set the codec microphone gain.
     * @param gain Gain value forwarded to the codec.
     */
    void setMicGain(float gain) override
    {
        codec->setGain(gain);
    }

    /**
     * @brief Get the codec microphone gain.
     * @return Current gain value reported by the codec.
     */
    float getMicGain() const override
    {
        return codec->getGain();
    }

    /**
     * @brief Read PCM bytes from the codec.
     * @param buffer Destination buffer for captured PCM data.
     * @param size Number of bytes requested.
     * @return Number of bytes read by the codec.
     */
    int read(uint8_t *buffer, size_t size) override
    {
        return codec->read(buffer, size);
    }

    /**
     * @brief Open the codec input stream.
     * @param bits Bits per sample.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @return true if the codec accepted the stream format.
     */
    bool open(uint8_t bits, uint8_t channels, uint32_t sample_rate) override
    {
        return codec->open(bits, channels, sample_rate) == ESP_CODEC_DEV_OK;
    }

    /**
     * @brief Close the codec input stream.
     */
    void close() override
    {
        codec->close();
    }
};

/**
 * @brief Audio output adapter for the shared EspCodec output path.
 */
class AudioOutputCodecDev : public AudioOutputIf
{
private:
    EspCodec *codec;
public:
    /**
     * @brief Construct a codec-backed audio output adapter.
     * @param c Codec instance used for playback.
     */
    AudioOutputCodecDev(EspCodec *c) : codec(c) {}

    /**
     * @brief Destroy the codec-backed audio output adapter.
     */
    ~AudioOutputCodecDev() = default;

    /**
     * @brief Report the codec output path as initialized.
     * @return Always true because the codec owner performs hardware setup.
     */
    bool begin()override
    {
        return true;
    }

    /**
     * @brief End the codec output path.
     */
    void end()override
    {
    }

    /**
     * @brief Write PCM bytes to the codec.
     * @param buffer Source buffer containing PCM data.
     * @param size Number of bytes to write.
     * @return Number of bytes written by the codec.
     */
    int write(const uint8_t *buffer, size_t size) override
    {
        return codec->write((uint8_t *)buffer, size);
    }

    /**
     * @brief Open the codec output stream.
     * @param bits Bits per sample.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @return true if the codec accepted the stream format.
     */
    bool open(uint8_t bits, uint8_t channels, uint32_t sample_rate) override
    {
        int ret = codec->open(bits, channels, sample_rate);
        if (_mute_callback && ret == ESP_CODEC_DEV_OK) {
            _mute_callback(true);
        }
        return ret == ESP_CODEC_DEV_OK;
    }

    /**
     * @brief Close the codec output stream.
     */
    void close() override
    {
        if (_mute_callback) {
            _mute_callback(false);
        }
        codec->close();
    }

    /**
     * @brief Set the codec playback volume.
     * @param level Volume value forwarded to the codec.
     */
    void setVolume(uint8_t level) override
    {
        codec->setVolume(level);
    }

    /**
     * @brief Get the codec playback volume.
     * @return Current volume reported by the codec.
     */
    int getVolume() override
    {
        return codec->getVolume();
    }
};
