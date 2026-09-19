/**
 * @file      KeyComboManager.h
 * @brief     Declares a small fixed-size keyboard shortcut dispatcher.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-15
 *
 */
#pragma once
#include <stdint.h>
#include "LilyGoKeyboardConfig.h"

/**
 * @brief Stores and dispatches modifier-key combinations.
 *
 * The manager keeps a fixed-size list of modifier + key pairs and invokes the
 * registered callback when a matching pressed event is observed.
 */
class KeyComboManager {
public:
    /**
     * @brief Callback invoked when a registered key combination is triggered.
     */
    using ComboCallback = void(*)();

    /**
     * @brief Register a key combination.
     * @param modifier Modifier key that must be active.
     * @param key Normal key value to match.
     * @param callback Function to invoke when the combination is pressed.
     * @return true if the combination was registered.
     */
    bool registerCombo(ModifierKey modifier, uint8_t key, ComboCallback callback);

    /**
     * @brief Remove a registered key combination.
     * @param modifier Modifier key of the combination.
     * @param key Normal key value of the combination.
     * @return true if a matching combination was found and removed.
     */
    bool unregisterCombo(ModifierKey modifier, uint8_t key);

    /**
     * @brief Remove all registered key combinations.
     */
    void clearAll();

    /**
     * @brief Check whether a key event matches a registered combination.
     * @param modifier Currently active modifier key.
     * @param key Normal key value from the key event.
     * @param pressed true for key press events, false for release events.
     * @return true if a registered combination consumed the event.
     */
    bool checkAndTrigger(ModifierKey modifier, uint8_t key, bool pressed);

private:
    /**
     * @brief One registered modifier + key callback entry.
     */
    struct ComboEntry {
        ModifierKey modifier;
        uint8_t key;
        ComboCallback callback;
    };

    /** Maximum number of key combinations stored by the manager. */
    static const int MAX_COMBOS = 16;
    /** Fixed storage for registered combinations. */
    ComboEntry combos[MAX_COMBOS];
    /** Number of active entries in combos. */
    int comboCount = 0;
};
