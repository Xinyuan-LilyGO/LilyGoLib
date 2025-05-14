/**
 * @file      LilyGoKeyboard.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */
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

#define KB_ROWS                     4
#define KB_CLOS                     10

static constexpr char keymap[KB_ROWS][KB_CLOS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0'},
    {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};
static constexpr char symbol_map[KB_ROWS][KB_CLOS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' '/*Space*/, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

static constexpr uint8_t SYMBOL_KEY_PRESS_VALUE = 0x1E;
static constexpr uint8_t ALT_KEY_PRESS_VALUE = 0x14; //EMPTY
static constexpr uint8_t CAPS_KEY_PRESS_VALUE = 0x1C; //CAP CHAR
static constexpr uint8_t CHAR_B_VALUE = 0x19; //CHAR 'b
static constexpr uint8_t BACKSPACE_VALUE = 0x1D; // Backspace

static bool keyboard_interrupted = false;

static void keyboard_isr()
{
    keyboard_interrupted = true;
}

LilyGoKeyboard::LilyGoKeyboard()
{
}

LilyGoKeyboard::~LilyGoKeyboard()
{
}

void LilyGoKeyboard::setPins(uint8_t backlight)
{
    _backlight = backlight;
}

void LilyGoKeyboard::setBrightness(uint8_t level)
{
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

bool LilyGoKeyboard::begin(TwoWire &w,  uint8_t irq, uint8_t sda, uint8_t scl)
{
    if (_backlight > 0) {
        ::pinMode(_backlight, OUTPUT);
        ::digitalWrite(_backlight, LOW);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
        ledcAttach(_backlight, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
#else
        ledcSetup(LEDC_BACKLIGHT_CHANNEL, LEDC_BACKLIGHT_FREQ, LEDC_BACKLIGHT_BIT_WIDTH);
        ledcAttachPin(_backlight, LEDC_BACKLIGHT_CHANNEL);
#endif
    }
    bool res = Adafruit_TCA8418::begin(TCA8418_DEFAULT_ADDR, &w);
    if (!res) {
        log_e("Failed to find Keyboard");
        return false;
    } else {
        log_d("Initializing Keyboard succeeded");
    }
    // configure the size of the keypad matrix.
    // all other pins will be inputs
    this->matrix(KB_ROWS, KB_CLOS);
    // flush the internal buffer
    this->flush();

    if (irq > 0) {
        _irq = irq;
        ::pinMode(_irq, INPUT_PULLUP);
        attachInterrupt(_irq, keyboard_isr, CHANGE);
        log_d("Set keyboard input pull. pin %d", _irq);
        //  enable interrupt mode
        this->enableInterrupts();
    }
    return true;
}

void LilyGoKeyboard::end()
{
    if (_irq > 0) {
        this->disableInterrupts();
        detachInterrupt(_irq);
        ::pinMode(_irq, OPEN_DRAIN);
    }
    if (_backlight > 0) {
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

void LilyGoKeyboard::setRepeat(bool enable)
{
    repeat_function = enable;
}

int LilyGoKeyboard::getKey(char *c)
{
    uint32_t REPEAT_INTERVAL = 300;

    static char output;
    int val = -1;

    if (repeat_function) {
        if (lastState) {
            if (millis() - lastPressedTime > REPEAT_INTERVAL) {
                // The space key conflicts with the symbol function,
                // so the space key is not processed as a continuous key.
                if (lastKeyVal == ' ') {
                    lastState = false;
                    return -1;
                }
                Serial.printf("Pressed repeat %c\n", output);
                lastPressedTime = millis();
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


    if (!keyboard_interrupted) {
        return val;
    }
    //  try to clear the IRQ flag
    //  if there are pending events it is not cleared
    this->writeRegister(TCA8418_REG_INT_STAT, 1);
    uint8_t  intstat = this->readRegister(TCA8418_REG_INT_STAT);
    if ((intstat & 0x01) == 0) {
        keyboard_interrupted = false;
    }
    int ret = update(&output);
    if (cb) {
        cb(ret, output);
    }
    if (c) {
        *c = output;
    }
    return ret;
}

int LilyGoKeyboard::handleSpecialKeys(uint8_t k, bool pressed, char *c)
{
    switch (k) {
    case SYMBOL_KEY_PRESS_VALUE:
        symbol_key_pressed = !symbol_key_pressed;
        break;
    case CAPS_KEY_PRESS_VALUE:
        cap_key_pressed = !cap_key_pressed;
        return -1;
    case ALT_KEY_PRESS_VALUE:
        alt_key_pressed = !alt_key_pressed;
        return -1;
    case BACKSPACE_VALUE:
        if (pressed) {
            *c = 0x08;
            lastKeyVal = 0x08;
            lastState = true;
            lastPressedTime = millis();
            return KB_PRESSED;
        } else {
            lastState = false;
            lastPressedTime = 0;
        }
        return -1;
    default:
        break;
    }
    return 0;
}

bool LilyGoKeyboard::handleBrightnessAdjustment(uint8_t k, bool pressed)
{
    static bool adjust_brightness_pressed = false;

    if (pressed) {
        if (alt_key_pressed && k == CHAR_B_VALUE && _backlight > 0) {
            if (_brightness > 0) {
                setBrightness(0);
            } else {
                setBrightness(127);
            }
            adjust_brightness_pressed = true;
            return true;
        }
    } else if (adjust_brightness_pressed) {
        adjust_brightness_pressed = false;
        return true;
    }
    return false;
}

char LilyGoKeyboard::getKeyChar(uint8_t k)
{
    char keyVal;
    if (symbol_key_pressed) {
        keyVal = symbol_map[k / 10][k % 10];
    } else {
        keyVal = keymap[k / 10][k % 10];
        if (cap_key_pressed) {
            keyVal = toupper(keyVal);
        }
    }
    return keyVal;
}

char LilyGoKeyboard::handleSpaceAndNullChar(char keyVal, char &lastKeyVal, bool &pressed)
{
    if (symbol_key_pressed && keyVal == ' ') {
        keyVal = '\0';
    } else if (!symbol_key_pressed && lastKeyVal == '\0') {
        keyVal = ' ';
        pressed = true;
    }
    return keyVal;
}

void LilyGoKeyboard::printDebugInfo(bool pressed, uint8_t k, char keyVal)
{
    Serial.printf("symbol_key_pressed:%d lastKeyVal:%X\n", symbol_key_pressed, lastKeyVal);
    Serial.print("->");
    Serial.print(__func__);
    Serial.print(pressed ? " isPressed " : " isReleased ");
    Serial.print("key val:0x");
    Serial.print(k, HEX);
    Serial.printf(" k[%u][%u] ", k / 10, k % 10);
    Serial.print(" - Char:");
    Serial.print(keyVal);
    Serial.print(" - Hex:");
    Serial.println(keyVal, HEX);
}

int LilyGoKeyboard::update(char *c)
{
    char keyVal = '\n';
    uint8_t k = this->getEvent();
    if (k == 0) {
        return -1;
    }
    bool pressed = k & 0x80;
    k &= 0x7F;
    k--;
    uint8_t row = k / 10;
    if (row < 4) {
        int specialKeyResult = handleSpecialKeys(k, pressed, c);
        if (specialKeyResult != 0) {
            return specialKeyResult;
        }
        if (handleBrightnessAdjustment(k, pressed)) {
            return -1;
        }
        keyVal = getKeyChar(k);
        keyVal = handleSpaceAndNullChar(keyVal, lastKeyVal, pressed);
        // printDebugInfo(pressed, k, keyVal);
        lastKeyVal = keyVal;
        if (c) {
            *c = keyVal;
        }
    } else {
        return -1;
    }
    lastState = pressed;
    lastPressedTime = pressed ? millis() : 0;
    return pressed ? KB_PRESSED : KB_RELEASED;
}


#endif

