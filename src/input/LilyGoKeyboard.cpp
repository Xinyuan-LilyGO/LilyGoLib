/**
 * @file      LilyGoKeyboard.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
#include "LilyGoLog.h"
#include "LilyGoKeyboard.h"

#ifdef USING_INPUT_DEV_KEYBOARD

#ifndef LEDC_BACKLIGHT_CHANNEL
#define LEDC_BACKLIGHT_CHANNEL      4
#endif

#ifndef LEDC_BACKLIGHT_BIT_WIDTH
#define LEDC_BACKLIGHT_BIT_WIDTH    8
#endif

#ifndef LEDC_BACKLIGHT_FREQ
#define LEDC_BACKLIGHT_FREQ         1000 //HZ
#endif


static bool keyboard_interrupted = false;

static void keyboard_isr()
{
    keyboard_interrupted = true;
}

static uint8_t normalizeComboKey(uint8_t key)
{
    if (key >= 'a' && key <= 'z') {
        return key - ('a' - 'A');
    }
    return key;
}

LilyGoKeyboard::LilyGoKeyboard()
    : _backlight(-1), _brightness(0), _irq(0), cb(nullptr),
      repeat_function(false), symbol_key_pressed(false),
      cap_key_pressed(false), alt_key_pressed(false),
      lastState(false),  lastKeyVal('\0'), lastPressedTime(0),
      _spaceSymbolPending(false), _spaceSymbolUsed(false)
{
}

LilyGoKeyboard::~LilyGoKeyboard()
{
}

void LilyGoKeyboard::setPins(int backlight)
{
    _backlight = backlight;
}

void LilyGoKeyboard::setBrightness(uint8_t level)
{
    if (this->bl_cb) {
        this->bl_cb(level);
        return;
    }
    if (_backlight == -1) {
        return;
    }
    _brightness = level;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
    ledcWrite(_backlight, _brightness);
#else
    ledcWrite(LEDC_BACKLIGHT_CHANNEL, _brightness);
#endif
}

uint8_t LilyGoKeyboard::getBrightness()
{
    return _brightness;
}

bool LilyGoKeyboard::begin(const LilyGoKeyboardConfig &config, TwoWire &w, uint8_t irq, uint8_t sda, uint8_t scl)
{
    _config = config;
    _comboManager.clearAll();
    for (int i = 0; i < static_cast<int>(ModifierKey::MAX_MODIFIERS); i++) {
        _modifierStates[i] = false;
    }
    _comboKeyActive = false;
    _comboModifier = ModifierKey::NONE;
    _comboRawKey = 0xFF;
    _comboLogicalKey = 0;
    _spaceSymbolPending = false;
    _spaceSymbolUsed = false;

    if (_backlight != -1) {
        ::pinMode(_backlight, OUTPUT);
        ::digitalWrite(_backlight, LOW);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        ledcAttach(_backlight, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
#else
        ledcSetup(LEDC_BACKLIGHT_CHANNEL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
        ledcAttachPin(_backlight, LEDC_BACKLIGHT_CHANNEL);
#endif
        setBrightness(127);
    }


    bool res = Adafruit_TCA8418::begin(TCA8418_DEFAULT_ADDR, &w);
    if (!res) {
        LILYGO_LOG_E("Failed to find Keyboard");
        return false;
    }

    symbol_key_pressed = false;
    cap_key_pressed = false;
    alt_key_pressed = false;
    _spaceSymbolPending = false;
    _spaceSymbolUsed = false;
    lastState = false;
    lastKeyVal = '\0';
    lastPressedTime = 0;

    LILYGO_LOG_D("Initializing Keyboard succeeded");

    // Configure the matrix size (using the number of rows and columns currently mapped)
    LILYGO_LOG_D("set matrix : rows: %d  cols: %d\n", _config.layout.kb_rows, _config.layout.kb_cols);
    this->matrix(_config.layout.kb_rows, _config.layout.kb_cols);
    this->flush();

    if (irq > 0) {
        _irq = irq;
        ::pinMode(_irq, INPUT_PULLUP);
        attachInterrupt(_irq, keyboard_isr, CHANGE);
        LILYGO_LOG_D("Set keyboard input pull. pin %d", _irq);
        this->enableInterrupts();
    }
    return true;
}

void LilyGoKeyboard::end()
{
    setBrightness(0);
    
    if (_irq > 0) {
        this->disableInterrupts();
        detachInterrupt(_irq);
        ::pinMode(_irq, OPEN_DRAIN);
    }
    if (_backlight != -1) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        ledcDetach(_backlight);
#else
        ledcDetachPin(_backlight);
#endif
        ::pinMode(_backlight, OPEN_DRAIN);
    }
    for (int pin = 0; pin < 18; pin++) {
        this->pinMode(pin, INPUT);
    }
}

void LilyGoKeyboard::setCallback(KeyboardReadCallback cb)
{
    this->cb = cb;
}

void LilyGoKeyboard::setGpioEventCallback(GpioEventCallback cb)
{
    this->gpio_cb = cb;
}

void LilyGoKeyboard::setBacklightChangeCallback(BacklightCallback cb)
{
    this->bl_cb = cb;
}

void LilyGoKeyboard::setRawCallback(KeyboardRawCallback cb)
{
    this->raw_cb = cb;
}

void LilyGoKeyboard::setRepeat(bool enable)
{
    repeat_function = enable;
}

void LilyGoKeyboard::setKeyRepeatConfig(const KeyRepeatConfig &config)
{
    _repeatConfig = config;
}

KeyRepeatConfig LilyGoKeyboard::getKeyRepeatConfig() const
{
    return _repeatConfig;
}

bool LilyGoKeyboard::registerKeyCombo(ModifierKey modifier, uint8_t key, KeyComboManager::ComboCallback callback)
{
    return _comboManager.registerCombo(modifier, key, callback);
}

bool LilyGoKeyboard::unregisterKeyCombo(ModifierKey modifier, uint8_t key)
{
    return _comboManager.unregisterCombo(modifier, key);
}

void LilyGoKeyboard::clearKeyCombos()
{
    _comboManager.clearAll();
}

bool LilyGoKeyboard::isSpaceSymbolLayerKey(uint8_t k) const
{
    return _config.layout.space_as_symbol_key &&
           !_config.layout.has_symbol_key &&
           k == _config.modifiers.symbol_key_value;
}

bool LilyGoKeyboard::isSymbolLayerKey(uint8_t k) const
{
    if (_config.layout.has_symbol_key && k == _config.modifiers.symbol_key_value) {
        return true;
    }
    return isSpaceSymbolLayerKey(k);
}

ModifierKey LilyGoKeyboard::identifyModifierKey(uint8_t k)
{
    const ModifierKeyConfig &mods = _config.modifiers;
    if (isSymbolLayerKey(k)) return ModifierKey::SYMBOL;
    if (k == mods.alt_key_value) return ModifierKey::ALT;
    if (k == mods.caps_key_value) return ModifierKey::CAPS;
    if (k == mods.caps_b_key_value) return ModifierKey::CAPS_B;
    if (mods.fn_key_value != 0xFF && k == mods.fn_key_value) return ModifierKey::FN;
    if (mods.ctrl_key_value != 0xFF && k == mods.ctrl_key_value) return ModifierKey::CTRL;
    if (mods.shift_key_value != 0xFF && k == mods.shift_key_value) return ModifierKey::SHIFT;

    return ModifierKey::NONE;
}

int LilyGoKeyboard::getKey(char *c)
{
    static char output;
    static uint32_t interval = 0;
    int val = -1;

    if (millis() - interval > 100) {
        // Polling detects whether there is an ignored state in the interrupt status that has not been processed.
        // The polling speed affects the response speed of the keyboard.
        interval = millis();
        this->readRegister(TCA8418_REG_INT_STAT);
        if (this->available() != 0 && !keyboard_interrupted) {
            keyboard_interrupted = true;
        }
    }

    if (repeat_function) {
        if (lastState) {
            // The space key conflicts with the symbol function,
            // so the space key is not processed as a continuous key.
            if (lastKeyVal == 0 || lastKeyVal == ' ') {
                lastState = false;
                _repeatStarted = false;
                return -1;
            }

            uint32_t elapsed = millis() - lastPressedTime;

            if (!_repeatStarted) {
                // Waiting for initial delay
                if (elapsed >= _repeatConfig.initialDelay) {
                    _repeatStarted = true;
                    lastPressedTime = millis();
                    LILYGO_LOG_D("Pressed repeat start %c\n", output);
                    if (c) {
                        *c = output;
                    }
                    if (cb) {
                        cb(KB_PRESSED, output);
                    }
                    return KB_PRESSED;
                }
            } else {
                // In repeat mode - emit at repeatInterval
                if (elapsed >= _repeatConfig.repeatInterval) {
                    lastPressedTime = millis();
                    LILYGO_LOG_D("Pressed repeat %c\n", output);
                    if (c) {
                        *c = output;
                    }
                    if (cb) {
                        cb(KB_PRESSED, output);
                    }
                    return KB_PRESSED;
                }
            }
        }
    }

    if (!keyboard_interrupted) {
        return val;
    }

    int intStat = this->readRegister(TCA8418_REG_INT_STAT);
    if (intStat & 0x02) {
        //  reading the registers is mandatory to clear IRQ flag
        //  can also be used to find the GPIO changed
        //  as these registers are a bitmap of the gpio pins.
        this->readRegister(TCA8418_REG_GPIO_INT_STAT_1);
        this->readRegister(TCA8418_REG_GPIO_INT_STAT_2);
        this->readRegister(TCA8418_REG_GPIO_INT_STAT_3);
        //  clear GPIO IRQ flag
        this->writeRegister(TCA8418_REG_INT_STAT, 2);
    }


    // Clear IRQ flag
    this->writeRegister(TCA8418_REG_INT_STAT, 1);
    uint8_t intstat = this->readRegister(TCA8418_REG_INT_STAT);
    if ((intstat & 0x01) == 0) {
        keyboard_interrupted = false;
    }

    int ret = update(&output);
    if (ret != KB_NONE && cb) {
        cb(ret, output);
    }
    if (ret != KB_NONE && c) {
        *c = output;
    } else if (ret == KB_NONE && c) {
        *c = '\0';
    }
    // Serial.printf("Update \"%c\" sate:%s\n", output, ret > 0 ? "Pressed" : "Released");
    return ret;
}


int LilyGoKeyboard::handleSpecialKeys(uint8_t k, bool pressed, char *c)
{
    if (isSymbolLayerKey(k)) {
        if (isSpaceSymbolLayerKey(k)) {
            if (pressed) {
                symbol_key_pressed = true;
                _spaceSymbolPending = true;
                _spaceSymbolUsed = false;
                lastState = false;
                _repeatStarted = false;
                if (c) {
                    *c = '\0';
                }
                return KB_NONE;
            }

            symbol_key_pressed = false;
            bool emitSpace = _spaceSymbolPending && !_spaceSymbolUsed;
            _spaceSymbolPending = false;
            _spaceSymbolUsed = false;
            lastState = false;
            _repeatStarted = false;
            lastPressedTime = 0;
            if (emitSpace) {
                lastKeyVal = ' ';
                if (c) {
                    *c = ' ';
                }
                return KB_PRESSED;
            }
            if (c) {
                *c = '\0';
            }
            return KB_NONE;
        }

        symbol_key_pressed = pressed;
        return KB_NONE;
    } else if (k == _config.modifiers.caps_key_value || k == _config.modifiers.caps_b_key_value) {
        cap_key_pressed = pressed;
        return KB_NONE;
    } else if (k == _config.modifiers.alt_key_value) {
        alt_key_pressed = pressed;
        return KB_NONE;
    } else if (k == _config.modifiers.backspace_value) {
        if (pressed) {
            *c = '\b'; // Backspace character
            lastKeyVal = '\b';
            lastState = true;
            lastPressedTime = millis();
            return KB_PRESSED;
        } else {
            lastState = false;
            lastPressedTime = 0;
        }
        return -1;
    }
    return 0;
}

char LilyGoKeyboard::getKeyChar(uint8_t k)
{
    uint8_t row = k / 10;
    uint8_t col = k % 10;

    if (row >= _config.layout.kb_rows || col >= _config.layout.kb_cols) {
        LILYGO_LOG_E("Returns a null character if out of bounds");
        return '\0'; // Return empty character if out of bounds
    }

    char keyVal;
    if (symbol_key_pressed) {
        // Symbol mode: access the current symbol map (first address + offset)
        keyVal = *(_config.layout.current_symbol_map + row * _config.layout.kb_cols + col);
    } else {
        // Character mode: access the current character map
        keyVal = *(_config.layout.current_keymap + row * _config.layout.kb_cols + col);
        // Uppercase conversion (skipping null characters)
        if (cap_key_pressed && keyVal != '\0') {
            keyVal = toupper(keyVal);
        }
    }
    return keyVal;
}

char LilyGoKeyboard::handleSpaceAndNullChar(char keyVal, char &lastKeyVal, bool &pressed)
{

#if 0
    if (_config.layout.has_symbol_key) {
        if (symbol_key_pressed) {
            if (keyVal == ' ') {
                keyVal = '\0'; // Spaces are invalid in symbolic mode
            }
        } else {
            if (keyVal == '\0' && lastKeyVal == '\0' && pressed) {
                keyVal = ' '; // In character mode, consecutive empty characters are considered spaces
            }
        }
    } else {
        if (symbol_key_pressed && keyVal == ' ') {
            keyVal = '\0';
        } else if (!symbol_key_pressed && lastKeyVal == '\0') {
            keyVal = ' ';
            pressed = true;
        }
    }
#else
    // 符号模式下空格无效，统一转换为'\0'
    if (symbol_key_pressed && keyVal == ' ') {
        keyVal = '\0';
    }
    // 非符号模式下处理空格逻辑
    else if (!symbol_key_pressed) {
        // 有符号键的配置：连续空字符视为空格
        if (_config.layout.has_symbol_key) {
            if (keyVal == '\0' && lastKeyVal == '\0' && pressed) {
                keyVal = ' ';
            }
        }
    }
#endif
    return keyVal;
}

void LilyGoKeyboard::printDebugInfo(bool pressed, uint8_t k, char keyVal)
{
    LILYGO_LOG_PRINTF("Debug: symbol=%d, caps=%d, alt=%d\n",
                  symbol_key_pressed, cap_key_pressed, alt_key_pressed);
    LILYGO_LOG_PRINT(pressed ? "Pressed" : "Released");
    LILYGO_LOG_PRINTF(" - Key:0x%X, Row:%d, Col:%d\n",
                  k, k / 10, k % 10);
    LILYGO_LOG_PRINTF("Char:'%c' (0x%X)\n", keyVal, keyVal);
}

int LilyGoKeyboard::update(char *c)
{
    char keyVal = '\0';
    uint8_t k = this->getEvent();
    if (k == 0) {
        return -1; // No event
    }

    bool pressed = (k & 0x80) != 0; //The highest bit indicates the pressed state
    k &= 0x7F; // Clear the status bit and keep the original key value

    // When this callback is set, the program only returns the original key
    // value and does not continue with subsequent processing. The user needs to handle it by himself.
    if (this->raw_cb) {
        this->raw_cb(pressed, k);
        return -1;
    }

    if (k > 96) {
        uint8_t idx = k - 97;
        if (this->gpio_cb) {
            this->gpio_cb(pressed, idx);
        }
        return -1;
    }

    k--; // Adjust key value index
    // Serial.printf("Value:%X\n", k);

    // Check if the key value is within the current mapping range
    uint8_t row = k / 10;
    if (row >= _config.layout.kb_rows) {
        LILYGO_LOG_E("Key values out of range are ignored,current  row:%d k:%d , _config.layout.kb_cols:%d\n", row, k, _config.layout.kb_cols);
        return -1;
    }

    // Identify modifier key and track its state
    ModifierKey modKey = identifyModifierKey(k);
    if (modKey != ModifierKey::NONE) {
        _modifierStates[static_cast<int>(modKey)] = pressed;
    }

    if (pressed && _spaceSymbolPending && !isSpaceSymbolLayerKey(k)) {
        _spaceSymbolUsed = true;
    }

    // Check custom key combos before special key handling (combo-first priority)
    if (modKey == ModifierKey::NONE) {
        keyVal = getKeyChar(k);
        uint8_t logicalComboKey = normalizeComboKey(static_cast<uint8_t>(keyVal));

        if (!pressed && _comboKeyActive && (_comboRawKey == k || _comboLogicalKey == logicalComboKey)) {
            _comboKeyActive = false;
            _comboModifier = ModifierKey::NONE;
            _comboRawKey = 0xFF;
            _comboLogicalKey = 0;
            lastState = false;
            _repeatStarted = false;
            if (c) {
                *c = '\0';
            }
            return KB_NONE;
        }

        // Find the currently active modifier
        ModifierKey activeModifier = ModifierKey::NONE;
        for (int i = static_cast<int>(ModifierKey::NONE) + 1;
             i < static_cast<int>(ModifierKey::MAX_MODIFIERS); i++) {
            if (_modifierStates[i]) {
                activeModifier = static_cast<ModifierKey>(i);
                break;
            }
        }

        if (pressed && activeModifier != ModifierKey::NONE) {
            bool comboHandled = false;
            if (logicalComboKey != 0) {
                comboHandled = _comboManager.checkAndTrigger(activeModifier, logicalComboKey, true);
            }
            if (!comboHandled) {
                comboHandled = _comboManager.checkAndTrigger(activeModifier, k, true);
            }

            if (comboHandled) {
                _comboKeyActive = true;
                _comboModifier = activeModifier;
                _comboRawKey = k;
                _comboLogicalKey = logicalComboKey;
                lastState = false;
                _repeatStarted = false;
                if (c) {
                    *c = '\0';
                }
                return KB_NONE;
            }
        }
    }

    // Handling special keys
    int specialKeyResult = handleSpecialKeys(k, pressed, c);
    if (specialKeyResult != 0) {
        return specialKeyResult;
    }

    // Get the character corresponding to the current key
    if (keyVal == '\0') {
        keyVal = getKeyChar(k);
    }
    // Handling spaces and null characters
    keyVal = handleSpaceAndNullChar(keyVal, lastKeyVal, pressed);

    // Print debug information
    // printDebugInfo(pressed, k, keyVal);

    // Update state variables
    lastKeyVal = keyVal;
    if (c) {
        *c = keyVal;
    }

    lastState = pressed;
    lastPressedTime = pressed ? millis() : 0;
    if (!pressed) {
        _repeatStarted = false;
    }

    return pressed ? KB_PRESSED : KB_RELEASED;
}


#endif // USING_INPUT_DEV_KEYBOARD
