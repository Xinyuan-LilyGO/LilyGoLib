/**
 * @file      LilyGoNfcType2.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Serializes Type 2 card images and implements their memory command responses.
 */
#include "nfc/LilyGoNfcType2.h"
#include <string.h>

namespace {
constexpr uint8_t kMagic[8] = {'L', 'G', 'T', '2', 'D', '0', '1', 0};
constexpr size_t kHeaderSize = 96U;
constexpr size_t kPagesOffset = kHeaderSize + LILYGO_NFC_TYPE2_MAX_PAGES;

static uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8U); }
static void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8U); }
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFU;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}
static uint16_t modelPages(LilyGoNfcType2Model model)
{
    switch (model) {
    case LILYGO_NFC_TYPE2_NTAG213: return 45;
    case LILYGO_NFC_TYPE2_NTAG215: return 135;
    case LILYGO_NFC_TYPE2_NTAG216: return 231;
    default: return 0;
    }
}
static uint16_t configPage(const LilyGoNfcType2Dump &dump)
{
    const uint16_t pages = modelPages(dump.model);
    return pages ? pages - 4U : 0U;
}
static bool configuration(const LilyGoNfcType2Dump &dump, uint8_t &auth0, uint8_t &access)
{
    const uint16_t cfg = configPage(dump);
    if (!cfg || !dump.pageRead[cfg] || !dump.pageRead[cfg + 1U]) return false;
    auth0 = dump.pages[cfg][3];
    access = dump.pages[cfg + 1U][0];
    return true;
}
static uint16_t readLimit(const LilyGoNfcType2Dump &dump, const LilyGoNfcType2Session &session)
{
    uint8_t auth0 = 0xFF, access = 0;
    if (!session.authenticated && configuration(dump, auth0, access) &&
            (access & 0x80U) && auth0 < dump.pageCount) return auth0;
    return dump.pageCount;
}
static bool canWrite(const LilyGoNfcType2Dump &dump, const LilyGoNfcType2Session &session, uint16_t page)
{
    const uint16_t cfg = configPage(dump);
    // Manufacturer, CC, lock and configuration pages remain immutable.
    if (!cfg || page < 4U || page >= cfg - 1U || !dump.pageRead[page] || !dump.pageRead[2]) return false;
    uint8_t auth0 = 0xFF, access = 0;
    if (!configuration(dump, auth0, access)) return false;
    if (!session.authenticated && page >= auth0) return false;
    if (page < 16U) {
        const uint16_t locks = get16(&dump.pages[2][2]);
        return (locks & (1U << page)) == 0U;
    }
    const uint16_t dynamic = cfg - 1U;
    if (!dump.pageRead[dynamic]) return false;
    const unsigned group = dump.model == LILYGO_NFC_TYPE2_NTAG213 ? 2U : 16U;
    const unsigned bit = (page - 16U) / group;
    return bit < 16U && (get16(dump.pages[dynamic]) & (1U << bit)) == 0U;
}
static void incrementCounter(LilyGoNfcType2Dump &dump, LilyGoNfcType2Session &session)
{
    uint8_t auth0 = 0, access = 0;
    if (session.counterIncremented || !dump.counterValid ||
            !configuration(dump, auth0, access) || !(access & 0x10U)) return;
    uint32_t value = dump.counter[0] | (uint32_t(dump.counter[1]) << 8U) | (uint32_t(dump.counter[2]) << 16U);
    if (value < 0xFFFFFFU) ++value;
    for (unsigned i = 0; i < 3; ++i) dump.counter[i] = uint8_t(value >> (8U * i));
    session.counterIncremented = true;
}
static uint8_t readByte(const LilyGoNfcType2Dump &dump, const LilyGoNfcType2Session &session, size_t address)
{
    const uint16_t cfg = configPage(dump);
    if (cfg && (address / 4U == cfg + 2U ||
            (address / 4U == cfg + 3U && address % 4U < 2U))) return 0;
    if (cfg && dump.pageRead[cfg]) {
        const uint8_t *config = dump.pages[cfg];
        const unsigned mode = config[0] >> 6U;
        const size_t start = size_t(config[2]) * 4U + ((config[0] >> 4U) & 3U);
        const size_t mirrorLength = mode == 1U ? 14U : mode == 2U ? 6U : mode == 3U ? 21U : 0U;
        if (config[2] >= 4U && start + mirrorLength <= size_t(cfg - 1U) * 4U &&
                address >= start && address < start + mirrorLength) {
            static const char hex[] = "0123456789ABCDEF";
            size_t index = address - start;
            if ((mode == 1U || mode == 3U) && index < 14U) {
                return hex[(dump.uid[index / 2U] >> ((index & 1U) ? 0U : 4U)) & 15U];
            }
            if (mode == 3U) {
                if (index == 14U) return 'x';
                index -= 15U;
            }
            uint8_t auth0 = 0, access = 0;
            const bool counterProtected = configuration(dump, auth0, access) && (access & 8U);
            if (dump.counterValid && index < 6U && (!counterProtected || session.authenticated)) {
                return hex[(dump.counter[2U - index / 2U] >> ((index & 1U) ? 0U : 4U)) & 15U];
            }
        }
    }
    return dump.pages[address / 4U][address % 4U];
}
} // namespace

LilyGoNfcType2Model lilygoNfcType2Identify(const uint8_t version[8], uint16_t *pageCount)
{
    LilyGoNfcType2Model model = LILYGO_NFC_TYPE2_GENERIC;
    if (version && version[0] == 0 && version[1] == 4 && version[2] == 4 && version[3] == 2 &&
            version[4] == 1 && version[5] == 0 && version[7] == 3) {
        if (version[6] == 0x0F) model = LILYGO_NFC_TYPE2_NTAG213;
        else if (version[6] == 0x11) model = LILYGO_NFC_TYPE2_NTAG215;
        else if (version[6] == 0x13) model = LILYGO_NFC_TYPE2_NTAG216;
    }
    if (pageCount) *pageCount = modelPages(model);
    return model;
}

const char *lilygoNfcType2ModelName(LilyGoNfcType2Model model)
{
    switch (model) {
    case LILYGO_NFC_TYPE2_NTAG213: return "NTAG213";
    case LILYGO_NFC_TYPE2_NTAG215: return "NTAG215";
    case LILYGO_NFC_TYPE2_NTAG216: return "NTAG216";
    default: return "Generic Type 2";
    }
}

bool lilygoNfcType2DumpValid(const LilyGoNfcType2Dump &dump)
{
    if ((dump.uidLen != 4 && dump.uidLen != 7) || dump.sak != 0 ||
            dump.pageCount < 4 || dump.pageCount > LILYGO_NFC_TYPE2_MAX_PAGES ||
            dump.model > LILYGO_NFC_TYPE2_NTAG216) return false;
    if (dump.geometryKnown != (dump.model != LILYGO_NFC_TYPE2_GENERIC)) return false;
    if (dump.geometryKnown && (!dump.versionValid || dump.uidLen != 7 ||
            modelPages(dump.model) != dump.pageCount ||
            lilygoNfcType2Identify(dump.version, nullptr) != dump.model)) return false;
    unsigned count = 0;
    for (unsigned i = 0; i < LILYGO_NFC_TYPE2_MAX_PAGES; ++i) {
        if (dump.pageRead[i]) {
            if (i >= dump.pageCount) return false;
            ++count;
        }
    }
    return count == dump.pagesRead && count > 0;
}

bool lilygoNfcType2DumpComplete(const LilyGoNfcType2Dump &dump)
{
    return lilygoNfcType2DumpValid(dump) && dump.geometryKnown && dump.pagesRead == dump.pageCount;
}

bool lilygoNfcType2CanEmulate(const LilyGoNfcType2Dump &dump)
{
    if (!lilygoNfcType2DumpValid(dump)) return false;
    if ((dump.atqa[0] >> 6U) != (dump.uidLen == 7 ? 1U : 0U)) return false;
    for (unsigned i = 0; i < 4; ++i) if (!dump.pageRead[i]) return false;
    if (dump.uidLen == 7) {
        if (memcmp(dump.uid, dump.pages[0], 3) || memcmp(&dump.uid[3], dump.pages[1], 4)) return false;
        if (dump.pages[0][3] != uint8_t(0x88U ^ dump.uid[0] ^ dump.uid[1] ^ dump.uid[2]) ||
                dump.pages[2][0] != uint8_t(dump.uid[3] ^ dump.uid[4] ^ dump.uid[5] ^ dump.uid[6])) return false;
    }
    return true;
}

size_t lilygoNfcType2Encode(const LilyGoNfcType2Dump &dump, uint8_t *data, size_t capacity)
{
    if (!data || capacity < LILYGO_NFC_TYPE2_FILE_SIZE || !lilygoNfcType2DumpValid(dump)) return 0;
    memset(data, 0, LILYGO_NFC_TYPE2_FILE_SIZE);
    memcpy(data, kMagic, sizeof(kMagic));
    put16(data + 8, 1); put16(data + 10, kHeaderSize);
    put16(data + 12, dump.pageCount); put16(data + 14, dump.pagesRead);
    data[16] = dump.uidLen; data[17] = dump.sak;
    memcpy(data + 18, dump.atqa, 2); data[20] = uint8_t(dump.model);
    data[21] = (dump.geometryKnown ? 1U : 0U) | (dump.versionValid ? 2U : 0U) |
               (dump.signatureValid ? 4U : 0U) | (dump.counterValid ? 8U : 0U) |
               (dump.credentialsKnown ? 16U : 0U);
    memcpy(data + 22, dump.uid, dump.uidLen);
    if (dump.versionValid) memcpy(data + 29, dump.version, 8);
    if (dump.signatureValid) memcpy(data + 37, dump.signature, 32);
    if (dump.counterValid) memcpy(data + 69, dump.counter, 3);
    if (dump.credentialsKnown) {
        memcpy(data + 72, dump.password, 4); memcpy(data + 76, dump.pack, 2);
    }
    for (unsigned i = 0; i < dump.pageCount; ++i) {
        data[kHeaderSize + i] = dump.pageRead[i] ? 1 : 0;
        if (dump.pageRead[i]) memcpy(data + kPagesOffset + i * 4U, dump.pages[i], 4);
    }
    const uint32_t crc = crc32(data, LILYGO_NFC_TYPE2_FILE_SIZE - 4U);
    for (unsigned i = 0; i < 4; ++i) data[LILYGO_NFC_TYPE2_FILE_SIZE - 4U + i] = uint8_t(crc >> (i * 8U));
    return LILYGO_NFC_TYPE2_FILE_SIZE;
}

bool lilygoNfcType2Decode(const uint8_t *data, size_t length, LilyGoNfcType2Dump &dump)
{
    memset(&dump, 0, sizeof(dump));
    if (!data || length != LILYGO_NFC_TYPE2_FILE_SIZE || memcmp(data, kMagic, 8) ||
            get16(data + 8) != 1 || get16(data + 10) != kHeaderSize || (data[21] & 0xE0U)) return false;
    const uint32_t crc = crc32(data, length - 4U);
    for (unsigned i = 0; i < 4; ++i) if (data[length - 4U + i] != uint8_t(crc >> (i * 8U))) return false;
    for (unsigned i = 78; i < kHeaderSize; ++i) if (data[i]) return false;
    dump.pageCount = get16(data + 12); dump.pagesRead = get16(data + 14);
    dump.uidLen = data[16]; dump.sak = data[17];
    memcpy(dump.atqa, data + 18, 2); dump.model = LilyGoNfcType2Model(data[20]);
    dump.geometryKnown = data[21] & 1U; dump.versionValid = data[21] & 2U;
    dump.signatureValid = data[21] & 4U; dump.counterValid = data[21] & 8U;
    dump.credentialsKnown = data[21] & 16U;
    memcpy(dump.uid, data + 22, 7); memcpy(dump.version, data + 29, 8);
    memcpy(dump.signature, data + 37, 32); memcpy(dump.counter, data + 69, 3);
    if (dump.credentialsKnown) { memcpy(dump.password, data + 72, 4); memcpy(dump.pack, data + 76, 2); }
    for (unsigned i = 0; i < LILYGO_NFC_TYPE2_MAX_PAGES; ++i) {
        if (data[kHeaderSize + i] > 1) { memset(&dump, 0, sizeof(dump)); return false; }
        dump.pageRead[i] = data[kHeaderSize + i] != 0;
        if (dump.pageRead[i]) memcpy(dump.pages[i], data + kPagesOffset + i * 4U, 4);
    }
    if (lilygoNfcType2DumpValid(dump)) return true;
    memset(&dump, 0, sizeof(dump));
    return false;
}

LilyGoNfcType2Response lilygoNfcType2Command(LilyGoNfcType2Dump &dump,
        LilyGoNfcType2Session &session, const uint8_t *command, size_t length,
        uint8_t *response, size_t capacity, bool allowUserWrites)
{
    LilyGoNfcType2Response result = {};
    if (!command || !length || !response || !capacity ||
            dump.pageCount < 4 || dump.pageCount > LILYGO_NFC_TYPE2_MAX_PAGES) return result;
    auto nak = [&](uint8_t code = 0U) { response[0] = code; result.bits = 4; return result; };
    auto ack = [&]() { response[0] = 0x0A; result.bits = 4; return result; };
    auto copy = [&](const uint8_t *data, size_t count) {
        if (count > capacity) return nak();
        memcpy(response, data, count); result.bits = uint16_t(count * 8U); return result;
    };
    if (length == 2 && command[0] == 0x50 && command[1] == 0) {
        session.authenticated = false; session.compatWritePending = false;
        result.sleep = true; return result;
    }
    if (session.compatWritePending) {
        session.compatWritePending = false;
        if (length != 16 || !allowUserWrites || !canWrite(dump, session, session.compatWritePage)) return nak();
        memcpy(dump.pages[session.compatWritePage], command, 4);
        result.modified = true; return ack();
    }
    switch (command[0]) {
    case 0x60:
        if (length == 1 && dump.versionValid) return copy(dump.version, 8);
        return nak();
    case 0x3C:
        if (length == 2 && command[1] == 0 && dump.signatureValid) return copy(dump.signature, 32);
        return nak();
    case 0x39: {
        uint8_t auth0 = 0, access = 0;
        if (length != 2 || command[1] != 2 || !dump.counterValid) return nak();
        if (configuration(dump, auth0, access) && (access & 8U) && !session.authenticated) return nak(4);
        return copy(dump.counter, 3);
    }
    case 0x1B: {
        session.authenticated = false;
        if (length != 5 || !dump.credentialsKnown) return nak(4);
        uint8_t difference = 0;
        for (unsigned i = 0; i < 4; ++i) difference |= command[i + 1U] ^ dump.password[i];
        if (difference) { session.authenticated = false; return nak(4); }
        session.authenticated = true;
        return copy(dump.pack, 2);
    }
    case 0x30:
    case 0x3A: {
        const bool fast = command[0] == 0x3A;
        if (length != (fast ? 3U : 2U)) return nak();
        const uint16_t start = command[1], limit = readLimit(dump, session);
        if (!limit || start >= limit) return nak();
        const uint16_t count = fast ? uint16_t(command[2]) - start + 1U : 4U;
        if ((fast && (command[2] < start || command[2] >= limit)) || size_t(count) * 4U > capacity) return nak();
        if (!fast && !dump.geometryKnown && start + count > limit) return nak();
        for (unsigned i = 0; i < count; ++i) if (!dump.pageRead[(start + i) % limit]) return nak();
        incrementCounter(dump, session);
        for (unsigned i = 0; i < count * 4U; ++i) response[i] = readByte(dump, session, ((start + i / 4U) % limit) * 4U + i % 4U);
        result.bits = uint16_t(count * 32U); result.read = true;
        return result;
    }
    case 0xA2:
    case 0xA0: {
        const bool compatibility = command[0] == 0xA0;
        if (length != (compatibility ? 2U : 6U) || !allowUserWrites || !canWrite(dump, session, command[1])) return nak();
        if (compatibility) { session.compatWritePending = true; session.compatWritePage = command[1]; }
        else { memcpy(dump.pages[command[1]], command + 2, 4); result.modified = true; }
        return ack();
    }
    default: return nak();
    }
}
