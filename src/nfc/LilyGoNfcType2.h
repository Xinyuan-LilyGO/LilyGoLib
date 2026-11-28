/**
 * @file      LilyGoNfcType2.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Defines Type 2 card images, their portable file format and command handling.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** Maximum pages in a single-sector Type 2 image. */
#define LILYGO_NFC_TYPE2_MAX_PAGES 256U
/** Bytes in one Type 2 memory page. */
#define LILYGO_NFC_TYPE2_PAGE_SIZE 4U
/** Serialized image size, including metadata, validity flags and CRC32. */
#define LILYGO_NFC_TYPE2_FILE_SIZE (96U + 256U + 1024U + 4U)

/** Type 2 geometries positively identified by GET_VERSION. */
enum LilyGoNfcType2Model : uint8_t {
    LILYGO_NFC_TYPE2_GENERIC = 0, /**< Geometry has not been positively identified. */
    LILYGO_NFC_TYPE2_NTAG213,     /**< 45 physical pages. */
    LILYGO_NFC_TYPE2_NTAG215,     /**< 135 physical pages. */
    LILYGO_NFC_TYPE2_NTAG216,     /**< 231 physical pages. */
};

/**
 * @brief Captured Type 2 identity, optional responses and readable pages.
 *
 * pageRead distinguishes missing pages from pages whose contents are zero.
 * Password and PACK are not disclosed by normal card reads; credentialsKnown
 * must only be set when their actual values have been supplied separately.
 */
struct LilyGoNfcType2Dump {
    uint8_t uid[7];                   /**< Anticollision UID bytes. */
    uint8_t uidLen;                   /**< Four or seven UID bytes. */
    uint8_t atqa[2];                  /**< ATQA bytes in RFAL wire order. */
    uint8_t sak;                      /**< Final cascade SAK. */
    LilyGoNfcType2Model model;        /**< Positively identified model, if any. */
    uint16_t pageCount;               /**< Captured address-space bound. */
    uint16_t pagesRead;               /**< Number of available pages. */
    bool geometryKnown;              /**< True only for a recognized geometry. */
    bool versionValid;               /**< GET_VERSION response was captured. */
    bool signatureValid;             /**< READ_SIG response was captured. */
    bool counterValid;               /**< READ_CNT index 2 was captured. */
    bool credentialsKnown;           /**< Actual password and PACK are available. */
    uint8_t version[8];              /**< Exact GET_VERSION response. */
    uint8_t signature[32];           /**< Exact originality signature response. */
    uint8_t counter[3];              /**< Little-endian 24-bit NFC counter. */
    uint8_t password[4];             /**< Separately supplied password. */
    uint8_t pack[2];                 /**< Separately supplied password acknowledgement. */
    bool pageRead[LILYGO_NFC_TYPE2_MAX_PAGES]; /**< Per-page availability. */
    uint8_t pages[LILYGO_NFC_TYPE2_MAX_PAGES][LILYGO_NFC_TYPE2_PAGE_SIZE]; /**< Page bytes. */
};

/** Volatile reader-session state; reset on RF field loss. */
struct LilyGoNfcType2Session {
    bool authenticated;             /**< Password was verified in this session. */
    bool counterIncremented;        /**< Automatic counter increment already applied. */
    bool compatWritePending;        /**< Waiting for COMPATIBILITY_WRITE stage two. */
    uint8_t compatWritePage;         /**< Destination of the pending compatibility write. */
};

/** Command result; the transport generates CRC only for byte-aligned responses. */
struct LilyGoNfcType2Response {
    uint16_t bits;                  /**< Response length, including four-bit ACK/NAK. */
    bool sleep;                    /**< HLTA requests the low-level sleep state. */
    bool read;                     /**< A memory read completed successfully. */
    bool modified;                 /**< User memory changed in the volatile image. */
};

/** @brief Identify the model and page count from an exact GET_VERSION response. */
LilyGoNfcType2Model lilygoNfcType2Identify(const uint8_t version[8], uint16_t *pageCount);
/** @brief Return a short model name suitable for user interfaces. */
const char *lilygoNfcType2ModelName(LilyGoNfcType2Model model);
/** @brief Validate identity, geometry and page-availability counts. */
bool lilygoNfcType2DumpValid(const LilyGoNfcType2Dump &dump);
/** @brief Check whether every page in a positively identified geometry was read. */
bool lilygoNfcType2DumpComplete(const LilyGoNfcType2Dump &dump);
/** @brief Check whether the image has a consistent identity and its first four pages. */
bool lilygoNfcType2CanEmulate(const LilyGoNfcType2Dump &dump);
/** @brief Encode a versioned, CRC-protected image; return zero on invalid input. */
size_t lilygoNfcType2Encode(const LilyGoNfcType2Dump &dump, uint8_t *data, size_t capacity);
/** @brief Decode and validate an entire image; clear the destination on failure. */
bool lilygoNfcType2Decode(const uint8_t *data, size_t length, LilyGoNfcType2Dump &dump);
/**
 * @brief Process a CRC-checked Type 2 command without its CRC bytes.
 * @param dump Volatile card image. The source file is never modified.
 * @param session Reader-session authentication and pending command state.
 * @param command Command bytes after CRC and parity removal.
 * @param length Command length in bytes.
 * @param response Response buffer, at least 16 bytes for READ.
 * @param capacity Response buffer capacity in bytes.
 * @param allowUserWrites Allow unlocked user-page writes to RAM only.
 * @return Response length and lifecycle flags. Unknown data is never synthesized.
 */
LilyGoNfcType2Response lilygoNfcType2Command(LilyGoNfcType2Dump &dump,
        LilyGoNfcType2Session &session, const uint8_t *command, size_t length,
        uint8_t *response, size_t capacity, bool allowUserWrites = false);
