/**
 * @file      app_nfc.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-11
 *
 */

 #pragma once

 #if defined(ARDUINO) 

 #include <Arduino.h>

 #if defined(USING_ST25R3916)

 #include "nfc_include.h"
 typedef struct _ndefRtdText {
     uint8_t utfEncoding;
     ndefConstBuffer8 bufLanguageCode;
     ndefConstBuffer  bufSentence;
 } ndefRtdText;
 
 typedef struct _RtdUri {
     ndefConstBuffer bufProtocol;
     ndefConstBuffer bufUriString;
 } ndefRtdUri;
 
 typedef void (*notify_callback_t)();
 typedef void (*ndef_event_callback_t)(ndefTypeId id, void*data);
 
 bool beginNFC(notify_callback_t notify_cb, ndef_event_callback_t event_cb);
 void loopNFCReader();
 
 extern RfalNfcClass NFCReader;
 
 #endif

 
 #endif /*ARDUINO*/
 