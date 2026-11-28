/**
 * @file      LilyGoMifareClassic.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Implements authenticated MIFARE Classic reading over the ST25R3916 RFAL
 * low-level transceive interface.
 */
#include "nfc/LilyGoMifareClassic.h"
#include "LilyGoLog.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)

#include <Arduino.h>
#include <esp_system.h>
#include <string.h>

#include "nfc/LilyGoMifareClassicCrypto1.h"

namespace {

constexpr uint8_t kMifareAuthKeyA = 0x60U;
constexpr uint8_t kMifareAuthKeyB = 0x61U;
constexpr uint8_t kMifareReadCommand = 0x30U;
constexpr uint32_t kMifareFrameWaitTime = rfalConvMsTo1fc(20U);
constexpr uint32_t kMifareTransceiveTimeoutMs = 60U;
constexpr uint8_t kMifareKeyCount = 15U;

static const uint8_t kMifareDefaultKeys[kMifareKeyCount][LILYGO_MIFARE_CLASSIC_KEY_SIZE] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},
    {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD},
    {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A},
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
    {0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97},
    {0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F},
    {0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91},
    {0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6},
    {0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9},
    {0xA6, 0x45, 0x98, 0xA7, 0x74, 0x78},
    {0x26, 0x94, 0x0B, 0x21, 0xFF, 0x5D},
    {0xFC, 0x00, 0x01, 0x87, 0x78, 0xF7},
    {0x00, 0x00, 0x0F, 0xFE, 0x24, 0x88},
};

// ISO14443-A CRC-A, with the RFAL preload and little-endian wire order.
static uint16_t crcA(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0x6363U;
    for (uint16_t index = 0; index < length; ++index) {
        uint8_t value = static_cast<uint8_t>(data[index] ^ static_cast<uint8_t>(crc & 0xFFU));
        value ^= static_cast<uint8_t>(value << 4U);
        crc = static_cast<uint16_t>((crc >> 8U) ^ (static_cast<uint16_t>(value) << 8U) ^
                                     (static_cast<uint16_t>(value) << 3U) ^
                                     (static_cast<uint16_t>(value) >> 4U));
    }
    return crc;
}

static uint16_t packBits(const uint8_t *data,
                         const uint8_t *parity,
                         uint8_t byteCount,
                         uint8_t *output,
                         uint16_t outputSize)
{
    if (!data || !parity || !output || outputSize == 0U) return 0U;
    memset(output, 0, outputSize);
    uint16_t bit = 0U;
    for (uint8_t index = 0; index < byteCount; ++index) {
        for (uint8_t dataBit = 0; dataBit < 8U; ++dataBit) {
            if (bit >= static_cast<uint16_t>(outputSize * 8U)) return 0U;
            if ((data[index] & (1U << dataBit)) != 0U) {
                output[bit >> 3U] |= static_cast<uint8_t>(1U << (bit & 7U));
            }
            ++bit;
        }
        if (bit >= static_cast<uint16_t>(outputSize * 8U)) return 0U;
        if ((parity[index] & 1U) != 0U) {
            output[bit >> 3U] |= static_cast<uint8_t>(1U << (bit & 7U));
        }
        ++bit;
    }
    return bit;
}

static uint8_t unpackBits(const uint8_t *input,
                          uint16_t bitCount,
                          uint8_t *data,
                          uint8_t maxBytes)
{
    if (!input || !data) return 0U;
    uint8_t byteCount = 0U;
    uint16_t bit = 0U;
    while ((bit + 8U) <= bitCount && byteCount < maxBytes) {
        uint8_t value = 0U;
        for (uint8_t dataBit = 0; dataBit < 8U; ++dataBit) {
            if ((input[bit >> 3U] & (1U << (bit & 7U))) != 0U) {
                value |= static_cast<uint8_t>(1U << dataBit);
            }
            ++bit;
        }
        data[byteCount++] = value;
        if (bit < bitCount) ++bit;
    }
    return byteCount;
}

static uint8_t unpackBitsWithParity(const uint8_t *input,
                                    uint16_t bitCount,
                                    uint8_t *data,
                                    uint8_t *parity,
                                    uint8_t maxBytes)
{
    if (!input || !data || !parity) return 0U;
    uint8_t byteCount = 0U;
    uint16_t bit = 0U;
    while ((bit + 9U) <= bitCount && byteCount < maxBytes) {
        uint8_t value = 0U;
        for (uint8_t dataBit = 0; dataBit < 8U; ++dataBit) {
            if ((input[bit >> 3U] & (1U << (bit & 7U))) != 0U) {
                value |= static_cast<uint8_t>(1U << dataBit);
            }
            ++bit;
        }
        data[byteCount] = value;
        parity[byteCount] = static_cast<uint8_t>((input[bit >> 3U] >> (bit & 7U)) & 1U);
        ++bit;
        ++byteCount;
    }
    return byteCount;
}

static bool receiveStatusIsUsable(ReturnCode error)
{
    return error == ST_ERR_NONE ||
           (error >= ST_ERR_INCOMPLETE_BYTE && error <= ST_ERR_INCOMPLETE_BYTE_07);
}

static bool isPresenceErrorUsable(ReturnCode error)
{
    return error == ST_ERR_NONE || error == ST_ERR_RF_COLLISION ||
           error == ST_ERR_CRC || error == ST_ERR_PAR ||
           error == ST_ERR_FRAMING || error == ST_ERR_PROTO;
}

static uint8_t firstBlockForSector(uint8_t sector)
{
    return sector < 32U ? static_cast<uint8_t>(sector * 4U)
                        : static_cast<uint8_t>(128U + (sector - 32U) * 16U);
}

static uint8_t blockCountForSector(uint8_t sector)
{
    return sector < 32U ? 4U : 16U;
}

static bool reselectCard(RfalNfcClass &reader, const uint8_t *uid, uint8_t uidLen, uint8_t expectedSak)
{
    if (!uid || uidLen == 0U || uidLen > RFAL_NFCA_CASCADE_3_UID_LEN) return false;
    RfalRfClass *rf = reader.getRfalRf();
    if (!rf) return false;
    // Keep the NFC discovery state intact. Repeated high-level deactivate()
    // calls become no-ops once RFAL is IDLE and leave the card encrypted.
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
#if LILYGO_NFC_DEBUG_ENABLED
        const char *stage = "field_off";
#endif
        ReturnCode error = rf->rfalFieldOff();
        delay(5);
        if (error == ST_ERR_NONE) { LILYGO_NFC_LOG_ONLY(stage = "poll_init"); error = reader.rfalNfcaPollerInitialize(); }
        if (error == ST_ERR_NONE) { LILYGO_NFC_LOG_ONLY(stage = "field_on"); error = rf->rfalFieldOnAndStartGT(); }
        rfalNfcaSensRes sensRes = {};
        if (error == ST_ERR_NONE) {
            LILYGO_NFC_LOG_ONLY(stage = "presence");
            error = reader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes);
            if (isPresenceErrorUsable(error)) error = ST_ERR_NONE;
        }
        rfalNfcaSelRes selRes = {};
        if (error == ST_ERR_NONE) { LILYGO_NFC_LOG_ONLY(stage = "select"); error = reader.rfalNfcaPollerSelect(uid, uidLen, &selRes); }
        if (error == ST_ERR_NONE && selRes.sak != expectedSak) { LILYGO_NFC_LOG_ONLY(stage = "sak"); error = ST_ERR_PROTO; }
        if (error == ST_ERR_NONE) return true;
        LILYGO_NFC_LOG_W("[ST25][MFC] reselect attempt=%u stage=%s code=%u", attempt, stage, error);
    }
    return false;
}

static bool transceiveRaw(RfalNfcClass &reader,
                          uint8_t *tx,
                          uint16_t txBits,
                          uint8_t *rx,
                          uint16_t rxCapacity,
                          uint16_t *rxBits,
                          uint32_t flags)
{
    RfalRfClass *rf = reader.getRfalRf();
    if (!rf || !tx || txBits == 0U || !rxBits) return false;

    rfalTransceiveContext context = {};
    context.txBuf = tx;
    context.txBufLen = txBits;
    context.rxBuf = rx;
    context.rxBufLen = static_cast<uint16_t>(rfalConvBytesToBits(rxCapacity));
    context.rxRcvdLen = rxBits;
    context.flags = flags;
    context.fwt = kMifareFrameWaitTime;
    *rxBits = 0U;

    ReturnCode error = rf->rfalStartTransceive(&context);
    if (error != ST_ERR_NONE) return false;

    const uint32_t start = millis();
    do {
        rf->rfalWorker();
        error = rf->rfalGetTransceiveStatus();
        if (error != ST_ERR_BUSY) break;
        delay(1);
    } while ((millis() - start) < kMifareTransceiveTimeoutMs);

    if (error == ST_ERR_BUSY) {
        LILYGO_NFC_LOG_W("[ST25][MFC] transceive timeout");
        return false;
    }
    return receiveStatusIsUsable(error);
}

static bool authenticate(RfalNfcClass &reader,
                         const uint8_t *uid,
                         uint8_t uidLen,
                         uint8_t block,
                         const uint8_t key[LILYGO_MIFARE_CLASSIC_KEY_SIZE],
                         bool keyB,
                         LilyGoMifareClassicCrypto1State &cipher)
{
    uint64_t keyValue = 0U;
    for (uint8_t index = 0; index < LILYGO_MIFARE_CLASSIC_KEY_SIZE; ++index) {
        keyValue = (keyValue << 8U) | key[index];
    }

    if (uidLen < 4U) return false;
    const uint8_t command[2] = {keyB ? kMifareAuthKeyB : kMifareAuthKeyA, block};
    const uint16_t commandCrc = crcA(command, sizeof(command));
    uint8_t firstTx[4] = {
        command[0], command[1], static_cast<uint8_t>(commandCrc & 0xFFU),
        static_cast<uint8_t>(commandCrc >> 8U)
    };
    uint8_t firstRx[8] = {};
    uint16_t firstRxBits = 0U;
    constexpr uint32_t kFirstAuthFlags =
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_TX_MANUAL) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_NFCIP1_OFF) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_AGC_ON) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_TX_AUTO);

    if (!transceiveRaw(reader, firstTx, rfalConvBytesToBits(sizeof(firstTx)),
                       firstRx, sizeof(firstRx), &firstRxBits, kFirstAuthFlags) ||
            firstRxBits < 32U) {
        LILYGO_NFC_LOG_D("[ST25][MFC] auth nonce unavailable block=%u bits=%u",
                         static_cast<unsigned>(block), static_cast<unsigned>(firstRxBits));
        return false;
    }

    uint8_t nonceBytes[4] = {};
    if (unpackBits(firstRx, firstRxBits, nonceBytes, sizeof(nonceBytes)) < sizeof(nonceBytes)) {
        return false;
    }
    uint32_t nonce = (static_cast<uint32_t>(nonceBytes[0]) << 24U) |
                     (static_cast<uint32_t>(nonceBytes[1]) << 16U) |
                     (static_cast<uint32_t>(nonceBytes[2]) << 8U) |
                     nonceBytes[3];
    LILYGO_NFC_LOG_D("[ST25][MFC] auth nonce block=%u key=%c nt=%08lX bits=%u",
                     static_cast<unsigned>(block), keyB ? 'B' : 'A',
                     static_cast<unsigned long>(nonce), static_cast<unsigned>(firstRxBits));

    const uint8_t uidOffset = uidLen > 4U ? static_cast<uint8_t>(uidLen - 4U) : 0U;
    const uint32_t uid32 = (static_cast<uint32_t>(uid[uidOffset]) << 24U) |
                           (static_cast<uint32_t>(uid[uidOffset + 1U]) << 16U) |
                           (static_cast<uint32_t>(uid[uidOffset + 2U]) << 8U) |
                           uid[uidOffset + 3U];
    lilygoMifareClassicCrypto1Init(&cipher, keyValue);
    lilygoMifareClassicCrypto1Word(&cipher, nonce ^ uid32, false);

    uint32_t readerNonce = esp_random();
    uint8_t readerNonceBytes[4] = {
        static_cast<uint8_t>(readerNonce >> 24U), static_cast<uint8_t>(readerNonce >> 16U),
        static_cast<uint8_t>(readerNonce >> 8U), static_cast<uint8_t>(readerNonce)
    };
    uint8_t answerData[8] = {};
    uint8_t answerParity[8] = {};
    for (uint8_t index = 0; index < 4U; ++index) {
        answerData[index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1Byte(&cipher, readerNonceBytes[index], false) ^
            readerNonceBytes[index]);
        // MIFARE Classic reuses the next data-bit keystream for parity; do
        // not clock Crypto1 a ninth time here.
        answerParity[index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1FilterBit(&cipher) ^
            lilygoMifareClassicOddParity(readerNonceBytes[index]));
    }

    const uint32_t expectedReaderAnswer = lilygoMifareClassicPrngSuccessor(nonce, 64U);
    for (uint8_t index = 0; index < 4U; ++index) {
        const uint8_t plain = static_cast<uint8_t>(expectedReaderAnswer >> (24U - 8U * index));
        answerData[4U + index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1Byte(&cipher, 0U, false) ^ plain);
        answerParity[4U + index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1FilterBit(&cipher) ^
            lilygoMifareClassicOddParity(plain));
    }

    uint8_t answerTx[16] = {};
    const uint16_t answerBits = packBits(answerData, answerParity, 8U, answerTx, sizeof(answerTx));
    uint8_t cardAnswer[8] = {};
    uint16_t cardAnswerBits = 0U;
    constexpr uint32_t kEncryptedFlags =
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_TX_MANUAL) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_NFCIP1_OFF) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_AGC_ON) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_TX_NONE);
    if (answerBits == 0U ||
            !transceiveRaw(reader, answerTx, answerBits, cardAnswer, sizeof(cardAnswer),
                           &cardAnswerBits, kEncryptedFlags) ||
            cardAnswerBits < 32U) {
        LILYGO_NFC_LOG_D("[ST25][MFC] auth AT unavailable block=%u bits=%u",
                         static_cast<unsigned>(block), static_cast<unsigned>(cardAnswerBits));
        return false;
    }

    uint8_t encryptedAt[4] = {};
    uint8_t encryptedAtParity[4] = {};
    if (unpackBitsWithParity(cardAnswer, cardAnswerBits, encryptedAt, encryptedAtParity,
                             sizeof(encryptedAt)) < sizeof(encryptedAt)) {
        LILYGO_NFC_LOG_D("[ST25][MFC] auth AT frame too short bits=%u",
                         static_cast<unsigned>(cardAnswerBits));
        return false;
    }
    uint32_t cardAnswerValue = 0U;
    for (uint8_t index = 0; index < 4U; ++index) {
        const uint8_t keyStream = lilygoMifareClassicCrypto1Byte(
            &cipher, 0U, false);
        const uint8_t plain = static_cast<uint8_t>(encryptedAt[index] ^ keyStream);
        const uint8_t plainParity = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1FilterBit(&cipher) ^ encryptedAtParity[index]);
        if (plainParity != lilygoMifareClassicOddParity(plain)) {
            LILYGO_NFC_LOG_D("[ST25][MFC] auth AT parity mismatch byte=%u",
                             static_cast<unsigned>(index));
            return false;
        }
        cardAnswerValue = (cardAnswerValue << 8U) |
                          plain;
    }
    const uint32_t expectedCardAnswer = lilygoMifareClassicPrngSuccessor(nonce, 96U);
    if (cardAnswerValue != expectedCardAnswer) {
        LILYGO_NFC_LOG_D("[ST25][MFC] auth AT mismatch nonce=%08lX got=%08lX expected=%08lX",
                         static_cast<unsigned long>(nonce),
                         static_cast<unsigned long>(cardAnswerValue),
                         static_cast<unsigned long>(expectedCardAnswer));
        return false;
    }
    return true;
}

static bool readBlock(RfalNfcClass &reader,
                      uint8_t block,
                      LilyGoMifareClassicCrypto1State &cipher,
                      uint8_t data[LILYGO_MIFARE_CLASSIC_BLOCK_SIZE])
{
    const uint8_t command[2] = {kMifareReadCommand, block};
    const uint16_t commandCrc = crcA(command, sizeof(command));
    const uint8_t plain[4] = {
        command[0], command[1], static_cast<uint8_t>(commandCrc & 0xFFU),
        static_cast<uint8_t>(commandCrc >> 8U)
    };
    uint8_t encrypted[4] = {};
    uint8_t parity[4] = {};
    for (uint8_t index = 0; index < sizeof(plain); ++index) {
        encrypted[index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1Byte(&cipher, 0U, false) ^ plain[index]);
        // The parity bit reuses the filter output at the next byte boundary.
        parity[index] = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1FilterBit(&cipher) ^
            lilygoMifareClassicOddParity(plain[index]));
    }

    uint8_t tx[8] = {};
    const uint16_t txBits = packBits(encrypted, parity, sizeof(encrypted), tx, sizeof(tx));
    uint8_t rx[32] = {};
    uint16_t rxBits = 0U;
    constexpr uint32_t kEncryptedFlags =
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_TX_MANUAL) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_CRC_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_NFCIP1_OFF) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_AGC_ON) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_RX_KEEP) |
        static_cast<uint32_t>(RFAL_TXRX_FLAGS_PAR_TX_NONE);
    if (txBits == 0U || !transceiveRaw(reader, tx, txBits, rx, sizeof(rx), &rxBits, kEncryptedFlags) ||
            rxBits < static_cast<uint16_t>(18U * 9U)) {
        return false;
    }

    uint8_t encryptedData[18] = {};
    uint8_t encryptedParity[18] = {};
    if (unpackBitsWithParity(rx, rxBits, encryptedData, encryptedParity,
                             sizeof(encryptedData)) < sizeof(encryptedData)) {
        return false;
    }
    for (uint8_t index = 0; index < sizeof(encryptedData); ++index) {
        const uint8_t keyStream = lilygoMifareClassicCrypto1Byte(
            &cipher, 0U, false);
        encryptedData[index] ^= keyStream;
        const uint8_t plainParity = static_cast<uint8_t>(
            lilygoMifareClassicCrypto1FilterBit(&cipher) ^ encryptedParity[index]);
        if (plainParity != lilygoMifareClassicOddParity(encryptedData[index])) {
            LILYGO_NFC_LOG_W("[ST25][MFC] block=%u parity mismatch byte=%u",
                             static_cast<unsigned>(block), static_cast<unsigned>(index));
            return false;
        }
    }
    const uint16_t receivedCrc = static_cast<uint16_t>(encryptedData[16]) |
                                 static_cast<uint16_t>(encryptedData[17] << 8U);
    if (crcA(encryptedData, 16U) != receivedCrc) {
        LILYGO_NFC_LOG_W("[ST25][MFC] block=%u CRC mismatch", static_cast<unsigned>(block));
        return false;
    }
    memcpy(data, encryptedData, LILYGO_MIFARE_CLASSIC_BLOCK_SIZE);
    return true;
}

static bool resetAndReselect(RfalNfcClass &reader, const uint8_t *uid, uint8_t uidLen, uint8_t sak)
{
    return reselectCard(reader, uid, uidLen, sak);
}

static void logBlock(uint8_t block, const uint8_t data[LILYGO_MIFARE_CLASSIC_BLOCK_SIZE])
{
    LILYGO_NFC_LOG_I(
        "[ST25][MFC] block=%u data=%02X %02X %02X %02X %02X %02X %02X %02X "
        "%02X %02X %02X %02X %02X %02X %02X %02X",
        static_cast<unsigned>(block), data[0], data[1], data[2], data[3], data[4], data[5],
        data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13],
        data[14], data[15]);
}

static void logKey(uint8_t sector, bool keyB, uint8_t keyIndex,
                   const uint8_t key[LILYGO_MIFARE_CLASSIC_KEY_SIZE])
{
    LILYGO_NFC_LOG_I("[ST25][MFC] sector=%u auth=Key%c key_index=%u key=%02X %02X %02X %02X %02X %02X",
                     static_cast<unsigned>(sector), keyB ? 'B' : 'A',
                     static_cast<unsigned>(keyIndex), key[0], key[1], key[2], key[3], key[4], key[5]);
}

}  // namespace

bool lilygoReadMifareClassic(RfalNfcClass &reader,
                             const uint8_t *uid,
                             uint8_t uidLen,
                             uint8_t sak,
                             const uint8_t atqa[2],
                             LilyGoMifareClassicDump &dump)
{
    memset(&dump, 0, sizeof(dump));
    dump.attempted = true;
    dump.sak = sak;
    if (!atqa) return false;
    memcpy(dump.atqa, atqa, sizeof(dump.atqa));
    dump.uidLen = uidLen > LILYGO_NFC_MAX_UID_LEN ? LILYGO_NFC_MAX_UID_LEN : uidLen;
    if (uid && dump.uidLen > 0U) {
        memcpy(dump.uid, uid, dump.uidLen);
    }

    if (sak == 0x18U || sak == 0x38U) {
        dump.sectors = 40U;
        dump.totalBlocks = 256U;
    } else if (sak == 0x09U) {
        dump.sectors = 5U;
        dump.totalBlocks = 20U;
    } else if (sak == 0x08U || sak == 0x88U || sak == 0x28U) {
        dump.sectors = 16U;
        dump.totalBlocks = 64U;
    } else {
        LILYGO_NFC_LOG_W("[ST25][MFC] unsupported SAK=0x%02X", static_cast<unsigned>(sak));
        return false;
    }

    LILYGO_NFC_LOG_I("[ST25][MFC] read start sak=0x%02X sectors=%u blocks=%u uid_len=%u",
                     static_cast<unsigned>(sak), static_cast<unsigned>(dump.sectors),
                     static_cast<unsigned>(dump.totalBlocks), static_cast<unsigned>(uidLen));
    if (!uid || uidLen < 4U) {
        LILYGO_NFC_LOG_E("[ST25][MFC] invalid UID for Crypto1 authentication");
        return false;
    }

    LilyGoMifareClassicCrypto1State cipher = {};
    for (uint8_t sector = 0; sector < dump.sectors; ++sector) {
        const uint8_t firstBlock = firstBlockForSector(sector);
        const uint8_t blockCount = blockCountForSector(sector);
        const uint8_t trailer = static_cast<uint8_t>(firstBlock + blockCount - 1U);
        bool authenticated = false;
        bool usedKeyB = false;
        uint8_t usedKeyIndex = 0U;

        for (uint8_t keyType = 0U; keyType < 2U && !authenticated; ++keyType) {
            for (uint8_t keyIndex = 0U; keyIndex < kMifareKeyCount; ++keyIndex) {
                LILYGO_NFC_LOG_D("[ST25][MFC] sector=%u try=Key%c index=%u trailer=%u",
                                 static_cast<unsigned>(sector), keyType ? 'B' : 'A',
                                 static_cast<unsigned>(keyIndex), static_cast<unsigned>(trailer));
                if (authenticate(reader, uid, uidLen, trailer,
                                 kMifareDefaultKeys[keyIndex], keyType != 0U, cipher)) {
                    authenticated = true;
                    usedKeyB = keyType != 0U;
                    usedKeyIndex = keyIndex;
                    break;
                }
                if (!resetAndReselect(reader, uid, uidLen, sak)) {
                    LILYGO_NFC_LOG_W("[ST25][MFC] sector=%u cannot reselect after auth failure",
                                     static_cast<unsigned>(sector));
                    return dump.blocksRead > 0U;
                }
            }
        }

        if (!authenticated) {
            LILYGO_NFC_LOG_W("[ST25][MFC] sector=%u no built-in key matched",
                             static_cast<unsigned>(sector));
            continue;
        }

        ++dump.authenticatedSectors;
        if (usedKeyB) {
            memcpy(dump.keyB[sector], kMifareDefaultKeys[usedKeyIndex], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
            dump.keyBFound[sector] = true;
        } else {
            memcpy(dump.keyA[sector], kMifareDefaultKeys[usedKeyIndex], LILYGO_MIFARE_CLASSIC_KEY_SIZE);
            dump.keyAFound[sector] = true;
        }
        logKey(sector, usedKeyB, usedKeyIndex, kMifareDefaultKeys[usedKeyIndex]);

        for (uint8_t offset = 0U; offset < blockCount; ++offset) {
            const uint8_t block = static_cast<uint8_t>(firstBlock + offset);
            uint8_t data[LILYGO_MIFARE_CLASSIC_BLOCK_SIZE] = {};
            if (readBlock(reader, block, cipher, data)) {
                memcpy(dump.blocks[block], data, LILYGO_MIFARE_CLASSIC_BLOCK_SIZE);
                dump.blockRead[block] = true;
                ++dump.blocksRead;
                logBlock(block, data);
            } else {
                LILYGO_NFC_LOG_W("[ST25][MFC] sector=%u block=%u read failed",
                                 static_cast<unsigned>(sector), static_cast<unsigned>(block));
                // A missing reply leaves the stream position uncertain. Do
                // not issue further encrypted reads with the stale state.
                break;
            }
        }

        if (sector + 1U < dump.sectors && !resetAndReselect(reader, uid, uidLen, sak)) {
            LILYGO_NFC_LOG_W("[ST25][MFC] cannot reselect before sector=%u",
                             static_cast<unsigned>(sector + 1U));
            break;
        }
    }

    dump.complete = dump.blocksRead == dump.totalBlocks;
    LILYGO_NFC_LOG_I("[ST25][MFC] read done authenticated_sectors=%u blocks=%u/%u complete=%u",
                     static_cast<unsigned>(dump.authenticatedSectors),
                     static_cast<unsigned>(dump.blocksRead), static_cast<unsigned>(dump.totalBlocks),
                     dump.complete ? 1U : 0U);
    return dump.blocksRead > 0U;
}

#endif
