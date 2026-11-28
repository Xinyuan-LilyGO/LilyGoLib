/**
 * @file      LilyGoMifareClassic.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Declares Classic dump data shared by authenticated reading and emulation,
 * and the ST25R3916 reader diagnostic entry point.
 */
#pragma once

#if defined(ARDUINO) && \
    (defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA) || \
     defined(ARDUINO_T_EMBED_PLUS_V2)) && \
    !defined(USING_ST25R3916)
#define USING_ST25R3916
#endif

#include <stdint.h>

/** Maximum number of MIFARE Classic sectors supported by the dump structure. */
#define LILYGO_MIFARE_CLASSIC_MAX_SECTORS 40
/** Maximum number of MIFARE Classic blocks supported by the dump structure. */
#define LILYGO_MIFARE_CLASSIC_MAX_BLOCKS 256
/** MIFARE Classic block size in bytes. */
#define LILYGO_MIFARE_CLASSIC_BLOCK_SIZE 16
/** MIFARE Classic key size in bytes. */
#define LILYGO_MIFARE_CLASSIC_KEY_SIZE 6
#ifndef LILYGO_NFC_MAX_UID_LEN
#define LILYGO_NFC_MAX_UID_LEN 16
#endif

/**
 * @brief Authenticated MIFARE Classic memory dump and diagnostic status.
 *
 * The reader only tries the built-in public/default key dictionary. No write
 * operation is performed by this diagnostic.
 */
struct LilyGoMifareClassicDump {
    bool attempted;                                             /**< true after a read was started. */
    bool complete;                                               /**< true when every block was read. */
    uint8_t sak;                                                 /**< NFC-A SAK used for sizing. */
    uint8_t atqa[2];                                             /**< Captured NFC-A ATQA in wire order. */
    uint8_t sectors;                                             /**< Number of sectors on the card. */
    uint8_t authenticatedSectors;                                /**< Sectors with a working key. */
    uint16_t totalBlocks;                                        /**< Expected number of blocks. */
    uint16_t blocksRead;                                         /**< Number of blocks read successfully. */
    uint8_t uid[LILYGO_NFC_MAX_UID_LEN];                         /**< UID captured with the dump. */
    uint8_t uidLen;                                               /**< Number of valid UID bytes. */
    uint8_t keyA[LILYGO_MIFARE_CLASSIC_MAX_SECTORS][LILYGO_MIFARE_CLASSIC_KEY_SIZE]; /**< Keys found. */
    uint8_t keyB[LILYGO_MIFARE_CLASSIC_MAX_SECTORS][LILYGO_MIFARE_CLASSIC_KEY_SIZE]; /**< Keys found. */
    bool keyAFound[LILYGO_MIFARE_CLASSIC_MAX_SECTORS];           /**< Key A availability. */
    bool keyBFound[LILYGO_MIFARE_CLASSIC_MAX_SECTORS];           /**< Key B availability. */
    uint8_t blocks[LILYGO_MIFARE_CLASSIC_MAX_BLOCKS][LILYGO_MIFARE_CLASSIC_BLOCK_SIZE]; /**< Block data. */
    bool blockRead[LILYGO_MIFARE_CLASSIC_MAX_BLOCKS];            /**< Block data availability. */
};

#if defined(ARDUINO) && defined(USING_ST25R3916)
#include "nfc_include.h"

/**
 * @brief Read a MIFARE Classic card using the built-in public/default keys.
 * @param reader Active RFAL NFC reader.
 * @param uid Activated card UID.
 * @param uidLen UID length in bytes.
 * @param sak Activated card SAK.
 * @param atqa Activated card ATQA, two bytes in wire order.
 * @param dump Destination for the authenticated memory dump and status.
 * @return true when at least one block was read successfully.
 */
bool lilygoReadMifareClassic(RfalNfcClass &reader,
                             const uint8_t *uid,
                             uint8_t uidLen,
                             uint8_t sak,
                             const uint8_t atqa[2],
                             LilyGoMifareClassicDump &dump);

#endif
