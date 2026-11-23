/**
 * @file      LilyGoMifareClassicCrypto1.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Provides the Crypto1 stream cipher primitives needed by the MIFARE Classic
 * reader diagnostic.
 */
#pragma once

#include <stdint.h>

/** Internal Crypto1 register state. */
struct LilyGoMifareClassicCrypto1State {
    uint32_t odd;
    uint32_t even;
};

/** Initialise Crypto1 from a 48-bit MIFARE Classic key. */
void lilygoMifareClassicCrypto1Init(LilyGoMifareClassicCrypto1State *state,
                                    uint64_t key);

/** Clock one bit and return the generated keystream bit. */
uint8_t lilygoMifareClassicCrypto1Bit(LilyGoMifareClassicCrypto1State *state,
                                      uint8_t input,
                                      bool encrypted);

/** Clock one byte, least-significant bit first. */
uint8_t lilygoMifareClassicCrypto1Byte(LilyGoMifareClassicCrypto1State *state,
                                       uint8_t input,
                                       bool encrypted);

/** Clock one 32-bit word in MIFARE Classic big-endian bit order. */
uint32_t lilygoMifareClassicCrypto1Word(LilyGoMifareClassicCrypto1State *state,
                                        uint32_t input,
                                        bool encrypted);

/** Return the next Crypto1 filter bit without clocking the state. */
uint8_t lilygoMifareClassicCrypto1FilterBit(
    const LilyGoMifareClassicCrypto1State *state);

/** Advance the MIFARE Classic nonce PRNG by the requested number of clocks. */
uint32_t lilygoMifareClassicPrngSuccessor(uint32_t value, uint32_t clocks);

/** Return the ISO14443-A odd parity bit for one byte. */
uint8_t lilygoMifareClassicOddParity(uint8_t value);
