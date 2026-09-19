/**
 * @file      ui_display_test.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025 ShenZhen XinYuan Electronic Technology Co., Ltd
 */
#include "ui_define.h"
#include <atomic>

enum display_test_kind_t { SOLID, BARS, GRAY, GRID, CHECKER };

static const struct {
    const char *title;
    display_test_kind_t kind;
    uint32_t color;
} patterns[] = {
    {"Black", SOLID, 0x000000},
    {"White", SOLID, 0xFFFFFF},
    {"Red", SOLID, 0xFF0000},
    {"Green", SOLID, 0x00FF00},
    {"Blue", SOLID, 0x0000FF},
    {"Cyan", SOLID, 0x00FFFF},
    {"Magenta", SOLID, 0xFF00FF},
    {"Yellow", SOLID, 0xFFFF00},
    {"Gray 50%", SOLID, 0x808080},
    {"RGB Bars", BARS, 0},
    {"Gray Steps", GRAY, 0},
    {"Grid", GRID, 0},
    {"Checker", CHECKER, 0},
};
static const int pattern_count = sizeof(patterns) / sizeof(patterns[0]);

static lv_obj_t *test_obj = NULL;
static lv_obj_t *overlay = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *control_buttons[4] = {};
static lv_group_t *test_group = NULL;
static lv_group_t *previous_group = NULL;
static lv_timer_t *input_timer = NULL;
static int pattern_index = 0;
static std::atomic<int> pending_steps(0);
static std::atomic<bool> pending_exit(false);

static void show_controls(bool visible)
{
    lv_group_remove_all_objs(test_group);
    if (visible) {
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        for (lv_obj_t *button : control_buttons) lv_group_add_obj(test_group, button);
        lv_group_focus_obj(control_buttons[1]);
        lv_group_set_editing(test_group, false);
    } else {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_group_add_obj(test_group, test_obj);
        lv_group_focus_obj(test_obj);
        lv_group_set_editing(test_group, true);
    }
}

static void step_pattern(int delta)
{
    if (!test_obj) return;
    pattern_index = (pattern_index + delta % pattern_count + pattern_count) % pattern_count;
    lv_obj_set_style_bg_color(test_obj, lv_color_hex(patterns[pattern_index].color), 0);
    lv_label_set_text_fmt(title_label, "%02d/%02d  %s", pattern_index + 1,
                         pattern_count, patterns[pattern_index].title);
    lv_obj_invalidate(test_obj);
}

static void cleanup()
{
    hw_set_button_callback(NULL);
    if (input_timer) {
        lv_timer_del(input_timer);
        input_timer = NULL;
    }
    if (test_group) set_default_group(previous_group);
    if (test_obj) {
        lv_obj_del(test_obj);
        test_obj = NULL;
    }
    if (test_group) {
        lv_group_del(test_group);
        test_group = NULL;
    }
    previous_group = NULL;
    overlay = NULL;
    title_label = NULL;
    for (lv_obj_t *&button : control_buttons) button = NULL;
    pending_steps.store(0);
    pending_exit.store(false);
    set_low_power_mode_flag(true);
}

static void cleanup_and_exit()
{
    cleanup();
    menu_show();
}

// Draw directly into LVGL's layer so large checker patterns need no image buffer
// or per-cell widgets, and cannot intercept touch events.
static void draw_pattern(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t bounds;
    lv_obj_get_coords(test_obj, &bounds);
    int w = lv_area_get_width(&bounds);
    int h = lv_area_get_height(&bounds);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    auto draw_rect = [&](int x, int y, int width, int height, uint32_t color) {
        lv_area_t area = {bounds.x1 + x, bounds.y1 + y,
                          bounds.x1 + x + width - 1, bounds.y1 + y + height - 1};
        rect.bg_color = lv_color_hex(color);
        lv_draw_rect(layer, &rect, &area);
    };

    switch (patterns[pattern_index].kind) {
    case SOLID:
        break;
    case BARS: {
        static const uint32_t colors[] = {
            0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00,
            0xFF00FF, 0xFF0000, 0x0000FF, 0x000000,
        };
        for (int i = 0; i < 8; ++i) {
            int x = w * i / 8;
            draw_rect(x, 0, w * (i + 1) / 8 - x, h, colors[i]);
        }
        break;
    }
    case GRAY:
        for (int i = 0; i < 16; ++i) {
            int x = w * i / 16;
            uint32_t v = 255 * i / 15;
            draw_rect(x, 0, w * (i + 1) / 16 - x, h, (v << 16) | (v << 8) | v);
        }
        break;
    case GRID: {
        int major = w > h ? 64 : 48;
        int minor = major / 2;
        for (int x = 0; x < w; x += minor) {
            draw_rect(x, 0, 1, h, x % major == 0 ? 0xFFFFFF : 0x334155);
        }
        for (int y = 0; y < h; y += minor) {
            draw_rect(0, y, w, 1, y % major == 0 ? 0xFFFFFF : 0x334155);
        }
        break;
    }
    case CHECKER: {
        int cell = w > h ? 44 : 36;
        for (int y = 0; y < h; y += cell) {
            for (int x = 0; x < w; x += cell) {
                int cw = x + cell > w ? w - x : cell;
                int ch = y + cell > h ? h - y : cell;
                draw_rect(x, y, cw, ch, ((x / cell + y / cell) & 1) ? 0xFFFFFF : 0x000000);
            }
        }
        break;
    }
    }
}

static void test_event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
        // Pointer focus and keypad navigation can reset the encoder edit mode.
        lv_group_set_editing(test_group, true);
        break;
    case LV_EVENT_DRAW_MAIN:
        draw_pattern(e);
        break;
    case LV_EVENT_SHORT_CLICKED:
        if (lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) show_controls(true);
        else step_pattern(1);
        break;
    case LV_EVENT_LONG_PRESSED: {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            pending_exit.store(true);
        } else {
            show_controls(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
        }
        break;
    }
    case LV_EVENT_KEY: {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN || key == ' ') step_pattern(1);
        else if (key == LV_KEY_LEFT || key == LV_KEY_UP) step_pattern(-1);
        else if (key == LV_KEY_ESC) pending_exit.store(true);
        else if (key == 'h' || key == 'H') show_controls(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
        break;
    }
    default:
        break;
    }
}

static void control_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC) pending_exit.store(true);
        else if (key == 'h' || key == 'H') show_controls(false);
        return;
    }
    if (code == LV_EVENT_LONG_PRESSED) {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            pending_exit.store(true);
        }
        return;
    }
    if (code != LV_EVENT_SHORT_CLICKED) return;
    intptr_t action = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (action == -1 || action == 1) step_pattern(action);
    else if (action == 0) show_controls(false);
    else pending_exit.store(true);
}

static void button_callback(uint8_t id, uint8_t state)
{
#ifdef ARDUINO
    // Hardware events may originate outside the LVGL thread.
    if (state == BUTTON_EVENT_CLICK) pending_steps.fetch_add(1);
    else if (state == BUTTON_EVENT_LONG_PRESSED) pending_exit.store(true);
#endif
}

static void input_timer_callback(lv_timer_t *timer)
{
    if (pending_exit.exchange(false)) {
        cleanup_and_exit();
        return;
    }
    int steps = pending_steps.exchange(0);
    if (steps) step_pattern(steps);
}

void ui_factory_enter(lv_obj_t *parent)
{
    if (test_obj) return;
    set_low_power_mode_flag(false);
    previous_group = lv_group_get_default();
    test_group = lv_group_create();
    set_default_group(test_group);
    pattern_index = 0;
    pending_steps.store(0);
    pending_exit.store(false);

    int w = lv_disp_get_hor_res(NULL);
    int h = lv_disp_get_ver_res(NULL);
    test_obj = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(test_obj);
    lv_obj_set_pos(test_obj, 0, 0);
    lv_obj_set_size(test_obj, w, h);
    lv_obj_set_style_bg_opa(test_obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(test_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(test_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(test_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(test_obj, test_event, LV_EVENT_ALL, NULL);

    overlay = lv_obj_create(test_obj);
    lv_obj_remove_style_all(overlay);
    int overlay_w = w > 480 ? 440 : w - 24;
    lv_obj_set_size(overlay, overlay_w, 84);
    lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(overlay, 8, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(overlay);
    lv_obj_set_width(title_label, overlay_w - 16);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 8, 8);

    static const char *symbols[] = {LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_EYE_CLOSE, LV_SYMBOL_CLOSE};
    static const uint32_t colors[] = {0x93C5FD, 0x86EFAC, 0xFDE68A, 0xFDA4AF};
    static const intptr_t actions[] = {-1, 1, 0, 2};
    int button_w = (overlay_w - 16 - 3 * 6) / 4;
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_button_create(overlay);
        control_buttons[i] = button;
        lv_obj_remove_style_all(button);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_pos(button, 8 + i * (button_w + 6), 34);
        lv_obj_set_size(button, button_w, 42);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_pad(button, 1, LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(button, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x505050), LV_STATE_FOCUSED);
        lv_obj_add_event_cb(button, control_event, LV_EVENT_ALL,
                            reinterpret_cast<void *>(actions[i]));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, symbols[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(colors[i]), 0);
        lv_obj_center(label);
    }

    ui_enable_edge_swipe_back(test_obj, control_buttons[3], LV_EVENT_SHORT_CLICKED);
    show_controls(true);
    step_pattern(0);
    input_timer = lv_timer_create(input_timer_callback, 20, NULL);
    hw_set_button_callback(button_callback);
}

void ui_factory_exit(lv_obj_t *parent)
{
    if (test_obj) cleanup();
}

app_t ui_factory_main = {
    .setup_func_cb = ui_factory_enter,
    .exit_func_cb = ui_factory_exit,
    .user_data = nullptr,
};
