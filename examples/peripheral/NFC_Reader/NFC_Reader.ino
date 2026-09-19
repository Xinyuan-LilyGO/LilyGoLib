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

static lv_obj_t *status_label;


#if defined(ARDUINO) && defined(USING_ST25R3916)

static void append_line(char *buf, size_t buf_size, const char *line)
{
    if (!buf || !line || buf_size == 0) return;
    const size_t used = strlen(buf);
    if (used >= buf_size - 1) return;
    snprintf(buf + used, buf_size - used, "%s", line);
}

static bool is_mifare_classic_like(const LilyGoNfcReaderResult &result)
{
    return result.mifareClassic ||
           strcmp(result.tagType, "MIFARE") == 0 ||
           strstr(result.cardType, "MIFARE Classic") != NULL;
}

static bool is_uid_only_result(const LilyGoNfcReaderResult &result)
{
    return strcmp(result.ndefStateText, "UID only") == 0;
}

static const char *card_detected_message(const LilyGoNfcReaderResult &result)
{
    if (is_mifare_classic_like(result)) {
        return "MIFARE Classic card detected. ID has been read.\n";
    }
    if (strstr(result.cardType, "Type 4") != NULL) {
        return "NFC Type 4 card/device detected. ID has been read.\n";
    }
    if (strstr(result.cardType, "Type 3") != NULL || strstr(result.cardType, "FeliCa") != NULL) {
        return "FeliCa / NFC Type 3 card detected. ID has been read.\n";
    }
    if (strstr(result.cardType, "Type 5") != NULL || strstr(result.cardType, "NFC-V") != NULL) {
        return "ISO15693 / NFC Type 5 card detected. ID has been read.\n";
    }
    if (strstr(result.cardType, "P2P") != NULL) {
        return "NFC peer-to-peer device detected. ID has been read.\n";
    }
    if (strstr(result.cardType, "ST25TB") != NULL) {
        return "ST25TB tag detected. ID has been read.\n";
    }
    return "NFC card detected. ID has been read.\n";
}

static void update_screen(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result)
{
    if (!status_label) return;

    char text[768];
    char line[224];
    text[0] = '\0';

    snprintf(line, sizeof(line), "NFC Reader Example\nState: %s\nID: %s\n",
             lilygoNfcEventName(event),
             result.uidText[0] ? result.uidText : "--");
    append_line(text, sizeof(text), line);

    snprintf(line, sizeof(line), "Card: %s\n%s / %s / %s\n",
             result.cardType[0] ? result.cardType : "--",
             result.technology[0] ? result.technology : "--",
             result.interfaceName[0] ? result.interfaceName : "--",
             result.tagType[0] ? result.tagType : "--");
    append_line(text, sizeof(text), line);

    if (result.cardInfo[0] && strcmp(result.cardInfo, "--") != 0) {
        append_line(text, sizeof(text), result.cardInfo);
        append_line(text, sizeof(text), "\n");
    }

    if (event == LILYGO_NFC_EVENT_CARD_DETECTED && is_uid_only_result(result)) {
        append_line(text, sizeof(text), card_detected_message(result));
    } else if (event == LILYGO_NFC_EVENT_NDEF_UNSUPPORTED) {
        append_line(text, sizeof(text), card_detected_message(result));
        append_line(text, sizeof(text), "No standard NDEF data found.\n");
    } else if (result.lastError != ST_ERR_NONE) {
        snprintf(line, sizeof(line), "Error %u: %s\n",
                 result.lastError,
                 result.errorText[0] ? result.errorText : "NFC error");
        append_line(text, sizeof(text), line);
    } else {
        snprintf(line, sizeof(line), "NDEF: %s  v%u.%u  %luB\n",
                 result.ndefStateText[0] ? result.ndefStateText : "--",
                 result.ndefMajorVersion,
                 result.ndefMinorVersion,
                 static_cast<unsigned long>(result.ndefMessageLen));
        append_line(text, sizeof(text), line);
    }

    if (result.recordCount > 0) {
        append_line(text, sizeof(text), "\nRecords:\n");
        for (uint8_t i = 0; i < result.recordCount; ++i) {
            const LilyGoNfcRecord &record = result.records[i];
            snprintf(line, sizeof(line), "%u. %s: %s\n",
                     record.index,
                     lilygoNfcRecordKindName(record.kind),
                     record.value[0] ? record.value : "--");
            append_line(text, sizeof(text), line);
        }
    } else if (event == LILYGO_NFC_EVENT_STARTED ||
               event == LILYGO_NFC_EVENT_CARD_RELEASED) {
        append_line(text, sizeof(text), "\nWaiting for card...");
    }

    lv_label_set_text(status_label, text);
}

static void nfc_event_callback(LilyGoNfcEvent event, const LilyGoNfcReaderResult &result, void *userData)
{
    (void)userData;
    Serial.printf("NFC event: %s  state:%s  card:%s  id:%s  ndef:%s  err:%u  records:%u\n",
                  lilygoNfcEventName(event),
                  lilygoNfcStateName(result.state),
                  result.cardType[0] ? result.cardType : "--",
                  result.uidText,
                  result.ndefStateText[0] ? result.ndefStateText : "--",
                  result.lastError,
                  result.recordCount);
    update_screen(event, result);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    status_label = lv_label_create(lv_scr_act());
    lv_obj_set_width(status_label, LV_PCT(94));
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(status_label, "NFC Reader Example\nStarting...");
    lv_obj_center(status_label);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    instance.powerControl(POWER_NFC, true);
    if (!LilyGoNfc.beginReader(nfc_event_callback)) {
        lv_label_set_text(status_label, "NFC Reader Example\nStart failed.");
    }
}

void loop()
{
    LilyGoNfc.loop();
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
    Serial.println("The example only supports devices with ST25R3916 NFC."); delay(1000);
}

#endif
