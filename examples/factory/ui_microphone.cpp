/**
 * @file      ui_microphone.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * Scrolling microphone oscilloscope with level-reactive glow.
 */
#include "ui_define.h"
#include "hal_es7210_microphone.h"
#include "hal_microphone_waveform.h"

#if !defined(EXCLUDE_MICROPHONE)

#define MIC_UI_MAX_CHANNELS ES7210_MIC_CHANNELS
#define MIC_UI_WAVE_POINTS  96

static lv_timer_t *timer = NULL;
static lv_obj_t *page_container = NULL;
static lv_obj_t *channel_container = NULL;
static lv_obj_t *wave_tracks[MIC_UI_MAX_CHANNELS];
static lv_obj_t *wave_center_lines[MIC_UI_MAX_CHANNELS];
static lv_obj_t *wave_lines[MIC_UI_MAX_CHANNELS];
static lv_obj_t *wave_glows[MIC_UI_MAX_CHANNELS];
static lv_obj_t *wave_labels[MIC_UI_MAX_CHANNELS];
static lv_obj_t *es7210_mic_buttons[ES7210_MIC_CHANNELS];
static lv_point_precise_t wave_points[MIC_UI_MAX_CHANNELS][MIC_UI_WAVE_POINTS];
static int16_t wave_history[MIC_UI_MAX_CHANNELS][MIC_UI_WAVE_POINTS];
static float wave_levels[MIC_UI_MAX_CHANNELS];
static uint8_t visible_mic_mask = 0;
static uint8_t microphone_channels = 0;
static bool es7210_ui = false;
static uint32_t applied_accent = UINT32_MAX;
static uint32_t applied_card_bg = UINT32_MAX;
static uint32_t applied_track = UINT32_MAX;

#if defined(T_DECK_V2_REV07) && defined(USING_AUDIO_CODEC)
#define UI_MIC_HAS_INPUT_SOURCE_SETTING 1
#else
#define UI_MIC_HAS_INPUT_SOURCE_SETTING 0
#endif

static lv_color_t channel_color(int channel)
{
    static const uint8_t brightness[MIC_UI_MAX_CHANNELS] = {
        255, 210, 170, 135
    };
    uint8_t scale = brightness[channel];
    uint32_t red = ((ui_active_accent >> 16) & 0xFF) * scale / 255;
    uint32_t green = ((ui_active_accent >> 8) & 0xFF) * scale / 255;
    uint32_t blue = (ui_active_accent & 0xFF) * scale / 255;
    return lv_color_hex((red << 16) | (green << 8) | blue);
}

static void apply_theme_colors(bool force = false)
{
    if (!force && applied_accent == ui_active_accent &&
            applied_card_bg == ui_active_card_bg &&
            applied_track == ui_active_track) {
        return;
    }

    applied_accent = ui_active_accent;
    applied_card_bg = ui_active_card_bg;
    applied_track = ui_active_track;

    for (int channel = 0; channel < MIC_UI_MAX_CHANNELS; channel++) {
        lv_color_t color = channel_color(channel);
        if (wave_tracks[channel]) {
            lv_obj_set_style_bg_color(wave_tracks[channel], UI_COLOR_CARD_BG, 0);
            lv_obj_set_style_border_color(wave_tracks[channel], color, 0);
        }
        if (wave_center_lines[channel]) {
            lv_obj_set_style_bg_color(wave_center_lines[channel], color, 0);
        }
        if (wave_glows[channel]) {
            lv_obj_set_style_line_color(wave_glows[channel], color, 0);
        }
        if (wave_lines[channel]) {
            lv_obj_set_style_line_color(wave_lines[channel], color, 0);
        }
        if (wave_labels[channel]) {
            lv_obj_set_style_text_color(wave_labels[channel], color, 0);
            lv_obj_set_style_bg_color(wave_labels[channel], UI_COLOR_CARD_BG, 0);
        }
    }

    for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
        if (!es7210_mic_buttons[mic]) continue;
        lv_obj_set_style_bg_color(es7210_mic_buttons[mic], UI_COLOR_TRACK, 0);
        lv_obj_set_style_bg_color(es7210_mic_buttons[mic], UI_COLOR_ACCENT,
                                  LV_STATE_CHECKED);
    }
}

static void clear_ui_references()
{
    channel_container = NULL;
    memset(wave_tracks, 0, sizeof(wave_tracks));
    memset(wave_center_lines, 0, sizeof(wave_center_lines));
    memset(wave_lines, 0, sizeof(wave_lines));
    memset(wave_glows, 0, sizeof(wave_glows));
    memset(wave_labels, 0, sizeof(wave_labels));
    memset(es7210_mic_buttons, 0, sizeof(es7210_mic_buttons));
}

static void stop_microphone()
{
    if (es7210_ui) {
        hw_set_es7210_mic_stop();
    } else {
        hw_set_mic_waveform_stop();
    }
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
    stop_microphone();
    es7210_ui = false;
    visible_mic_mask = 0;
    microphone_channels = 0;
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    clear_ui_references();
    menu_show();
}

static void refresh_wave_points(int channel, bool include_history)
{
    lv_obj_t *track = wave_tracks[channel];
    if (!track) return;

    int plot_w = lv_obj_get_content_width(track);
    int plot_h = lv_obj_get_content_height(track);
    if (plot_w < 2 || plot_h < 6) return;

    int center_y = plot_h / 2;
    int amplitude = plot_h / 2 - 3;
    for (int point = 0; point < MIC_UI_WAVE_POINTS; point++) {
        int32_t y_offset = 0;
        if (include_history) {
            y_offset = (int32_t)wave_history[channel][point] * amplitude * 5 / 32768;
            y_offset = constrain(y_offset, -amplitude, amplitude);
        }
        wave_points[channel][point].x =
            point * (plot_w - 1) / (MIC_UI_WAVE_POINTS - 1);
        wave_points[channel][point].y = center_y - y_offset;
    }

    lv_line_set_points_mutable(wave_glows[channel], wave_points[channel],
                               MIC_UI_WAVE_POINTS);
    lv_line_set_points_mutable(wave_lines[channel], wave_points[channel],
                               MIC_UI_WAVE_POINTS);
}

static void append_waveform(int channel, const int16_t *samples,
                            int sample_count, uint16_t level)
{
    if (!wave_tracks[channel] || !samples || sample_count <= 0 ||
            sample_count > MIC_UI_WAVE_POINTS) {
        return;
    }

    memmove(wave_history[channel], wave_history[channel] + sample_count,
            (MIC_UI_WAVE_POINTS - sample_count) * sizeof(int16_t));
    memcpy(wave_history[channel] + MIC_UI_WAVE_POINTS - sample_count,
           samples, sample_count * sizeof(int16_t));
    refresh_wave_points(channel, true);

    float target_level = constrain((float)level / 5000.0f, 0.0f, 1.0f);
    float smoothing = target_level > wave_levels[channel] ? 0.45f : 0.14f;
    wave_levels[channel] += (target_level - wave_levels[channel]) * smoothing;
    uint8_t glow_opa = (uint8_t)(25 + wave_levels[channel] * 85);
    int glow_width = 5 + (int)(wave_levels[channel] * 6);
    lv_obj_set_style_line_opa(wave_glows[channel], glow_opa, 0);
    lv_obj_set_style_line_width(wave_glows[channel], glow_width, 0);
    lv_obj_set_style_border_opa(wave_tracks[channel],
                                (uint8_t)(30 + wave_levels[channel] * 80), 0);
}

static void update_waveform_display(lv_timer_t *t)
{
    (void)t;
    apply_theme_colors();
    if (es7210_ui) {
        ES7210WaveformData data;
        if (!hw_audio_get_es7210_waveform_data(&data)) return;

        uint8_t active_mask = hw_get_es7210_mic_mask();
        for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
            if (active_mask & (1U << mic)) {
                append_waveform(mic, data.mic_samples[mic],
                                ES7210_WAVEFORM_SAMPLES, data.mic_level[mic]);
            }
        }
        return;
    }

    MicrophoneWaveformData data;
    if (!hw_audio_get_waveform_data(&data)) return;
    uint8_t channels = data.channels > MIC_WAVEFORM_MAX_CHANNELS ?
                       MIC_WAVEFORM_MAX_CHANNELS : data.channels;
    for (int channel = 0; channel < channels; channel++) {
        append_waveform(channel, data.samples[channel],
                        MIC_WAVEFORM_SAMPLES, data.level[channel]);
    }
}

static void create_wave_track(lv_obj_t *parent, int channel)
{
    lv_color_t color = channel_color(channel);
    lv_obj_t *track = lv_obj_create(parent);
    wave_tracks[channel] = track;
    lv_obj_set_size(track, LV_PCT(100), 1);
    lv_obj_set_flex_grow(track, 1);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_pad_all(track, 4, 0);
    lv_obj_set_style_bg_color(track, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_40, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, color, 0);
    lv_obj_set_style_border_opa(track, LV_OPA_20, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *center_line = lv_obj_create(track);
    wave_center_lines[channel] = center_line;
    lv_obj_set_size(center_line, LV_PCT(100), 1);
    lv_obj_align(center_line, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(center_line, color, 0);
    lv_obj_set_style_bg_opa(center_line, LV_OPA_20, 0);
    lv_obj_set_style_border_width(center_line, 0, 0);

    lv_obj_t *glow = lv_line_create(track);
    wave_glows[channel] = glow;
    lv_line_set_points_mutable(glow, wave_points[channel], MIC_UI_WAVE_POINTS);
    lv_obj_set_style_line_color(glow, color, 0);
    lv_obj_set_style_line_width(glow, 5, 0);
    lv_obj_set_style_line_opa(glow, LV_OPA_20, 0);
    lv_obj_set_style_line_rounded(glow, true, 0);

    lv_obj_t *line = lv_line_create(track);
    wave_lines[channel] = line;
    lv_line_set_points_mutable(line, wave_points[channel], MIC_UI_WAVE_POINTS);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(line, true, 0);

    static const char *channel_names[MIC_UI_MAX_CHANNELS] = {
        "MIC1", "MIC2", "MIC3", "MIC4"
    };
    lv_obj_t *label = lv_label_create(track);
    wave_labels[channel] = label;
    lv_label_set_text(label, microphone_channels == 1 && !es7210_ui ?
                      "MIC" : channel_names[channel]);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(label, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 2, 1);
}

static void rebuild_wave_tracks(uint8_t mic_mask)
{
    if (!channel_container || mic_mask == 0) return;

    lv_obj_clean(channel_container);
    memset(wave_tracks, 0, sizeof(wave_tracks));
    memset(wave_center_lines, 0, sizeof(wave_center_lines));
    memset(wave_lines, 0, sizeof(wave_lines));
    memset(wave_glows, 0, sizeof(wave_glows));
    memset(wave_labels, 0, sizeof(wave_labels));
    visible_mic_mask = mic_mask;

    for (int channel = 0; channel < microphone_channels; channel++) {
        if (mic_mask & (1U << channel)) create_wave_track(channel_container, channel);
    }
    apply_theme_colors(true);

    lv_obj_update_layout(channel_container);
    for (int channel = 0; channel < microphone_channels; channel++) {
        if (mic_mask & (1U << channel)) refresh_wave_points(channel, true);
    }
}

static void sync_es7210_button_states(uint8_t mic_mask)
{
    for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
        if (!es7210_mic_buttons[mic]) continue;
        if (mic_mask & (1U << mic)) {
            lv_obj_add_state(es7210_mic_buttons[mic], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(es7210_mic_buttons[mic], LV_STATE_CHECKED);
        }
    }
}

static void es7210_mic_button_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    uint8_t requested_mask = 0;
    for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
        if (es7210_mic_buttons[mic] &&
                lv_obj_has_state(es7210_mic_buttons[mic], LV_STATE_CHECKED)) {
            requested_mask |= 1U << mic;
        }
    }

    uint8_t current_mask = hw_get_es7210_mic_mask();
    if (requested_mask == 0 || !hw_set_es7210_mic_mask(requested_mask)) {
        sync_es7210_button_states(current_mask);
        return;
    }

    uint8_t newly_enabled = requested_mask & ~current_mask;
    for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
        if (newly_enabled & (1U << mic)) {
            memset(wave_history[mic], 0, sizeof(wave_history[mic]));
            wave_levels[mic] = 0.0f;
        }
    }
    rebuild_wave_tracks(requested_mask);
}

static void create_es7210_controls(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    static const char *labels[ES7210_MIC_CHANNELS] = {
        "MIC1", "MIC2", "MIC3", "MIC4"
    };
    for (int mic = 0; mic < ES7210_MIC_CHANNELS; mic++) {
        lv_obj_t *button = lv_button_create(row);
        es7210_mic_buttons[mic] = button;
        lv_obj_set_size(button, 1, 32);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_bg_color(button, UI_COLOR_TRACK, 0);
        lv_obj_set_style_bg_color(button, UI_COLOR_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_color(button, UI_COLOR_TEXT_PRIMARY, LV_STATE_CHECKED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(button, es7210_mic_button_event, LV_EVENT_CLICKED, NULL);
        ui_add_accent_focus_style(button);
        lv_group_t *group = lv_group_get_default();
        if (group) lv_group_add_obj(group, button);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, labels[mic]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_center(label);
    }
    sync_es7210_button_states(HW_ES7210_DEFAULT_MIC_MASK);
}

static void show_start_error(lv_obj_t *parent)
{
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    clear_ui_references();
    es7210_ui = false;
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Microphone start failed");
    lv_obj_center(label);
}

void ui_microphone_enter(lv_obj_t *parent)
{
    hw_set_es7210_mic_stop();
    hw_set_mic_waveform_stop();
    clear_ui_references();

    es7210_ui = hw_has_es7210_mic_array();
    microphone_channels = es7210_ui ? ES7210_MIC_CHANNELS :
                          constrain(hw_get_codec_input_channels(), 1,
                                    MIC_WAVEFORM_MAX_CHANNELS);
    visible_mic_mask = es7210_ui ? HW_ES7210_DEFAULT_MIC_MASK :
                       (uint8_t)((1U << microphone_channels) - 1U);

    page_container = ui_create_app_page(parent, "Microphone", back_event_handler);
    int screen_h = lv_display_get_vertical_resolution(NULL);
    int screen_w = lv_display_get_horizontal_resolution(NULL);
    bool compact_watch = screen_w == 240 && screen_h == 240;
    if (compact_watch) {
        lv_obj_set_style_pad_all(page_container, 0, 0);
        lv_obj_set_style_pad_row(page_container, 0, 0);
        lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);
    }

#if UI_MIC_HAS_INPUT_SOURCE_SETTING
    if (hw_has_mic_input_source_setting()) {
        lv_obj_t *control_card = ui_create_card(page_container, "Input");
        ui_create_card_info(control_card, LV_SYMBOL_AUDIO, "Source",
                            hw_get_mic_input_source_name());
    }
#endif

    if (es7210_ui) create_es7210_controls(page_container);

    lv_obj_t *card = ui_create_card(page_container, NULL);
    lv_obj_set_height(card, 0);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);
    if (compact_watch) lv_obj_set_style_radius(card, 0, 0);

    channel_container = lv_obj_create(card);
    lv_obj_set_size(channel_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(channel_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(channel_container, 0, 0);
    lv_obj_set_style_radius(channel_container, 0, 0);
    lv_obj_set_style_pad_all(channel_container, 0, 0);
    lv_obj_set_style_pad_row(channel_container, 3, 0);
    lv_obj_set_flex_flow(channel_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(channel_container, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    memset(wave_history, 0, sizeof(wave_history));
    memset(wave_levels, 0, sizeof(wave_levels));
    rebuild_wave_tracks(visible_mic_mask);

    bool started = es7210_ui ?
                   hw_set_es7210_mic_start(HW_ES7210_DEFAULT_MIC_MASK) :
                   hw_set_mic_waveform_start();
    if (!started) {
        show_start_error(parent);
        return;
    }

    timer = lv_timer_create(update_waveform_display, 50, NULL);
}

void ui_microphone_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_microphone_main = {
    .setup_func_cb = ui_microphone_enter,
    .exit_func_cb  = ui_microphone_exit,
    .user_data     = nullptr,
};

#endif /* EXCLUDE_MICROPHONE */
