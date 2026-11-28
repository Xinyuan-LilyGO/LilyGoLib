/**
 * @file      LilyGoMifareClassicDumpFile.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Implements the portable SD-card file format used for MIFARE Classic dumps.
 */
#include "nfc/LilyGoMifareClassicDumpFile.h"
#include "LilyGoLog.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)

#include <string.h>

namespace {

static const uint8_t kMagic[8] = {'L', 'G', 'M', 'F', 'C', 'D', '2', 0};
static const uint16_t kHeaderSize = 38U;
static const uint8_t kFlagAttempted = 0x01U;
static const uint8_t kFlagComplete = 0x02U;

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length)
{
    if (!data) return crc;
    while (length--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc;
}

static bool writePart(File &file, const uint8_t *data, size_t length, uint32_t *crc)
{
    if (length == 0U) return true;
    if (!data || file.write(data, length) != length) return false;
    if (crc) *crc = crc32Update(*crc, data, length);
    return true;
}

static bool readPart(File &file, uint8_t *data, size_t length, uint32_t *crc)
{
    if (length == 0U) return true;
    if (!data || file.read(data, length) != length) return false;
    if (crc) *crc = crc32Update(*crc, data, length);
    return true;
}

static void putU16(uint8_t out[2], uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>(value >> 8U);
}

static uint16_t getU16(const uint8_t in[2])
{
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8U);
}

static void putU32(uint8_t out[4], uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    out[3] = static_cast<uint8_t>(value >> 24U);
}

static uint32_t getU32(const uint8_t in[4])
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8U) |
           (static_cast<uint32_t>(in[2]) << 16U) |
           (static_cast<uint32_t>(in[3]) << 24U);
}

static bool validDumpGeometry(const LilyGoMifareClassicDump &dump)
{
    return dump.sectors > 0U &&
           dump.sectors <= LILYGO_MIFARE_CLASSIC_MAX_SECTORS &&
           dump.totalBlocks > 0U &&
           dump.totalBlocks <= LILYGO_MIFARE_CLASSIC_MAX_BLOCKS &&
           dump.uidLen <= LILYGO_NFC_MAX_UID_LEN;
}

static bool readStoredDump(File &file, LilyGoMifareClassicDump &dump)
{
    uint8_t header[kHeaderSize] = {};
    uint32_t crc = 0xFFFFFFFFUL;
    if (!readPart(file, header, sizeof(header), &crc) ||
            memcmp(header, kMagic, sizeof(kMagic)) != 0 ||
            getU16(&header[8]) != LILYGO_MIFARE_CLASSIC_DUMP_FILE_VERSION ||
            getU16(&header[10]) != kHeaderSize) {
        return false;
    }

    memset(&dump, 0, sizeof(dump));
    dump.sak = header[12];
    dump.sectors = header[13];
    dump.totalBlocks = getU16(&header[14]);
    dump.blocksRead = getU16(&header[16]);
    dump.uidLen = header[18];
    dump.attempted = (header[19] & kFlagAttempted) != 0U;
    dump.complete = (header[19] & kFlagComplete) != 0U;
    memcpy(dump.atqa, &header[20], sizeof(dump.atqa));
    memcpy(dump.uid, &header[22], sizeof(dump.uid));

    if (!validDumpGeometry(dump)) return false;

    for (uint8_t sector = 0; sector < LILYGO_MIFARE_CLASSIC_MAX_SECTORS; ++sector) {
        uint8_t keyRecord[14] = {};
        if (!readPart(file, keyRecord, sizeof(keyRecord), &crc)) return false;
        dump.keyAFound[sector] = keyRecord[0] != 0U;
        memcpy(dump.keyA[sector], &keyRecord[1], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
        dump.keyBFound[sector] = keyRecord[7] != 0U;
        memcpy(dump.keyB[sector], &keyRecord[8], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
    }

    uint8_t blockFlags[LILYGO_MIFARE_CLASSIC_MAX_BLOCKS] = {};
    if (!readPart(file, blockFlags, sizeof(blockFlags), &crc) ||
            !readPart(file, reinterpret_cast<uint8_t *>(dump.blocks), sizeof(dump.blocks), &crc)) {
        return false;
    }
    for (uint16_t block = 0; block < LILYGO_MIFARE_CLASSIC_MAX_BLOCKS; ++block) {
        dump.blockRead[block] = blockFlags[block] != 0U;
    }

    uint8_t storedCrc[4] = {};
    if (!readPart(file, storedCrc, sizeof(storedCrc), nullptr) ||
            getU32(storedCrc) != (crc ^ 0xFFFFFFFFUL)) {
        return false;
    }

    uint16_t actualBlocksRead = 0U;
    for (uint16_t block = 0; block < dump.totalBlocks; ++block) {
        if (dump.blockRead[block]) ++actualBlocksRead;
    }
    if (actualBlocksRead != dump.blocksRead) return false;
    dump.authenticatedSectors = 0U;
    for (uint8_t sector = 0; sector < dump.sectors; ++sector) {
        if (dump.keyAFound[sector] || dump.keyBFound[sector]) ++dump.authenticatedSectors;
    }
    dump.complete = dump.complete && lilygoMifareClassicDumpIsComplete(dump);
    return file.available() == 0;
}

}  // namespace

bool lilygoMifareClassicDumpIsComplete(const LilyGoMifareClassicDump &dump)
{
    if (!validDumpGeometry(dump) || dump.blocksRead != dump.totalBlocks) return false;
    for (uint16_t block = 0; block < dump.totalBlocks; ++block) {
        if (!dump.blockRead[block]) return false;
    }
    return true;
}

bool lilygoSaveMifareClassicDump(fs::FS &filesystem,
                                 const char *path,
                                 const LilyGoMifareClassicDump &dump)
{
    if (!path || path[0] == '\0' || !validDumpGeometry(dump)) {
        LILYGO_NFC_LOG_E("[ST25][MFC][DUMP] invalid save arguments");
        return false;
    }

    filesystem.remove(path);
    File file = filesystem.open(path, FILE_WRITE);
    if (!file) {
        LILYGO_NFC_LOG_E("[ST25][MFC][DUMP] open for write failed path=%s", path);
        return false;
    }

    uint8_t header[kHeaderSize] = {};
    memcpy(header, kMagic, sizeof(kMagic));
    putU16(&header[8], LILYGO_MIFARE_CLASSIC_DUMP_FILE_VERSION);
    putU16(&header[10], kHeaderSize);
    header[12] = dump.sak;
    header[13] = dump.sectors;
    putU16(&header[14], dump.totalBlocks);
    putU16(&header[16], dump.blocksRead);
    header[18] = dump.uidLen;
    header[19] = (dump.attempted ? kFlagAttempted : 0U) |
                 (lilygoMifareClassicDumpIsComplete(dump) ? kFlagComplete : 0U);
    memcpy(&header[20], dump.atqa, sizeof(dump.atqa));
    memcpy(&header[22], dump.uid, sizeof(dump.uid));

    uint32_t crc = 0xFFFFFFFFUL;
    bool ok = writePart(file, header, sizeof(header), &crc);
    for (uint8_t sector = 0; ok && sector < LILYGO_MIFARE_CLASSIC_MAX_SECTORS; ++sector) {
        uint8_t keyRecord[14] = {};
        keyRecord[0] = dump.keyAFound[sector] ? 1U : 0U;
        memcpy(&keyRecord[1], dump.keyA[sector], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
        keyRecord[7] = dump.keyBFound[sector] ? 1U : 0U;
        memcpy(&keyRecord[8], dump.keyB[sector], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
        ok = writePart(file, keyRecord, sizeof(keyRecord), &crc);
    }

    uint8_t blockFlags[LILYGO_MIFARE_CLASSIC_MAX_BLOCKS] = {};
    for (uint16_t block = 0; block < LILYGO_MIFARE_CLASSIC_MAX_BLOCKS; ++block) {
        blockFlags[block] = dump.blockRead[block] ? 1U : 0U;
    }
    ok = ok && writePart(file, blockFlags, sizeof(blockFlags), &crc) &&
         writePart(file, reinterpret_cast<const uint8_t *>(dump.blocks), sizeof(dump.blocks), &crc);

    uint8_t storedCrc[4] = {};
    putU32(storedCrc, crc ^ 0xFFFFFFFFUL);
    ok = ok && writePart(file, storedCrc, sizeof(storedCrc), nullptr);
    file.flush();
    const size_t expectedSize = kHeaderSize +
                                (LILYGO_MIFARE_CLASSIC_MAX_SECTORS * 14U) +
                                LILYGO_MIFARE_CLASSIC_MAX_BLOCKS +
                                (LILYGO_MIFARE_CLASSIC_MAX_BLOCKS * LILYGO_MIFARE_CLASSIC_BLOCK_SIZE) +
                                sizeof(storedCrc);
    ok = ok && file.size() == expectedSize;
    file.close();
    LILYGO_NFC_LOG_I("[ST25][MFC][DUMP] save %s path=%s blocks=%u/%u",
                     ok ? "success" : "failed", path,
                     static_cast<unsigned>(dump.blocksRead),
                     static_cast<unsigned>(dump.totalBlocks));
    return ok;
}

bool lilygoLoadMifareClassicDump(fs::FS &filesystem,
                                 const char *path,
                                 LilyGoMifareClassicDump &dump)
{
    if (!path || path[0] == '\0') return false;
    File file = filesystem.open(path, FILE_READ);
    if (!file) {
        LILYGO_NFC_LOG_W("[ST25][MFC][DUMP] open for read failed path=%s", path);
        return false;
    }
    const bool ok = readStoredDump(file, dump);
    file.close();
    if (!ok) {
        memset(&dump, 0, sizeof(dump));
        LILYGO_NFC_LOG_E("[ST25][MFC][DUMP] invalid file path=%s", path);
    } else {
        LILYGO_NFC_LOG_I("[ST25][MFC][DUMP] loaded path=%s blocks=%u/%u complete=%u",
                         path, static_cast<unsigned>(dump.blocksRead),
                         static_cast<unsigned>(dump.totalBlocks),
                         dump.complete ? 1U : 0U);
    }
    return ok;
}

#endif
