/**
 * @file      LilyGoKeyboard.h
 * @brief     Declares the TCA8418-based LilyGo keyboard driver wrapper.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-04
 *
 */

#pragma once
#include <Arduino.h>
#include "LilyGoKeyboardConfig.h"
#include "KeyComboManager.h"

#ifdef USING_INPUT_DEV_KEYBOARD
#include <Adafruit_TCA8418.h>

/** No keyboard event or no key available. */
#define KB_NONE     -1
/** Key pressed event state. */
#define KB_PRESSED  1
/** Key released event state. */
#define KB_RELEASED 0

/**
 * @brief Keyboard driver wrapper for TCA8418 matrix keyboard devices.
 */
class LilyGoKeyboard : public Adafruit_TCA8418
{
public:
    /**
     * @brief Callback invoked when a translated key event is read.
     * @param state Key state such as KB_PRESSED or KB_RELEASED.
     * @param c Translated key character.
     */
    using KeyboardReadCallback = void (*)(int state, char &c);

    /**
     * @brief Callback invoked for matrix pins used as GPIO inputs.
     * @param pressed true when the GPIO-style key is active.
     * @param gpio_idx GPIO index reported by the keyboard controller.
     */
    using GpioEventCallback = void (*)(bool pressed, uint8_t gpio_idx);

    /**
     * @brief Callback invoked when the keyboard backlight level changes.
     * @param level Backlight level.
     */
    using BacklightCallback = void (*)(uint8_t level);

    /**
     * @brief Callback invoked with raw keyboard controller events.
     * @param pressed true for press events, false for release events.
     * @param raw Raw key code from the controller.
     */
    using KeyboardRawCallback = void (*)(bool pressed, uint8_t raw);

    /**
     * @brief Default constructor for the LilyGoKeyboard class.
     * Initializes the keyboard object.
     */
    LilyGoKeyboard();

    /**
     * @brief Destructor for the LilyGoKeyboard class.
     * Cleans up any resources allocated by the keyboard object.
     */
    ~LilyGoKeyboard();

    /**
     * @brief Sets the pin number for the keyboard backlight.
     *
     * @param backlight The pin number to which the backlight is connected.
     */
    void setPins(int backlight);

    // /**
    //  * @brief Set the keyboard mapping index.
    //  *
    //  * @param maps T-LoRa-Pager uses 0, T-DeckV2 uses 1.
    //  */
    // void setMaps(uint8_t maps);

    /**
     * @brief Initializes the keyboard with the new configuration structure.
     *
     * @param config A reference to a LilyGoKeyboardConfig object containing layout and modifier key configuration.
     * @param w A reference to a TwoWire object for I2C communication.
     * @param irq The interrupt request pin number.
     * @param sda The I2C data line pin number. Defaults to the SDA macro.
     * @param scl The I2C clock line pin number. Defaults to the SCL macro.
     * @return true if the initialization is successful, false otherwise.
     */
    bool begin(const LilyGoKeyboardConfig &config, TwoWire &w, uint8_t irq, uint8_t sda = SDA, uint8_t scl = SCL);

    /**
     * @brief Ends the keyboard operation and releases associated resources.
     */
    void end();

    /**
     * @brief Retrieves the currently pressed key and stores its character value.
     *
     * @param c A pointer to a character where the key value will be stored.
     * @return An integer representing the state of the key press.
     */
    int getKey(char *c);

    /**
     * @brief Sets the brightness level of the keyboard backlight.
     *
     * @param level The brightness level, with a valid range of 0-255.
     */
    void setBrightness(uint8_t level);

    /**
     * @brief Gets the current brightness level of the keyboard backlight.
     *
     * @return The current brightness level as an 8-bit unsigned integer.
     */
    uint8_t getBrightness();

    /**
     * @brief Sets the callback function to be executed when a key is read.
     *
     * @param cb A pointer to the callback function of type KeyboardReadCallback.
     */
    void setCallback(KeyboardReadCallback cb);

    /**
     * @brief When the row and column gpio of an undefined keyboard are changed,
     *      the host is notified through this callback function.
     *
     * @param cb A pointer to the callback function of type GpioEventCallback.
     */
    void setGpioEventCallback(GpioEventCallback cb);

    /**
     * @brief When the backlight callback function is set, the host will be
     *      notified of the backlight adjustment through the callback function.
     *
     * @param cb A pointer to the callback function of type BacklightCallback.
     */
    void setBacklightChangeCallback(BacklightCallback cb);

    /**
     * @brief Set keyboard press or release raw callback function
     * @note  When this callback is set, the program only returns the original key value and does
     *          not continue with subsequent processing. The user needs to handle it by himself.
     *
     * @param cb A pointer to the callback function of type KeyboardRawCallback.
     */
    void setRawCallback(KeyboardRawCallback cb);

    /**
     * @brief Enables or disables the key repeat functionality.
     *
     * @param enable true to enable key repeat, false to disable it.
     */
    void setRepeat(bool enable);

    /**
     * @brief Sets the key repeat timing configuration.
     *
     * @param config The KeyRepeatConfig with initialDelay and repeatInterval values.
     */
    void setKeyRepeatConfig(const KeyRepeatConfig &config);

    /**
     * @brief Gets the current key repeat timing configuration.
     *
     * @return The current KeyRepeatConfig.
     */
    KeyRepeatConfig getKeyRepeatConfig() const;

    /**
     * @brief Registers a custom key combination.
     *
     * @param modifier The modifier key enum (e.g., ModifierKey::FN, ModifierKey::ALT).
     * @param key The normal key value (ASCII or raw key code).
     * @param callback The callback function to invoke when the combo is triggered.
     * @return true if the combo was registered successfully, false otherwise.
     */
    bool registerKeyCombo(ModifierKey modifier, uint8_t key, KeyComboManager::ComboCallback callback);

    /**
     * @brief Unregisters a custom key combination.
     *
     * @param modifier The modifier key enum.
     * @param key The normal key value.
     * @return true if the combo was found and removed, false otherwise.
     */
    bool unregisterKeyCombo(ModifierKey modifier, uint8_t key);

    /**
     * @brief Clears all registered key combinations.
     */
    void clearKeyCombos();

private:
    /**
     * @brief Updates the keyboard state and retrieves the currently pressed key.
     *
     * @param c A pointer to a character where the key value will be stored.
     * @return An integer representing the state of the key press.
     */
    int update(char *c);

    /**
     * @brief Prints debug information about the key press event.
     *
     * @param pressed true if the key is currently pressed, false if released.
     * @param k The key code of the key event.
     * @param keyVal The character value corresponding to the key code.
     */
    void printDebugInfo(bool pressed, uint8_t k, char keyVal);

    /**
     * @brief Handles special cases for space and null characters.
     *
     * @param keyVal The character value of the currently pressed key.
     * @param lastKeyVal A reference to the last key value.
     * @param pressed A reference to a boolean indicating if the key is pressed.
     * @return The processed character value.
     */
    char handleSpaceAndNullChar(char keyVal, char &lastKeyVal, bool &pressed);

    /**
     * @brief Converts a key code to its corresponding character value.
     *
     * @param k The key code.
     * @return The character value corresponding to the key code.
     */
    char getKeyChar(uint8_t k);

    /**
     * @brief Handles special keys and their associated actions.
     *
     * @param k The key code of the special key.
     * @param pressed true if the key is pressed, false if released.
     * @param c A pointer to a character to store the result of the special key action.
     * @return An integer representing the state of the special key action.
     */
    int handleSpecialKeys(uint8_t k, bool pressed, char *c);

    /**
     * @brief Identifies which modifier key a given key code corresponds to.
     *
     * @param k The key code to identify.
     * @return The ModifierKey enum value, or ModifierKey::NONE if not a modifier.
     */
    ModifierKey identifyModifierKey(uint8_t k);

    /**
     * @brief Returns true if the key activates the symbol layer.
     *
     * @param k The key code to check.
     * @return true when k is a dedicated symbol key or the configured Space symbol prefix.
     */
    bool isSymbolLayerKey(uint8_t k) const;

    /**
     * @brief Returns true if the key is Space acting as the temporary symbol prefix.
     *
     * @param k The key code to check.
     * @return true when k is the configured Space symbol prefix key.
     */
    bool isSpaceSymbolLayerKey(uint8_t k) const;

    /** Last translated key value. */
    char lastKeyVal = '\n';
    /** Backlight control pin. */
    int _backlight = -1;
    /** Current keyboard backlight level. */
    uint8_t _brightness;
    /** Keyboard interrupt pin. */
    uint8_t _irq;
    /** true while the symbol key is pressed. */
    bool symbol_key_pressed = false;
    /** true while the caps key is pressed. */
    bool cap_key_pressed = false;
    /** true while the alt key is pressed. */
    bool alt_key_pressed = false;
    /** true when key repeat is enabled. */
    bool repeat_function = true;
    /** Previous key state used for edge detection. */
    bool lastState = false;
    /** Translated key callback. */
    KeyboardReadCallback cb = NULL;
    /** GPIO-style matrix callback. */
    GpioEventCallback gpio_cb = NULL;
    /** Backlight change callback. */
    BacklightCallback bl_cb = NULL;
    /** Raw key event callback. */
    KeyboardRawCallback raw_cb = NULL;
    /** millis() timestamp of the last key press. */
    uint32_t lastPressedTime = 0;
    /** Current keyboard configuration. */
    LilyGoKeyboardConfig _config = {};
    /** Manager for user-defined key combinations. */
    KeyComboManager _comboManager;
    /** Tracks which modifier keys are currently pressed. */
    bool _modifierStates[static_cast<int>(ModifierKey::MAX_MODIFIERS)] = {};
    /** Tracks whether a combo key press is active so its release can be consumed. */
    bool _comboKeyActive = false;
    /** Modifier that triggered the active combo. */
    ModifierKey _comboModifier = ModifierKey::NONE;
    /** Raw key that triggered the active combo. */
    uint8_t _comboRawKey = 0xFF;
    /** Logical key that triggered the active combo. */
    uint8_t _comboLogicalKey = 0;
    /** true while Space is pending as a symbol-prefix key. */
    bool _spaceSymbolPending = false;
    /** true when the pending Space symbol prefix has been used. */
    bool _spaceSymbolUsed = false;
    /** Key repeat timing configuration. */
    KeyRepeatConfig _repeatConfig = {500, 40};
    /** true after the initial repeat delay has elapsed. */
    bool _repeatStarted = false;

};
#endif
