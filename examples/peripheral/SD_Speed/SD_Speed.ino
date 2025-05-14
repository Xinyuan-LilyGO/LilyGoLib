/**
 * @file      SD_Speed.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-18
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef HAS_SD_CARD_SOCKET

const size_t TEST_FILE_SIZE = 1024 * 1024; // 1MB
const size_t BLOCK_SIZE = 512;

bool testWriteSpeed(float &writeTime, float &writeSpeed)
{
    Serial.println("Starting write speed test...");
    File testFile = SD.open("/testfile.bin", FILE_WRITE);
    if (!testFile) {
        Serial.println("Error opening file for writing!");
        return false;
    }

    uint32_t startTime = millis();
    for (size_t i = 0; i < TEST_FILE_SIZE; i += BLOCK_SIZE) {
        uint8_t buffer[BLOCK_SIZE];
        for (size_t j = 0; j < BLOCK_SIZE; j++) {
            buffer[j] = (uint8_t)(i + j);
        }
        testFile.write(buffer, BLOCK_SIZE);
    }
    uint32_t endTime = millis();
    testFile.close();

    writeTime = (endTime - startTime) / 1000.0;
    writeSpeed = TEST_FILE_SIZE / (writeTime * 1024.0);
    Serial.printf("Write time: %.2f s\n", writeTime);
    Serial.printf("Write speed: %.2f KB/s\n", writeSpeed);
    return true;
}

bool testReadSpeed(float &readTime, float &readSpeed)
{
    Serial.println("Starting read speed test...");
    File testFile = SD.open("/testfile.bin", FILE_READ);
    if (!testFile) {
        Serial.println("Error opening file for reading!");
        return false;
    }

    uint32_t startTime = millis();
    uint8_t buffer[BLOCK_SIZE];
    for (size_t i = 0; i < TEST_FILE_SIZE; i += BLOCK_SIZE) {
        testFile.read(buffer, BLOCK_SIZE);
    }
    uint32_t endTime = millis();
    testFile.close();

    readTime = (endTime - startTime) / 1000.0;
    readSpeed = TEST_FILE_SIZE / (readTime * 1024.0);
    Serial.printf("Read time: %.2f s\n", readTime);
    Serial.printf("Read speed: %.2f KB/s\n", readSpeed);
    return true;
}



void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, "SD Speed test");
    lv_obj_center(label);
    lv_task_handler();

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);


    int retry = 10;
    bool is_mount = false;
    do {
        is_mount = instance.installSD();
        delay(1000);
    } while (!is_mount);

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        lv_label_set_text(label, "SD Mount Failed!");

        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }


    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    lv_label_set_text_fmt(label, "SD Size :%lluMB", cardSize);


    float writeTime, writeSpeed;
    bool rlst = testWriteSpeed(writeTime, writeSpeed);

    lv_obj_t *writeRlst = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(writeRlst, &lv_font_montserrat_20, 0);
    if (rlst) {
        lv_label_set_text_fmt(writeRlst, "Write block use %.2f s\nWrite Speed is %.2f KB/s", writeTime, writeSpeed);
    } else {
        lv_label_set_text(writeRlst, "Write speed test Failed");
    }
    lv_obj_align_to(writeRlst, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    float readTime, readSpeed;
    rlst = testReadSpeed(readTime, readSpeed);
    lv_obj_t *readRlst = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(readRlst, &lv_font_montserrat_20, 0);
    if (rlst) {
        lv_label_set_text_fmt(readRlst, "Read block use %.2f s\nRead Speed is %.2f KB/s", readTime, readSpeed);
    } else {
        lv_label_set_text(readRlst, "Read speed test Failed");
    }
    lv_obj_align_to(readRlst, writeRlst, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

}

void loop()
{
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
    Serial.println("This example only supports examples with SD-Card socket.");
    delay(1000);
}

#endif
