/**
 * @file      USB_MSC.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-08-14
 *
 */
#include "LilyGoLog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include "ff.h"
#include "diskio.h"
#include "LilyGoLib.h"
#include "USB_Service.h"

#if defined(USING_FATFS)
#include <FFat.h>
#elif defined(USING_SPIFFS)
#include <SPIFFS.h>
#endif

static lock_callback_t mutexUnlock = NULL;
static lock_callback_t mutexLock = NULL;

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !ARDUINO_USB_MODE
#include <USB.h>
#include <USBMSC.h>
#include <SD.h>
#include <sd_diskio.h>
#include <stdarg.h>
#include <string.h>
#if CONFIG_TINYUSB_HID_ENABLED
#include <USBHIDKeyboard.h>
#endif
#include "FFat.h"
#include "ff.h"
#include "diskio.h"
#include "esp_vfs_fat.h"

#if CONFIG_TINYUSB_MSC_ENABLED
static USBMSC msc;
#endif
#if CONFIG_TINYUSB_HID_ENABLED
static USBHIDKeyboard usb_keyboard;
#endif

static uint32_t block_count = 0;
static uint16_t block_size = 0;
static uint8_t  pdrv = 0; //The default drive number of ESP32 Flash is 0
static uint8_t  sd_pdrv = 0xFF;
static lilygo_usb_msc_backend_t msc_backend = LILYGO_USB_MSC_BACKEND_SD;
static bool usb_started = false;
static bool msc_configured = false;
static bool msc_active = false;
static bool msc_ejected = false;
static bool msc_host_accessed = false;
static bool msc_close_pending = false;
static bool msc_media_present = false;
static bool hid_configured = false;
static uint32_t msc_ejected_at_ms = 0;
static char msc_status_text[80] = "USB not started";
static uint8_t msc_io_sector[512];

static void set_msc_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(msc_status_text, sizeof(msc_status_text), fmt, args);
    va_end(args);
}

static bool lock_shared_spi(bool *locked_spi)
{
    if (locked_spi) *locked_spi = false;
    if (!mutexLock) return true;
    if (!mutexLock()) {
        set_msc_status("SPI busy");
        return false;
    }
    if (locked_spi) *locked_spi = true;
    return true;
}

static void unlock_shared_spi(bool locked_spi)
{
    if (locked_spi && mutexUnlock) {
        mutexUnlock();
    }
}

static bool begin_storage_io(bool *locked_spi)
{
    if (locked_spi) *locked_spi = false;
    if (msc_backend != LILYGO_USB_MSC_BACKEND_SD) {
        return true;
    }
    return lock_shared_spi(locked_spi);
}

static void end_storage_io(bool locked_spi)
{
    unlock_shared_spi(locked_spi);
}

static void yield_during_large_storage_io()
{
    if (msc_backend == LILYGO_USB_MSC_BACKEND_SD) {
        taskYIELD();
    }
}

static void set_media_present(bool present)
{
    if (msc_media_present == present) return;
    msc.mediaPresent(present);
    msc_media_present = present;
}

static bool sd_backend_ready()
{
#if defined(HAS_SD_CARD_SOCKET)
    return instance.isCardReady();
#else
    return false;
#endif
}

static bool flash_backend_ready()
{
#ifdef USING_FATFS
    DWORD count = 0;
    WORD size = 0;
    if (disk_ioctl(pdrv, GET_SECTOR_COUNT, &count) != RES_OK) return false;
    if (disk_ioctl(pdrv, GET_SECTOR_SIZE, &size) != RES_OK) return false;
    return count > 0 && size > 0;
#else
    return false;
#endif
}

static bool resolve_sd_pdrv(uint32_t expected_sectors, uint16_t expected_sector_size)
{
#if defined(HAS_SD_CARD_SOCKET)
    if (sd_pdrv != 0xFF &&
            sdcard_num_sectors(sd_pdrv) == expected_sectors &&
            sdcard_sector_size(sd_pdrv) == expected_sector_size) {
        return true;
    }

    for (uint8_t candidate = 0; candidate < FF_VOLUMES; candidate++) {
        if (sdcard_num_sectors(candidate) == expected_sectors &&
                sdcard_sector_size(candidate) == expected_sector_size) {
            sd_pdrv = candidate;
            return true;
        }
    }

    sd_pdrv = 0xFF;
#else
    (void)expected_sectors;
    (void)expected_sector_size;
#endif
    return false;
}

static bool refresh_storage_geometry()
{
    block_count = 0;
    block_size = 0;

    switch (msc_backend) {
    case LILYGO_USB_MSC_BACKEND_SD:
#if defined(HAS_SD_CARD_SOCKET)
    {
        bool locked_spi = false;
        if (!lock_shared_spi(&locked_spi)) {
            set_msc_status("SD SPI busy");
            return false;
        }
        block_count = SD.numSectors();
        block_size = SD.sectorSize();
        if (!block_count || !block_size) {
            unlock_shared_spi(locked_spi);
            set_msc_status("SD not ready");
            return false;
        }
        LILYGO_LOG_D("SD Card Size: %lluMB\n", SD.totalBytes() / 1024 / 1024);
        LILYGO_LOG_D("SD Sector: %d\tCount: %d\n", SD.sectorSize(), SD.numSectors());
        resolve_sd_pdrv(block_count, block_size);
        unlock_shared_spi(locked_spi);
        return true;
    }
#else
        set_msc_status("SD backend disabled");
        return false;
#endif

    case LILYGO_USB_MSC_BACKEND_FLASH:
#ifdef USING_FATFS
        disk_ioctl(pdrv, GET_SECTOR_COUNT, &block_count);
        disk_ioctl(pdrv, GET_SECTOR_SIZE, &block_size);
        if (!block_count || !block_size) {
            set_msc_status("Flash FAT not ready");
            return false;
        }
        return true;
#else
        set_msc_status("Flash backend disabled");
        return false;
#endif

    default:
        set_msc_status("No storage backend");
        return false;
    }
}

static bool backend_read_blocks(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    if (!count) return true;

    switch (msc_backend) {
    case LILYGO_USB_MSC_BACKEND_SD:
#if defined(HAS_SD_CARD_SOCKET)
        if (sd_pdrv != 0xFF) {
            DRESULT res = disk_read(sd_pdrv, (BYTE *)buffer, lba, count);
            if (res == RES_OK) return true;
        }
        if (count == 1) return SD.readRAW(buffer, lba);
        for (uint32_t i = 0; i < count; i++) {
            if (!SD.readRAW(buffer + (i * block_size), lba + i)) return false;
        }
        return true;
#else
        return false;
#endif

    case LILYGO_USB_MSC_BACKEND_FLASH:
#ifdef USING_FATFS
        return disk_read(pdrv, (BYTE *)buffer, lba, count) == RES_OK;
#else
        return false;
#endif

    default:
        return false;
    }
}

static bool backend_write_blocks(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    if (!count) return true;

    switch (msc_backend) {
    case LILYGO_USB_MSC_BACKEND_SD:
#if defined(HAS_SD_CARD_SOCKET)
        if (sd_pdrv != 0xFF) {
            DRESULT res = disk_write(sd_pdrv, (const BYTE *)buffer, lba, count);
            if (res == RES_OK) return true;
        }
        if (count == 1) return SD.writeRAW((uint8_t *)buffer, lba);
        for (uint32_t i = 0; i < count; i++) {
            if (!SD.writeRAW((uint8_t *)(buffer + (i * block_size)), lba + i)) return false;
        }
        return true;
#else
        return false;
#endif

    case LILYGO_USB_MSC_BACKEND_FLASH:
#ifdef USING_FATFS
        return disk_write(pdrv, (const BYTE *)buffer, lba, count) == RES_OK;
#else
        return false;
#endif

    default:
        return false;
    }
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    msc_host_accessed = true;
    bool locked_spi = false;
    if (!begin_storage_io(&locked_spi)) return -1;

    const uint32_t secSize = block_size;
    if (!secSize || offset >= secSize) {
        end_storage_io(locked_spi);
        return -1;
    }

    LILYGO_LOG_V("Write %s lba: %ld\toffset: %ld\tbufsize: %ld",
          lilygo_usb_msc_backend_name(msc_backend), lba, offset, bufsize);
    uint32_t written = 0;
    uint32_t current_lba = lba;
    uint32_t current_offset = offset;

    while (written < bufsize) {
        const uint32_t remaining = bufsize - written;
        const uint8_t *src = buffer + written;

        if (current_offset == 0 && remaining >= secSize) {
            uint32_t sector_count = remaining / secSize;
            if (!backend_write_blocks(current_lba, src, sector_count)) {
                end_storage_io(locked_spi);
                return -1;
            }
            written += sector_count * secSize;
            current_lba += sector_count;
        } else {
            const uint32_t chunk = min(secSize - current_offset, remaining);
            if (secSize > sizeof(msc_io_sector)) {
                end_storage_io(locked_spi);
                return -1;
            }
            if (!backend_read_blocks(current_lba, msc_io_sector, 1)) {
                end_storage_io(locked_spi);
                return -1;
            }
            memcpy(msc_io_sector + current_offset, src, chunk);
            if (!backend_write_blocks(current_lba, msc_io_sector, 1)) {
                end_storage_io(locked_spi);
                return -1;
            }

            written += chunk;
            current_lba++;
        }
        current_offset = 0;
        if (msc_backend == LILYGO_USB_MSC_BACKEND_SD && written < bufsize && (current_lba & 0x3F) == 0) {
            end_storage_io(locked_spi);
            yield_during_large_storage_io();
            if (!begin_storage_io(&locked_spi)) return -1;
        }
    }

    end_storage_io(locked_spi);
    return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    msc_host_accessed = true;
    bool locked_spi = false;
    if (!begin_storage_io(&locked_spi)) return -1;

    const uint32_t secSize = block_size;
    if (!secSize || offset >= secSize) {
        end_storage_io(locked_spi);
        return -1;
    }

    LILYGO_LOG_V("Read %s lba: %ld\toffset: %ld\tbufsize: %ld\tsector: %lu",
          lilygo_usb_msc_backend_name(msc_backend), lba, offset, bufsize, secSize);
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t read = 0;
    uint32_t current_lba = lba;
    uint32_t current_offset = offset;

    while (read < bufsize) {
        const uint32_t remaining = bufsize - read;

        if (current_offset == 0 && remaining >= secSize) {
            uint32_t sector_count = remaining / secSize;
            if (!backend_read_blocks(current_lba, dst + read, sector_count)) {
                end_storage_io(locked_spi);
                return -1;
            }
            read += sector_count * secSize;
            current_lba += sector_count;
        } else {
            const uint32_t chunk = min(secSize - current_offset, remaining);
            if (secSize > sizeof(msc_io_sector)) {
                end_storage_io(locked_spi);
                return -1;
            }
            if (!backend_read_blocks(current_lba, msc_io_sector, 1)) {
                end_storage_io(locked_spi);
                return -1;
            }
            memcpy(dst + read, msc_io_sector + current_offset, chunk);

            read += chunk;
            current_lba++;
        }
        current_offset = 0;
        if (msc_backend == LILYGO_USB_MSC_BACKEND_SD && read < bufsize && (current_lba & 0x3F) == 0) {
            end_storage_io(locked_spi);
            yield_during_large_storage_io();
            if (!begin_storage_io(&locked_spi)) return -1;
        }
    }

    end_storage_io(locked_spi);
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject)
{
    LILYGO_LOG_I("Start/Stop %s power: %u\tstart: %d\teject: %d",
          lilygo_usb_msc_backend_name(msc_backend), power_condition, start, load_eject);

#ifdef USING_FATFS
    if (msc_backend == LILYGO_USB_MSC_BACKEND_FLASH && !start) {
        if (disk_ioctl(pdrv, CTRL_SYNC, NULL) != RES_OK) {
            return false;
        }
    }
#endif

    if (!start && load_eject) {
        msc_ejected = true;
        msc_ejected_at_ms = millis();
        msc_host_accessed = false;
        msc_close_pending = false;
        set_msc_status("Host ejected");
    }

    return true;
}

static bool configure_msc(bool media_present)
{
#if CONFIG_TINYUSB_MSC_ENABLED
    if (!refresh_storage_geometry()) return false;

    msc.vendorID("LilyGo");
    msc.productID(msc_backend == LILYGO_USB_MSC_BACKEND_SD ? "TDeck SD" : "TDeck Flash");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    set_media_present(media_present);
    msc_active = media_present;

    if (!msc.begin(block_count, block_size)) {
        set_media_present(false);
        msc_active = false;
        set_msc_status("MSC begin failed");
        return false;
    }

    msc_configured = true;
    set_msc_status("%s ready", lilygo_usb_msc_backend_name(msc_backend));
    return true;
#else
    set_msc_status("MSC disabled");
    return false;
#endif
}

static void on_usb_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base != ARDUINO_USB_EVENTS) return;

    switch (event_id) {
    case ARDUINO_USB_STARTED_EVENT:
        set_msc_status(msc_active ? "USB started, MSC exposed" : "USB started");
        break;
    case ARDUINO_USB_STOPPED_EVENT:
        set_msc_status("USB stopped");
        break;
    case ARDUINO_USB_SUSPEND_EVENT:
        set_msc_status("USB suspended");
        break;
    case ARDUINO_USB_RESUME_EVENT:
        set_msc_status(msc_active ? "USB resumed, MSC exposed" : "USB resumed");
        break;
    default:
        break;
    }
}

#if CONFIG_TINYUSB_HID_ENABLED
static uint8_t map_service_key(uint8_t key)
{
    if (key >= 32 && key <= 126) return key;

    switch (key) {
    case LILYGO_USB_KEY_ENTER:
        return KEY_RETURN;
    case LILYGO_USB_KEY_TAB:
        return KEY_TAB;
    case LILYGO_USB_KEY_ESC:
        return KEY_ESC;
    case LILYGO_USB_KEY_BACKSPACE:
        return KEY_BACKSPACE;
    case LILYGO_USB_KEY_DELETE:
        return KEY_DELETE;
    case LILYGO_USB_KEY_UP:
        return KEY_UP_ARROW;
    case LILYGO_USB_KEY_DOWN:
        return KEY_DOWN_ARROW;
    case LILYGO_USB_KEY_LEFT:
        return KEY_LEFT_ARROW;
    case LILYGO_USB_KEY_RIGHT:
        return KEY_RIGHT_ARROW;
    case LILYGO_USB_KEY_HOME:
        return KEY_HOME;
    case LILYGO_USB_KEY_END:
        return KEY_END;
    case LILYGO_USB_KEY_PAGE_UP:
        return KEY_PAGE_UP;
    case LILYGO_USB_KEY_PAGE_DOWN:
        return KEY_PAGE_DOWN;
    case LILYGO_USB_KEY_INSERT:
        return KEY_INSERT;
    case LILYGO_USB_KEY_F1:
        return KEY_F1;
    case LILYGO_USB_KEY_F2:
        return KEY_F2;
    case LILYGO_USB_KEY_F3:
        return KEY_F3;
    case LILYGO_USB_KEY_F4:
        return KEY_F4;
    case LILYGO_USB_KEY_F5:
        return KEY_F5;
    case LILYGO_USB_KEY_F6:
        return KEY_F6;
    case LILYGO_USB_KEY_F7:
        return KEY_F7;
    case LILYGO_USB_KEY_F8:
        return KEY_F8;
    case LILYGO_USB_KEY_F9:
        return KEY_F9;
    case LILYGO_USB_KEY_F10:
        return KEY_F10;
    case LILYGO_USB_KEY_F11:
        return KEY_F11;
    case LILYGO_USB_KEY_F12:
        return KEY_F12;
    default:
        return 0;
    }
}
#endif

bool lilygo_usb_service_begin()
{
#if CONFIG_TINYUSB_ENABLED
    if (usb_started) return true;

#if CONFIG_TINYUSB_HID_ENABLED
    usb_keyboard.begin();
    hid_configured = true;
#endif

    USB.onEvent(on_usb_event);
    USB.firmwareVersion(0x0100);
    USB.productName("LilyGo T-Deck V2");
    USB.manufacturerName("LilyGo");
    usb_started = USB.begin();
    set_msc_status(usb_started ? "USB started" : "USB begin failed");
    return usb_started;
#else
    set_msc_status("TinyUSB disabled");
    return false;
#endif
}

bool lilygo_usb_service_available()
{
#if CONFIG_TINYUSB_ENABLED
    return true;
#else
    return false;
#endif
}

bool lilygo_usb_msc_available()
{
#if CONFIG_TINYUSB_MSC_ENABLED
    return lilygo_usb_service_available() && lilygo_usb_msc_backend_available(msc_backend);
#else
    set_msc_status("MSC disabled");
    return false;
#endif
}

bool lilygo_usb_msc_set_active(bool active)
{
#if CONFIG_TINYUSB_MSC_ENABLED
    if (!active) {
        if (msc_backend == LILYGO_USB_MSC_BACKEND_SD && msc_active && !msc_ejected) {
            msc_close_pending = true;
            set_msc_status("Eject SD on host first");
            return false;
        }
        if (msc_configured) set_media_present(false);
        msc_active = false;
        msc_host_accessed = false;
        msc_close_pending = false;
        msc_ejected_at_ms = 0;
        set_msc_status("%s hidden", lilygo_usb_msc_backend_name(msc_backend));
        return true;
    }

    if (msc_active) {
        set_msc_status("%s exposed", lilygo_usb_msc_backend_name(msc_backend));
        return true;
    }

    if (!configure_msc(true)) return false;

    if (!lilygo_usb_service_begin()) {
        set_media_present(false);
        msc_active = false;
        set_msc_status("USB begin failed");
        return false;
    }

    msc_ejected = false;
    msc_host_accessed = false;
    msc_close_pending = false;
    msc_ejected_at_ms = 0;
    set_msc_status("%s exposed", lilygo_usb_msc_backend_name(msc_backend));
    return true;
#else
    set_msc_status("MSC disabled");
    return false;
#endif
}

bool lilygo_usb_msc_is_active()
{
    return msc_active;
}

bool lilygo_usb_msc_was_ejected()
{
    return msc_ejected;
}

void lilygo_usb_msc_poll()
{
#if CONFIG_TINYUSB_MSC_ENABLED
    if (msc_ejected &&
        msc_active &&
        msc_ejected_at_ms &&
        millis() - msc_ejected_at_ms >= 750) {
        set_media_present(false);
        msc_active = false;
        msc_host_accessed = false;
        msc_close_pending = false;
        msc_ejected_at_ms = 0;
        set_msc_status("%s hidden", lilygo_usb_msc_backend_name(msc_backend));
    }
#endif
}

bool lilygo_usb_msc_select_backend(lilygo_usb_msc_backend_t backend)
{
#if CONFIG_TINYUSB_MSC_ENABLED
    if (backend != LILYGO_USB_MSC_BACKEND_SD && backend != LILYGO_USB_MSC_BACKEND_FLASH) {
        set_msc_status("Invalid backend");
        return false;
    }

    if (msc_active && !lilygo_usb_msc_set_active(false)) {
        return false;
    }
    if (msc_configured && !msc_active) {
        set_media_present(false);
    }
    msc_active = false;
    msc_ejected = false;
    msc_host_accessed = false;
    msc_close_pending = false;
    msc_ejected_at_ms = 0;
    msc_backend = backend;
    block_count = 0;
    block_size = 0;

    set_msc_status("%s selected", lilygo_usb_msc_backend_name(msc_backend));
    return true;
#else
    (void)backend;
    return false;
#endif
}

lilygo_usb_msc_backend_t lilygo_usb_msc_get_backend()
{
    return msc_backend;
}

bool lilygo_usb_msc_backend_available(lilygo_usb_msc_backend_t backend)
{
    switch (backend) {
    case LILYGO_USB_MSC_BACKEND_SD:
        return sd_backend_ready();
    case LILYGO_USB_MSC_BACKEND_FLASH:
        return flash_backend_ready();
    default:
        return false;
    }
}

const char *lilygo_usb_msc_backend_name(lilygo_usb_msc_backend_t backend)
{
    switch (backend) {
    case LILYGO_USB_MSC_BACKEND_SD:
        return "SD Card";
    case LILYGO_USB_MSC_BACKEND_FLASH:
        return "Internal Flash";
    default:
        return "Unknown";
    }
}

uint32_t lilygo_usb_msc_block_count()
{
    return block_count;
}

uint16_t lilygo_usb_msc_block_size()
{
    return block_size;
}

uint64_t lilygo_usb_msc_capacity_bytes()
{
    return (uint64_t)block_count * block_size;
}

const char *lilygo_usb_msc_status()
{
    return msc_status_text;
}

bool lilygo_usb_hid_keyboard_available()
{
#if CONFIG_TINYUSB_HID_ENABLED
    return lilygo_usb_service_available() && (hid_configured || lilygo_usb_service_begin());
#else
    return false;
#endif
}

bool lilygo_usb_hid_keyboard_send_text(const char *text, uint16_t char_delay_ms)
{
#if CONFIG_TINYUSB_HID_ENABLED
    if (!text || !lilygo_usb_hid_keyboard_available()) return false;
    while (*text) {
        usb_keyboard.write((uint8_t)*text++);
        if (char_delay_ms) delay(char_delay_ms);
    }
    return true;
#else
    (void)text;
    (void)char_delay_ms;
    return false;
#endif
}

bool lilygo_usb_hid_keyboard_tap_key(uint8_t key)
{
#if CONFIG_TINYUSB_HID_ENABLED
    const uint8_t mapped = map_service_key(key);
    if (!mapped || !lilygo_usb_hid_keyboard_available()) return false;
    usb_keyboard.write(mapped);
    return true;
#else
    (void)key;
    return false;
#endif
}

bool lilygo_usb_hid_keyboard_combo(uint8_t modifiers, uint8_t key)
{
#if CONFIG_TINYUSB_HID_ENABLED
    const uint8_t mapped = map_service_key(key);
    if (!mapped || !lilygo_usb_hid_keyboard_available()) return false;

    if (modifiers & LILYGO_USB_MOD_CTRL) usb_keyboard.press(KEY_LEFT_CTRL);
    if (modifiers & LILYGO_USB_MOD_SHIFT) usb_keyboard.press(KEY_LEFT_SHIFT);
    if (modifiers & LILYGO_USB_MOD_ALT) usb_keyboard.press(KEY_LEFT_ALT);
    if (modifiers & LILYGO_USB_MOD_GUI) usb_keyboard.press(KEY_LEFT_GUI);
    usb_keyboard.press(mapped);
    delay(25);
    usb_keyboard.releaseAll();
    delay(20);
    return true;
#else
    (void)modifiers;
    (void)key;
    return false;
#endif
}

void lilygo_usb_hid_keyboard_release_all()
{
#if CONFIG_TINYUSB_HID_ENABLED
    if (hid_configured) usb_keyboard.releaseAll();
#endif
}

#else

bool lilygo_usb_service_begin()
{
    return false;
}

bool lilygo_usb_service_available()
{
    return false;
}

bool lilygo_usb_msc_available()
{
    return false;
}

bool lilygo_usb_msc_set_active(bool active)
{
    (void)active;
    return false;
}

bool lilygo_usb_msc_is_active()
{
    return false;
}

bool lilygo_usb_msc_was_ejected()
{
    return false;
}

void lilygo_usb_msc_poll()
{
}

bool lilygo_usb_msc_select_backend(lilygo_usb_msc_backend_t backend)
{
    (void)backend;
    return false;
}

lilygo_usb_msc_backend_t lilygo_usb_msc_get_backend()
{
    return LILYGO_USB_MSC_BACKEND_SD;
}

bool lilygo_usb_msc_backend_available(lilygo_usb_msc_backend_t backend)
{
    (void)backend;
    return false;
}

const char *lilygo_usb_msc_backend_name(lilygo_usb_msc_backend_t backend)
{
    (void)backend;
    return "Unavailable";
}

uint32_t lilygo_usb_msc_block_count()
{
    return 0;
}

uint16_t lilygo_usb_msc_block_size()
{
    return 0;
}

uint64_t lilygo_usb_msc_capacity_bytes()
{
    return 0;
}

const char *lilygo_usb_msc_status()
{
    return "TinyUSB requires ARDUINO_USB_MODE=0";
}

bool lilygo_usb_hid_keyboard_available()
{
    return false;
}

bool lilygo_usb_hid_keyboard_send_text(const char *text, uint16_t char_delay_ms)
{
    (void)text;
    (void)char_delay_ms;
    return false;
}

bool lilygo_usb_hid_keyboard_tap_key(uint8_t key)
{
    (void)key;
    return false;
}

bool lilygo_usb_hid_keyboard_combo(uint8_t modifiers, uint8_t key)
{
    (void)modifiers;
    (void)key;
    return false;
}

void lilygo_usb_hid_keyboard_release_all()
{
}

#endif /* defined(CONFIG_IDF_TARGET_ESP32S3) && !ARDUINO_USB_MODE */


static void __listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    if (!LILYGO_DEBUG_ENABLED) return;
    LILYGO_LOG_PRINTF("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        LILYGO_LOG_PRINTLN("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        LILYGO_LOG_PRINTLN("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            LILYGO_LOG_PRINT("  DIR : ");
            LILYGO_LOG_PRINT(file.name());
            time_t t = file.getLastWrite();
            struct tm *tmstruct = localtime(&t);
            LILYGO_LOG_PRINTF(
                "  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour,
                tmstruct->tm_min, tmstruct->tm_sec
            );
            if (levels) {
                __listDir(fs, file.path(), levels - 1);
            }
        } else {
            LILYGO_LOG_PRINT("  FILE: ");
            LILYGO_LOG_PRINT(file.name());
            LILYGO_LOG_PRINT("  SIZE: ");
            LILYGO_LOG_PRINT(file.size());
            time_t t = file.getLastWrite();
            struct tm *tmstruct = localtime(&t);
            LILYGO_LOG_PRINTF(
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
    LILYGO_LOG_D("Init FFat");
    if (!FFat.begin(false, "/fs")) {
        LILYGO_LOG_E("FFAT BEGIN FAILED, FORMAT START");
        FFat.format();
        if (!FFat.begin()) {
            while (1) {
                delay(1000);
                LILYGO_LOG_E("FFAT INIT FAILED! > ");
            }
        }
    }
    __listDir(FFat, "/", 3);

#elif defined(USING_SPIFFS)
    if (!SPIFFS.begin()) {
        LILYGO_LOG_E("SPIFFS INIT FAILED, FORMAT START");
        SPIFFS.format();
        if (!SPIFFS.begin()) {
            while (1) {
                delay(1000);
                LILYGO_LOG_E("SPIFFS INIT FAILED! > ");
            }
        }
    }
    __listDir(SPIFFS, "/", 3);

#endif

}
