/*
 * ES8389 codec configuration for the legacy LilyGo esp_codec_dev interface.
 * The register programming sequence is based on Espressif's esp-audio-dev
 * ES8389 driver.
 */
#ifndef _ES8389_CODEC_H_
#define _ES8389_CODEC_H_

#include "../../interface/audio_codec_if.h"
#include "../../interface/audio_codec_ctrl_if.h"
#include "../../interface/audio_codec_gpio_if.h"
#include "../../include/esp_codec_dev_vol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8389_CODEC_DEFAULT_ADDR (0x20)

typedef struct {
    bool is_master;
    bool no_mclk;
} es8389_sys_cfg_t;

typedef struct {
    bool digital_mic;
    const char *label;
} es8389_adc_cfg_t;

typedef struct {
    bool ref_enable;
    int8_t ref_dac_ch;
    int8_t real_adc_data_ch;
    int headphone;
} es8389_dac_cfg_t;

typedef struct {
    int16_t pa_pin;
    bool pa_active_low;
    esp_codec_dev_hw_gain_t hw_gain;
} es8389_pa_cfg_t;

typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    esp_codec_dec_work_mode_t codec_mode;
    es8389_sys_cfg_t sys_cfg;
    es8389_adc_cfg_t adc_cfg;
    es8389_dac_cfg_t dac_cfg;
    es8389_pa_cfg_t pa_cfg;
} es8389_codec_cfg_t;

const audio_codec_if_t *es8389_codec_new(es8389_codec_cfg_t *codec_cfg);

#ifdef __cplusplus
}
#endif

#endif
