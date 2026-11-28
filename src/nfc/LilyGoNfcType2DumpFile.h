/**
 * @file      LilyGoNfcType2DumpFile.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Provides filesystem storage for versioned Type 2 card images.
 */
#pragma once
#include "nfc/LilyGoNfcType2.h"

#if defined(ARDUINO)
#include <FS.h>
/** @brief Save a CRC-protected Type 2 image to an absolute filesystem path. */
bool lilygoSaveNfcType2Dump(fs::FS &filesystem, const char *path, const LilyGoNfcType2Dump &dump);
/** @brief Load and validate a Type 2 image; clear the destination on failure. */
bool lilygoLoadNfcType2Dump(fs::FS &filesystem, const char *path, LilyGoNfcType2Dump &dump);
#endif
