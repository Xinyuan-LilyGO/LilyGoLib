/**
 * @file      PDM.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-11
 *
 */

#include "PDM.h"

#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)

#include "_wav_header.h"

const int WAVE_HEADER_SIZE = PCM_WAV_HEADER_SIZE;

PDM::PDM() : _i2s_port(
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        I2S_NUM_AUTO
#else
        I2S_NUM_MAX
#endif
    )
{
}

PDM::~PDM()
{
    i2s_driver_uninstall(_i2s_port);
}

bool PDM::init(int sck_pin, int data_pin, i2s_port_t i2s_port)
{
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
    if (_i2s_port != I2S_NUM_MAX) {
        log_e("Has been install i2s port");
        return true;
    }
#endif

    static i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate =  MIC_I2S_SAMPLE_RATE,
        .bits_per_sample = MIC_I2S_BITS_PER_SAMPLE,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 512,
        .use_apll = true
    };

    static i2s_pin_config_t i2s_cfg = {0};
    i2s_cfg.bck_io_num   = I2S_PIN_NO_CHANGE;
    i2s_cfg.ws_io_num    = sck_pin;
    i2s_cfg.data_out_num = I2S_PIN_NO_CHANGE;
    i2s_cfg.data_in_num  = data_pin;
    i2s_cfg.mck_io_num = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(i2s_port, &i2s_config, 0, NULL) != ESP_OK) {
        log_e("i2s_driver_install error");
        return false;
    }

    if (i2s_set_pin(i2s_port, &i2s_cfg) != ESP_OK) {
        log_e("i2s_set_pin error");
        return false;
    }

    _i2s_port = i2s_port;

    return true;
}

void PDM::end()
{
    i2s_driver_uninstall(_i2s_port);
}

bool PDM::read(void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait)
{
    return i2s_read(_i2s_port, dest, size, bytes_read, ticks_to_wait) == ESP_OK;
}

size_t PDM::readBytes(void *dest, size_t size)
{
    size_t bytes_read = 0;
    i2s_read(_i2s_port, dest, size, &bytes_read, portMAX_DELAY);
    return bytes_read;
}

uint8_t *PDM::recordWAV(size_t rec_seconds, size_t *out_size)
{
    uint32_t sample_rate = MIC_I2S_SAMPLE_RATE;
    uint16_t sample_width = 16;
    uint16_t num_channels = 1;
    size_t rec_size = rec_seconds * ((sample_rate * (sample_width / 8)) * num_channels);
    const pcm_wav_header_t wav_header = PCM_WAV_HEADER_DEFAULT(rec_size, sample_width, sample_rate, num_channels);
    *out_size = 0;

    log_d("Record WAV: rate:%lu, bits:%u, channels:%u, size:%lu", sample_rate, sample_width, num_channels, rec_size);

    uint8_t *wav_buf = (uint8_t *)ps_malloc(rec_size + WAVE_HEADER_SIZE);
    if (wav_buf == NULL) {
        log_e("Failed to allocate WAV buffer with size %u", rec_size + WAVE_HEADER_SIZE);
        return NULL;
    }
    memcpy(wav_buf, &wav_header, WAVE_HEADER_SIZE);
    size_t wav_size;
    read((char *)(wav_buf + WAVE_HEADER_SIZE), rec_size, &wav_size);
    if (wav_size < rec_size) {
        log_e("Recorded %u bytes from %u", wav_size, rec_size);
    } else {
        *out_size = rec_size + WAVE_HEADER_SIZE;
        return wav_buf;
    }
    free(wav_buf);
    return NULL;
}


Player::Player()
{

}

Player::~Player()
{
    i2s_driver_uninstall(_i2s_port);
}

bool Player::init(int bclk, int ws, int data, i2s_port_t i2s_port)
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
        .bck_io_num = bclk,
        .ws_io_num = ws,
        .data_out_num = data,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err =  i2s_driver_install(i2s_port, &i2s_config, 0, NULL);
    i2s_set_pin(i2s_port, &pin_config);
    _i2s_port = i2s_port;
    return err == ESP_OK;
}

size_t Player::write(void *dest, size_t size, TickType_t ticks_to_wait)
{
    size_t bytes_write = 0;
    i2s_write(_i2s_port, dest, size, &bytes_write, portMAX_DELAY);
    return bytes_write;
}

void Player::end()
{
    i2s_driver_uninstall(_i2s_port);
}

bool Player::setSampleRate(uint32_t rate)
{
    return i2s_set_sample_rates(_i2s_port, rate) == ESP_OK;
}

bool Player::configureTX(uint32_t rate, uint32_t bits_cfg, i2s_channel_t ch)
{
    return i2s_set_clk(_i2s_port, rate, bits_cfg, ch) == ESP_OK;
}

void Player::playWAV(uint8_t *data, size_t len)
{
    pcm_wav_header_t *header = (pcm_wav_header_t *)data;
    if (header->fmt_chunk.audio_format != 1) {
        log_e("Audio format is not PCM!");
        return;
    }
    wav_data_chunk_t *data_chunk = &header->data_chunk;
    size_t data_offset = 0;
    while (memcmp(data_chunk->subchunk_id, "data", 4) != 0) {
        log_d(
            "Skip chunk: %c%c%c%c, len: %lu", data_chunk->subchunk_id[0], data_chunk->subchunk_id[1], data_chunk->subchunk_id[2], data_chunk->subchunk_id[3],
            data_chunk->subchunk_size + 8
        );
        data_offset += data_chunk->subchunk_size + 8;
        data_chunk = (wav_data_chunk_t *)(data + WAVE_HEADER_SIZE + data_offset - 8);
    }
    log_d(
        "Play WAV: rate:%lu, bits:%d, channels:%d, size:%lu", header->fmt_chunk.sample_rate, header->fmt_chunk.bits_per_sample, header->fmt_chunk.num_of_channels,
        data_chunk->subchunk_size
    );
    i2s_set_clk(_i2s_port, header->fmt_chunk.sample_rate, header->fmt_chunk.bits_per_sample, (i2s_channel_t )header->fmt_chunk.num_of_channels);
    write(data + WAVE_HEADER_SIZE + data_offset, data_chunk->subchunk_size);
}

#endif
