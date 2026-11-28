/**
 * @file      LilyGoMifareClassicEmulation.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Handles Classic first/nested authentication, encrypted reads and access control.
 */
#include "nfc/LilyGoMifareClassicEmulation.h"
#include <string.h>

namespace {
uint8_t sectorOf(unsigned block) { return block < 128 ? block / 4 : 32 + (block - 128) / 16; }
unsigned trailerOf(unsigned sector) { return sector < 32 ? sector * 4 + 3 : 128 + (sector - 32) * 16 + 15; }
uint32_t word(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void putWord(uint8_t *p, uint32_t value) { for (unsigned i = 0; i < 4; ++i) p[i] = value >> (24 - i * 8); }
bool geometry(const LilyGoMifareClassicDump &d)
{
    if (d.sak == 0x09) return d.totalBlocks == 20 && d.sectors == 5;
    if (d.sak == 0x08 || d.sak == 0x88 || d.sak == 0x28) return d.totalBlocks == 64 && d.sectors == 16;
    if (d.sak == 0x18 || d.sak == 0x38) return d.totalBlocks == 256 && d.sectors == 40;
    return false;
}
void encode(LilyGoMifareClassicSession &s, LilyGoMifareClassicResponse &r, unsigned bytes, bool encrypted)
{
    r.bits = bytes * 8;
    for (unsigned i = 0; i < bytes; ++i) {
        const uint8_t plain = r.data[i];
        if (encrypted) r.data[i] ^= lilygoMifareClassicCrypto1Byte(&s.cipher, 0, false);
        r.parity[i] = lilygoMifareClassicOddParity(plain) ^
                      (encrypted ? lilygoMifareClassicCrypto1FilterBit(&s.cipher) : 0);
    }
}
void reject(LilyGoMifareClassicSession &s, LilyGoMifareClassicResponse &r, LilyGoMifareClassicReject reason)
{
    r.rejected = reason;
    if (!s.authenticated) { s = {}; return; }
    r.bits = 4;
    // A NAK still consumes four keystream clocks; parity is absent.
    for (unsigned i = 0; i < 4; ++i) r.data[0] |= lilygoMifareClassicCrypto1Bit(&s.cipher, 0, false) << i;
}
} // namespace

uint16_t lilygoMifareClassicCrcA(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < length; ++i) {
        uint8_t value = data[i] ^ uint8_t(crc);
        value ^= value << 4;
        crc = (crc >> 8) ^ (uint16_t(value) << 8) ^ (uint16_t(value) << 3) ^ (value >> 4);
    }
    return crc;
}

size_t lilygoMifareClassicEncodePulses(const LilyGoMifareClassicResponse &r,
        uint32_t clockHz, LilyGoMifareClassicPulse *out, size_t capacity)
{
    if (!out || !capacity || clockHz < 2000000 || clockHz > 100000000 ||
            !r.bits || r.bits > sizeof(r.data) * 8 || (r.bits != 4 && r.bits % 8)) return 0;
    size_t count = 0;
    uint32_t units = 0, elapsed = 0;
    bool overflow = false;
    auto pulse = [&](bool high, unsigned duration) {
        units += duration;
        const uint32_t end = (uint64_t(units) * clockHz * 8 + 6780000) / 13560000;
        const uint32_t ticks = end - elapsed;
        elapsed = end;
        if (count && out[count - 1].high == high) out[count - 1].ticks += ticks;
        else if (count < capacity) out[count++] = {uint16_t(ticks), high};
        else overflow = true;
    };
    auto bit = [&](bool one) {
        if (!one) pulse(false, 8);
        for (unsigned i = 0; i < 8; ++i) pulse(!(i & 1), 1);
        if (one) pulse(false, 8);
    };
    bit(true); // PICC start of communication.
    for (unsigned i = 0; i < r.bits; ++i) {
        bit((r.data[i / 8] >> (i % 8)) & 1);
        if (i % 8 == 7) bit(r.parity[i / 8] & 1);
    }
    pulse(false, 16); // No subcarrier during EOF; leave MOSI low.
    return overflow ? 0 : count;
}

int lilygoMifareClassicAccess(const uint8_t trailer[16], uint8_t group)
{
    if (!trailer || group > 3) return -1;
    const unsigned c1 = trailer[7] >> 4, c2 = trailer[8] & 15, c3 = trailer[8] >> 4;
    if (((trailer[6] & 15) ^ c1) != 15 || ((trailer[6] >> 4) ^ c2) != 15 ||
            ((trailer[7] & 15) ^ c3) != 15) return -1;
    return ((c1 >> group & 1) << 2) | ((c2 >> group & 1) << 1) | (c3 >> group & 1);
}

bool lilygoMifareClassicCanEmulate(const LilyGoMifareClassicDump &d)
{
    if (!geometry(d) || d.uidLen != 4 || !d.blockRead[0] || memcmp(d.uid, d.blocks[0], 4) ||
            d.blocks[0][4] != uint8_t(d.uid[0] ^ d.uid[1] ^ d.uid[2] ^ d.uid[3]) ||
            d.blocks[0][5] != d.sak || memcmp(d.blocks[0] + 6, d.atqa, 2) ||
            (d.atqa[0] & 0xC0) || !(d.atqa[0] & 0x1F)) return false;
    unsigned count = 0;
    for (unsigned b = 0; b < LILYGO_MIFARE_CLASSIC_MAX_BLOCKS; ++b) {
        if (!d.blockRead[b]) continue;
        if (b >= d.totalBlocks) return false;
        ++count;
    }
    if (!count || count != d.blocksRead) return false;
    for (unsigned sector = 0; sector < d.sectors; ++sector) {
        const unsigned trailer = trailerOf(sector);
        const int access = d.blockRead[trailer] ? lilygoMifareClassicAccess(d.blocks[trailer], 3) : -1;
        if (access >= 0 && (d.keyAFound[sector] || d.keyBFound[sector])) return true;
    }
    return false;
}

LilyGoMifareClassicResponse lilygoMifareClassicCommand(const LilyGoMifareClassicDump &d,
        LilyGoMifareClassicSession &s, const uint8_t *request, size_t length, uint32_t freshNonce)
{
    LilyGoMifareClassicResponse r = {};
    if (!request || !length || length > sizeof(r.data) || !geometry(d) || d.uidLen != 4) {
        r.rejected = LILYGO_MFC_BAD_FRAME; s = {}; return r;
    }
    if (s.authenticating) {
        s.authenticating = false;
        if (length != 8) { r.rejected = LILYGO_MFC_BAD_FRAME; s = {}; return r; }
        uint8_t answer[4];
        for (unsigned i = 0; i < 8; ++i) {
            const uint8_t ks = lilygoMifareClassicCrypto1Byte(&s.cipher, i < 4 ? request[i] : 0, i < 4);
            if (i >= 4) answer[i - 4] = request[i] ^ ks;
        }
        if (word(answer) != lilygoMifareClassicPrngSuccessor(s.nonce, 64)) {
            r.rejected = LILYGO_MFC_AUTH_FAILED; s = {}; return r;
        }
        putWord(r.data, lilygoMifareClassicPrngSuccessor(s.nonce, 96));
        encode(s, r, 4, true);
        s.authenticated = true;
        r.authenticated = true;
        r.command = s.keyB ? 0x61 : 0x60;
        return r;
    }
    uint8_t data[18];
    memcpy(data, request, length);
    const bool encrypted = s.authenticated;
    if (encrypted) for (size_t i = 0; i < length; ++i) data[i] ^= lilygoMifareClassicCrypto1Byte(&s.cipher, 0, false);
    r.command = data[0];
    if (length != 4 || lilygoMifareClassicCrcA(data, length - 2) != uint16_t(data[length - 2] | uint16_t(data[length - 1]) << 8)) {
        // Invalid CRC makes the cipher position unusable for subsequent commands.
        r.rejected = LILYGO_MFC_BAD_FRAME; s = {}; return r;
    }
    if (data[0] == 0x50 && data[1] == 0) { r.sleep = true; s = {}; return r; }
    const unsigned block = data[1], sector = sectorOf(block), trailer = trailerOf(sector);
    if (block >= d.totalBlocks || !d.blockRead[trailer]) { reject(s, r, LILYGO_MFC_MISSING_BLOCK); return r; }
    const int trailerAccess = lilygoMifareClassicAccess(d.blocks[trailer], 3);
    if (trailerAccess < 0) { reject(s, r, LILYGO_MFC_ACCESS_DENIED); return r; }
    if (data[0] == 0x60 || data[0] == 0x61) {
        const bool keyB = data[0] == 0x61;
        // A found flag records a successful authentication on the source.
        // Some compatible cards accept B even when their trailer marks it
        // readable. Preserve that observed capability; never infer a key
        // solely from the readable trailer bytes.
        if ((keyB && !d.keyBFound[sector]) || (!keyB && !d.keyAFound[sector])) {
            reject(s, r, LILYGO_MFC_UNKNOWN_KEY); return r;
        }
        const uint8_t *key = keyB ? d.keyB[sector] : d.keyA[sector];
        uint64_t value = 0;
        for (unsigned i = 0; i < 6; ++i) value = value << 8 | key[i];
        s = {}; s.sector = sector; s.keyB = keyB; s.nonce = freshNonce; s.authenticating = true;
        lilygoMifareClassicCrypto1Init(&s.cipher, value);
        putWord(r.data, freshNonce);
        if (encrypted) {
            uint8_t input[4]; putWord(input, freshNonce ^ word(d.uid));
            for (unsigned i = 0; i < 4; ++i) {
                r.data[i] ^= lilygoMifareClassicCrypto1Byte(&s.cipher, input[i], false);
                r.parity[i] = lilygoMifareClassicCrypto1FilterBit(&s.cipher) ^
                              lilygoMifareClassicOddParity(uint8_t(freshNonce >> (24 - i * 8)));
            }
            r.bits = 32;
        } else {
            lilygoMifareClassicCrypto1Word(&s.cipher, freshNonce ^ word(d.uid), false);
            encode(s, r, 4, false);
        }
        return r;
    }
    if (!s.authenticated || s.sector != sector) { reject(s, r, LILYGO_MFC_NOT_AUTHENTICATED); return r; }
    if (data[0] != 0x30) { reject(s, r, LILYGO_MFC_READ_ONLY); return r; }
    if (!d.blockRead[block]) { reject(s, r, LILYGO_MFC_MISSING_BLOCK); return r; }
    const unsigned group = block == trailer ? 3 : sector < 32 ? block % 4 : (block % 16) / 5;
    const int access = lilygoMifareClassicAccess(d.blocks[trailer], group);
    // Data read rights for C1C2C3 = 000..111, encoded as A=1 / B=2.
    static const uint8_t readable[8] = {3, 3, 3, 2, 3, 2, 3, 0};
    if (access < 0 || (block != trailer && !(readable[access] & (s.keyB ? 2 : 1)))) {
        reject(s, r, LILYGO_MFC_ACCESS_DENIED); return r;
    }
    memcpy(r.data, d.blocks[block], 16);
    if (block == trailer) {
        memset(r.data, 0, 6); // Key A can never be read.
        if (trailerAccess > 2) memset(r.data + 10, 0, 6);
    }
    const uint16_t crc = lilygoMifareClassicCrcA(r.data, 16);
    r.data[16] = crc; r.data[17] = crc >> 8;
    encode(s, r, 18, true);
    r.read = true;
    return r;
}
