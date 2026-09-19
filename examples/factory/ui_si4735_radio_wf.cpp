/**
 * @file      ui_si4735_radio_wf.cpp
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 *
 * SI4735 Radio + Waterfall — compact layout for 320x240 DeckV2
 */
#include "ui_define.h"

#if !defined(EXCLUDE_SI4735_RADIO_WF)

#include <esp_heap_caps.h>
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"

LV_FONT_DECLARE(font_alibaba_24);

#define SI4735_WF_FFT_SIZE 256
#define WF_COLS             304  /* 320px - 8px*2 padding */
#define WF_ROWS             50   /* time steps   (Y axis, scrolling down) */

/* ── State ── */
static lv_obj_t *page_container = NULL;
static lv_obj_t *freq_lbl = NULL;
static lv_obj_t *info_lbl = NULL;
static lv_obj_t *stereo_lbl = NULL;
static lv_obj_t *wf_canvas = NULL;
static lv_obj_t *vol_slider = NULL;
static lv_obj_t *vol_lbl = NULL;
static lv_timer_t *update_timer = NULL;
static int current_vol = 43;
static int timer_tick = 0;

/* FFT */
static int16_t i2s_buf[SI4735_WF_FFT_SIZE * 2];
static float fft_in[SI4735_WF_FFT_SIZE * 2] __attribute__((aligned(16)));
static float hann_win[SI4735_WF_FFT_SIZE];

/* Waterfall pixels — RGB565, freq on X, time on Y (newest at top) */
static uint16_t *wf_pixels = NULL;
static lv_image_dsc_t wf_dsc;

/* ── FFT with interpolation ── */
static float fft_mag[SI4735_WF_FFT_SIZE / 2]; /* magnitude of each FFT bin */

static bool wf_alloc_pixels(void)
{
    if (wf_pixels) return true;
    wf_pixels = (uint16_t *)heap_caps_malloc(WF_ROWS * WF_COLS * sizeof(uint16_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wf_pixels) {
        wf_pixels = (uint16_t *)heap_caps_malloc(WF_ROWS * WF_COLS * sizeof(uint16_t),
                                                 MALLOC_CAP_8BIT);
    }
    if (!wf_pixels) return false;
    memset(wf_pixels, 0, WF_ROWS * WF_COLS * sizeof(uint16_t));
    return true;
}

static void wf_free_pixels(void)
{
    if (!wf_pixels) return;
    heap_caps_free(wf_pixels);
    wf_pixels = NULL;
}

static void do_fft(int16_t *samples, float *out)
{
    for (int i = 0; i < SI4735_WF_FFT_SIZE; i++) {
        fft_in[2 * i] = (float)samples[i] * 3.0f / 32768.0f * hann_win[i];
        fft_in[2 * i + 1] = 0;
    }
    dsps_fft2r_fc32(fft_in, SI4735_WF_FFT_SIZE);
    dsps_bit_rev_fc32(fft_in, SI4735_WF_FFT_SIZE);
    dsps_cplx2reC_fc32(fft_in, SI4735_WF_FFT_SIZE);

    /* Compute magnitudes in dB scale */
    int fft_bins = SI4735_WF_FFT_SIZE / 2;  /* 128 */
    for (int i = 0; i < fft_bins; i++) {
        float r = fft_in[2 * i], im = fft_in[2 * i + 1];
        float mag = sqrtf(r * r + im * im);
        /* Convert to dB: 10*log10(mag), add floor to avoid -inf */
        fft_mag[i] = 10.0f * log10f(mag + 0.0001f);
    }

    /* Normalize dB values to 0-1 range */
    float mn = fft_mag[0], mx = fft_mag[0];
    for (int i = 1; i < fft_bins; i++) {
        if (fft_mag[i] < mn) mn = fft_mag[i];
        if (fft_mag[i] > mx) mx = fft_mag[i];
    }
    float range = mx - mn;
    if (range < 0.1f) range = 0.1f;
    for (int i = 0; i < fft_bins; i++) {
        fft_mag[i] = (fft_mag[i] - mn) / range;
    }

    /* Interpolate 128 bins → WF_COLS (304) output columns */
    for (int c = 0; c < WF_COLS; c++) {
        float pos = (float)c * (fft_bins - 1) / (WF_COLS - 1);
        int idx = (int)pos;
        float frac = pos - idx;
        if (idx >= fft_bins - 1) {
            out[c] = fft_mag[fft_bins - 1];
        } else {
            out[c] = fft_mag[idx] * (1 - frac) + fft_mag[idx + 1] * frac;
        }
    }
}

/* ── Heatmap: 0-1 → RGB565 ── */
static uint16_t heat_color(float v)
{
    if (v < 0) v = 0; if (v > 1) v = 1;
    uint8_t r, g, b;
    if (v < 0.25f)      {
        r = 0;
        g = 0;
        b = (uint8_t)(v * 4 * 255);
    } else if (v < 0.5f)  {
        r = 0;
        g = (uint8_t)((v - 0.25f) * 4 * 255);
        b = 255;
    } else if (v < 0.75f) {
        r = (uint8_t)((v - 0.5f) * 4 * 255);
        g = 255;
        b = (uint8_t)(255 - r);
    } else                {
        r = 255;
        g = (uint8_t)((1 - v) * 4 * 255);
        b = 0;
    }
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* ── Update timer (100ms) ── */
static void update_cb(lv_timer_t *t)
{
    timer_tick++;

    /* Read I2S audio from SI4735 */
    AudioInputIf *mic = instance.getAudioInput();
    if (mic && wf_pixels) {
        int rd = mic->read((uint8_t *)i2s_buf, SI4735_WF_FFT_SIZE * 2 * sizeof(int16_t));
        if (rd > 0) {
            int16_t left[SI4735_WF_FFT_SIZE];
            for (int i = 0; i < SI4735_WF_FFT_SIZE; i++) left[i] = i2s_buf[2 * i];

            float bands[WF_COLS];
            do_fft(left, bands);  /* already normalized inside */

            /* Scroll waterfall: shift rows DOWN (newest at top) */
            for (int r = WF_ROWS - 1; r > 0; r--)
                memcpy(&wf_pixels[r * WF_COLS], &wf_pixels[(r - 1) * WF_COLS], WF_COLS * sizeof(uint16_t));

            /* Write newest row at top */
            for (int c = 0; c < WF_COLS; c++)
                wf_pixels[c] = heat_color(bands[c]);

            if (wf_canvas) {
                lv_obj_invalidate(wf_canvas);
            }
        }
    }

    /* RSSI/SNR + Stereo every 1s */
    if (timer_tick % 10 == 0 && info_lbl) {
        int16_t rssi = hw_si4735_get_rssi();
        int16_t snr = hw_si4735_get_snr();
        char buf[48];
        snprintf(buf, sizeof(buf), "RSSI:%d SNR:%d", rssi, snr);
        lv_label_set_text(info_lbl, buf);

        /* Stereo indicator (FM only) */
        if (stereo_lbl && hw_si4735_is_fm()) {
            instance.si4735.getCurrentReceivedSignalQuality();
            bool stereo = instance.si4735.getCurrentPilot();
            lv_label_set_text(stereo_lbl, stereo ? "ST" : "MO");
        }
    }

    /* Frequency every 500ms */
    if (timer_tick % 5 == 0 && freq_lbl) {
        char buf[32];
        uint16_t freq = hw_si4735_get_freq();
        snprintf(buf, sizeof(buf), "%d.%02d %s",
                 freq / 100, freq % 100,
                 hw_si4735_is_fm() ? "MHz" : "KHz");
        lv_label_set_text(freq_lbl, buf);
    }
}

/* ── Buttons ── */
static void tune_up_cb(lv_event_t *e)
{
    hw_si4735_set_freq_up();
}
static void tune_down_cb(lv_event_t *e)
{
    hw_si4735_set_freq_down();
}
static void seek_up_cb(lv_event_t *e)
{
    hw_si4735_seek_up();
}
static void seek_down_cb(lv_event_t *e)
{
    hw_si4735_seek_down();
}

static void vol_cb(lv_event_t *e)
{
    current_vol = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    hw_si4735_set_volume(current_vol);
    if (vol_lbl) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", current_vol);
        lv_label_set_text(vol_lbl, buf);
    }
}

/* ── Back ── */
static void back_event_handler(lv_event_t *e)
{
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }
    hw_si4735_set_power(false);
    dsps_fft2r_deinit_fc32();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    wf_free_pixels();
    freq_lbl = info_lbl = stereo_lbl = wf_canvas = vol_slider = vol_lbl = NULL;
    menu_show();
}

/* ── Make button helper ── */
static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 6, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    return btn;
}

/* ── Enter (auto-start) ── */
void ui_si4735_radio_wf_enter(lv_obj_t *parent)
{
    page_container = ui_create_app_page(parent, "Radio+WF", back_event_handler);

    /* Init waterfall */
    if (!wf_alloc_pixels()) {
        lv_obj_t *lbl = lv_label_create(page_container);
        lv_label_set_text(lbl, "No RAM for waterfall");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF4444), 0);
        return;
    }
    wf_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    wf_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    wf_dsc.header.w = WF_COLS;
    wf_dsc.header.h = WF_ROWS;
    wf_dsc.header.stride = WF_COLS * 2;
    wf_dsc.data_size = WF_COLS * WF_ROWS * 2;
    wf_dsc.data = (const uint8_t *)wf_pixels;

    /* Init FFT */
    dsps_fft2r_init_fc32(NULL, SI4735_WF_FFT_SIZE);
    dsps_wind_hann_f32(hann_win, SI4735_WF_FFT_SIZE);

    /* ── Row 1: Frequency + RSSI/SNR (compact) ── */
    lv_obj_t *top_row = lv_obj_create(page_container);
    lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 4, 0);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_row, LV_SCROLLBAR_MODE_OFF);

    freq_lbl = lv_label_create(top_row);
    lv_label_set_text(freq_lbl, "920.00 MHz");
    lv_obj_set_width(freq_lbl, 145);
    lv_obj_set_style_text_align(freq_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(freq_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(freq_lbl, &font_alibaba_24, 0);

    info_lbl = lv_label_create(top_row);
    lv_label_set_text(info_lbl, "RSSI:-- SNR:--");
    lv_obj_set_width(info_lbl, 110);
    lv_obj_set_style_text_align(info_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(info_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_12, 0);

    stereo_lbl = lv_label_create(top_row);
    lv_label_set_text(stereo_lbl, "ST");
    lv_obj_set_style_text_color(stereo_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(stereo_lbl, &lv_font_montserrat_14, 0);

    /* ── Row 2: Waterfall canvas (fills full width) ── */
    wf_canvas = lv_canvas_create(page_container);
    lv_canvas_set_buffer(wf_canvas, wf_pixels, WF_COLS, WF_ROWS, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(wf_canvas, LV_PCT(100), WF_ROWS);
    lv_obj_align(wf_canvas, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(wf_canvas, 4, 0);
    lv_obj_set_style_clip_corner(wf_canvas, true, 0);

    /* ── Row 3: Tune buttons (compact) ── */
    lv_obj_t *tune_row = lv_obj_create(page_container);
    lv_obj_set_size(tune_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tune_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tune_row, 0, 0);
    lv_obj_set_style_pad_all(tune_row, 2, 0);
    lv_obj_set_flex_flow(tune_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tune_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tune_row, 6, 0);

    make_btn(tune_row, LV_SYMBOL_PREV, lv_color_hex(0x333333), seek_down_cb);
    make_btn(tune_row, "-", lv_color_hex(0x444444), tune_down_cb);
    make_btn(tune_row, "+", lv_color_hex(0x444444), tune_up_cb);
    make_btn(tune_row, LV_SYMBOL_NEXT, lv_color_hex(0x333333), seek_up_cb);

    /* ── Row 4: Volume slider (compact) ── */
    lv_obj_t *vol_row = lv_obj_create(page_container);
    lv_obj_set_size(vol_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_row, 0, 0);
    lv_obj_set_style_pad_all(vol_row, 2, 0);
    lv_obj_set_flex_flow(vol_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vol_row, 16, 0);

    lv_obj_t *vol_icon = lv_label_create(vol_row);
    lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_icon, UI_COLOR_ACCENT, 0);

    vol_slider = lv_slider_create(vol_row);
    lv_obj_set_width(vol_slider, LV_PCT(70));
    lv_slider_set_range(vol_slider, 0, 63);
    lv_slider_set_value(vol_slider, current_vol, LV_ANIM_OFF);
    ui_prepare_slider_for_encoder(vol_slider);
    lv_obj_set_style_bg_color(vol_slider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vol_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(vol_slider, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);

    vol_lbl = lv_label_create(vol_row);
    char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%d", current_vol);
    lv_label_set_text(vol_lbl, vbuf);
    lv_obj_set_width(vol_lbl, 32);
    lv_obj_set_style_text_align(vol_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(vol_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(vol_lbl, &lv_font_montserrat_12, 0);

    /* ── Auto-start radio ── */
    hw_si4735_set_power(true);
    hw_si4735_set_volume(current_vol);
    timer_tick = 0;
    update_timer = lv_timer_create(update_cb, 100, NULL);
}

void ui_si4735_radio_wf_exit(lv_obj_t *parent) {}

app_t ui_si4735_radio_wf_main = {
    .setup_func_cb = ui_si4735_radio_wf_enter,
    .exit_func_cb  = ui_si4735_radio_wf_exit,
    .user_data     = nullptr,
};

#endif
