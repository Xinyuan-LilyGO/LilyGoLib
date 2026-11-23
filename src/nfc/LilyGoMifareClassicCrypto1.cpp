/**
 * @file      LilyGoMifareClassicCrypto1.cpp
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Implements the Crypto1 stream cipher primitives used by the MIFARE Classic
 * reader diagnostic.
 */
#include "nfc/LilyGoMifareClassicCrypto1.h"

namespace {

constexpr uint32_t kLfsrPolyOdd = 0x29CE5CU;
constexpr uint32_t kLfsrPolyEven = 0x870804U;

static inline uint32_t bitAt(uint32_t value, uint8_t bit)
{
    return (value >> bit) & 1U;
}

static inline uint32_t bitAt64(uint64_t value, uint8_t bit)
{
    return static_cast<uint32_t>((value >> bit) & 1ULL);
}

static inline uint32_t bigEndianBit(uint32_t value, uint8_t bit)
{
    return bitAt(value, bit ^ 24U);
}

static inline uint32_t crypto1Filter(uint32_t value)
{
    uint32_t filtered = (0xF22C0U >> (value & 0xFU)) & 16U;
    filtered |= (0x6C9C0U >> ((value >> 4U) & 0xFU)) & 8U;
    filtered |= (0x3C8B0U >> ((value >> 8U) & 0xFU)) & 4U;
    filtered |= (0x1E458U >> ((value >> 12U) & 0xFU)) & 2U;
    filtered |= (0x0D938U >> ((value >> 16U) & 0xFU)) & 1U;
    return bitAt(0xEC57E80AU, static_cast<uint8_t>(filtered));
}

static inline uint32_t crypto1Parity(uint32_t value)
{
    value ^= value >> 16U;
    value ^= value >> 8U;
    value ^= value >> 4U;
    return bitAt(0x6996U, static_cast<uint8_t>(value & 0xFU));
}

static inline uint32_t swapEndian(uint32_t value)
{
    value = ((value >> 8U) & 0xFF00FFU) | ((value & 0xFF00FFU) << 8U);
    return (value >> 16U) | (value << 16U);
}

}  // namespace

void lilygoMifareClassicCrypto1Init(LilyGoMifareClassicCrypto1State *state,
                                    uint64_t key)
{
    if (!state) return;
    state->odd = 0;
    state->even = 0;
    for (int bit = 47; bit > 0; bit -= 2) {
        state->odd = (state->odd << 1U) | bitAt64(key, static_cast<uint8_t>((bit - 1) ^ 7));
        state->even = (state->even << 1U) | bitAt64(key, static_cast<uint8_t>(bit ^ 7));
    }
}

uint8_t lilygoMifareClassicCrypto1Bit(LilyGoMifareClassicCrypto1State *state,
                                      uint8_t input,
                                      bool encrypted)
{
    if (!state) return 0;
    const uint8_t output = static_cast<uint8_t>(crypto1Filter(state->odd));
    uint32_t feedback = (output & (encrypted ? 1U : 0U)) ^ (input ? 1U : 0U);
    feedback ^= kLfsrPolyOdd & state->odd;
    feedback ^= kLfsrPolyEven & state->even;
    state->even = (state->even << 1U) | crypto1Parity(feedback);

    const uint32_t oldOdd = state->odd;
    state->odd = state->even;
    state->even = oldOdd;
    return output;
}

uint8_t lilygoMifareClassicCrypto1Byte(LilyGoMifareClassicCrypto1State *state,
                                       uint8_t input,
                                       bool encrypted)
{
    uint8_t output = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
        output |= static_cast<uint8_t>(
            lilygoMifareClassicCrypto1Bit(state, static_cast<uint8_t>((input >> bit) & 1U), encrypted)
            << bit);
    }
    return output;
}

uint32_t lilygoMifareClassicCrypto1Word(LilyGoMifareClassicCrypto1State *state,
                                        uint32_t input,
                                        bool encrypted)
{
    uint32_t output = 0;
    for (uint8_t bit = 0; bit < 32U; ++bit) {
        output |= static_cast<uint32_t>(
            lilygoMifareClassicCrypto1Bit(state,
                                          static_cast<uint8_t>(bigEndianBit(input, bit)),
                                          encrypted)
            << (24U ^ bit));
    }
    return output;
}

uint8_t lilygoMifareClassicCrypto1FilterBit(
    const LilyGoMifareClassicCrypto1State *state)
{
    return state ? static_cast<uint8_t>(crypto1Filter(state->odd)) : 0;
}

uint32_t lilygoMifareClassicPrngSuccessor(uint32_t value, uint32_t clocks)
{
    value = swapEndian(value);
    while (clocks--) {
        const uint32_t feedback = ((value >> 16U) ^ (value >> 18U) ^
                                   (value >> 19U) ^ (value >> 21U)) & 1U;
        value = (value >> 1U) | (feedback << 31U);
    }
    return swapEndian(value);
}

uint8_t lilygoMifareClassicOddParity(uint8_t value)
{
    value ^= value >> 4U;
    value ^= value >> 2U;
    value ^= value >> 1U;
    return static_cast<uint8_t>((value & 1U) ^ 1U);
}
