/**
 * @file      LilyGoLogConfig.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-23
 *
 * Configures compile-time diagnostic logging switches for LilyGoLib.
 */
#pragma once

// Override for the entire build, or change this default when using Arduino IDE.
#ifndef LILYGO_DEBUG_ENABLED
#define LILYGO_DEBUG_ENABLED 0
#endif

#if LILYGO_DEBUG_ENABLED != 0 && LILYGO_DEBUG_ENABLED != 1
#error "LILYGO_DEBUG_ENABLED must be 0 or 1"
#endif

// Set this to 0 to silence ST25R3916/NFC diagnostics while keeping other
// LilyGoLib diagnostics enabled. It follows the global switch by default.
#ifndef LILYGO_NFC_DEBUG_ENABLED
#define LILYGO_NFC_DEBUG_ENABLED LILYGO_DEBUG_ENABLED
#endif

#if LILYGO_NFC_DEBUG_ENABLED != 0 && LILYGO_NFC_DEBUG_ENABLED != 1
#error "LILYGO_NFC_DEBUG_ENABLED must be 0 or 1"
#endif
