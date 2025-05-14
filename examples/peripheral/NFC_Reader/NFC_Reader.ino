/**
 * @file      NFC_Reader.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-11
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include "app_nfc.h"

lv_obj_t *label1;


#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA)

void notify_callback()
{
    Serial.println("NDEF Detected.");
}

void ndef_event_callback(ndefTypeId id, void*data)
{
    static ndefTypeRtdDeviceInfo   devInfoData;
    static ndefConstBuffer         bufAarString;
    static ndefRtdUri              url;
    static ndefRtdText             text;
    switch (id) {
    case NDEF_TYPE_EMPTY:
        break;
    case NDEF_TYPE_RTD_DEVICE_INFO:
        memcpy(&devInfoData, data, sizeof(ndefTypeRtdDeviceInfo));
        break;
    case NDEF_TYPE_RTD_TEXT:
        memcpy(&text, data, sizeof(ndefRtdText));
        Serial.printf("LanguageCode:%s Sentence:%s\n", reinterpret_cast<const char *>(text.bufLanguageCode.buffer), reinterpret_cast<const char *>(text.bufSentence.buffer));
        break;
    case NDEF_TYPE_RTD_URI:
        memcpy(&url, data, sizeof(ndefRtdUri));
        Serial.printf("PROTOCOL:%s URL:%s\n", reinterpret_cast<const char *>(url.bufProtocol.buffer), reinterpret_cast<const char *>(url.bufUriString.buffer));
        break;
    case NDEF_TYPE_RTD_AAR:
        memcpy(&bufAarString, data, sizeof(ndefConstBuffer));
        Serial.printf("NDEF_TYPE_RTD_AAR :%s\n", (char*)bufAarString.buffer);
        break;
    case NDEF_TYPE_MEDIA:
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        break;
    case NDEF_TYPE_MEDIA_WIFI: {
        ndefTypeWifi * wifi = (ndefTypeWifi*)data;
        std::string ssid = std::string(reinterpret_cast<const char *>(wifi->bufNetworkSSID.buffer), wifi->bufNetworkSSID.length);
        std::string password = std::string(reinterpret_cast<const char *>(wifi->bufNetworkKey.buffer), wifi->bufNetworkKey.length);
        Serial.printf("ssid:<%s> password:<%s>\n", ssid.c_str(), password.c_str());
    }
    break;
    default:
        break;
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label;
    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "NFC Reader Example");

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    beginNFC(notify_callback, ndef_event_callback);

}

void loop()
{
    loopNFCReader();
    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support T-Watch-S3 or T-LoRa-Pager"); delay(1000);
}

#endif


