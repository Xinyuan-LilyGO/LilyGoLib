#include "vibe_input.h"

#include <LV_Helper.h>
#include <ctype.h>

namespace {

constexpr uint8_t KEYBOARD_ROWS = 4;
constexpr uint8_t KEYBOARD_COLS = 10;
constexpr uint8_t RAW_FN = 0x14 + 1;
constexpr uint8_t RAW_CAP = 0x1C + 1;
constexpr uint8_t KEY_SYMBOL = 0x1E;
constexpr uint8_t KEY_BACKSPACE = 0x1D;
constexpr uint8_t KEY_B = 0x19;

constexpr char KEYMAP[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0'},
    {' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'},
};

constexpr char SYMBOL_MAP[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'},
};

LilyGoLoRaPager *s_board = nullptr;
vk_input_key_state_cb_t s_key_state_callback = nullptr;

bool s_raw_event_ready = false;
bool s_raw_pressed = false;
uint8_t s_raw_key = 0;

bool s_symbol_pressed = false;
bool s_cap_pressed = false;
bool s_alt_pressed = false;
bool s_brightness_adjusted = false;
char s_last_key_value = '\0';

void raw_key_callback(bool pressed, uint8_t raw)
{
    s_raw_pressed = pressed;
    s_raw_key = raw;
    s_raw_event_ready = true;
}

bool focused_object_is_textarea(lv_indev_t *indev)
{
    lv_group_t *group = lv_indev_get_group(indev);
    if (!group) {
        group = lv_group_get_default();
    }
    if (!group) {
        return false;
    }

    lv_obj_t *focused = lv_group_get_focused(group);
    while (focused) {
        if (lv_obj_check_type(focused, &lv_textarea_class)) {
            return true;
        }
        focused = lv_obj_get_parent(focused);
    }
    return false;
}

int translate_raw_key(bool &pressed, uint8_t raw, char &key)
{
    if (raw == 0 || raw > KEYBOARD_ROWS * KEYBOARD_COLS) {
        return KB_NONE;
    }

    const uint8_t index = raw - 1;
    if (index == KEY_SYMBOL) {
        s_symbol_pressed = !s_symbol_pressed;
    } else if (raw == RAW_CAP) {
        s_cap_pressed = !s_cap_pressed;
        return KB_NONE;
    } else if (raw == RAW_FN) {
        s_alt_pressed = !s_alt_pressed;
        return KB_NONE;
    } else if (index == KEY_BACKSPACE) {
        if (!pressed) {
            return KB_NONE;
        }
        key = '\b';
        s_last_key_value = key;
        return KB_PRESSED;
    }

    if (pressed && s_alt_pressed && index == KEY_B) {
        const uint8_t brightness = s_board->kb.getBrightness() > 0 ? 0 : 127;
        s_board->kb.setBrightness(brightness);
        s_brightness_adjusted = true;
        return KB_NONE;
    }
    if (!pressed && s_brightness_adjusted) {
        s_brightness_adjusted = false;
        return KB_NONE;
    }

    const uint8_t row = index / KEYBOARD_COLS;
    const uint8_t col = index % KEYBOARD_COLS;
    key = s_symbol_pressed ? SYMBOL_MAP[row][col] : KEYMAP[row][col];
    if (!s_symbol_pressed && s_cap_pressed && key != '\0') {
        key = (char)toupper((unsigned char)key);
    }

    if (s_symbol_pressed && key == ' ') {
        key = '\0';
    } else if (!s_symbol_pressed && s_last_key_value == '\0') {
        key = ' ';
        pressed = true;
    }

    s_last_key_value = key;
    return pressed ? KB_PRESSED : KB_RELEASED;
}

void keyboard_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    static bool last_pressed = false;
    static bool last_raw_was_app_key = false;

    char ignored = '\0';
    s_raw_event_ready = false;
    s_board->getKeyChar(&ignored);

    if (!s_raw_event_ready) {
        data->key = last_key;
        data->state = last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }

    bool pressed = s_raw_pressed;
    const uint8_t raw = s_raw_key;
    if (!pressed) {
        if (!last_raw_was_app_key) {
            char released_key = '\0';
            translate_raw_key(pressed, raw, released_key);
        }
        last_raw_was_app_key = false;
        last_pressed = false;
        if (s_key_state_callback) {
            s_key_state_callback(last_key, false);
        }
        data->key = last_key;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!focused_object_is_textarea(indev) && (raw == RAW_CAP || raw == RAW_FN)) {
        last_raw_was_app_key = true;
        last_key = raw == RAW_CAP ? VK_KEY_CAP : VK_KEY_FN;
        last_pressed = true;
        if (s_key_state_callback) {
            s_key_state_callback(last_key, true);
        }
        data->key = last_key;
        data->state = LV_INDEV_STATE_PRESSED;
        s_board->feedback(indev);
        return;
    }

    last_raw_was_app_key = false;
    char key = '\0';
    const int state = translate_raw_key(pressed, raw, key);
    if (state == KB_PRESSED) {
        last_key = (uint8_t)key;
        last_pressed = true;
        if (s_key_state_callback) {
            s_key_state_callback(last_key, true);
        }
        data->key = last_key;
        data->state = LV_INDEV_STATE_PRESSED;
        s_board->feedback(indev);
        return;
    }

    data->key = last_key;
    data->state = last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t accumulated = 0;
    static bool center_was_pressed = false;
    const RotaryMsg_t message = s_board->getRotary();

    if (message.dir == ROTARY_DIR_UP) {
        accumulated++;
        s_board->feedback(indev);
    } else if (message.dir == ROTARY_DIR_DOWN) {
        accumulated--;
        s_board->feedback(indev);
    }

    data->enc_diff = accumulated;
    accumulated = 0;
    if (message.centerBtnPressed && !center_was_pressed) {
        s_board->feedback(indev);
    }
    center_was_pressed = message.centerBtnPressed;
    data->state = message.centerBtnPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace

void vk_input_begin(LilyGoLoRaPager &board)
{
    s_board = &board;

    lv_indev_t *library_keyboard = lv_get_keyboard_indev();
    if (library_keyboard) {
        lv_indev_enable(library_keyboard, false);
        board.kb.setRawCallback(raw_key_callback);

        lv_indev_t *keyboard = lv_indev_create();
        lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(keyboard, keyboard_read);
        lv_indev_set_display(keyboard, lv_display_get_default());
        lv_indev_set_group(keyboard, lv_group_get_default());
    }

    lv_indev_t *library_encoder = lv_get_encoder_indev();
    if (library_encoder) {
        lv_indev_enable(library_encoder, false);

        lv_indev_t *encoder = lv_indev_create();
        lv_indev_set_type(encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(encoder, encoder_read);
        lv_indev_set_display(encoder, lv_display_get_default());
        lv_indev_set_group(encoder, lv_group_get_default());
    }
}

void vk_input_set_key_state_callback(vk_input_key_state_cb_t callback)
{
    s_key_state_callback = callback;
}
