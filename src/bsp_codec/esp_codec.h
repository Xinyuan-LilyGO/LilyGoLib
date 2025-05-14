/**
 * @file      esp_codec.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-02
 *
 */
#pragma once

#ifdef ARDUINO
#include "esp_codec_config.h"
#include "include/esp_codec_dev.h"
#include "include/esp_codec_dev_defaults.h"
#ifdef CONFIG_CODEC_ES8311_SUPPORT
#include "device/include/es8311_codec.h"
#endif
#ifdef CONFIG_CODEC_ES7210_SUPPORT
#include "device/include/es7210_adc.h"
#endif
#ifdef CONFIG_CODEC_ES7243_SUPPORT
#include "device/include/es7243_adc.h"
#endif
#ifdef CONFIG_CODEC_ES7243E_SUPPORT
#include "device/include/es7243e_adc.h"
#endif
#ifdef CONFIG_CODEC_ES8156_SUPPORT
#include "device/include/es8156_dac.h"
#endif
#ifdef CONFIG_CODEC_AW88298_SUPPORT
#include "device/include/aw88298_dac.h"
#endif
#ifdef CONFIG_CODEC_ES8374_SUPPORT
#include "device/include/es8374_codec.h"
#endif
#ifdef CONFIG_CODEC_TAS5805M_SUPPORT
#include "device/include/tas5805m_dac.h"
#endif
#ifdef CONFIG_CODEC_ZL38063_SUPPORT
#include "device/include/zl38063_codec.h"
#endif
#include <Wire.h>


typedef enum {
    CODEC_TYPE_ES8311,
    CODEC_TYPE_ES7210,
    CODEC_TYPE_ES7243,
    CODEC_TYPE_ES7243E,
    CODEC_TYPE_ES8156,
    CODEC_TYPE_AW88298,
    CODEC_TYPE_ES8374,
    CODEC_TYPE_ES8388,
    CODEC_TYPE_TAS5805M,
    CODEC_TYPE_ZL38063,
} EspCodecType;


using EspCodecPaPinCallback_t = void(*)(bool enable, void *user_data);

class EspCodec
{
public:
    void setPaParams(int pa_pin, float pa_voltage);
    void setPaPinCallback(EspCodecPaPinCallback_t cb, void *user_data);
    void setPins(int mclk, int sck, int ws, int data_out, int data_in);
    bool begin(TwoWire&wire, uint8_t address);
    void end();

    int open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate);
    void close();
    int write(uint8_t * buffer, size_t size);
    int read(uint8_t * buffer, size_t size);


    void setVolume(uint8_t level);
    int  getVolume();

    void setMute(bool enable);
    bool getMute();

    void setGain(float db_value);
    float getGain();

    void playWAV(uint8_t *data, size_t len);
    uint8_t *recordWAV(size_t rec_seconds, size_t *out_size, uint32_t sample_rate = 16000, uint16_t sample_width = 16);

    EspCodec(uint8_t i2s_channel = 0);

    ~EspCodec();

private:

    esp_err_t _i2s_init();

    int _mck_io_num;     /*!< MCK in out pin. Note that ESP32 supports setting MCK on GPIO0/GPIO1/GPIO3 only*/
    int _bck_io_num;     /*!< BCK in out pin*/
    int _ws_io_num;      /*!< WS in out pin*/
    int _data_out_num;   /*!< DATA out pin*/
    int _data_in_num;    /*!< DATA in pin*/
    int _pa_num;
    float _pa_voltage;
    uint8_t _i2s_num;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_ctrl_if_t *i2c_ctrl_if;
    const audio_codec_if_t      *codec_if;
    const audio_codec_data_if_t *i2s_data_if;
    esp_codec_dev_handle_t      codec_dev;
    TwoWire *wire;
    EspCodecPaPinCallback_t     paPinCb;
    void                       *paPinUserData;
};

#endif

