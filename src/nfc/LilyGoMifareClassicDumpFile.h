/**
 * @file      LilyGoMifareClassicDumpFile.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Declares the portable SD-card file format used for MIFARE Classic dumps.
 */
#pragma once

#if defined(ARDUINO) && \
    (defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA) || \
     defined(ARDUINO_T_EMBED_PLUS_V2)) && \
    !defined(USING_ST25R3916)
#define USING_ST25R3916
#endif

#if defined(ARDUINO) && defined(USING_ST25R3916)

#include <FS.h>

#include "nfc/LilyGoMifareClassic.h"

/** Current on-card MIFARE Classic dump format version. */
#define LILYGO_MIFARE_CLASSIC_DUMP_FILE_VERSION 2U

/**
 * @brief Save a MIFARE Classic dump to a filesystem path.
 *
 * The file contains the UID, ATQA, SAK, card geometry, discovered keys, a read bitmap,
 * and all block buffers. Unread blocks remain marked unavailable, so a partial
 * scan cannot be mistaken for a complete card image.
 *
 * @param filesystem Destination filesystem, normally SD.
 * @param path Absolute filesystem path.
 * @param dump Dump to serialize.
 * @return true when the complete file was written and closed successfully.
 */
bool lilygoSaveMifareClassicDump(fs::FS &filesystem,
                                 const char *path,
                                 const LilyGoMifareClassicDump &dump);

/**
 * @brief Load a MIFARE Classic dump from a filesystem path.
 * @param filesystem Source filesystem, normally SD.
 * @param path Absolute filesystem path.
 * @param dump Destination dump structure.
 * @return true when the file is valid and was loaded successfully.
 */
bool lilygoLoadMifareClassicDump(fs::FS &filesystem,
                                 const char *path,
                                 LilyGoMifareClassicDump &dump);

/**
 * @brief Check whether a dump contains every block expected for its SAK.
 * @param dump Dump to inspect.
 * @return true only for a complete dump.
 */
bool lilygoMifareClassicDumpIsComplete(const LilyGoMifareClassicDump &dump);

#endif
