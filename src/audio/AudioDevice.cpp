/**
 * @file      AudioDevice.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-11
 *
 */

#include "LilyGoLog.h"
#include "AudioDevice.h"

#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)

#include "_wav_header.h"

const int WAVE_HEADER_SIZE = PCM_WAV_HEADER_SIZE;

AudioInputDev::~AudioInputDev()
{
    i2s_driver_uninstall(_i2s_port);
}

bool AudioInputDev::begin()
{
    static i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate =  MIC_I2S_SAMPLE_RATE,
        .bits_per_sample = MIC_I2S_BITS_PER_SAMPLE,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
        .use_apll = false
    };

    static i2s_pin_config_t i2s_cfg = {0};
    i2s_cfg.bck_io_num   = I2S_PIN_NO_CHANGE;
    i2s_cfg.ws_io_num    = _sck_pin;
    i2s_cfg.data_out_num = I2S_PIN_NO_CHANGE;
    i2s_cfg.data_in_num  = _data_pin;
    i2s_cfg.mck_io_num = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(_i2s_port, &i2s_config, 0, NULL) != ESP_OK) {
        LILYGO_LOG_E("i2s_driver_install error");
        return false;
    }

    if (i2s_set_pin(_i2s_port, &i2s_cfg) != ESP_OK) {
        LILYGO_LOG_E("i2s_set_pin error");
        return false;
    }

    return true;
}

void AudioInputDev::end()
{
    i2s_driver_uninstall(_i2s_port);
}

bool AudioInputDev::read(void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait)
{
    return i2s_read(_i2s_port, dest, size, bytes_read, ticks_to_wait) == ESP_OK;
}

size_t AudioInputDev::readBytes(void *dest, size_t size)
{
    size_t bytes_read = 0;
    esp_err_t  err = i2s_read(_i2s_port, dest, size, &bytes_read, portMAX_DELAY);
    if (err != ESP_OK)return 0;
    return bytes_read;
}

AudioOutputDev::~AudioOutputDev()
{
    i2s_driver_uninstall(_i2s_port);
}

bool AudioOutputDev::begin()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = _bclk_pin,
        .ws_io_num = _ws_pin,
        .data_out_num = _data_pin,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err =  i2s_driver_install(_i2s_port, &i2s_config, 0, NULL);
    i2s_set_pin(_i2s_port, &pin_config);
    return err == ESP_OK;
}

size_t AudioOutputDev::write(void *dest, size_t size, TickType_t ticks_to_wait)
{
    size_t bytes_write = 0;
    i2s_write(_i2s_port, dest, size, &bytes_write, portMAX_DELAY);
    return bytes_write;
}

void AudioOutputDev::end()
{
    i2s_driver_uninstall(_i2s_port);
}

bool AudioOutputDev::setSampleRate(uint32_t rate)
{
    return i2s_set_sample_rates(_i2s_port, rate) == ESP_OK;
}

bool AudioOutputDev::configureTX(uint32_t rate, uint32_t bits_cfg, i2s_channel_t ch)
{
    return i2s_set_clk(_i2s_port, rate, bits_cfg, ch) == ESP_OK;
}

#endif
