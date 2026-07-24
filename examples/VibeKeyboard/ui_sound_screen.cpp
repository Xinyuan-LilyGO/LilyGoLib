/**
 * @file      ui_sound_screen.cpp
 * @brief     Sound Screen - volume control & event sound mapping
 *
 * Layout (480x222):
 *   Header (28px)       - "SOUND"
 *   Divider (1px)
 *   Scroll Content (164px) - Master Volume section + Event Sound Mapping section
 *   Divider (1px)
 *   Bottom Bar (28px)   - [↑ SCROLL] [● CONFIRM] [↓ SCROLL]
 *
 * Navigation:
 *   ui_sound_screen_move(+1/-1)  - navigate rows or adjust volume
 *   ui_sound_screen_confirm()    - enter/exit volume adjustment, open selector, or preview
 */
#include <LilyGoLib.h>
#include <lvgl.h>
#include <stdint.h>

#include "ui_sound_screen.h"
#include "vk_sound.h"

extern "C" {
LV_FONT_DECLARE(usr_montserrat_12)
LV_FONT_DECLARE(usr_montserrat_14)
}

#if LVGL_VERSION_MAJOR == 9
#define lv_mem_alloc  lv_malloc
#define lv_mem_free   lv_free
#endif

/* ── Palette ──────────────────────────────────────────────────────────── */
#define SD_BG          lv_color_make(242, 243, 245)
#define SD_WHITE       lv_color_make(255, 255, 255)
#define SD_DIVIDER     lv_color_make(209, 211, 215)
#define SD_DIVIDER_SEC lv_color_make(224, 226, 231)
#define SD_ACCENT      lv_color_make( 88, 101, 242)
#define SD_TEXT_DARK   lv_color_make( 30,  34,  41)
#define SD_TEXT_MID    lv_color_make( 78,  80,  88)
#define SD_TEXT_MUTED  lv_color_make(128, 136, 153)
#define SD_SLIDER_BG   lv_color_make(224, 227, 232)
#define SD_VALUE_COLOR lv_color_make(240, 178,  50)
#define SD_FOCUS_BG    lv_color_make(238, 240, 255)

/* ── Dimensions ───────────────────────────────────────────────────────── */
#define TOPBAR_H   28
#define BTMBAR_H   28
#define SCREEN_H  222
#define CONTENT_H (SCREEN_H - TOPBAR_H - 1 - 1 - BTMBAR_H)  /* 164 */
#define SCROLL_W    3
#define SECTION_PAD 8
#define EVENT_ROW_H 52
#define DIVIDER_H   1
#define SOUND_SCROLL_FOCUS_MAX  16

/* ── Event sound entries ──────────────────────────────────────────────── */
typedef struct {
    const char *label;
} sound_event_t;

#define EVENT_COUNT 4
static const sound_event_t s_events[EVENT_COUNT] = {
    { "Permission Alert" },
    { "Session Complete" },
    { "Error" },
    { "Button Click" },
};

static const char *const SOUND_OPTION_LIST =
    "mute\n"
    "alert\n"
    "ding\n"
    "buzz\n"
    "click";

typedef enum {
    SOUND_FOCUS_VOLUME = 0,
    SOUND_FOCUS_DROPDOWN,
    SOUND_FOCUS_PLAY,
} sound_focus_kind_t;

/* ── State ────────────────────────────────────────────────────────────── */
static int8_t       s_selected  = 0;   /* 0 = volume row, 1-4 = event rows */
static int16_t      s_volume    = 51;  /* 0-100 */
static bool         s_volume_editing = false;
static lv_obj_t    *s_scroll_cont  = NULL;
static lv_obj_t    *s_slider_fill  = NULL;
static lv_obj_t    *s_slider_thumb = NULL;
static lv_obj_t    *s_vol_pct      = NULL;
static lv_obj_t    *s_scroll_thumb = NULL;
static lv_obj_t    *s_volume_row = NULL;
static lv_obj_t    *s_event_rows[EVENT_COUNT] = {};
static lv_obj_t    *s_event_dropdowns[EVENT_COUNT] = {};
static lv_obj_t    *s_play_btns[EVENT_COUNT] = {};
static bool         s_dropdown_selecting[EVENT_COUNT] = {};
static lv_group_t  *s_group = NULL;
static lv_obj_t    *s_focus_items[SOUND_SCROLL_FOCUS_MAX] = {};
static sound_focus_kind_t s_focus_kinds[SOUND_SCROLL_FOCUS_MAX] = {};
static uint8_t      s_focus_events[SOUND_SCROLL_FOCUS_MAX] = {};
static int8_t       s_scroll_focus_count = 0;
static int8_t       s_scroll_focus_index = 0;

/* ── Helpers ──────────────────────────────────────────────────────────── */
static void adjust_volume(int8_t dir);

static void update_selection_style(void)
{
    if (s_volume_row) {
        lv_obj_set_style_bg_color(s_volume_row, s_selected == 0 ? SD_FOCUS_BG : SD_WHITE, 0);
    }
    if (s_vol_pct) {
        lv_obj_set_style_text_color(s_vol_pct, s_volume_editing ? SD_ACCENT : SD_TEXT_MID, 0);
    }
    if (s_slider_thumb) {
        lv_obj_set_style_bg_color(s_slider_thumb, s_volume_editing ? SD_ACCENT : SD_WHITE, 0);
    }
    for (int8_t i = 0; i < EVENT_COUNT; i++) {
        if (s_event_rows[i]) {
            lv_obj_set_style_bg_color(s_event_rows[i], s_selected == i + 1 ? SD_FOCUS_BG : SD_WHITE, 0);
        }
    }
}

static uint32_t sound_type_to_dropdown_index(int8_t sound_type)
{
    if (sound_type < 0 || sound_type >= VK_SOUND_TYPE_COUNT) return 0;
    return (uint32_t)sound_type + 1;
}

static int8_t dropdown_index_to_sound_type(uint32_t index)
{
    if (index == 0 || index > VK_SOUND_TYPE_COUNT) return -1;
    return (int8_t)(index - 1);
}

static void update_event_value(uint8_t event_type)
{
    if (event_type >= EVENT_COUNT || !s_event_dropdowns[event_type]) return;

    lv_dropdown_set_selected(s_event_dropdowns[event_type],
                             sound_type_to_dropdown_index(vk_sound_get_mapping(event_type)));
}

static lv_obj_t *make_bottom_btn(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(btn, (lv_opa_t)(255 * 0.12f), 0);
    lv_obj_set_style_bg_color(btn, SD_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, SD_ACCENT, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_left(btn, 6, 0);
    lv_obj_set_style_pad_right(btn, 6, 0);
    lv_obj_set_style_pad_top(btn, 3, 0);
    lv_obj_set_style_pad_bottom(btn, 3, 0);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, SD_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_12, 0);
    return btn;
}

static lv_obj_t *make_hdivider(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, LV_PCT(100), DIVIDER_H);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    return d;
}

static void update_volume_slider(void)
{
    if (!s_slider_fill || !s_slider_thumb || !s_vol_pct) return;

    /* Slider track width inside Section Master Volume:
       section w=464, pad 8 each side → row w=448, label "Volume"=47 + gap20 → fill area ~339px */
    const int32_t track_w = 339;
    int32_t fill_w = (track_w * s_volume) / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > track_w) fill_w = track_w;

    lv_obj_set_width(s_slider_fill, fill_w);

    /* thumb sits at fill_w - 7 (half of 14px thumb) */
    lv_obj_set_x(s_slider_thumb, fill_w - 7);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)s_volume);
    lv_label_set_text(s_vol_pct, buf);
}

static int32_t get_scroll_max(void)
{
    if (!s_scroll_cont) return 0;
    lv_obj_update_layout(s_scroll_cont);
    int32_t cont_h = lv_obj_get_height(s_scroll_cont);
    int32_t max_scroll = cont_h - CONTENT_H;
    return max_scroll > 0 ? max_scroll : 0;
}

static int8_t focus_index_for_obj(lv_obj_t *obj)
{
    if (!obj) return -1;
    for (int8_t i = 0; i < s_scroll_focus_count; i++) {
        if (s_focus_items[i] == obj) return i;
    }
    return -1;
}

static uint8_t focus_event_for_index(int8_t index)
{
    if (index < 0 || index >= s_scroll_focus_count) return 0;
    return s_focus_events[index];
}

static lv_obj_t *focused_sound_obj(void)
{
    if (!s_group) return NULL;
    return lv_group_get_focused(s_group);
}

static void scroll_to_focus_index(int8_t index, lv_anim_enable_t anim)
{
    (void)anim;
    if (!s_scroll_cont || s_scroll_focus_count <= 0) return;

    if (index < 0) index = 0;
    if (index >= s_scroll_focus_count) index = s_scroll_focus_count - 1;
    s_scroll_focus_index = index;

    if (s_focus_kinds[index] == SOUND_FOCUS_VOLUME) {
        s_selected = 0;
    } else if (s_focus_kinds[index] == SOUND_FOCUS_DROPDOWN ||
               s_focus_kinds[index] == SOUND_FOCUS_PLAY) {
        s_selected = (int8_t)focus_event_for_index(index) + 1;
        s_volume_editing = false;
    } else {
        s_selected = -1;
        s_volume_editing = false;
    }
    update_selection_style();

    lv_obj_t *target = NULL;
    if (s_selected == 0) {
        target = s_volume_row;
    } else if (s_selected > 0) {
        target = s_event_rows[s_selected - 1];
    }
    int32_t offset = 0;
    if (target) {
        lv_obj_t *section = lv_obj_get_parent(target);
        int32_t target_y = lv_obj_get_y(section) + lv_obj_get_y(target);
        offset = target_y - (CONTENT_H - lv_obj_get_height(target)) / 2;
        if (offset < 0) offset = 0;
    }
    int32_t max_scroll = get_scroll_max();
    if (offset > max_scroll) offset = max_scroll;
    lv_obj_set_y(s_scroll_cont, -offset);

    if (s_scroll_thumb && max_scroll > 0) {
        int32_t thumb_range = CONTENT_H - lv_obj_get_height(s_scroll_thumb);
        int32_t thumb_y = (offset * thumb_range) / max_scroll;
        lv_obj_set_y(s_scroll_thumb, thumb_y);
    }
}

/* ── Header ───────────────────────────────────────────────────────────── */
static void create_header(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, SD_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "SOUND");
    lv_obj_set_style_text_color(title, SD_TEXT_DARK, 0);
    lv_obj_set_style_text_font(title, &usr_montserrat_14, 0);
}

/* ── Section card helper ──────────────────────────────────────────────── */
static lv_obj_t *create_section_card(lv_obj_t *parent, const char *section_title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, SD_WHITE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, SD_DIVIDER, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(card, 10, 0);
    lv_obj_set_style_pad_bottom(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, section_title);
    lv_obj_set_style_text_color(lbl, SD_TEXT_DARK, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_14, 0);
    lv_obj_set_style_pad_left(lbl, 10, 0);

    /* spacer to reach divider at y=38 from card top */
    lv_obj_t *sp = lv_obj_create(card);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, LV_PCT(100), 10);

    make_hdivider(card, SD_DIVIDER_SEC);

    return card;
}

/* ── Master Volume section ────────────────────────────────────────────── */
static void create_section_volume(lv_obj_t *parent)
{
    lv_obj_t *card = create_section_card(parent, "Master Volume");

    /* Volume row: h=28, pad_hor=10, gap=20 */
    lv_obj_t *row = lv_obj_create(card);
    s_volume_row = row;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 28);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_column(row, 20, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, SD_FOCUS_BG, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Volume");
    lv_obj_set_style_text_color(lbl, SD_TEXT_DARK, 0);
    lv_obj_set_style_text_font(lbl, &usr_montserrat_14, 0);

    /* Slider track: fills remaining space, h=6, radius=3 */
    lv_obj_t *track = lv_obj_create(row);
    lv_obj_remove_style_all(track);
    lv_obj_set_flex_grow(track, 1);
    lv_obj_set_height(track, 6);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, SD_SLIDER_BG, 0);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_add_flag(track, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    s_slider_fill = lv_obj_create(track);
    lv_obj_remove_style_all(s_slider_fill);
    lv_obj_set_pos(s_slider_fill, 0, 0);
    lv_obj_set_height(s_slider_fill, 6);
    lv_obj_set_style_bg_opa(s_slider_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_slider_fill, SD_ACCENT, 0);
    lv_obj_set_style_radius(s_slider_fill, 3, 0);

    s_slider_thumb = lv_obj_create(track);
    lv_obj_remove_style_all(s_slider_thumb);
    lv_obj_set_size(s_slider_thumb, 14, 14);
    lv_obj_set_y(s_slider_thumb, -4);
    lv_obj_set_style_bg_opa(s_slider_thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_slider_thumb, SD_WHITE, 0);
    lv_obj_set_style_radius(s_slider_thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(s_slider_thumb, 4, 0);
    lv_obj_set_style_shadow_opa(s_slider_thumb, LV_OPA_30, 0);

    s_vol_pct = lv_label_create(row);
    lv_obj_set_style_text_color(s_vol_pct, SD_TEXT_MID, 0);
    lv_obj_set_style_text_font(s_vol_pct, &usr_montserrat_12, 0);

    update_volume_slider();
}

static lv_obj_t *create_lv_button(lv_obj_t *parent)
{
#if LVGL_VERSION_MAJOR == 9
    return lv_button_create(parent);
#else
    return lv_btn_create(parent);
#endif
}

static void move_focus_from_obj(lv_obj_t *obj, int8_t dir)
{
    int8_t index = focus_index_for_obj(obj);
    if (index < 0) return;

    int8_t next = index + dir;
    if (next < 0) next = 0;
    if (next >= s_scroll_focus_count) next = s_scroll_focus_count - 1;

    if (s_focus_items[next]) lv_group_focus_obj(s_focus_items[next]);
}

static void sound_control_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        int8_t index = focus_index_for_obj(obj);
        if (index >= 0) scroll_to_focus_index(index, LV_ANIM_ON);
        return;
    }

    if (code != LV_EVENT_KEY) return;

    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_UP || key == LV_KEY_PREV) {
        move_focus_from_obj(obj, -1);
        lv_event_stop_bubbling(e);
    } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
        move_focus_from_obj(obj, 1);
        lv_event_stop_bubbling(e);
    }
}

static void play_btn_event_cb(lv_event_t *e)
{
    sound_control_focus_cb(e);

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY) return;

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key != LV_KEY_ENTER && key != '\n' && key != '\r') return;
        lv_event_stop_bubbling(e);
    }

    uintptr_t event_type = (uintptr_t)lv_event_get_user_data(e);
    if (event_type >= EVENT_COUNT) return;

    vk_sound_play_event((uint8_t)event_type);
}

static void sound_dropdown_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    uintptr_t event_type = (uintptr_t)lv_event_get_user_data(e);
    if (event_type >= EVENT_COUNT) return;

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_UP || key == LV_KEY_DOWN ||
                key == LV_KEY_PREV || key == LV_KEY_NEXT) {
            if (s_dropdown_selecting[event_type]) return;
            if (lv_dropdown_is_open(dd)) lv_dropdown_close(dd);
            move_focus_from_obj(dd, (key == LV_KEY_UP || key == LV_KEY_PREV) ? -1 : 1);
            lv_event_stop_bubbling(e);
            return;
        }
        if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
            s_dropdown_selecting[event_type] = lv_dropdown_is_open(dd);
            return;
        }
    }

    sound_control_focus_cb(e);

    if (code == LV_EVENT_RELEASED) {
        s_dropdown_selecting[event_type] = lv_dropdown_is_open(dd);
        return;
    }
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_DEFOCUSED || code == LV_EVENT_LEAVE) {
        s_dropdown_selecting[event_type] = false;
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    vk_sound_set_mapping((uint8_t)event_type,
                         dropdown_index_to_sound_type(lv_dropdown_get_selected(dd)));
    s_dropdown_selecting[event_type] = false;
}

/* ── Play button helper (small accent button with ▶) ─────────────────── */
static lv_obj_t *make_play_btn(lv_obj_t *parent, uint8_t event_type)
{
    lv_obj_t *btn = create_lv_button(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 28, 20);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, SD_ACCENT, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_bg_color(btn, SD_TEXT_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(btn, SD_TEXT_DARK, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(btn, 1, LV_STATE_FOCUSED);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(btn, play_btn_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)event_type);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(ic, SD_WHITE, 0);
    lv_obj_set_style_text_font(ic, &usr_montserrat_12, 0);
    return btn;
}

static lv_obj_t *make_sound_dropdown(lv_obj_t *parent, uint8_t event_type)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_remove_style_all(dd);
    lv_obj_set_size(dd, 86, 24);
    lv_dropdown_set_options_static(dd, SOUND_OPTION_LIST);
    lv_dropdown_set_symbol(dd, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dd, sound_type_to_dropdown_index(vk_sound_get_mapping(event_type)));
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dd, SD_WHITE, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, SD_DIVIDER, 0);
    lv_obj_set_style_radius(dd, 3, 0);
    lv_obj_set_style_pad_left(dd, 6, 0);
    lv_obj_set_style_pad_right(dd, 16, 0);
    lv_obj_set_style_text_color(dd, SD_VALUE_COLOR, 0);
    lv_obj_set_style_text_font(dd, &usr_montserrat_12, 0);
    lv_obj_set_style_outline_width(dd, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(dd, SD_TEXT_DARK, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(dd, 1, LV_STATE_FOCUSED);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dd, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(dd, sound_dropdown_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)event_type);
    return dd;
}

/* ── Event Sound Mapping section ─────────────────────────────────────── */
static void create_section_events(lv_obj_t *parent)
{
    lv_obj_t *card = create_section_card(parent, "Event Sound Mapping");

    for (int i = 0; i < EVENT_COUNT; i++) {
        /* divider between rows (not before first) */
        if (i > 0) make_hdivider(card, SD_DIVIDER_SEC);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), EVENT_ROW_H);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, SD_WHITE, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        s_event_rows[i] = row;

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, s_events[i].label);
        lv_obj_set_style_text_color(name, SD_TEXT_DARK, 0);
        lv_obj_set_style_text_font(name, &usr_montserrat_14, 0);
        lv_obj_set_flex_grow(name, 1);

        s_event_dropdowns[i] = make_sound_dropdown(row, (uint8_t)i);
        s_play_btns[i] = make_play_btn(row, (uint8_t)i);
    }
}

/* ── Scroll content area ──────────────────────────────────────────────── */
static void create_scroll_content(lv_obj_t *parent)
{
    /* Outer clip frame: fixed 164px height, bg = SD_BG */
    lv_obj_t *clip = lv_obj_create(parent);
    lv_obj_remove_style_all(clip);
    lv_obj_set_size(clip, LV_PCT(100), CONTENT_H);
    lv_obj_set_style_bg_opa(clip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(clip, SD_BG, 0);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(clip, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Scrollable inner container */
    s_scroll_cont = lv_obj_create(clip);
    lv_obj_remove_style_all(s_scroll_cont);
    lv_obj_set_size(s_scroll_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_scroll_cont, LV_OPA_0, 0);
    lv_obj_set_layout(s_scroll_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_scroll_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scroll_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_scroll_cont, SECTION_PAD, 0);
    lv_obj_set_style_pad_row(s_scroll_cont, SECTION_PAD, 0);
    lv_obj_clear_flag(s_scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scroll_cont, LV_OBJ_FLAG_EVENT_BUBBLE);

    create_section_volume(s_scroll_cont);
    create_section_events(s_scroll_cont);

    /* Scroll thumb (right edge, 3px wide) */
    s_scroll_thumb = lv_obj_create(clip);
    lv_obj_remove_style_all(s_scroll_thumb);
    lv_obj_set_size(s_scroll_thumb, SCROLL_W, 55);
    lv_obj_set_pos(s_scroll_thumb, 477, 0);
    lv_obj_set_style_bg_opa(s_scroll_thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scroll_thumb, SD_ACCENT, 0);
    lv_obj_set_style_radius(s_scroll_thumb, 2, 0);
}

/* ── Bottom Bar ───────────────────────────────────────────────────────── */
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BTMBAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, SD_WHITE, 0);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 10, 0);
    lv_obj_set_style_pad_right(bar, 10, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ↑ SCROLL */
    lv_obj_t *nav_up = lv_label_create(bar);
    lv_label_set_text(nav_up, "\xe2\x86\x91 SCROLL");
    lv_obj_set_style_text_color(nav_up, SD_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_up, &usr_montserrat_12, 0);

    /* spacer */
    lv_obj_t *sp1 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    /* BACK + CONFIRM buttons */
    lv_obj_t *actions = lv_obj_create(bar);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, 22);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    make_bottom_btn(actions, "\xe2\x86\x90 BACK");
    make_bottom_btn(actions, "\xe2\x97\x8f  CONFIRM");

    /* spacer */
    lv_obj_t *sp2 = lv_obj_create(bar);
    lv_obj_remove_style_all(sp2);
    lv_obj_set_height(sp2, 1);
    lv_obj_set_flex_grow(sp2, 1);

    /* ↓ SCROLL */
    lv_obj_t *nav_dn = lv_label_create(bar);
    lv_label_set_text(nav_dn, "\xe2\x86\x93 SCROLL");
    lv_obj_set_style_text_color(nav_dn, SD_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(nav_dn, &usr_montserrat_12, 0);
}

/* ── Encoder focus bridge ────────────────────────────────────────────── */
static void focus_item_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *self = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        for (int8_t i = 0; i < s_scroll_focus_count; i++) {
            if (s_focus_items[i] == self) {
                s_volume_editing = i == 0 && s_group && lv_group_get_editing(s_group);
                scroll_to_focus_index(i, LV_ANIM_ON);
                break;
            }
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        ui_sound_screen_confirm();
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (s_selected == 0 && s_volume_editing && key == LV_KEY_LEFT) {
            adjust_volume(-1);
            lv_event_stop_bubbling(e);
        } else if (s_selected == 0 && s_volume_editing && key == LV_KEY_RIGHT) {
            adjust_volume(1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_UP || key == LV_KEY_PREV) {
            ui_sound_screen_move(-1);
            lv_event_stop_bubbling(e);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT) {
            ui_sound_screen_move(1);
            lv_event_stop_bubbling(e);
        }
    }
}

static void register_focus_item(lv_obj_t *item, sound_focus_kind_t kind, uint8_t event_type)
{
    if (!item || s_scroll_focus_count >= SOUND_SCROLL_FOCUS_MAX) return;

    s_focus_items[s_scroll_focus_count] = item;
    s_focus_kinds[s_scroll_focus_count] = kind;
    s_focus_events[s_scroll_focus_count] = event_type;
    lv_group_add_obj(s_group, item);
    s_scroll_focus_count++;
}

static void create_focus_items(lv_obj_t *parent)
{
    s_scroll_focus_count = 0;

    s_group = lv_group_create();
    lv_group_set_wrap(s_group, false);

    lv_obj_t *volume_item = lv_obj_create(parent);
    lv_obj_remove_style_all(volume_item);
    lv_obj_set_size(volume_item, 0, 0);
    lv_obj_set_pos(volume_item, 0, 0);
    /* LVGL enters encoder edit mode only for editable/scrollable objects. */
    lv_obj_add_flag(volume_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(volume_item, LV_DIR_NONE);
    lv_obj_add_flag(volume_item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(volume_item, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(volume_item, focus_item_event_cb, LV_EVENT_ALL, NULL);
    register_focus_item(volume_item, SOUND_FOCUS_VOLUME, 0);

    for (uint8_t i = 0; i < EVENT_COUNT; i++) {
        register_focus_item(s_event_dropdowns[i], SOUND_FOCUS_DROPDOWN, i);
        register_focus_item(s_play_btns[i], SOUND_FOCUS_PLAY, i);
    }

    s_scroll_focus_index = 0;
    lv_group_focus_obj(s_focus_items[0]);
}

static void adjust_volume(int8_t dir)
{
    if (dir == 0) return;

    int16_t next = s_volume + (dir > 0 ? 10 : -10);
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    if (next == s_volume) return;

    s_volume = next;
    vk_sound_set_volume((uint8_t)s_volume);
    update_volume_slider();
    vk_sound_preview(VK_SOUND_CLICK);
}

/* ── Public API ───────────────────────────────────────────────────────── */
void ui_sound_screen_create(lv_obj_t *parent)
{
    s_group = NULL;
    s_scroll_focus_count = 0;
    s_scroll_focus_index = 0;
    s_selected = 0;
    s_volume_editing = false;
    s_volume = vk_sound_get_volume();
    s_volume_row = NULL;
    for (int8_t i = 0; i < EVENT_COUNT; i++) {
        s_event_rows[i] = NULL;
        s_event_dropdowns[i] = NULL;
        s_play_btns[i] = NULL;
        s_dropdown_selecting[i] = false;
    }
    for (int8_t i = 0; i < SOUND_SCROLL_FOCUS_MAX; i++) {
        s_focus_items[i] = NULL;
        s_focus_kinds[i] = SOUND_FOCUS_VOLUME;
        s_focus_events[i] = 0;
    }

    lv_obj_remove_style_all(parent);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, SD_BG, 0);
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_header(parent);
    make_hdivider(parent, SD_DIVIDER);
    create_scroll_content(parent);
    make_hdivider(parent, SD_DIVIDER);
    create_bottom_bar(parent);
    create_focus_items(parent);
}

/* Navigate rows, or adjust volume while the volume row is being edited. */
void ui_sound_screen_move(int8_t dir)
{
    if (!s_scroll_cont) return;

    if (s_selected == 0 && s_volume_editing) {
        adjust_volume(dir);
        return;
    }

    int8_t next = s_scroll_focus_index + dir;
    if (next < 0) next = 0;
    if (next >= s_scroll_focus_count) next = s_scroll_focus_count - 1;

    if (s_focus_items[next]) {
        lv_group_focus_obj(s_focus_items[next]);
    } else {
        scroll_to_focus_index(next, LV_ANIM_ON);
    }
}

void ui_sound_screen_confirm(void)
{
    /* Press the encoder to enter/exit volume adjustment; rotate to change it. */
    lv_obj_t *focused = focused_sound_obj();
    int8_t focus_index = focus_index_for_obj(focused);
    sound_focus_kind_t kind = focus_index >= 0 ? s_focus_kinds[focus_index] : SOUND_FOCUS_VOLUME;

    if (kind == SOUND_FOCUS_VOLUME) {
        if (s_group) {
            lv_group_set_editing(s_group, !lv_group_get_editing(s_group));
            s_volume_editing = lv_group_get_editing(s_group);
        } else {
            s_volume_editing = !s_volume_editing;
        }
        update_selection_style();
        return;
    }

    uint8_t event_type = focus_event_for_index(focus_index);
    if (kind == SOUND_FOCUS_DROPDOWN) {
        if (event_type < EVENT_COUNT && s_event_dropdowns[event_type]) {
            s_dropdown_selecting[event_type] = true;
            lv_dropdown_open(s_event_dropdowns[event_type]);
        }
    } else if (kind == SOUND_FOCUS_PLAY) {
        vk_sound_play_event(event_type);
    }
}

void ui_sound_screen_refresh(void)
{
    s_volume = vk_sound_get_volume();
    update_volume_slider();
    for (uint8_t i = 0; i < EVENT_COUNT; i++) {
        update_event_value(i);
    }
}

lv_group_t *ui_sound_screen_get_group(void)
{
    return s_group;
}
