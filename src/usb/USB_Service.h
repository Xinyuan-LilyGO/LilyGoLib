/**
 * @file      USB_Service.h
 * @brief     Declares TinyUSB service helpers for MSC storage and HID keyboard.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-10
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** HID keyboard modifier bit for Control. */
#define LILYGO_USB_MOD_CTRL  0x01
/** HID keyboard modifier bit for Shift. */
#define LILYGO_USB_MOD_SHIFT 0x02
/** HID keyboard modifier bit for Alt. */
#define LILYGO_USB_MOD_ALT   0x04
/** HID keyboard modifier bit for GUI / Windows / Command. */
#define LILYGO_USB_MOD_GUI   0x08

/**
 * @brief Logical HID keyboard key identifiers accepted by the USB service.
 */
enum {
    LILYGO_USB_KEY_NONE = 0,
    LILYGO_USB_KEY_ENTER = 1,
    LILYGO_USB_KEY_TAB,
    LILYGO_USB_KEY_ESC,
    LILYGO_USB_KEY_BACKSPACE,
    LILYGO_USB_KEY_DELETE,
    LILYGO_USB_KEY_UP,
    LILYGO_USB_KEY_DOWN,
    LILYGO_USB_KEY_LEFT,
    LILYGO_USB_KEY_RIGHT,
    LILYGO_USB_KEY_HOME,
    LILYGO_USB_KEY_END,
    LILYGO_USB_KEY_PAGE_UP,
    LILYGO_USB_KEY_PAGE_DOWN,
    LILYGO_USB_KEY_INSERT,
    LILYGO_USB_KEY_F1,
    LILYGO_USB_KEY_F2,
    LILYGO_USB_KEY_F3,
    LILYGO_USB_KEY_F4,
    LILYGO_USB_KEY_F5,
    LILYGO_USB_KEY_F6,
    LILYGO_USB_KEY_F7,
    LILYGO_USB_KEY_F8,
    LILYGO_USB_KEY_F9,
    LILYGO_USB_KEY_F10,
    LILYGO_USB_KEY_F11,
    LILYGO_USB_KEY_F12,
};

/**
 * @brief Initialize the shared TinyUSB service.
 * @return true if TinyUSB is available or was started successfully.
 */
bool lilygo_usb_service_begin();

/**
 * @brief Check whether the shared TinyUSB service is available.
 * @return true when USB service functions can be used.
 */
bool lilygo_usb_service_available();

/**
 * @brief Check whether the selected MSC backend can be exposed over USB.
 * @return true if MSC is available for the current backend.
 */
bool lilygo_usb_msc_available();

/**
 * @brief Show or hide the USB mass-storage interface.
 * @param active true to expose storage to the USB host, false to hide it.
 * @return true if the requested state was applied.
 */
bool lilygo_usb_msc_set_active(bool active);

/**
 * @brief Check whether USB mass storage is currently exposed.
 * @return true when MSC is active.
 */
bool lilygo_usb_msc_is_active();

/**
 * @brief Check whether the host requested a media eject.
 * @return true if an eject event has been observed since the previous poll.
 */
bool lilygo_usb_msc_was_ejected();

/**
 * @brief Poll MSC state and handle deferred host events.
 */
void lilygo_usb_msc_poll();

/**
 * @brief Available storage backends for USB mass storage.
 */
typedef enum {
    LILYGO_USB_MSC_BACKEND_SD = 0, /**< Expose the SD card backend. */
    LILYGO_USB_MSC_BACKEND_FLASH,  /**< Expose the internal flash / FATFS backend. */
} lilygo_usb_msc_backend_t;

/**
 * @brief Select the storage backend used by USB MSC.
 * @param backend Backend to expose.
 * @return true if the backend is available and selected.
 */
bool lilygo_usb_msc_select_backend(lilygo_usb_msc_backend_t backend);

/**
 * @brief Get the currently selected MSC backend.
 * @return Current backend selection.
 */
lilygo_usb_msc_backend_t lilygo_usb_msc_get_backend();

/**
 * @brief Check whether a specific MSC backend is available.
 * @param backend Backend to test.
 * @return true if the backend can be used.
 */
bool lilygo_usb_msc_backend_available(lilygo_usb_msc_backend_t backend);

/**
 * @brief Get a display name for an MSC backend.
 * @param backend Backend value to describe.
 * @return Static backend name string.
 */
const char *lilygo_usb_msc_backend_name(lilygo_usb_msc_backend_t backend);

/**
 * @brief Get the current MSC backend capacity in blocks.
 * @return Number of addressable storage blocks.
 */
uint32_t lilygo_usb_msc_block_count();

/**
 * @brief Get the current MSC backend block size.
 * @return Block size in bytes.
 */
uint16_t lilygo_usb_msc_block_size();

/**
 * @brief Get the current MSC backend capacity in bytes.
 * @return Total capacity in bytes.
 */
uint64_t lilygo_usb_msc_capacity_bytes();

/**
 * @brief Get the current MSC service status text.
 * @return Static status string.
 */
const char *lilygo_usb_msc_status();

/**
 * @brief Check whether HID keyboard output is available.
 * @return true if HID keyboard reports can be sent.
 */
bool lilygo_usb_hid_keyboard_available();

/**
 * @brief Send UTF-8 text as HID keyboard input.
 * @param text Null-terminated text to type.
 * @param char_delay_ms Delay between characters, in milliseconds.
 * @return true if all supported characters were sent.
 */
bool lilygo_usb_hid_keyboard_send_text(const char *text, uint16_t char_delay_ms);

/**
 * @brief Tap one logical HID key.
 * @param key Logical key identifier from LILYGO_USB_KEY_*.
 * @return true if the key was mapped and sent.
 */
bool lilygo_usb_hid_keyboard_tap_key(uint8_t key);

/**
 * @brief Send a modifier + key HID combo.
 * @param modifiers Bit mask made from LILYGO_USB_MOD_* values.
 * @param key Logical key identifier from LILYGO_USB_KEY_*.
 * @return true if the combo was mapped and sent.
 */
bool lilygo_usb_hid_keyboard_combo(uint8_t modifiers, uint8_t key);

/**
 * @brief Release all currently pressed HID keyboard keys.
 */
void lilygo_usb_hid_keyboard_release_all();
