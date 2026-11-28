/**
 * @file      LilyGoMifareClassicEmulation.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-24
 *
 * Defines a portable, read-only Classic card session using captured data and known keys.
 */
#pragma once
#include "nfc/LilyGoMifareClassic.h"
#include "nfc/LilyGoMifareClassicCrypto1.h"
#include <stddef.h>

/** Why a command was refused; no unavailable data or credentials are invented. */
enum LilyGoMifareClassicReject : uint8_t {
    LILYGO_MFC_ACCEPTED = 0,
    LILYGO_MFC_BAD_FRAME,
    LILYGO_MFC_UNKNOWN_KEY,
    LILYGO_MFC_AUTH_FAILED,
    LILYGO_MFC_NOT_AUTHENTICATED,
    LILYGO_MFC_MISSING_BLOCK,
    LILYGO_MFC_ACCESS_DENIED,
    LILYGO_MFC_READ_ONLY,
};

/** One field session; reset on deselection, field loss or transport failure. */
struct LilyGoMifareClassicSession {
    LilyGoMifareClassicCrypto1State cipher;
    uint32_t nonce;
    uint8_t sector;
    bool keyB;
    bool authenticating;
    bool authenticated;
};

/** Response bits exclude parity; parity contains one supplied bit per full byte. */
struct LilyGoMifareClassicResponse {
    uint8_t data[18];
    uint8_t parity[18];
    uint16_t bits;
    uint8_t command;
    LilyGoMifareClassicReject rejected;
    bool authenticated;
    bool read;
    bool sleep;
};

/** One transparent-mode modulation interval, measured in output-clock ticks. */
struct LilyGoMifareClassicPulse {
    uint16_t ticks;
    bool high;
};

/** @brief Encode SOF, data/custom parity and EOF as 848 kHz Manchester load modulation. */
size_t lilygoMifareClassicEncodePulses(const LilyGoMifareClassicResponse &response,
        uint32_t clockHz, LilyGoMifareClassicPulse *output, size_t capacity);

/** @brief Validate a four-byte-UID image and at least one usable sector/key. Partial images are allowed. */
bool lilygoMifareClassicCanEmulate(const LilyGoMifareClassicDump &dump);
/** @brief Decode and validate the redundant access bits for one trailer group; -1 means invalid. */
int lilygoMifareClassicAccess(const uint8_t trailer[16], uint8_t group);
/** @brief Compute CRC-A in little-endian wire order. */
uint16_t lilygoMifareClassicCrcA(const uint8_t *data, size_t length);
/**
 * @brief Handle a selected card's plaintext or encrypted request, including CRC bytes.
 * @param dump Immutable captured memory; no writes to the image or original card occur.
 * @param session Mutable authentication and stream state.
 * @param request Bytes after hardware parity removal. Encrypted CRC is retained.
 * @param length Number of received bytes; partial-byte frames are rejected by the caller.
 * @param freshNonce Fresh random card nonce for a new authentication, supplied by the transport.
 * @return Plain or encrypted response with explicit parity, or silence/sleep.
 */
LilyGoMifareClassicResponse lilygoMifareClassicCommand(const LilyGoMifareClassicDump &dump,
        LilyGoMifareClassicSession &session, const uint8_t *request, size_t length, uint32_t freshNonce);
