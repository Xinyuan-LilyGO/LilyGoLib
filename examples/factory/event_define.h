/**
 * @file      event_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-19
 *
 */
#pragma once

enum app_event {
    APP_EVENT_PLAY,
    APP_EVENT_PLAY_KEY,
    APP_EVENT_RECOVER,
    APP_NFC_EVENT,
};


#if defined(ARDUINO) && defined(USING_ST25R3916)
#include <LilyGoLib.h>

typedef struct {
    ndefConstBuffer bufProtocol;
    ndefConstBuffer bufUriString;
} ndefTypeURL;

typedef struct {
    uint8_t utfEncoding;
    ndefConstBuffer bufLanguageCode;
    ndefConstBuffer bufSentence;
} ndefTypeText;

typedef struct {
    ndefTypeId event;
    union __ {
        ndefTypeWifi wifiConfig;
        ndefTypeURL url;
        ndefTypeText text;
        ndefTypeRtdDeviceInfo devInfoData;
    } data;
} nfcData_t;

typedef struct {
    enum app_event event;
    union __ {
        nfcData_t nfc;
    } u;
} app_event_t;

typedef struct {
    enum app_event event;
    const char *filename ;
} app_audio_play_t;

#endif /*ARDUINO*/
