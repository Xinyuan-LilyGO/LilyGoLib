/**
 * @file      ui_thermal.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-05
 * 
 * MLX90640 thermal camera viewer for the factory UI.
 */
#include <LilyGoLog.h>
#include "ui_define.h"

#include <math.h>
#include <stdlib.h>

#if defined(ARDUINO) && __has_include(<MLX90640_API.h>) && __has_include(<MLX90640_I2C_Driver.h>)
#include <Wire.h>
#include <esp_heap_caps.h>
#include <MLX90640_API.h>
#include <MLX90640_I2C_Driver.h>
#define THERMAL_HAS_MLX90640 1
#else
#define THERMAL_HAS_MLX90640 0
#endif

#if !defined(EXCLUDE_THERMAL)

#define THERMAL_COLS       32
#define THERMAL_ROWS       24
#define THERMAL_CELL       5
#define THERMAL_CANVAS_W   (THERMAL_COLS * THERMAL_CELL)
#define THERMAL_CANVAS_H   (THERMAL_ROWS * THERMAL_CELL)
#define THERMAL_FRAME_LEN  (THERMAL_COLS * THERMAL_ROWS)
#define THERMAL_I2C_ADDR   0x33
#define THERMAL_I2C_KHZ    100
#define THERMAL_TA_SHIFT   8.0f
#define THERMAL_EMISSIVITY 0.95f
#define THERMAL_RES_18BIT  2
#define THERMAL_REFRESH_4HZ 3
#define THERMAL_DEBUG_LOG  0

#if THERMAL_DEBUG_LOG
#define THERMAL_LOG(fmt, ...) LILYGO_LOG_PRINTF("THERMAL " fmt "\n", ##__VA_ARGS__)
#else
#define THERMAL_LOG(fmt, ...)
#endif

typedef struct {
    float min_temp;
    float max_temp;
    float avg_temp;
    float center_temp;
    uint16_t min_idx;
    uint16_t max_idx;
    uint16_t valid_count;
} thermal_stats_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *thermal_view = NULL;
static lv_obj_t *thermal_canvas = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *range_label = NULL;
static lv_obj_t *center_label = NULL;
static lv_obj_t *max_label = NULL;
static lv_obj_t *min_label = NULL;
static lv_obj_t *avg_label = NULL;
static lv_obj_t *hot_marker = NULL;
static lv_obj_t *cold_marker = NULL;
static lv_obj_t *hot_tag = NULL;
static lv_obj_t *cold_tag = NULL;
static lv_obj_t *retry_btn = NULL;
static lv_obj_t *sensor_msgbox = NULL;
static lv_timer_t *thermal_timer = NULL;

static uint16_t *thermal_pixels = NULL;
static float *thermal_frame = NULL;

#if THERMAL_HAS_MLX90640
static paramsMLX90640 *mlx_params = NULL;
static uint16_t *mlx_ee = NULL;
static uint16_t *mlx_frame_data = NULL;
static bool mlx_ready = false;
static uint8_t mlx_subpage_mask = 0;
static int mlx_param_warning = 0;
#endif

static void *thermal_alloc(size_t size)
{
#ifdef ARDUINO
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return ptr;
#else
    return malloc(size);
#endif
}

static void thermal_free(void *ptr)
{
    if (!ptr) return;
#ifdef ARDUINO
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

static bool thermal_alloc_buffers(void)
{
    if (thermal_pixels && thermal_frame
#if THERMAL_HAS_MLX90640
        && mlx_params && mlx_ee && mlx_frame_data
#endif
       ) {
        return true;
    }

    thermal_pixels = (uint16_t *)thermal_alloc(THERMAL_CANVAS_W * THERMAL_CANVAS_H * sizeof(uint16_t));
    thermal_frame = (float *)thermal_alloc(THERMAL_FRAME_LEN * sizeof(float));
#if THERMAL_HAS_MLX90640
    mlx_params = (paramsMLX90640 *)thermal_alloc(sizeof(paramsMLX90640));
    mlx_ee = (uint16_t *)thermal_alloc(832 * sizeof(uint16_t));
    mlx_frame_data = (uint16_t *)thermal_alloc(834 * sizeof(uint16_t));
#endif

    if (!thermal_pixels || !thermal_frame
#if THERMAL_HAS_MLX90640
        || !mlx_params || !mlx_ee || !mlx_frame_data
#endif
       ) {
        return false;
    }

    memset(thermal_pixels, 0, THERMAL_CANVAS_W * THERMAL_CANVAS_H * sizeof(uint16_t));
    memset(thermal_frame, 0, THERMAL_FRAME_LEN * sizeof(float));
    return true;
}

static void thermal_free_buffers(void)
{
    thermal_free(thermal_pixels);
    thermal_pixels = NULL;
    thermal_free(thermal_frame);
    thermal_frame = NULL;
#if THERMAL_HAS_MLX90640
    thermal_free(mlx_params);
    mlx_params = NULL;
    thermal_free(mlx_ee);
    mlx_ee = NULL;
    thermal_free(mlx_frame_data);
    mlx_frame_data = NULL;
#endif
}

static uint16_t thermal_color(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    const uint8_t stops[][3] = {
        {  0,   0,  16},
        { 42,   0, 112},
        {  0,  80, 190},
        {  0, 205, 215},
        { 76, 215,  70},
        {255, 224,  48},
        {242,  70,  34},
        {255, 255, 245},
    };
    const int stop_count = sizeof(stops) / sizeof(stops[0]);
    float scaled = v * (stop_count - 1);
    int idx = (int)scaled;
    if (idx >= stop_count - 1) idx = stop_count - 2;
    float frac = scaled - idx;

    uint8_t r = (uint8_t)(stops[idx][0] + (stops[idx + 1][0] - stops[idx][0]) * frac);
    uint8_t g = (uint8_t)(stops[idx][1] + (stops[idx + 1][1] - stops[idx][1]) * frac);
    uint8_t b = (uint8_t)(stops[idx][2] + (stops[idx + 1][2] - stops[idx][2]) * frac);
    return lv_color_to_u16(lv_color_make(r, g, b));
}

static void clear_canvas(uint16_t color)
{
    if (!thermal_pixels) return;
    for (int y = 0; y < THERMAL_CANVAS_H; ++y) {
        for (int x = 0; x < THERMAL_CANVAS_W; ++x) {
            thermal_pixels[y * THERMAL_CANVAS_W + x] = color;
        }
    }
    if (thermal_canvas) {
        lv_obj_invalidate(thermal_canvas);
    }
}

static void set_marker_style(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_size(obj, 8, 8);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *create_metric(lv_obj_t *parent, const char *name)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_width(box, 70);
    lv_obj_set_height(box, 31);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_70, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 3, 0);
    lv_obj_set_style_pad_row(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);

    lv_obj_t *value = lv_label_create(box);
    lv_label_set_text(value, "--.- C");
    lv_obj_set_style_text_color(value, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    return value;
}

static void update_metric(lv_obj_t *label, float value)
{
    if (!label) return;
    if (!isfinite(value)) {
        lv_label_set_text(label, "--.- C");
    } else {
        lv_label_set_text_fmt(label, "%.1f C", value);
    }
}

static void set_status(const char *status, lv_color_t color)
{
    if (!status_label) return;
    lv_label_set_text(status_label, status);
    lv_obj_set_style_text_color(status_label, color, 0);
}

static void set_status_error(const char *phase, int status)
{
    if (!status_label) return;
    lv_label_set_text_fmt(status_label, "%s %d @%dk", phase, status, THERMAL_I2C_KHZ);
    lv_obj_set_style_text_color(status_label, UI_COLOR_WARNING, 0);
}

static bool probe_sensor(void)
{
#if THERMAL_HAS_MLX90640
    Wire.setTimeOut(200);
    MLX90640_I2CFreqSet(THERMAL_I2C_KHZ);
    Wire.beginTransmission((uint8_t)THERMAL_I2C_ADDR);
    return Wire.endTransmission() == 0;
#else
    return false;
#endif
}

#if THERMAL_HAS_MLX90640
static void log_eeprom_summary(const uint16_t *ee)
{
    if (!LILYGO_DEBUG_ENABLED) return;
    uint16_t zero_count = 0;
    uint16_t ffff_count = 0;
    uint32_t checksum = 0;

    for (uint16_t i = 0; i < 832; ++i) {
        if (ee[i] == 0x0000) zero_count++;
        if (ee[i] == 0xFFFF) ffff_count++;
        checksum += ee[i];
    }

    THERMAL_LOG("EEPROM summary ee10=0x%04X deviceSelect=0x%04X zero=%u ffff=%u sum=0x%08lX",
                ee[10], ee[10] & 0x0040, zero_count, ffff_count, (unsigned long)checksum);
    THERMAL_LOG("EEPROM first16 %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
                ee[0], ee[1], ee[2], ee[3], ee[4], ee[5], ee[6], ee[7],
                ee[8], ee[9], ee[10], ee[11], ee[12], ee[13], ee[14], ee[15]);
}
#endif

static void place_marker(lv_obj_t *marker, lv_obj_t *tag, uint16_t idx, float temp, int y_offset)
{
    if (!thermal_view || !marker || !tag) return;

    int cell_x = (idx % THERMAL_COLS) * THERMAL_CELL;
    int cell_y = (idx / THERMAL_COLS) * THERMAL_CELL;
    int marker_x = cell_x + THERMAL_CELL / 2 - 4;
    int marker_y = cell_y + THERMAL_CELL / 2 - 4;
    if (marker_x < 0) marker_x = 0;
    if (marker_y < 0) marker_y = 0;
    if (marker_x > THERMAL_CANVAS_W - 8) marker_x = THERMAL_CANVAS_W - 8;
    if (marker_y > THERMAL_CANVAS_H - 8) marker_y = THERMAL_CANVAS_H - 8;

    lv_obj_align(marker, LV_ALIGN_TOP_LEFT, marker_x, marker_y);
    lv_label_set_text_fmt(tag, "%.1f", temp);
    lv_obj_update_layout(tag);

    int tag_w = lv_obj_get_width(tag);
    int tag_h = lv_obj_get_height(tag);
    int tag_x = marker_x + 10;
    int tag_y = marker_y + y_offset;
    if (tag_x > THERMAL_CANVAS_W - tag_w) tag_x = marker_x - tag_w - 2;
    if (tag_x < 0) tag_x = 0;
    if (tag_y < 0) tag_y = 0;
    if (tag_y > THERMAL_CANVAS_H - tag_h) tag_y = THERMAL_CANVAS_H - tag_h;
    lv_obj_align(tag, LV_ALIGN_TOP_LEFT, tag_x, tag_y);
}

static bool analyse_frame(thermal_stats_t *stats)
{
    if (!stats || !thermal_frame) return false;

    stats->min_temp = 9999.0f;
    stats->max_temp = -9999.0f;
    stats->avg_temp = 0.0f;
    stats->center_temp = thermal_frame[(THERMAL_ROWS / 2) * THERMAL_COLS + (THERMAL_COLS / 2)];
    stats->min_idx = 0;
    stats->max_idx = 0;
    stats->valid_count = 0;

    float sum = 0.0f;
    for (uint16_t i = 0; i < THERMAL_FRAME_LEN; ++i) {
        float value = thermal_frame[i];
        if (!isfinite(value) || value < -70.0f || value > 380.0f) {
            continue;
        }
        if (value < stats->min_temp) {
            stats->min_temp = value;
            stats->min_idx = i;
        }
        if (value > stats->max_temp) {
            stats->max_temp = value;
            stats->max_idx = i;
        }
        sum += value;
        stats->valid_count++;
    }

    if (stats->valid_count == 0) {
        return false;
    }

    stats->avg_temp = sum / stats->valid_count;
    return true;
}

static void draw_frame(const thermal_stats_t *stats)
{
    if (!stats || !thermal_canvas || !thermal_pixels || !thermal_frame) return;

    float range_min = stats->min_temp;
    float range_max = stats->max_temp;
    float range = range_max - range_min;
    if (range < 5.0f) {
        float mid = (range_max + range_min) * 0.5f;
        range_min = mid - 2.5f;
        range_max = mid + 2.5f;
        range = 5.0f;
    }

    for (int src_y = 0; src_y < THERMAL_ROWS; ++src_y) {
        for (int src_x = 0; src_x < THERMAL_COLS; ++src_x) {
            float temp = thermal_frame[src_y * THERMAL_COLS + src_x];
            float n = (temp - range_min) / range;
            uint16_t color = thermal_color(n);
            int dst_x = src_x * THERMAL_CELL;
            int dst_y = src_y * THERMAL_CELL;
            for (int py = 0; py < THERMAL_CELL; ++py) {
                for (int px = 0; px < THERMAL_CELL; ++px) {
                    thermal_pixels[(dst_y + py) * THERMAL_CANVAS_W + dst_x + px] = color;
                }
            }
        }
    }

    lv_obj_invalidate(thermal_canvas);
    lv_label_set_text_fmt(range_label, "%.1f-%.1f C", range_min, range_max);
    update_metric(center_label, stats->center_temp);
    update_metric(max_label, stats->max_temp);
    update_metric(min_label, stats->min_temp);
    update_metric(avg_label, stats->avg_temp);
    place_marker(hot_marker, hot_tag, stats->max_idx, stats->max_temp, -12);
    place_marker(cold_marker, cold_tag, stats->min_idx, stats->min_temp, 8);
}

static bool thermal_begin()
{
#if THERMAL_HAS_MLX90640
    if (!thermal_alloc_buffers()) {
        set_status("No RAM for thermal", lv_color_hex(0xFF4444));
        return false;
    }

    THERMAL_LOG("begin addr=0x%02X i2c=%dk", THERMAL_I2C_ADDR, THERMAL_I2C_KHZ);
    Wire.setTimeOut(200);
    MLX90640_I2CFreqSet(THERMAL_I2C_KHZ);

    Wire.beginTransmission((uint8_t)THERMAL_I2C_ADDR);
    uint8_t ack = Wire.endTransmission();
    THERMAL_LOG("probe ack=%u", ack);
    if (ack != 0) {
        mlx_ready = false;
        set_status("MLX90640 not found", UI_COLOR_WARNING);
        return false;
    }

    int status = MLX90640_DumpEE(THERMAL_I2C_ADDR, mlx_ee);
    THERMAL_LOG("DumpEE status=%d", status);
    log_eeprom_summary(mlx_ee);
    if (status != 0) {
        mlx_ready = false;
        set_status_error("EEPROM", status);
        return false;
    }

    status = MLX90640_ExtractParameters(mlx_ee, mlx_params);
    THERMAL_LOG("ExtractParameters status=%d", status);
    if (status != 0) {
        mlx_param_warning = status;
        THERMAL_LOG("ExtractParameters warning=%d, continuing like Adafruit", status);
    } else {
        mlx_param_warning = 0;
    }

    status = MLX90640_SetChessMode(THERMAL_I2C_ADDR);
    THERMAL_LOG("SetChessMode status=%d", status);
    if (status != 0) {
        mlx_ready = false;
        set_status_error("Mode", status);
        return false;
    }

    status = MLX90640_SetResolution(THERMAL_I2C_ADDR, THERMAL_RES_18BIT);
    THERMAL_LOG("SetResolution(%u) status=%d", THERMAL_RES_18BIT, status);
    if (status != 0) {
        mlx_ready = false;
        set_status_error("Res", status);
        return false;
    }

    status = MLX90640_SetRefreshRate(THERMAL_I2C_ADDR, THERMAL_REFRESH_4HZ);
    THERMAL_LOG("SetRefreshRate(%u) status=%d", THERMAL_REFRESH_4HZ, status);
    if (status != 0) {
        mlx_ready = false;
        set_status_error("Rate", status);
        return false;
    }

    memset(thermal_frame, 0, THERMAL_FRAME_LEN * sizeof(float));
    mlx_subpage_mask = 0;
    mlx_ready = true;
    if (status_label) {
        if (mlx_param_warning != 0) {
            lv_label_set_text_fmt(status_label, "Ready warn %d", mlx_param_warning);
        } else {
            lv_label_set_text_fmt(status_label, "Ready @%dk", THERMAL_I2C_KHZ);
        }
        lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    }
    THERMAL_LOG("ready");
    return true;
#else
    set_status("MLX90640 library missing", UI_COLOR_WARNING);
    return false;
#endif
}

static bool thermal_read_frame()
{
#if THERMAL_HAS_MLX90640
    if (!thermal_alloc_buffers()) {
        set_status("No RAM for thermal", lv_color_hex(0xFF4444));
        return false;
    }

    if (!mlx_ready && !thermal_begin()) {
        return false;
    }

    int status = MLX90640_GetFrameData(THERMAL_I2C_ADDR, mlx_frame_data);
    if (status < 0) {
        THERMAL_LOG("GetFrameData status=%d", status);
        set_status_error("Frame", status);
        return false;
    }

    float ta = MLX90640_GetTa(mlx_frame_data, mlx_params);
    float tr = ta - THERMAL_TA_SHIFT;
    MLX90640_CalculateTo(mlx_frame_data, mlx_params, THERMAL_EMISSIVITY, tr, thermal_frame);

    int subpage = MLX90640_GetSubPageNumber(mlx_frame_data);
    if (subpage >= 0 && subpage < 2) {
        mlx_subpage_mask |= (1U << subpage);
    }
    THERMAL_LOG("frame status=%d subpage=%d ta=%.2f mask=0x%02X",
                status, subpage, ta, mlx_subpage_mask);

    lv_label_set_text_fmt(status_label, "Live @%dk", THERMAL_I2C_KHZ);
    lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    if ((mlx_subpage_mask & 0x03) != 0x03) {
        return false;
    }

    mlx_subpage_mask = 0;
    return true;
#else
    return false;
#endif
}

static void thermal_update_cb(lv_timer_t *t)
{
    (void)t;

    if (!thermal_read_frame()) {
        return;
    }

    thermal_stats_t stats;
    if (!analyse_frame(&stats)) {
        set_status("No valid thermal data", UI_COLOR_WARNING);
        return;
    }
    draw_frame(&stats);
}

static void retry_event_handler(lv_event_t *e)
{
    (void)e;
#if THERMAL_HAS_MLX90640
    mlx_ready = false;
    mlx_subpage_mask = 0;
    mlx_param_warning = 0;
#endif
    if (thermal_begin()) {
        thermal_update_cb(NULL);
    }
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    if (sensor_msgbox) {
        destroy_msgbox(sensor_msgbox);
        sensor_msgbox = NULL;
    }
    if (thermal_timer) {
        lv_timer_del(thermal_timer);
        thermal_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    thermal_view = NULL;
    thermal_canvas = NULL;
    status_label = NULL;
    range_label = NULL;
    center_label = NULL;
    max_label = NULL;
    min_label = NULL;
    avg_label = NULL;
    hot_marker = NULL;
    cold_marker = NULL;
    hot_tag = NULL;
    cold_tag = NULL;
    retry_btn = NULL;
#if THERMAL_HAS_MLX90640
    mlx_ready = false;
    mlx_subpage_mask = 0;
    mlx_param_warning = 0;
#endif
    thermal_free_buffers();
    menu_show();
}

static void sensor_msgbox_cb(lv_event_t *e)
{
    (void)e;
    if (sensor_msgbox) {
        destroy_msgbox(sensor_msgbox);
        sensor_msgbox = NULL;
    }
    thermal_free_buffers();
    menu_show();
}

static void show_sensor_unavailable_msgbox(void)
{
    if (sensor_msgbox) return;

    static const char *btns[] = {"OK", ""};
    sensor_msgbox = create_msgbox(
                        lv_scr_act(),
                        "Thermal",
#if THERMAL_HAS_MLX90640
                        "MLX90640 is not built into this board.\n"
                        "Connect and power an external MLX90640 thermal camera on the I2C pins to use this test.\n"
                        "No camera was found at address 0x33. Check SDA/SCL, VCC/GND, and wiring.",
#else
                        "MLX90640 is not built into this board; an external thermal camera is required.\n"
                        "MLX90640 support is not enabled in this firmware.",
#endif
                        btns,
                        sensor_msgbox_cb,
                        NULL);
}

static void create_overlay(lv_obj_t *parent)
{
    lv_obj_t *h_line = lv_obj_create(parent);
    lv_obj_set_size(h_line, 24, 1);
    lv_obj_set_style_bg_color(h_line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(h_line, LV_OPA_80, 0);
    lv_obj_set_style_border_width(h_line, 0, 0);
    lv_obj_set_style_radius(h_line, 0, 0);
    lv_obj_remove_flag(h_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(h_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(h_line, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *v_line = lv_obj_create(parent);
    lv_obj_set_size(v_line, 1, 24);
    lv_obj_set_style_bg_color(v_line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(v_line, LV_OPA_80, 0);
    lv_obj_set_style_border_width(v_line, 0, 0);
    lv_obj_set_style_radius(v_line, 0, 0);
    lv_obj_remove_flag(v_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(v_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(v_line, LV_ALIGN_CENTER, 0, 0);

    hot_marker = lv_obj_create(parent);
    set_marker_style(hot_marker, lv_color_hex(0xFFD23F));
    hot_tag = lv_label_create(parent);
    lv_label_set_text(hot_tag, "--.-");
    lv_obj_set_style_text_color(hot_tag, lv_color_hex(0xFFD23F), 0);
    lv_obj_set_style_text_font(hot_tag, &lv_font_montserrat_10, 0);

    cold_marker = lv_obj_create(parent);
    set_marker_style(cold_marker, lv_color_hex(0x57C7FF));
    cold_tag = lv_label_create(parent);
    lv_label_set_text(cold_tag, "--.-");
    lv_obj_set_style_text_color(cold_tag, lv_color_hex(0x57C7FF), 0);
    lv_obj_set_style_text_font(cold_tag, &lv_font_montserrat_10, 0);
}

void ui_thermal_enter(lv_obj_t *parent)
{
    if (!probe_sensor()) {
        show_sensor_unavailable_msgbox();
        return;
    }

    if (!thermal_alloc_buffers()) {
        page_container = ui_create_app_page(parent, "Thermal", back_event_handler);
        status_label = lv_label_create(page_container);
        lv_label_set_text(status_label, "No RAM for MLX90640 buffers");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF4444), 0);
        return;
    }

    page_container = ui_create_app_page(parent, "Thermal", back_event_handler);
    lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(page_container, 6, 0);
    lv_obj_set_flex_align(page_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *top_row = lv_obj_create(page_container);
    lv_obj_set_size(top_row, LV_PCT(100), 20);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_radius(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(top_row);
    lv_label_set_text(status_label, "Opening MLX90640");
    lv_obj_set_width(status_label, 165);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    retry_btn = lv_btn_create(top_row);
    lv_obj_set_size(retry_btn, 52, 20);
    lv_obj_set_style_bg_color(retry_btn, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(retry_btn, 1, 0);
    lv_obj_set_style_border_color(retry_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(retry_btn, 4, 0);
    lv_obj_set_style_pad_all(retry_btn, 0, 0);
    lv_obj_add_event_cb(retry_btn, retry_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t *retry_label = lv_label_create(retry_btn);
    lv_label_set_text(retry_label, "Retry");
    lv_obj_set_style_text_font(retry_label, &lv_font_montserrat_10, 0);
    lv_obj_center(retry_label);

    range_label = lv_label_create(top_row);
    lv_label_set_text(range_label, "--.- --.- C");
    lv_obj_set_width(range_label, 78);
    lv_obj_set_style_text_align(range_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(range_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(range_label, &lv_font_montserrat_10, 0);

    thermal_view = lv_obj_create(page_container);
    lv_obj_set_size(thermal_view, THERMAL_CANVAS_W, THERMAL_CANVAS_H);
    lv_obj_set_style_bg_color(thermal_view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(thermal_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(thermal_view, 1, 0);
    lv_obj_set_style_border_color(thermal_view, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_radius(thermal_view, 0, 0);
    lv_obj_set_style_pad_all(thermal_view, 0, 0);
    lv_obj_remove_flag(thermal_view, LV_OBJ_FLAG_SCROLLABLE);

    memset(thermal_pixels, 0, THERMAL_CANVAS_W * THERMAL_CANVAS_H * sizeof(uint16_t));
    thermal_canvas = lv_canvas_create(thermal_view);
    lv_canvas_set_buffer(thermal_canvas, thermal_pixels, THERMAL_CANVAS_W, THERMAL_CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(thermal_canvas, THERMAL_CANVAS_W, THERMAL_CANVAS_H);
    lv_obj_align(thermal_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(thermal_canvas, 0, 0);
    lv_obj_remove_flag(thermal_canvas, LV_OBJ_FLAG_SCROLLABLE);
    clear_canvas(lv_color_to_u16(lv_color_make(0, 0, 16)));
    create_overlay(thermal_view);

    lv_obj_t *metric_row = lv_obj_create(page_container);
    lv_obj_set_size(metric_row, LV_PCT(100), 34);
    lv_obj_set_style_bg_opa(metric_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metric_row, 0, 0);
    lv_obj_set_style_radius(metric_row, 0, 0);
    lv_obj_set_style_pad_all(metric_row, 0, 0);
    lv_obj_set_style_pad_column(metric_row, 4, 0);
    lv_obj_set_flex_flow(metric_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(metric_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(metric_row, LV_OBJ_FLAG_SCROLLABLE);

    center_label = create_metric(metric_row, "CENTER");
    max_label = create_metric(metric_row, "MAX");
    min_label = create_metric(metric_row, "MIN");
    avg_label = create_metric(metric_row, "AVG");

    thermal_begin();
    thermal_update_cb(NULL);
    thermal_timer = lv_timer_create(thermal_update_cb, 250, NULL);
}

void ui_thermal_exit(lv_obj_t *parent)
{
    (void)parent;
    if (sensor_msgbox) {
        destroy_msgbox(sensor_msgbox);
        sensor_msgbox = NULL;
    }
    if (thermal_timer) {
        lv_timer_del(thermal_timer);
        thermal_timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
#if THERMAL_HAS_MLX90640
    mlx_ready = false;
    mlx_subpage_mask = 0;
    mlx_param_warning = 0;
#endif
    thermal_free_buffers();
}

app_t ui_thermal_main = {
    .setup_func_cb = ui_thermal_enter,
    .exit_func_cb = ui_thermal_exit,
    .user_data = nullptr,
};

#endif
