/**
 * @file      LilyGoNfcType2Write.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Preflights Type 2 user-memory copies and verifies every physical page write.
 */
#include "nfc/LilyGoNfcType2Write.h"
#include <string.h>

namespace {
static bool mirrored(const LilyGoNfcType2Dump &dump)
{
    const uint16_t end = dump.pageCount - 5U;
    const uint8_t *config = dump.pages[end + 1U];
    const unsigned mode = config[0] >> 6U;
    const unsigned length = mode == 1 ? 14 : mode == 2 ? 6 : mode == 3 ? 21 : 0;
    const unsigned start = unsigned(config[2]) * 4U + ((config[0] >> 4U) & 3U);
    return length && config[2] >= 4 && start + length <= unsigned(end) * 4U;
}
static bool locked(const LilyGoNfcType2Dump &dump, unsigned page)
{
    if (page < 16) {
        const uint16_t locks = dump.pages[2][2] | (uint16_t(dump.pages[2][3]) << 8U);
        return (locks & (1U << page)) != 0;
    }
    const uint16_t dynamic = dump.pageCount - 5U;
    const unsigned group = dump.model == LILYGO_NFC_TYPE2_NTAG213 ? 2U : 16U;
    const unsigned bit = (page - 16U) / group;
    const uint16_t locks = dump.pages[dynamic][0] | (uint16_t(dump.pages[dynamic][1]) << 8U);
    return (locks & (1U << bit)) != 0;
}
} // namespace

LilyGoNfcType2WriteStatus lilygoNfcType2CheckWriteSource(const LilyGoNfcType2Dump &source, uint16_t *failedPage)
{
    if (failedPage) *failedPage = UINT16_MAX;
    if (!lilygoNfcType2DumpValid(source)) return LILYGO_NFC_TYPE2_WRITE_INVALID_IMAGE;
    if (!source.geometryKnown) return LILYGO_NFC_TYPE2_WRITE_UNKNOWN_MODEL;
    const uint16_t end = source.pageCount - 5U;
    for (uint16_t page = 3; page <= end + 1U; ++page) {
        if (page == end) continue; // The source's lock bytes are not copied.
        if (!source.pageRead[page]) {
            if (failedPage) *failedPage = page;
            return LILYGO_NFC_TYPE2_WRITE_MISSING_SOURCE;
        }
    }
    return mirrored(source) ? LILYGO_NFC_TYPE2_WRITE_SOURCE_MIRROR : LILYGO_NFC_TYPE2_WRITE_READY;
}

const char *lilygoNfcType2WriteStatusText(LilyGoNfcType2WriteStatus status)
{
    switch (status) {
    case LILYGO_NFC_TYPE2_WRITE_READY: return "Ready to write Type 2 user pages";
    case LILYGO_NFC_TYPE2_WRITE_COMPLETE: return "Type 2 write complete";
    case LILYGO_NFC_TYPE2_WRITE_INVALID_IMAGE: return "Invalid Type 2 image";
    case LILYGO_NFC_TYPE2_WRITE_UNKNOWN_MODEL: return "Physical writing requires a supported NTAG21x model";
    case LILYGO_NFC_TYPE2_WRITE_MISSING_SOURCE: return "Image is missing user data or required metadata";
    case LILYGO_NFC_TYPE2_WRITE_SOURCE_MIRROR: return "Image contains mirrored data; physical bytes are unknown";
    case LILYGO_NFC_TYPE2_WRITE_INVALID_TARGET: return "Target identity or required pages are unavailable";
    case LILYGO_NFC_TYPE2_WRITE_MODEL_MISMATCH: return "Target model does not match the image";
    case LILYGO_NFC_TYPE2_WRITE_CC_MISMATCH: return "Target capability container does not match the image";
    case LILYGO_NFC_TYPE2_WRITE_TARGET_MIRROR: return "Target has an active memory mirror";
    case LILYGO_NFC_TYPE2_WRITE_LOCKED: return "A target user page is locked or read-only";
    case LILYGO_NFC_TYPE2_WRITE_PROTECTED: return "A target user page requires password authentication";
    case LILYGO_NFC_TYPE2_WRITE_READ_FAILED: return "Target page read failed";
    case LILYGO_NFC_TYPE2_WRITE_TARGET_CHANGED: return "Target data changed after preflight";
    case LILYGO_NFC_TYPE2_WRITE_COMMAND_FAILED: return "Type 2 WRITE was not acknowledged";
    case LILYGO_NFC_TYPE2_WRITE_VERIFY_FAILED: return "Written page did not pass readback verification";
    default: return "Unknown Type 2 write status";
    }
}

LilyGoNfcType2WriteResult lilygoWriteNfcType2UserPages(const LilyGoNfcType2Dump &source,
        const LilyGoNfcType2Dump &target, const LilyGoNfcType2WriteIo &io)
{
    LilyGoNfcType2WriteResult result = {};
    result.status = lilygoNfcType2CheckWriteSource(source, &result.failedPage);
    if (result.status != LILYGO_NFC_TYPE2_WRITE_READY) return result;
    result.status = LILYGO_NFC_TYPE2_WRITE_INVALID_TARGET;
    if (!io.readPage || !io.writePage || !lilygoNfcType2DumpValid(target)) return result;
    if (source.model != target.model || source.pageCount != target.pageCount || !target.geometryKnown) {
        result.status = LILYGO_NFC_TYPE2_WRITE_MODEL_MISMATCH;
        return result;
    }
    const uint16_t end = source.pageCount - 5U;
    for (uint16_t page = 0; page <= end + 2U; ++page) {
        if (!target.pageRead[page]) { result.failedPage = page; return result; }
    }
    if (!lilygoNfcType2CanEmulate(target)) return result; // Verify anticollision UID against manufacturer pages.
    if (memcmp(source.pages[3], target.pages[3], 3)) {
        result.status = LILYGO_NFC_TYPE2_WRITE_CC_MISMATCH;
        return result;
    }
    if (mirrored(target)) { result.status = LILYGO_NFC_TYPE2_WRITE_TARGET_MIRROR; return result; }
    bool changed[LILYGO_NFC_TYPE2_MAX_PAGES] = {};
    for (uint16_t page = 4; page < end; ++page) {
        if (!memcmp(source.pages[page], target.pages[page], 4)) { ++result.unchangedPages; continue; }
        changed[page] = true;
        ++result.plannedPages;
        result.failedPage = page;
        if (locked(target, page) || (target.pages[3][0] == 0xE1 && (target.pages[3][3] & 0x0FU))) {
            result.status = LILYGO_NFC_TYPE2_WRITE_LOCKED;
            return result;
        }
        if (page >= target.pages[end + 1U][3]) {
            result.status = LILYGO_NFC_TYPE2_WRITE_PROTECTED;
            return result;
        }
    }
    // No EEPROM changes occur before all pages have passed the preflight.
    for (uint16_t page = 4; page < end; ++page) {
        if (!changed[page]) continue;
        result.failedPage = page;
        uint8_t data[4];
        result.transportError = io.readPage(io.context, uint8_t(page), data);
        if (result.transportError) { result.status = LILYGO_NFC_TYPE2_WRITE_READ_FAILED; return result; }
        if (memcmp(data, target.pages[page], 4)) { result.status = LILYGO_NFC_TYPE2_WRITE_TARGET_CHANGED; return result; }
        ++result.attemptedPages;
        result.transportError = io.writePage(io.context, uint8_t(page), source.pages[page]);
        if (result.transportError) { result.status = LILYGO_NFC_TYPE2_WRITE_COMMAND_FAILED; return result; }
        ++result.writtenPages;
        result.transportError = io.readPage(io.context, uint8_t(page), data);
        if (result.transportError) { result.status = LILYGO_NFC_TYPE2_WRITE_READ_FAILED; return result; }
        if (memcmp(data, source.pages[page], 4)) { result.status = LILYGO_NFC_TYPE2_WRITE_VERIFY_FAILED; return result; }
        ++result.verifiedPages;
    }
    result.failedPage = UINT16_MAX;
    result.status = LILYGO_NFC_TYPE2_WRITE_COMPLETE;
    return result;
}
