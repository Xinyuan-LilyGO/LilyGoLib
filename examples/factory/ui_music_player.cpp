/**
 * @file      ui_music_player.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-24
 * 
 * local music player preview.
 */
#include "ui_define.h"
#include <algorithm>
#include <ctype.h>

#if !defined(EXCLUDE_AUDIO_PLAYER)

#ifdef USING_AUDIO_CODEC
#define HAS_VOLUME_SLIDER
#endif

typedef struct {
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *meta;
} music_row_view_t;

static vector<AudioParams_t> music_list;
static vector<music_row_view_t> row_views;

static lv_obj_t *page_container = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_timer_t *player_timer = NULL;

static lv_obj_t *track_title_label = NULL;
static lv_obj_t *track_meta_label = NULL;
static lv_obj_t *player_state_label = NULL;
static lv_obj_t *play_btn_label = NULL;
static lv_obj_t *track_counter_label = NULL;
static lv_obj_t *library_card = NULL;

static int current_index = -1;
static bool play_requested = false;
static bool play_started = false;
static bool play_paused = false;
static bool play_failed = false;
static uint32_t play_request_tick = 0;

static bool ends_with_ignore_case(const char *text, const char *suffix)
{
    if (!text || !suffix) return false;
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) return false;

    const char *start = text + text_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static bool is_supported_audio_file(const char *file_name)
{
    return ends_with_ignore_case(file_name, ".mp3") ||
           ends_with_ignore_case(file_name, ".wav") ||
           ends_with_ignore_case(file_name, ".flac") ||
           ends_with_ignore_case(file_name, ".fla");
}

static const char *audio_source_label(audio_source_type_t source)
{
    return source == AUDIO_SOURCE_SDCARD ? "SD" : "FS";
}

static const char *audio_format_label(const char *file_name)
{
    if (ends_with_ignore_case(file_name, ".mp3")) return "MP3";
    if (ends_with_ignore_case(file_name, ".wav")) return "WAV";
    if (ends_with_ignore_case(file_name, ".flac") || ends_with_ignore_case(file_name, ".fla")) return "FLAC";
    return "AUDIO";
}

static const char *track_display_name(const char *file_name)
{
    const char *name = file_name ? file_name : "";
    const char *slash = strrchr(name, '/');
    return slash ? slash + 1 : name;
}

static void filter_supported_audio_files()
{
    music_list.erase(
        std::remove_if(music_list.begin(), music_list.end(), [](const AudioParams_t &item) {
            return !is_supported_audio_file(item.file_name);
        }),
        music_list.end());
}

static bool has_tracks()
{
    return !music_list.empty();
}

static bool valid_index(int index)
{
    return index >= 0 && index < (int)music_list.size();
}

static void set_player_state(const char *state, lv_color_t color)
{
    if (!player_state_label) return;
    lv_label_set_text(player_state_label, state);
    lv_obj_set_style_text_color(player_state_label, color, 0);
}

static void update_track_counter()
{
    if (!track_counter_label) return;

    if (!has_tracks()) {
        lv_label_set_text(track_counter_label, "0 tracks");
    } else if (valid_index(current_index)) {
        lv_label_set_text_fmt(track_counter_label, "%d / %d",
                              current_index + 1, (int)music_list.size());
    } else {
        lv_label_set_text_fmt(track_counter_label, "%d tracks", (int)music_list.size());
    }
}

static void refresh_row_styles()
{
    for (size_t i = 0; i < row_views.size(); ++i) {
        bool selected = valid_index(current_index) && (int)i == current_index;
        music_row_view_t &view = row_views[i];

        if (!view.row) continue;

        lv_obj_set_style_bg_color(view.row, selected ? UI_COLOR_ACCENT : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(view.row, selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(view.row, selected ? 1 : 0, 0);
        lv_obj_set_style_border_color(view.row, UI_COLOR_ACCENT, 0);

        if (view.icon) {
            lv_label_set_text(view.icon, selected && play_requested && !play_paused ?
                              LV_SYMBOL_PAUSE : LV_SYMBOL_AUDIO);
            lv_obj_set_style_text_color(view.icon, selected ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
        }
        if (view.name) {
            lv_obj_set_style_text_color(view.name, selected ? UI_COLOR_TEXT_PRIMARY : UI_COLOR_TEXT_PRIMARY, 0);
        }
        if (view.meta) {
            lv_obj_set_style_text_color(view.meta, selected ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
        }
    }
}

static void refresh_now_playing()
{
    if (valid_index(current_index)) {
        const AudioParams_t &track = music_list[current_index];
        if (track_title_label) {
            lv_label_set_text(track_title_label, track_display_name(track.file_name));
        }
        if (track_meta_label) {
            lv_label_set_text_fmt(track_meta_label, "%s  %s",
                                  audio_source_label(track.source_type),
                                  audio_format_label(track.file_name));
        }
    } else {
        if (track_title_label) {
            lv_label_set_text(track_title_label, has_tracks() ? "Select a track" : "No music");
        }
        if (track_meta_label) {
            lv_label_set_text(track_meta_label, has_tracks() ? "MP3 / WAV / FLAC" : "Add files to SD or flash");
        }
    }

    if (play_btn_label) {
        lv_label_set_text(play_btn_label, play_requested && !play_paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    if (!has_tracks()) {
        set_player_state("No tracks", UI_COLOR_TEXT_SECONDARY);
    } else if (play_failed) {
        set_player_state("Failed", lv_color_hex(0xFF4444));
    } else if (!play_requested) {
        set_player_state(valid_index(current_index) ? "Stopped" : "Ready", UI_COLOR_TEXT_SECONDARY);
    } else if (play_paused) {
        set_player_state("Paused", UI_COLOR_WARNING);
    } else if (!play_started) {
        set_player_state("Starting", UI_COLOR_ACCENT);
    } else {
        set_player_state("Playing", UI_COLOR_ACCENT);
    }

    update_track_counter();
    refresh_row_styles();
}

static void start_track(int index)
{
    if (!valid_index(index)) return;

    current_index = index;
    play_requested = true;
    play_started = false;
    play_paused = false;
    play_failed = false;
    play_request_tick = lv_tick_get();
    refresh_now_playing();

    const AudioParams_t &track = music_list[current_index];
    hw_set_sd_music_play(track.source_type, track.file_name);
}

static void stop_playback(bool update_ui)
{
    hw_set_play_stop_async();
    play_requested = false;
    play_started = false;
    play_paused = false;
    play_failed = false;
    if (update_ui) {
        refresh_now_playing();
    }
}

static void play_pause_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !has_tracks()) return;

    if (!valid_index(current_index)) {
        start_track(0);
        return;
    }

    if (!play_requested) {
        start_track(current_index);
        return;
    }

    if (play_paused) {
        play_paused = false;
        hw_set_sd_music_resume();
    } else {
        play_paused = true;
        hw_set_sd_music_pause();
    }
    refresh_now_playing();
}

static void prev_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !has_tracks()) return;

    int next = valid_index(current_index) ? current_index - 1 : 0;
    if (next < 0) next = (int)music_list.size() - 1;
    start_track(next);
}

static void next_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !has_tracks()) return;

    int next = valid_index(current_index) ? current_index + 1 : 0;
    if (next >= (int)music_list.size()) next = 0;
    start_track(next);
}

static void track_row_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *row = lv_event_get_target_obj(e);
    uintptr_t stored = (uintptr_t)lv_obj_get_user_data(row);
    if (stored == 0) return;

    int index = (int)stored - 1;
    start_track(index);
}

static void volume_slider_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *obj = lv_event_get_target_obj(e);
    hw_set_volume((uint8_t)lv_slider_get_value(obj));
}

static void player_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!play_requested) return;

    bool running = hw_player_running();
    if (running) {
        play_started = true;
        refresh_now_playing();
        return;
    }

    if (play_started) {
        stop_playback(true);
        return;
    }

    if (lv_tick_elaps(play_request_tick) > 7000) {
        play_requested = false;
        play_paused = false;
        play_failed = true;
        refresh_now_playing();
    }
}

static lv_obj_t *create_control_button(lv_obj_t *parent, const char *symbol,
                                       bool primary, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, primary ? 62 : 46, primary ? 62 : 46);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_ACCENT : lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_ACCENT : lv_color_hex(0x242424), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_ACCENT : lv_color_hex(0x242424), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(btn, primary ? 16 : 0, 0);
    lv_obj_set_style_shadow_width(btn, primary ? 16 : 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, primary ? 16 : 0, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
    if (primary) {
        ui_add_accent_focus_style(btn);
    }
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, primary ? lv_color_black() : UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, primary ? &lv_font_montserrat_24 : &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    if (primary) {
        play_btn_label = label;
    }

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, btn);
    }
    return btn;
}

static void create_player_header(lv_obj_t *parent)
{
    lv_obj_t *card = ui_create_card(parent, NULL);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
#if !defined(USING_TOUCHPAD) && !defined(HAS_TOUCHSCREEN)
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD_BG, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(card, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(card, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_STATE_FOCUSED);
    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, card);
    }
#endif
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *art = lv_obj_create(card);
    lv_obj_set_size(art, is_screen_small() ? 70 : 84, is_screen_small() ? 70 : 84);
    lv_obj_set_style_radius(art, 10, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(art, 1, 0);
    lv_obj_set_style_border_color(art, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_pad_all(art, 0, 0);
    lv_obj_remove_flag(art, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *art_icon = lv_label_create(art);
    lv_label_set_text(art_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(art_icon, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(art_icon, &lv_font_montserrat_28, 0);
    lv_obj_center(art_icon);

    lv_obj_t *info = lv_obj_create(card);
    lv_obj_set_size(info, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_shadow_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_style_pad_row(info, 6, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    track_title_label = lv_label_create(info);
    lv_label_set_text(track_title_label, "Select a track");
    lv_label_set_long_mode(track_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(track_title_label, LV_PCT(100));
    lv_obj_set_style_text_color(track_title_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(track_title_label, is_screen_small() ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);

    track_meta_label = lv_label_create(info);
    lv_label_set_text(track_meta_label, "MP3 / WAV / FLAC");
    lv_label_set_long_mode(track_meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(track_meta_label, LV_PCT(100));
    lv_obj_set_style_text_color(track_meta_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(track_meta_label, &lv_font_montserrat_12, 0);

    lv_obj_t *state_row = lv_obj_create(info);
    lv_obj_set_size(state_row, LV_PCT(100), 22);
    lv_obj_set_style_bg_opa(state_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state_row, 0, 0);
    lv_obj_set_style_shadow_width(state_row, 0, 0);
    lv_obj_set_style_pad_all(state_row, 0, 0);
    lv_obj_set_flex_flow(state_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    player_state_label = lv_label_create(state_row);
    lv_label_set_text(player_state_label, "Ready");
    lv_obj_set_style_text_color(player_state_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(player_state_label, &lv_font_montserrat_12, 0);

    track_counter_label = lv_label_create(state_row);
    lv_label_set_text(track_counter_label, "0 tracks");
    lv_obj_set_style_text_color(track_counter_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(track_counter_label, &lv_font_montserrat_12, 0);
}

static void create_controls(lv_obj_t *parent)
{
    lv_obj_t *card = ui_create_card(parent, NULL);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *control_row = lv_obj_create(card);
    lv_obj_set_size(control_row, LV_PCT(100), 68);
    lv_obj_set_style_bg_opa(control_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_row, 0, 0);
    lv_obj_set_style_shadow_width(control_row, 0, 0);
    lv_obj_set_style_pad_all(control_row, 0, 0);
    lv_obj_set_style_pad_column(control_row, 18, 0);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_control_button(control_row, LV_SYMBOL_PREV, false, prev_cb);
    create_control_button(control_row, LV_SYMBOL_PLAY, true, play_pause_cb);
    create_control_button(control_row, LV_SYMBOL_NEXT, false, next_cb);

#ifdef HAS_VOLUME_SLIDER
    lv_obj_t *volume_row = lv_obj_create(card);
    lv_obj_set_size(volume_row, LV_PCT(92), 28);
    lv_obj_set_style_bg_opa(volume_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volume_row, 0, 0);
    lv_obj_set_style_shadow_width(volume_row, 0, 0);
    lv_obj_set_style_pad_all(volume_row, 0, 0);
    lv_obj_set_style_pad_column(volume_row, 6, 0);
    lv_obj_set_flex_flow(volume_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(volume_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(volume_row);
    lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
    lv_obj_set_width(icon, 14);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *slider = lv_slider_create(volume_row);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_width(slider, 1);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, hw_get_volume(), LV_ANIM_OFF);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_set_style_margin_left(slider, 2, 0);
    lv_obj_set_style_margin_right(slider, 2, 0);
    lv_obj_add_event_cb(slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_height(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_size(slider, 18, 18, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, slider);
    }
#endif
}

static lv_obj_t *create_track_row(lv_obj_t *parent, size_t index)
{
    const AudioParams_t &track = music_list[index];

#if defined(USING_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_t *row = lv_btn_create(parent);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(row, 0, LV_STATE_PRESSED);
#endif
    lv_obj_set_size(row, LV_PCT(100), 42);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(row, LV_OPA_80, LV_STATE_FOCUSED);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(row, (void *)(index + 1));
    lv_obj_add_event_cb(row, track_row_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(icon, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(icon, 18);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, track_display_name(track.file_name));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_width(name, 1);
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);

    lv_obj_t *meta = lv_label_create(row);
    lv_label_set_text_fmt(meta, "%s %s", audio_source_label(track.source_type),
                          audio_format_label(track.file_name));
    lv_obj_set_style_text_color(meta, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
    lv_obj_set_width(meta, 54);
    lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_RIGHT, 0);

    row_views.push_back({row, icon, name, meta});

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, row);
    }
    return row;
}

static void create_library(lv_obj_t *parent)
{
    library_card = ui_create_card(parent, "Library");

    if (!has_tracks()) {
        lv_obj_t *empty = lv_label_create(library_card);
        lv_label_set_text(empty, "No supported audio files found.\nSupported: MP3, WAV, FLAC.");
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
        return;
    }

    row_views.clear();
    for (size_t i = 0; i < music_list.size(); ++i) {
        create_track_row(library_card, i);
    }
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    stop_playback(false);

    if (player_timer) {
        lv_timer_del(player_timer);
        player_timer = NULL;
    }

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }

    music_list.clear();
    row_views.clear();
    track_title_label = NULL;
    track_meta_label = NULL;
    player_state_label = NULL;
    play_btn_label = NULL;
    track_counter_label = NULL;
    library_card = NULL;
    current_index = -1;
    play_requested = false;
    play_started = false;
    play_paused = false;
    play_failed = false;

    menu_show();
}

void ui_music_player_enter(lv_obj_t *parent)
{
    music_list.clear();
    row_views.clear();
    current_index = -1;
    play_requested = false;
    play_started = false;
    play_paused = false;
    play_failed = false;

    page_container = ui_create_app_page(parent, "Player", back_event_handler);

    hw_get_filesystem_music(music_list);
    filter_supported_audio_files();

    if (has_tracks()) {
        current_index = 0;
    }

    create_player_header(page_container);
    create_controls(page_container);
    create_library(page_container);

    refresh_now_playing();
    player_timer = lv_timer_create(player_status_timer_cb, 500, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif
}

void ui_music_player_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_music_player_main = {
    .setup_func_cb = ui_music_player_enter,
    .exit_func_cb = ui_music_player_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_AUDIO_PLAYER */
