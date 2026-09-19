/**
 * @file      ui_audio.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <algorithm>
#ifndef ARDUINO
#include <cctype>
#include <string>
#include <cstring>
#endif

#if !defined(EXCLUDE_AUDIO_PLAYER)

#ifdef USING_AUDIO_CODEC
#define HAS_VOLUME_SLIDER
#endif

static vector<AudioParams_t> music_list;
static lv_timer_t *timer = NULL;
static lv_obj_t *last_play_obj = NULL;
static lv_obj_t *page_container = NULL;
static lv_obj_t *quit_btn = NULL;

static void audio_play_event(lv_event_t *e);

static const char *audio_source_label(audio_source_type_t source)
{
    return source == AUDIO_SOURCE_SDCARD ? "SD" : "FS";
}

static bool is_supported_audio_file(const char *file_name)
{
#ifndef ARDUINO
    String lower = file_name ? file_name : "";
    lower.toLowerCase();
    return lower.endsWith(".mp3") ||
           lower.endsWith(".wav") ||
           lower.endsWith(".flac") ||
           lower.endsWith(".fla");
#else
    std::string lower = file_name ? file_name : "";
    std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) {
        return (char)std::tolower(c);
    });
    auto ends_with = [&lower](const char *suffix) {
        const size_t len = strlen(suffix);
        return lower.size() >= len && lower.compare(lower.size() - len, len, suffix) == 0;
    };
    return ends_with(".mp3") || ends_with(".wav") || ends_with(".flac") || ends_with(".fla");
#endif
}

static void filter_supported_audio_files()
{
    music_list.erase(
    std::remove_if(music_list.begin(), music_list.end(), [](const AudioParams_t &item) {
        return !is_supported_audio_file(item.file_name);
    }),
    music_list.end());
}

static lv_obj_t *create_music_row(lv_obj_t *card, AudioParams_t *file_info)
{
#if defined(USING_TOUCHPAD) || defined(HAS_TOUCHSCREEN)
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#else
    lv_obj_t *row = lv_btn_create(card);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(row, 0, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(row, UI_COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(row, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(row, 8, LV_STATE_FOCUSED);
#endif
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_CARD_BG, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_FOCUSED);
    lv_obj_set_style_pad_left(row, 4, 0);
    lv_obj_set_style_pad_right(row, 4, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(row);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(icon, UI_COLOR_ACCENT, 0);

    lv_obj_t *source = lv_label_create(row);
    lv_label_set_text(source, audio_source_label(file_info->source_type));
    lv_obj_set_style_text_color(source, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(source, &lv_font_montserrat_12, 0);
    lv_obj_set_width(source, 18);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, file_info->file_name);
    lv_obj_set_style_text_color(name, UI_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_width(name, 1);

    lv_obj_t *play = lv_label_create(row);
    lv_label_set_text(play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play, UI_COLOR_ACCENT, 0);
    lv_obj_set_width(play, 18);
    lv_obj_set_style_text_align(play, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, LV_PCT(95), 1);
    lv_obj_set_style_bg_color(div, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    lv_obj_set_user_data(row, file_info);
    lv_obj_add_event_cb(row, audio_play_event, LV_EVENT_CLICKED, play);

    return row;
}

static void back_event_handler(lv_event_t *e)
{
    hw_set_play_stop();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    last_play_obj = NULL;

    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }

    menu_show();
}


static void audio_play_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *symbol = (lv_obj_t *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED) {
        char *text = lv_label_get_text(symbol);

        if (strcmp(text, LV_SYMBOL_PLAY) == 0) {
            if (last_play_obj != NULL) {
                lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
            }
            if (last_play_obj == symbol) {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                hw_set_sd_music_resume();

            } else {
                lv_label_set_text(symbol, LV_SYMBOL_PAUSE);
                last_play_obj = symbol;

                AudioParams_t param = *(AudioParams_t *)lv_obj_get_user_data(obj);
                hw_set_sd_music_play(param.source_type, param.file_name);

                LILYGO_LOG_PRINTF("Click %s source :%d  obj:%p \n", param.file_name, param.source_type, obj);

                if (timer) {
                    lv_timer_del(timer);
                }
                timer =  lv_timer_create([](lv_timer_t *t) {
                    if (!hw_player_running()) {
                        if (last_play_obj) {
                            lv_label_set_text(last_play_obj, LV_SYMBOL_PLAY);
                            lv_timer_del(t);
                            timer = NULL;
                            last_play_obj = NULL;
                        }
                    }
                }, 500, NULL);
            }
        } else {
            lv_label_set_text(symbol, LV_SYMBOL_PLAY);
            hw_set_sd_music_pause();
        }
    }
}

static void volume_slider_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        int volume = lv_slider_get_value(obj);
        LILYGO_LOG_PRINTF("Set volume to %d\n", volume);
        hw_set_volume(volume);
    }
}

#ifdef HAS_EFFECT_BUTTONS
void effect_button_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED) {
        bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
        lv_obj_t *label = lv_obj_get_child(obj, 0);
        const char *text = lv_label_get_text(label);
        if (strcmp(text, "3D") == 0) {
            // printf("3D Effect: %s\n", checked ? "ON" : "OFF");
        } else if (strcmp(text, "A/B") == 0) {
            // printf("A/B Effect: %s\n", checked ? "ON" : "OFF");
        }
    }
}
#endif /*HAS_EFFECT_BUTTONS*/

void ui_audio_enter(lv_obj_t *parent)
{
    music_list.clear();
    page_container = ui_create_app_page(parent, "Music", back_event_handler);

    hw_get_filesystem_music(music_list);
    filter_supported_audio_files();

    if (!music_list.size()) {
        LV_IMG_DECLARE(img_cry);
        lv_obj_t *img = lv_img_create(page_container);
        lv_img_set_src(img, &img_cry);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, lv_pct(10));

        lv_obj_t *label = lv_label_create(page_container);
        lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(label, LV_PCT(80));

#ifdef HAS_SD_CARD_SOCKET
        lv_label_set_text(label, "No supported audio files found.\nSupported: MP3, WAV, FLAC.");
#else
        lv_label_set_text(label, "No supported audio files found.\nSupported: MP3, WAV, FLAC.");
#endif

        lv_obj_align_to(label, img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

#ifdef USING_TOUCHPAD
        quit_btn  = create_floating_button([](lv_event_t *e) {
            lv_obj_send_event(lv_obj_get_child(lv_scr_act(), 0), LV_EVENT_CLICKED, NULL);
        }, NULL);
#endif

        return;
    }

    /* ── Music list card ── */
    lv_obj_t *card = ui_create_card(page_container, "Music");

    lv_group_t *g = lv_group_get_default();
    for (size_t index = 0; index < music_list.size(); ++index) {
        lv_obj_t *row = create_music_row(card, &music_list[index]);
#if !defined(USING_TOUCHPAD) && !defined(HAS_TOUCHSCREEN)
        if (g) lv_group_add_obj(g, row);
#else
        (void)g;
#endif
    }

#ifdef HAS_VOLUME_SLIDER
    /* ── Volume card ── */
    card = ui_create_card(page_container, "Volume");

    lv_obj_t *slider = lv_slider_create(card);
#ifdef HAS_EFFECT_BUTTONS
    lv_obj_set_width(slider, lv_pct(35));
#else
    lv_obj_set_width(slider, lv_pct(80));
#endif
    lv_slider_set_value(slider, hw_get_volume(), LV_ANIM_OFF);
    lv_slider_set_range(slider, 0, 100);
    ui_prepare_slider_for_encoder(slider);
    lv_obj_add_event_cb(slider, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_height(slider, 10, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_size(slider, 20, 20, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

    ui_create_card_item(card, LV_SYMBOL_VOLUME_MAX, "Level", slider);

#ifdef HAS_EFFECT_BUTTONS
    lv_obj_t *ab_btn = lv_button_create(card);
    lv_obj_set_size(ab_btn, lv_pct(18), 30);
    lv_obj_add_flag(ab_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *ab_label = lv_label_create(ab_btn);
    lv_label_set_text(ab_label, "3D");
    lv_obj_center(ab_label);
    lv_obj_add_event_cb(ab_btn, effect_button_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *eff_btn = lv_button_create(card);
    lv_obj_set_size(eff_btn, lv_pct(18), 30);
    lv_obj_add_flag(eff_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *eff_label = lv_label_create(eff_btn);
    lv_label_set_text(eff_label, "A/B");
    lv_obj_center(eff_label);
    lv_obj_add_event_cb(eff_btn, effect_button_event, LV_EVENT_CLICKED, NULL);

    ui_create_card_item(card, LV_SYMBOL_AUDIO, "Effects", ab_btn);
    ui_create_card_item(card, LV_SYMBOL_AUDIO, "Loop", eff_btn);
#endif /*HAS_EFFECT_BUTTONS*/
#endif /*HAS_VOLUME_SLIDER*/

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif

}

void ui_audio_exit(lv_obj_t *parent)
{

}

app_t ui_audio_main = {
    .setup_func_cb = ui_audio_enter,
    .exit_func_cb = ui_audio_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_AUDIO_PLAYER */
