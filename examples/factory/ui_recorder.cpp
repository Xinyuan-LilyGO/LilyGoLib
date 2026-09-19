/**
 * @file      ui_recorder.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-09-18
 *
 * Audio Recorder — records 5 s to PSRAM (320-byte chunks), plays back via speaker.
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include <cmath>

#if !defined(EXCLUDE_AUDIO_RECORDER)

// ============================================================
// Audio parameters
// ============================================================
static constexpr uint16_t kSampleRate    = 16000;
static constexpr uint8_t  kBitsPerSample = 16;
static constexpr uint32_t kMaxRecordSec  = 5;
static constexpr size_t   kChunkBytes    = 320;   // 10 ms @ 16 kHz 16-bit mono
static const uint8_t  kChannels      = instance.getCodecInputChannels();

// Mic level sensitivity (lower = more sensitive)
static constexpr int kMicSensitivity = 1000;

// These Watch PDM paths have a board-dependent DC bias in their PCM output.
#if defined(USING_PDM_MICROPHONE) && \
    (defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || \
     defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA))
static constexpr bool kRemoveMicDcOffset = true;
#else
static constexpr bool kRemoveMicDcOffset = false;
#endif

// ============================================================
// LVGL widgets
// ============================================================
static lv_obj_t   *page_container = NULL;
static lv_obj_t   *quit_btn     = NULL;
static lv_obj_t   *record_btn   = NULL;
static lv_obj_t   *play_btn     = NULL;
static lv_obj_t   *gain_slider  = NULL;
static lv_obj_t   *time_label   = NULL;
static lv_obj_t   *level_bar    = NULL;   // mic level indicator
static lv_obj_t   *level_label  = NULL;
static lv_obj_t   *input_source_label = NULL;

// ============================================================
// Audio devices
// ============================================================
static AudioInputIf  *audioInput  = NULL;
static AudioOutputIf *audioOutput = NULL;

// ============================================================
// State
// ============================================================
static volatile int  s_mode       = 0;     // 0=idle, 1=recording, 2=playing
static uint8_t      *s_wav_buf    = NULL;
static size_t        s_wav_size   = 0;
static size_t        s_wav_alloc  = 0;
static bool          s_can_play   = false;
static bool          s_mic_is_open = false;
static volatile int  s_current_level = 0;  // Current mic level for display (0-100)

static TaskHandle_t  s_rec_task   = NULL;
static TaskHandle_t  s_play_task  = NULL;
static volatile bool s_rec_done   = false;
static volatile bool s_play_done  = false;
static volatile bool s_stop_rec   = false;
static volatile bool s_rec_started = false;
static volatile uint32_t s_rec_start_ms = 0;
static volatile uint32_t s_rec_elapsed_ms = 0;

static lv_timer_t   *s_tick_timer = NULL;

// ============================================================
// Visual helpers
// ============================================================
static void btn_set_enabled(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_opa(btn, on ? LV_OPA_100 : LV_OPA_40, 0);
}

static void slider_set_enabled(lv_obj_t *sl, bool on)
{
    if (!sl) return;
    lv_obj_set_style_bg_opa(sl, on ? LV_OPA_100 : LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, on ? LV_OPA_100 : LV_OPA_40, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, on ? LV_OPA_100 : LV_OPA_40, LV_PART_KNOB);
}

static bool using_external_mic_source()
{
    return hw_has_mic_input_source_setting() && hw_get_mic_input_source() == MIC_INPUT_SOURCE_JACK;
}

static void normalize_record_chunk(uint8_t *chunk, int bytes)
{
    if (!using_external_mic_source() || kChannels < 2 || !chunk || bytes < (int)(sizeof(int16_t) * kChannels)) {
        return;
    }

    int16_t *samples = (int16_t *)chunk;
    int frames = bytes / (sizeof(int16_t) * kChannels);
    for (int i = 0; i < frames; i++) {
        int16_t left = samples[i * kChannels];
        for (uint8_t ch = 1; ch < kChannels; ch++) {
            samples[i * kChannels + ch] = left;
        }
    }
}

static int calculate_mic_level(const int16_t *samples, int sample_count)
{
    if (!samples || sample_count <= 0) {
        return 0;
    }

    int stride = 1;
    if (using_external_mic_source() && kChannels >= 2) {
        stride = kChannels;
    }

    int count = sample_count / stride;
    if (count <= 0) {
        return 0;
    }

    int32_t mean = 0;
    if (kRemoveMicDcOffset) {
        int64_t sum = 0;
        for (int i = 0; i < count * stride; i += stride) {
            sum += samples[i];
        }
        mean = sum / count;
    }

    int64_t sum_sq = 0;
    for (int i = 0; i < count * stride; i += stride) {
        int32_t val = (int32_t)samples[i] - mean;
        sum_sq += (int64_t)val * val;
    }

    int32_t rms = sqrt(sum_sq / count);
    int level = map(rms, 0, kMicSensitivity, 0, 100);
    return constrain(level, 0, 100);
}

static void update_time_label(uint32_t elapsed_ms)
{
    if (!time_label) {
        return;
    }

    uint32_t max_ms = kMaxRecordSec * 1000UL;
    if (elapsed_ms > max_ms) {
        elapsed_ms = max_ms;
    }
    uint32_t sec = elapsed_ms / 1000;
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 60, sec % 60);
    lv_label_set_text(time_label, buf);
}

// ============================================================
// UI refresh
// ============================================================
static void refresh_ui()
{
    switch (s_mode) {
    case 1: /* Recording */
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x333333), 0);
        lv_label_set_text(lv_obj_get_child(record_btn, 0), "Stop");
        btn_set_enabled(record_btn, false);
        btn_set_enabled(play_btn, false);
        slider_set_enabled(gain_slider, false);
        break;

    case 2: /* Playing */
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x2A2A2A), 0);
        lv_label_set_text(lv_obj_get_child(record_btn, 0), "Record");
        btn_set_enabled(record_btn, false);
        lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x333333), 0);
        lv_label_set_text(lv_obj_get_child(play_btn, 0), "Stop");
        slider_set_enabled(gain_slider, false);
        break;

    default: /* Idle */
        lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x2A2A2A), 0);
        lv_label_set_text(lv_obj_get_child(record_btn, 0), "Record");
        btn_set_enabled(record_btn, true);
        btn_set_enabled(play_btn, s_can_play);
        lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x2A2A2A), 0);
        lv_label_set_text(lv_obj_get_child(play_btn, 0), "Play");
        slider_set_enabled(gain_slider, true);
        break;
    }
}

// ============================================================
// Recording task — reads 320-byte chunks (same as walkie TX)
// ============================================================
static void rec_task_fn(void *arg)
{
    LILYGO_LOG_PRINTLN("[REC] task started");

    // Allocate PSRAM: header + max PCM for 5 s
    size_t pcm_total = kMaxRecordSec * kSampleRate * (kBitsPerSample / 8) * kChannels;
    s_wav_alloc = PCM_WAV_HEADER_SIZE + pcm_total;

    if (s_wav_buf) {
        free(s_wav_buf);
        s_wav_buf = NULL;
    }
    s_wav_buf = (uint8_t *)ps_malloc(s_wav_alloc);
    if (!s_wav_buf) {
        LILYGO_LOG_PRINTF("[REC] PSRAM alloc failed: %u bytes\n", (unsigned)s_wav_alloc);
        s_rec_task = NULL;
        s_rec_done = true;
        vTaskDelete(NULL);
        return;
    }

    // Write placeholder WAV header (will be patched at the end)
    pcm_wav_header_t hdr = PCM_WAV_HEADER_DEFAULT(pcm_total, kBitsPerSample, kSampleRate, kChannels);
    memcpy(s_wav_buf, &hdr, PCM_WAV_HEADER_SIZE);

    // Open microphone
    LILYGO_LOG_PRINTF("[REC] opening mic: bits=%d ch=%d rate=%lu\n", kBitsPerSample, kChannels, kSampleRate);
    bool ok = audioInput->open(kBitsPerSample, kChannels, kSampleRate);
    LILYGO_LOG_PRINTF("[REC] open returned %d\n", ok);
    if (!ok) {
        free(s_wav_buf);
        s_wav_buf = NULL;
        s_rec_task = NULL;
        s_rec_done = true;
        vTaskDelete(NULL);
        return;
    }
    if (hw_has_mic_input_source_setting()) {
        hw_apply_mic_input_source();
    }

    // Read loop — 320 bytes per iteration, same as walkie TX task
    size_t pcm_offset = PCM_WAV_HEADER_SIZE;
    uint32_t start_ms = millis();
    s_rec_start_ms = start_ms;
    s_rec_started = true;
    s_stop_rec = false;

    while (!s_stop_rec) {
        if (millis() - start_ms >= kMaxRecordSec * 1000) break;

        uint8_t chunk[kChunkBytes];
        int got = audioInput->read(chunk, sizeof(chunk));
        if (got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (pcm_offset + got > s_wav_alloc) break;

        normalize_record_chunk(chunk, got);
        memcpy(s_wav_buf + pcm_offset, chunk, got);
        pcm_offset += got;

        // Calculate mic level for display
        if (got >= 2) {
            int16_t *samples = (int16_t *)chunk;
            int num_samples = got / 2;
            s_current_level = calculate_mic_level(samples, num_samples);
        }
    }

    uint32_t elapsed_ms = millis() - start_ms;
    uint32_t max_ms = kMaxRecordSec * 1000UL;
    s_rec_elapsed_ms = elapsed_ms > max_ms ? max_ms : elapsed_ms;
    s_rec_started = false;
    audioInput->close();

    s_wav_size = pcm_offset;

    // Patch WAV header with actual data size
    size_t pcm_data = s_wav_size - PCM_WAV_HEADER_SIZE;
    pcm_wav_header_t *hdr_ptr = (pcm_wav_header_t *)s_wav_buf;
    hdr_ptr->data_chunk.subchunk_size = pcm_data;
    hdr_ptr->descriptor_chunk.chunk_size = pcm_data + sizeof(pcm_wav_header_t) - 8;

    s_can_play = (pcm_data > 0);

    LILYGO_LOG_PRINTF("[REC] done: wav_size=%u pcm_data=%u can_play=%d\n",
                  (unsigned)s_wav_size, (unsigned)pcm_data, s_can_play);

    s_rec_task = NULL;
    s_rec_done = true;
    vTaskDelete(NULL);
}

// ============================================================
// Playback task
// ============================================================
static void play_task_fn(void *arg)
{
    LILYGO_LOG_PRINTF("[PLAY] task started, size=%u\n", (unsigned)s_wav_size);

    if (s_wav_buf && s_wav_size > PCM_WAV_HEADER_SIZE) {
        audioOutput->open(kBitsPerSample, kChannels, kSampleRate);
        audioOutput->setVolume(85);
        audioOutput->playWAV(s_wav_buf, s_wav_size);
        audioOutput->close();
        LILYGO_LOG_PRINTLN("[PLAY] playWAV finished");
    } else {
        LILYGO_LOG_PRINTLN("[PLAY] no data");
    }

    s_play_task = NULL;
    s_play_done = true;
    vTaskDelete(NULL);
}

// ============================================================
// LVGL tick timer
// ============================================================
static void tick_timer_cb(lv_timer_t *t)
{
    if (s_rec_done) {
        LILYGO_LOG_PRINTLN("[TICK] rec_done -> idle");
        s_rec_done = false;
        s_mode = 0;
        update_time_label(s_rec_elapsed_ms);
        refresh_ui();
    }

    if (s_play_done) {
        LILYGO_LOG_PRINTLN("[TICK] play_done -> idle");
        s_play_done = false;
        s_mode = 0;
        refresh_ui();
    }

    if (s_mode == 1 && time_label) {
        uint32_t elapsed_ms = s_rec_started ? millis() - s_rec_start_ms : 0;
        update_time_label(elapsed_ms);
    }

    // Update mic level bar
    if (level_bar && level_label) {
        int level = 0;
        if (s_mode == 1) {
            // Recording: use level calculated by recording task
            level = s_current_level;
        } else {
            // Idle or playing: read mic directly if open
            if (s_mic_is_open) {
                int16_t buf[80];
                int samples = audioInput->read((uint8_t *)buf, sizeof(buf)) / 2;
                if (samples > 0) {
                    level = calculate_mic_level(buf, samples);
                } else {
                    // Read failed, mic may have been closed
                    s_mic_is_open = false;
                    if (audioInput->open(kBitsPerSample, kChannels, kSampleRate)) {
                        s_mic_is_open = true;
                        if (hw_has_mic_input_source_setting()) {
                            hw_apply_mic_input_source();
                        }
                        LILYGO_LOG_PRINTLN("[TICK] Mic reopened after read failure");
                    }
                }
            } else {
                // Try to open mic for monitoring
                if (audioInput && audioInput->open(kBitsPerSample, kChannels, kSampleRate)) {
                    s_mic_is_open = true;
                    if (hw_has_mic_input_source_setting()) {
                        hw_apply_mic_input_source();
                    }
                    LILYGO_LOG_PRINTLN("[TICK] Mic opened for level monitoring");
                }
            }
        }
        lv_bar_set_value(level_bar, level, LV_ANIM_ON);
        lv_label_set_text_fmt(level_label, "Level %d%%", level);
    }
}

// ============================================================
// Button callbacks
// ============================================================
static void record_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    LILYGO_LOG_PRINTF("[BTN] record clicked, mode=%d\n", s_mode);

    if (s_mode == 1) {
        // Stop recording
        LILYGO_LOG_PRINTLN("[BTN] stop recording");
        s_stop_rec = true;
        return;
    }

    if (s_mode != 0) return;

    s_rec_done  = false;
    s_can_play  = false;
    s_wav_size  = 0;
    s_stop_rec  = false;
    s_rec_started = false;
    s_rec_start_ms = 0;
    s_rec_elapsed_ms = 0;
    s_current_level = 0;
    s_mode      = 1;
    refresh_ui();

    update_time_label(0);

    BaseType_t ret = xTaskCreate(rec_task_fn, "rec", 4096, NULL, 1, &s_rec_task);
    LILYGO_LOG_PRINTF("[BTN] xTaskCreate rec=%d\n", ret);
}

static void play_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    LILYGO_LOG_PRINTF("[BTN] play clicked, mode=%d can_play=%d\n", s_mode, s_can_play);

    if (s_mode != 0 || !s_can_play) return;

    s_play_done = false;
    s_mode      = 2;
    refresh_ui();

    BaseType_t ret = xTaskCreate(play_task_fn, "play", 4096, NULL, 1, &s_play_task);
    LILYGO_LOG_PRINTF("[BTN] xTaskCreate play=%d\n", ret);
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

// ============================================================
// Button factory
// ============================================================
static lv_obj_t *create_icon_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 90, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

// ============================================================
// Build the recorder page
// ============================================================
static void build_recorder_ui(lv_obj_t *parent)
{
    lv_obj_t *mc = parent;
    /* Keep the container size from ui_create_app_page, just set flex and colors */
    lv_obj_set_flex_flow(mc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(mc, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mc, UI_COLOR_BG, 0);
    lv_obj_set_style_pad_row(mc, 8, LV_PART_MAIN);

    /* Time display — centered, large */
    time_label = lv_label_create(mc);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_margin_top(time_label, 8, 0);

    if (hw_has_mic_input_source_setting()) {
        input_source_label = lv_label_create(mc);
        lv_label_set_text_fmt(input_source_label, "Source: %s", hw_get_mic_input_source_name());
        lv_obj_set_style_text_font(input_source_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(input_source_label, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_margin_top(input_source_label, 0, 0);
        lv_obj_set_style_margin_bottom(input_source_label, 2, 0);
    } else {
        input_source_label = NULL;
    }

    lv_obj_t *bc = lv_obj_create(mc);
    lv_obj_set_size(bc, LV_PCT(90), 45);
    lv_obj_set_flex_flow(bc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bc, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(bc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bc, 0, 0);
    lv_obj_set_style_radius(bc, 0, 0);
    lv_obj_set_style_pad_all(bc, 0, 0);
    lv_obj_set_style_margin_bottom(bc, 4, 0);

    record_btn = create_icon_button(bc, "Record");
    lv_obj_add_event_cb(record_btn, record_btn_cb, LV_EVENT_CLICKED, NULL);

    play_btn = create_icon_button(bc, "Play");
    lv_obj_add_event_cb(play_btn, play_btn_cb, LV_EVENT_CLICKED, NULL);
    btn_set_enabled(play_btn, false);

    /* Mic level indicator — same layout as volume */
    lv_obj_t *lc = lv_obj_create(mc);
    lv_obj_set_size(lc, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(lc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lc, 0, 0);
    lv_obj_set_style_radius(lc, 0, 0);
    lv_obj_set_style_pad_all(lc, 0, 0);

    level_label = lv_label_create(lc);
    lv_label_set_text(level_label, "Level 0%");
    lv_obj_set_style_text_font(level_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(level_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_margin_bottom(level_label, 4, 0);

    level_bar = lv_bar_create(lc);
    lv_obj_set_width(level_bar, LV_PCT(85));
    lv_obj_set_height(level_bar, 8);
    lv_bar_set_range(level_bar, 0, 100);
    lv_bar_set_value(level_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(level_bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(level_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(level_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(level_bar, 4, LV_PART_INDICATOR);

    lv_obj_t *ac = lv_obj_create(mc);
    lv_obj_set_size(ac, LV_PCT(90), 70);
    lv_obj_set_flex_flow(ac, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ac, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ac, 0, 0);
    lv_obj_set_style_bg_opa(ac, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ac, 0, 0);
    lv_obj_set_style_radius(ac, 0, 0);
    lv_obj_set_style_margin_top(ac, 0, 0);

    lv_obj_t *vl = lv_label_create(ac);
    lv_label_set_text(vl, "Volume");
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(vl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_margin_bottom(vl, 4, 0);

    lv_obj_t *vs = lv_slider_create(ac);
    lv_obj_set_width(vs, LV_PCT(85));
    lv_slider_set_range(vs, 0, 100);
    lv_slider_set_value(vs, 70, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vs, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(vs, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vs, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(vs, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(vs, 8, LV_PART_INDICATOR);
    lv_obj_set_style_radius(vs, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_size(vs, 16, 14, LV_PART_KNOB);

    lv_slider_set_value(vs, hw_get_volume(), LV_ANIM_OFF);
    lv_slider_set_range(vs, 0, 100);
    ui_prepare_slider_for_encoder(vs);
    lv_obj_add_event_cb(vs, volume_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
}

// ============================================================
// Back button
// ============================================================
static void back_event_handler(lv_event_t *e)
{
    s_stop_rec = true;

    uint32_t t0 = millis();
    while ((s_rec_task || s_play_task) && millis() - t0 < 6000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Close microphone if opened for monitoring
    if (s_mic_is_open && audioInput) {
        audioInput->close();
        s_mic_is_open = false;
        LILYGO_LOG_PRINTLN("[BACK] Mic closed");
    }

    if (s_tick_timer) {
        lv_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }
    if (s_wav_buf)    {
        free(s_wav_buf);
        s_wav_buf = NULL;
    }

    s_mode     = 0;
    s_can_play = false;
    input_source_label = NULL;

    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    if (quit_btn) {
        lv_obj_delete_async(quit_btn);
        quit_btn = NULL;
    }

    menu_show();
}

// ============================================================
// Enter / Exit
// ============================================================
void ui_recorder_enter(lv_obj_t *parent)
{
    LILYGO_LOG_PRINTLN("[INIT] ui_recorder_enter");

    audioOutput = instance.getAudioOutput();
    audioInput  = instance.getAudioInput();
    LILYGO_LOG_PRINTF("[INIT] audioInput=%p audioOutput=%p\n", audioInput, audioOutput);

    if (!audioInput || !audioOutput) {
        LILYGO_LOG_PRINTLN("[INIT] ERROR: Audio device not initialized");
        return;
    }

    // Open microphone for level monitoring
    if (audioInput->open(kBitsPerSample, kChannels, kSampleRate)) {
        s_mic_is_open = true;
        if (hw_has_mic_input_source_setting()) {
            hw_apply_mic_input_source();
        }
        LILYGO_LOG_PRINTLN("[INIT] Mic opened for level monitoring");
    } else {
        LILYGO_LOG_PRINTLN("[INIT] Failed to open mic for level monitoring");
    }

    page_container = ui_create_app_page(parent, "Recorder", back_event_handler);
    build_recorder_ui(page_container);

    s_tick_timer = lv_timer_create(tick_timer_cb, 100, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        back_event_handler(e);
    }, NULL);
#endif
}

void ui_recorder_exit(lv_obj_t *parent) {}

app_t ui_recorder_main = {
    .setup_func_cb = ui_recorder_enter,
    .exit_func_cb  = ui_recorder_exit,
    .user_data     = nullptr,
};

#endif /* EXCLUDE_AUDIO_RECORDER */
