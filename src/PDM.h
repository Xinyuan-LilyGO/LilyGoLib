/**
 * @file      PDM.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-11
 *
 */


#pragma once

#include <Arduino.h>

#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)

#include <driver/i2s.h>

// !The PDM microphone can only be up to 16KHZ and cannot be changed
#define MIC_I2S_SAMPLE_RATE         16000
// !The PDM microphone can only use I2S channel 0 and cannot be changed
#define MIC_I2S_PORT                I2S_NUM_0
#define MIC_I2S_BITS_PER_SAMPLE     I2S_BITS_PER_SAMPLE_16BIT

#define PLAYER_IS2_PORT             I2S_NUM_1

class PDM
{
private:
    i2s_port_t _i2s_port;
public:
    PDM();
    ~PDM();
    bool init(int sck_pin, int data_pin, i2s_port_t i2s_port = MIC_I2S_PORT);
    void end();
    bool read(void *dest, size_t size, size_t *bytes_read, TickType_t ticks_to_wait = portMAX_DELAY);
    size_t readBytes(void *dest, size_t size);
    uint8_t *recordWAV(size_t rec_seconds, size_t *out_size);
};

class Player
{
private:
    i2s_port_t _i2s_port;
public:
    Player();
    ~Player();
    bool init(int bclk, int ws, int data, i2s_port_t i2s_port = PLAYER_IS2_PORT);
    void end();
    bool setSampleRate(uint32_t rate);
    bool configureTX(uint32_t rate, uint32_t bits_cfg, i2s_channel_t ch);
    size_t write(void *dest, size_t size, TickType_t ticks_to_wait = portMAX_DELAY);
    void playWAV(uint8_t *data, size_t len);
};
#endif