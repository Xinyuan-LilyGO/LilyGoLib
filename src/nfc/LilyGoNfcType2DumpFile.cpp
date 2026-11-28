/**
 * @file      LilyGoNfcType2DumpFile.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Saves and loads Type 2 card images through Arduino filesystem interfaces.
 */
#include "nfc/LilyGoNfcType2DumpFile.h"
#include "LilyGoLog.h"
#if defined(ARDUINO)
#include <string.h>

bool lilygoSaveNfcType2Dump(fs::FS &filesystem, const char *path, const LilyGoNfcType2Dump &dump)
{
    uint8_t data[LILYGO_NFC_TYPE2_FILE_SIZE];
    const size_t length = lilygoNfcType2Encode(dump, data, sizeof(data));
    if (!path || !length) return false;
    File file = filesystem.open(path, FILE_WRITE);
    if (!file) return false;
    const bool written = file.write(data, length) == length;
    file.flush();
    const bool ok = written && file.size() == length;
    file.close();
    LILYGO_NFC_LOG_I("[ST25][T2][FILE] save ok=%u pages=%u/%u", ok, dump.pagesRead, dump.pageCount);
    return ok;
}

bool lilygoLoadNfcType2Dump(fs::FS &filesystem, const char *path, LilyGoNfcType2Dump &dump)
{
    memset(&dump, 0, sizeof(dump));
    if (!path) return false;
    File file = filesystem.open(path, FILE_READ);
    if (!file || file.size() != LILYGO_NFC_TYPE2_FILE_SIZE) return false;
    uint8_t data[LILYGO_NFC_TYPE2_FILE_SIZE];
    const size_t length = file.read(data, sizeof(data));
    file.close();
    const bool ok = length == sizeof(data) && lilygoNfcType2Decode(data, length, dump);
    LILYGO_NFC_LOG_I("[ST25][T2][FILE] load ok=%u pages=%u/%u", ok, dump.pagesRead, dump.pageCount);
    return ok;
}
#endif
