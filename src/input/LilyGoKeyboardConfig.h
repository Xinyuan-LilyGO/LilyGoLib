/**
 * @file      LilyGoKeyboardConfig.h
 * @brief     Defines keyboard layout, modifier, and repeat timing structures.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-15
 *
 */
#pragma once
#include <stdint.h>

/**
 * @brief Device-independent modifier key identifiers.
 */
enum class ModifierKey : uint8_t {
    NONE = 0,       /**< No modifier key. */
    SYMBOL,         /**< Symbol layer key. */
    ALT,            /**< Alt modifier key. */
    CAPS,           /**< Caps lock key. */
    CAPS_B,         /**< Caps lock + B combo key. */
    FN,             /**< Function layer key. */
    CTRL,           /**< Control modifier key. */
    SHIFT,          /**< Shift modifier key. */
    MAX_MODIFIERS   /**< Number of modifier identifiers. */
};

/**
 * @brief Physical keyboard matrix and keymap configuration.
 */
struct KeyboardLayoutConfig {
    uint8_t kb_rows;                /**< Number of keyboard matrix rows. */
    uint8_t kb_cols;                /**< Number of keyboard matrix columns. */
    const char *current_keymap;     /**< Base keymap table. */
    const char *current_symbol_map; /**< Symbol-layer keymap table. */
    bool has_symbol_key;            /**< true when the layout has a dedicated symbol key. */
    bool space_as_symbol_key;       /**< true when Space acts as a temporary symbol-layer prefix. */
};

/**
 * @brief Maps modifier identifiers to physical or logical key values.
 */
struct ModifierKeyConfig {
    uint8_t symbol_key_value;    /**< Key value for ModifierKey::SYMBOL. */
    uint8_t alt_key_value;       /**< Key value for ModifierKey::ALT. */
    uint8_t caps_key_value;      /**< Key value for ModifierKey::CAPS. */
    uint8_t caps_b_key_value;    /**< Key value for ModifierKey::CAPS_B. */
    uint8_t fn_key_value;        /**< Key value for ModifierKey::FN. */
    uint8_t ctrl_key_value;      /**< Key value for ModifierKey::CTRL. */
    uint8_t shift_key_value;     /**< Key value for ModifierKey::SHIFT. */
    uint8_t backspace_value;     /**< Key value used for Backspace. */
};

/**
 * @brief Complete keyboard configuration passed to LilyGoKeyboard.
 */
struct LilyGoKeyboardConfig {
    KeyboardLayoutConfig layout;  /**< Physical layout and keymaps. */
    ModifierKeyConfig modifiers;  /**< Modifier key mapping. */
};

/**
 * @brief Key repeat timing configuration.
 */
struct KeyRepeatConfig {
    uint32_t initialDelay;   /**< Delay before repeat starts, in milliseconds. */
    uint32_t repeatInterval; /**< Interval between repeat events, in milliseconds. */
};
