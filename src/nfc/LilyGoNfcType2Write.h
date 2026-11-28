/**
 * @file      LilyGoNfcType2Write.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Defines checked physical Type 2 user-memory restoration and its transport interface.
 */
#pragma once
#include "nfc/LilyGoNfcType2.h"

/** Outcome of image validation, target preflight or physical restoration. */
enum LilyGoNfcType2WriteStatus : uint8_t {
    LILYGO_NFC_TYPE2_WRITE_READY = 0,   /**< Source can be offered for writing. */
    LILYGO_NFC_TYPE2_WRITE_COMPLETE,    /**< Every requested user page matches the image. */
    LILYGO_NFC_TYPE2_WRITE_INVALID_IMAGE, /**< Image metadata is invalid. */
    LILYGO_NFC_TYPE2_WRITE_UNKNOWN_MODEL, /**< No supported physical memory geometry. */
    LILYGO_NFC_TYPE2_WRITE_MISSING_SOURCE, /**< A required source page is missing. */
    LILYGO_NFC_TYPE2_WRITE_SOURCE_MIRROR, /**< Source read responses contain a live mirror. */
    LILYGO_NFC_TYPE2_WRITE_INVALID_TARGET, /**< Target identity or required pages are unavailable. */
    LILYGO_NFC_TYPE2_WRITE_MODEL_MISMATCH, /**< Target model differs from the image. */
    LILYGO_NFC_TYPE2_WRITE_CC_MISMATCH,  /**< Capability containers advertise different layouts. */
    LILYGO_NFC_TYPE2_WRITE_TARGET_MIRROR, /**< Target has an active virtual-memory mirror. */
    LILYGO_NFC_TYPE2_WRITE_LOCKED,      /**< At least one changed user page is locked. */
    LILYGO_NFC_TYPE2_WRITE_PROTECTED,   /**< A changed user page requires target authentication. */
    LILYGO_NFC_TYPE2_WRITE_READ_FAILED, /**< A physical page read failed. */
    LILYGO_NFC_TYPE2_WRITE_TARGET_CHANGED, /**< Target page changed after preflight. */
    LILYGO_NFC_TYPE2_WRITE_COMMAND_FAILED, /**< Physical WRITE did not acknowledge success. */
    LILYGO_NFC_TYPE2_WRITE_VERIFY_FAILED, /**< Readback differs from the requested bytes. */
};

/** Physical restoration counters; a failed command may still have changed EEPROM. */
struct LilyGoNfcType2WriteResult {
    LilyGoNfcType2WriteStatus status; /**< Final outcome. */
    uint16_t plannedPages;           /**< Changed user pages identified during preflight. */
    uint16_t unchangedPages;         /**< User pages already matching the image. */
    uint16_t attemptedPages;         /**< WRITE commands sent, including failed commands. */
    uint16_t writtenPages;           /**< WRITE commands acknowledged by the target. */
    uint16_t verifiedPages;          /**< Changed pages whose readback was verified. */
    uint16_t failedPage;             /**< Failing page, or UINT16_MAX when not page-specific. */
    uint16_t transportError;         /**< Transport return code, zero on success. */
};

/** Page operations for the already selected target; zero means transport success. */
struct LilyGoNfcType2WriteIo {
    void *context; /**< User context passed to both operations. */
    uint16_t (*readPage)(void *context, uint8_t page, uint8_t data[4]); /**< Read exactly the addressed page. */
    uint16_t (*writePage)(void *context, uint8_t page, const uint8_t data[4]); /**< WRITE with ACK validation. */
};

/** @brief Check supported geometry, complete user data and non-mirrored source memory. */
LilyGoNfcType2WriteStatus lilygoNfcType2CheckWriteSource(const LilyGoNfcType2Dump &source,
        uint16_t *failedPage = nullptr);
/** @brief Return a concise UI description for a physical-write status. */
const char *lilygoNfcType2WriteStatusText(LilyGoNfcType2WriteStatus status);
/**
 * @brief Restore changed user pages after validating the entire source and target.
 * @param source Captured image. Manufacturer, lock and configuration pages are never written.
 * @param target Fresh snapshot of the selected target, including lock/configuration pages.
 * @param io Transport for the same selected card; it must not select another card on failure.
 * @return Counts and failure detail. Every WRITE is followed by a readback check.
 */
LilyGoNfcType2WriteResult lilygoWriteNfcType2UserPages(const LilyGoNfcType2Dump &source,
        const LilyGoNfcType2Dump &target, const LilyGoNfcType2WriteIo &io);
