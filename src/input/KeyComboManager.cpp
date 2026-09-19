/**
 * @file      KeyComboManager.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-15
 *
 */
#include "KeyComboManager.h"

static uint8_t normalizeComboKey(uint8_t key)
{
    if (key >= 'a' && key <= 'z') {
        return key - ('a' - 'A');
    }
    return key;
}

bool KeyComboManager::registerCombo(ModifierKey modifier, uint8_t key, ComboCallback callback)
{
    key = normalizeComboKey(key);

    // Check if combo already exists
    for (int i = 0; i < comboCount; i++) {
        if (combos[i].modifier == modifier && combos[i].key == key) {
            // Update existing combo callback
            combos[i].callback = callback;
            return true;
        }
    }

    if (comboCount >= MAX_COMBOS) {
        return false;
    }

    // Add new combo
    combos[comboCount].modifier = modifier;
    combos[comboCount].key = key;
    combos[comboCount].callback = callback;
    comboCount++;

    return true;
}

bool KeyComboManager::unregisterCombo(ModifierKey modifier, uint8_t key)
{
    key = normalizeComboKey(key);

    for (int i = 0; i < comboCount; i++) {
        if (combos[i].modifier == modifier && combos[i].key == key) {
            // Remove combo by shifting elements forward
            for (int j = i; j < comboCount - 1; j++) {
                combos[j] = combos[j + 1];
            }
            comboCount--;
            return true;
        }
    }
    return false;
}

void KeyComboManager::clearAll()
{
    comboCount = 0;
}

bool KeyComboManager::checkAndTrigger(ModifierKey modifier, uint8_t key, bool pressed)
{
    key = normalizeComboKey(key);

    if (!pressed) {
        return false; // Only trigger on press
    }

    for (int i = 0; i < comboCount; i++) {
        if (combos[i].modifier == modifier && combos[i].key == key) {
            if (combos[i].callback) {
                combos[i].callback();
                return true;
            }
        }
    }

    return false;
}
