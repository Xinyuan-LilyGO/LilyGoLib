/**
 * @file      USB_MSC.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-08-14
 *
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include "ff.h"
#include "diskio.h"
#include "LilyGoLib.h"

#if defined(USING_FATFS)
#include <FFat.h>
#elif defined(USING_SPIFFS)
#include <SPIFFS.h>
#endif

static lock_callback_t mutexUnlock = NULL;
static lock_callback_t mutexLock = NULL;

#if !ARDUINO_USB_MODE
#include <USB.h>
#include <USBMSC.h>
#include <SD.h>
#include "FFat.h"
#include "ff.h"
#include "diskio.h"
#include "esp_vfs_fat.h"

// USB Mass Storage Class (MSC) object
static USBMSC msc;
static uint32_t block_count = 0;
static uint16_t block_size = 0;
static uint8_t  pdrv = 0; //The default drive number of ESP32 Flash is 0

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
#ifdef USING_SD_FAT
    if (mutexLock) {
        // xSemaphoreTake(_mutex, portMAX_DELAY);
        mutexLock();
    }
    uint32_t secSize = SD.sectorSize();
    if (!secSize) {
        if (mutexUnlock) {
            // xSemaphoreGive(_mutex);
            mutexUnlock();
        }
        return -1;  // disk error
    }
    log_v("Write lba: %ld\toffset: %ld\tbufsize: %ld", lba, offset, bufsize);
    for (int x = 0; x < bufsize / secSize; x++) {
        uint8_t blkbuffer[secSize];
        memcpy(blkbuffer, (uint8_t *)buffer + secSize * x, secSize);
        if (!SD.writeRAW(blkbuffer, lba + x)) {
            if (mutexUnlock) {
                // xSemaphoreGive(_mutex);
                mutexUnlock();
            }
            return -1;
        }
    }
    if (mutexUnlock) {
        // xSemaphoreGive(_mutex);
        mutexUnlock();
    }
    return bufsize;
#else
#ifdef USING_FATFS
    const uint32_t block_count = bufsize / block_size;
    disk_write(pdrv, (BYTE *)buffer, lba, block_count);
    return block_count * block_size;
#endif
#endif
    return 0;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
#ifdef USING_SD_FAT
    if (mutexLock) {
        // xSemaphoreTake(_mutex, portMAX_DELAY);
        mutexLock();
    }
    uint32_t secSize = SD.sectorSize();
    if (!secSize) {
        if (mutexUnlock) {
            // xSemaphoreGive(_mutex);
            mutexUnlock();
        }
        return -1;  // disk error
    }
    log_v("Read lba: %ld\toffset: %ld\tbufsize: %ld\tsector: %lu", lba, offset, bufsize, secSize);
    for (int x = 0; x < bufsize / secSize; x++) {
        if (!SD.readRAW((uint8_t *)buffer + (x * secSize), lba + x)) {
            if (mutexUnlock) {
                // xSemaphoreGive(_mutex);
                mutexUnlock();
            }
            return -1;  // outside of volume boundary
        }
    }
    if (mutexUnlock) {
        // xSemaphoreGive(_mutex);
        mutexUnlock();
    }
    return bufsize;
#else
#ifdef USING_FATFS
    const uint32_t block_count = bufsize / block_size;
    disk_read(pdrv, (BYTE *)buffer, lba, block_count);
    return block_count * block_size;
#endif
#endif
    return 0;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    log_i("Start/Stop power: %u\tstart: %d\teject: %d", power_condition, start, load_eject);
#ifdef USING_SD_FAT
#else
#ifdef USING_FATFS
    if (load_eject) {
        if (!start) {
            // Eject but first flush.
            if (disk_ioctl(pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            }
        }
    } else {
        if (!start) {
            // Stop the unit but don't eject.
            if (disk_ioctl(pdrv, CTRL_SYNC, NULL) != RES_OK) {
                return false;
            }
        }
    }
#endif
#endif
    return true;
}
#endif


static void __listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.print(file.name());
            time_t t = file.getLastWrite();
            struct tm *tmstruct = localtime(&t);
            Serial.printf(
                "  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour,
                tmstruct->tm_min, tmstruct->tm_sec
            );
            if (levels) {
                __listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.print(file.size());
            time_t t = file.getLastWrite();
            struct tm *tmstruct = localtime(&t);
            Serial.printf(
                "  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour,
                tmstruct->tm_min, tmstruct->tm_sec
            );
        }
        file = root.openNextFile();
    }
}

void setupMSC(lock_callback_t lock_cb, lock_callback_t ulock_cb)
{
    mutexUnlock = ulock_cb;
    mutexLock = lock_cb;

#if defined(USING_FATFS)
    log_d("Init FFat");
    if (!FFat.begin(false, "/fs")) {
        log_e("FFAT BEGIN FAILED, FORMAT START");
        FFat.format();
        if (!FFat.begin()) {
            while (1) {
                delay(1000);
                log_e("FFAT INIT FAILED! > ");
            }
        }
    }
    __listDir(FFat, "/", 3);

#elif defined(USING_SPIFFS)
    if (!SPIFFS.begin()) {
        log_e("SPIFFS INIT FAILED, FORMAT START");
        SPIFFS.format();
        if (!SPIFFS.begin()) {
            while (1) {
                delay(1000);
                log_e("SPIFFS INIT FAILED! > ");
            }
        }
    }
    __listDir(SPIFFS, "/", 3);

#endif

#if !ARDUINO_USB_MODE

#ifdef USING_SD_FAT
    block_count = SD.numSectors();
    block_size =  SD.sectorSize();
    log_d("Card Size: %lluMB\n", SD.totalBytes() / 1024 / 1024);
    log_d("Sector: %d\tCount: %d\n", SD.sectorSize(), SD.numSectors());
#else /*USING_SD_FAT*/

#ifdef USING_FATFS
    disk_ioctl(pdrv, GET_SECTOR_COUNT, &block_count);
    disk_ioctl(pdrv, GET_SECTOR_SIZE, &block_size);
#endif /*USING_FATFS*/

#endif /*USING_SD_FAT*/

    Serial.println("Initializing MSC");
    // Initialize USB metadata and callbacks for MSC (Mass Storage Class)
    msc.vendorID("ESP32");
    msc.productID("USB_MSC");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);

    msc.begin(block_count, block_size);
    Serial.println("Initializing USB");
    USB.firmwareVersion(0x0100);
    USB.begin();

#endif /*ARDUINO_USB_MODE*/

}

