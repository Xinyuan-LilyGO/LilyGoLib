/**
 * @file      LV_Helper_v9.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-02-27
 * @note      Adapt to lvgl 9 version
 */
#include "LilyGoLog.h"
#include <Arduino.h>
#include "LV_Helper.h"

#if LVGL_VERSION_MAJOR == 9

static lv_display_t *disp_drv;
static lv_draw_buf_t draw_buf;
static lv_indev_t   *indev_touch;
static lv_indev_t   *indev_encoder;
static lv_indev_t   *indev_keyboard;

static lv_color16_t *buf  = NULL;
static lv_color16_t *buf1  = NULL;
#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
#define SCREEN_CAPTURE_RENDER_ROWS 20U
static uint16_t *screen_capture_buf = NULL;
static size_t screen_capture_capacity = 0;
static uint16_t screen_capture_width = 0;
static uint16_t screen_capture_height = 0;
static bool screen_capture_rgb565_swapped = false;

static void update_screen_capture(lv_display_t *display, const lv_area_t *area, const uint8_t *color_p)
{
    if (!screen_capture_buf || !display || !area || !color_p) return;

    int32_t display_width = lv_display_get_horizontal_resolution(display);
    int32_t display_height = lv_display_get_vertical_resolution(display);
    if (display_width <= 0 || display_height <= 0 ||
            (size_t)display_width * display_height > screen_capture_capacity) {
        return;
    }

    if (screen_capture_width != display_width || screen_capture_height != display_height) {
        screen_capture_width = (uint16_t)display_width;
        screen_capture_height = (uint16_t)display_height;
        memset(screen_capture_buf, 0, screen_capture_capacity * sizeof(uint16_t));
    }

    int32_t x1 = LV_MAX(area->x1, 0);
    int32_t y1 = LV_MAX(area->y1, 0);
    int32_t x2 = LV_MIN(area->x2, display_width - 1);
    int32_t y2 = LV_MIN(area->y2, display_height - 1);
    if (x1 > x2 || y1 > y2) return;

    const uint16_t *source = (const uint16_t *)color_p;
    int32_t source_stride = lv_area_get_width(area);
    int32_t source_x = x1 - area->x1;
    int32_t source_y = y1 - area->y1;
    size_t copy_bytes = (size_t)(x2 - x1 + 1) * sizeof(uint16_t);

    for (int32_t y = y1; y <= y2; ++y) {
        const uint16_t *source_row = source + (source_y + y - y1) * source_stride + source_x;
        uint16_t *target_row = screen_capture_buf + (size_t)y * display_width + x1;
        memcpy(target_row, source_row, copy_bytes);
    }
}
#endif

static void disp_flush( lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    auto *plane = (LilyGo_Display *)lv_display_get_user_data(disp_drv);

#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
    update_screen_capture(disp_drv, area, color_p);
#endif
    plane->pushColors(area->x1, area->y1, w, h, (uint16_t *)color_p);
    lv_display_flush_ready(disp_drv);
}

#ifdef USING_INPUT_DEV_TOUCHPAD
static void touchpad_read( lv_indev_t *drv, lv_indev_data_t *data )
{
    static int16_t x, y;
    auto *plane = (LilyGo_Display *)lv_indev_get_user_data(drv);
    uint8_t touched = plane->getPoint(&x, &y, 1);
    if ( touched ) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif //USING_INPUT_DEV_TOUCHPAD

#ifdef USING_INPUT_DEV_ROTARY
static void lv_encoder_read(lv_indev_t *drv, lv_indev_data_t *data)
{
    auto *plane = (LilyGo_Display *)lv_indev_get_user_data(drv);
    RotaryMsg_t msg =  plane->getRotary();
    data->enc_diff = msg.enc_diff;
    data->state = msg.centerBtnPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (msg.enc_diff != 0 || msg.centerBtnClicked || msg.centerBtnReleased) {
        plane->feedback((void *)drv);
    }
}
#endif //USING_INPUT_DEV_ROTARY

#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD)
static void keypad_read(lv_indev_t *drv, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t act_key ;
    char c = '\0';
    auto *plane = (LilyGo_Display *)lv_indev_get_user_data(drv);
    int state = plane->getKeyChar(&c);
    if (state == KEYBOARD_PRESSED) {
        act_key = c;
        last_key = act_key;
        data->key = act_key;
        data->state = LV_INDEV_STATE_PR;
        plane->feedback((void *)drv);
        return;
    }
    data->state = LV_INDEV_STATE_REL;
    data->key = last_key;
}
#endif // USING_INPUT_DEV_KEYBOARD || USING_TDECK_KEYBOARD

static uint32_t lv_tick_get_callback(void)
{
    return millis();
}

static void lv_rounder_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    // Limit the starting coordinate of T-Watch-S3-Ultra to 0, otherwise the display will be abnormal
    if (area->x1 > 1)
        area->x1 = 0;
#else
    if (!(area->x2 & 1))
        area->x2++;
    if (area->y1 & 1)
        area->y1--;
    if (!(area->y2 & 1))
        area->y2++;
#endif
    if (!(area->x2 & 1))
        area->x2++;
    if (area->y1 & 1)
        area->y1--;
    if (!(area->y2 & 1))
        area->y2++;
}

static void lv_res_changed_cb(lv_event_t *e)
{
    auto *plane = (LilyGo_Display *)lv_event_get_user_data(e);
    plane->setRotation(lv_display_get_rotation(NULL));
}

#if LV_USE_LOG
static bool lv_debug_enabled;
static void lv_log_print_g_cb(lv_log_level_t level, const char *buf)
{
    (void)level;
    (void)buf;
    if (lv_debug_enabled) {
        LILYGO_LOG_PRINTF("%s", buf);
    }
}
#endif

void beginLvglHelper(LilyGo_Display &board, bool debug)
{


    lv_init();

#if LV_USE_LOG
    lv_debug_enabled = LILYGO_DEBUG_ENABLED && debug;
    lv_log_register_print_cb(lv_log_print_g_cb);
#endif

    LILYGO_LOG_D("lv set display size : Width %u Height:%u", board.width(), board.height());

    bool useDMA = board.useDMA();

#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
    screen_capture_capacity = (size_t)board.width() * board.height();
    screen_capture_buf = (uint16_t *)heap_caps_malloc(screen_capture_capacity * sizeof(uint16_t),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (screen_capture_buf) {
        memset(screen_capture_buf, 0, screen_capture_capacity * sizeof(uint16_t));
        screen_capture_width = board.width();
        screen_capture_height = board.height();
        screen_capture_rgb565_swapped = board.needSwapColors();
    } else {
        screen_capture_capacity = 0;
        screen_capture_width = 0;
        screen_capture_height = 0;
        LILYGO_LOG_E("Unable to allocate screen capture buffer");
    }
#endif

    size_t lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);

#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
    if (!board.needFullRefresh()) {
        size_t render_rows = LV_MIN((size_t)board.height(), (size_t)SCREEN_CAPTURE_RENDER_ROWS);
        lv_buffer_size = (size_t)board.width() * render_rows * sizeof(lv_color16_t);
        LILYGO_LOG_D("Screen capture render buffer: %u rows, %u bytes",
                     (unsigned)render_rows, (unsigned)lv_buffer_size);
    }
#endif

    if (useDMA) {

        LILYGO_LOG_D("Using DMA pushColors..");

        // For ESP32-S3, DMA can access PSRAM
        // Try to allocate full screen buffer in PSRAM for TE sync
        size_t full_screen_size = board.width() * board.height() * sizeof(lv_color16_t);
        LILYGO_LOG_D("Full screen size: %u bytes (%.1f KB)", full_screen_size, full_screen_size / 1024.0);

        // Try PSRAM first for full screen buffer (ESP32-S3 DMA can access PSRAM)
        lv_buffer_size = full_screen_size;
        buf = (lv_color16_t *)heap_caps_aligned_alloc(16, lv_buffer_size, MALLOC_CAP_SPIRAM);
        buf1 = (lv_color16_t *)heap_caps_aligned_alloc(16, lv_buffer_size, MALLOC_CAP_SPIRAM);

        if (buf && buf1) {
            LILYGO_LOG_D("Using full screen PSRAM buffers for TE sync");
        } else {
            // Fallback to internal DMA memory with smaller size
            LILYGO_LOG_D("PSRAM allocation failed, using internal DMA memory");
            if (buf) {
                heap_caps_free(buf);
                buf = NULL;
            }
            if (buf1) {
                heap_caps_free(buf1);
                buf1 = NULL;
            }

            lv_buffer_size = (board.width() * board.height() / 6) * sizeof(lv_color16_t);
            buf = (lv_color16_t *)heap_caps_aligned_alloc(16, lv_buffer_size, MALLOC_CAP_DMA);
            assert(buf);
            buf1 = (lv_color16_t *)heap_caps_aligned_alloc(16, lv_buffer_size, MALLOC_CAP_DMA);
            assert(buf1);
        }

        LILYGO_LOG_D("DMA buffers allocated: buf=%p buf1=%p size=%u", buf, buf1, lv_buffer_size);

    } else {
        LILYGO_LOG_D("Using Not DMA pushColors..");
        buf = (lv_color16_t *)ps_malloc(lv_buffer_size);
        assert(buf);
        buf1 = (lv_color16_t *)ps_malloc(lv_buffer_size);
        assert(buf1);
    }

    disp_drv = lv_display_create(board.width(), board.height());

    if (board.needFullRefresh()) {
        LILYGO_LOG_D("lv set double buffer and full refresh");
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    } else {
        LILYGO_LOG_D("lv set double buffer and partial refresh");
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp_drv, lv_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    }

    if (board.needSwapColors()) {
        lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565_SWAPPED);
    } else {
        lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
    }

    lv_display_set_flush_cb(disp_drv, disp_flush);
    lv_display_set_user_data(disp_drv, &board);

    lv_display_set_resolution(disp_drv, board.width(), board.height());

    lv_display_set_rotation(disp_drv, (lv_display_rotation_t)board.getRotation());
    lv_display_add_event_cb(disp_drv, lv_res_changed_cb, LV_EVENT_RESOLUTION_CHANGED, &board);

#ifdef USING_INPUT_DEV_TOUCHPAD
    if (board.hasTouch()) {
        LILYGO_LOG_D("lv register touchpad");
        indev_touch = lv_indev_create();
        lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_touch, touchpad_read);
        lv_indev_set_user_data(indev_touch, &board);
        lv_indev_enable(indev_touch, true);
        lv_indev_set_display(indev_touch, disp_drv);
        lv_indev_set_group(indev_touch, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_TOUCHPAD

#ifdef USING_INPUT_DEV_ROTARY
    if (board.hasEncoder()) {
        LILYGO_LOG_D("lv register encoder");
        indev_encoder = lv_indev_create();
        lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev_encoder, lv_encoder_read);
        lv_indev_set_user_data(indev_encoder, &board);
        lv_indev_enable(indev_encoder, true);
        lv_indev_set_display(indev_encoder, disp_drv);
        lv_indev_set_group(indev_encoder, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_ROTARY

#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD)
    if (board.hasKeyboard()) {
        LILYGO_LOG_D("lv register keyboard");
        indev_keyboard = lv_indev_create();
        lv_indev_set_type(indev_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev_keyboard, keypad_read);
        lv_indev_set_user_data(indev_keyboard, &board);
        lv_indev_enable(indev_keyboard, true);
        lv_indev_set_display(indev_keyboard, disp_drv);
        lv_indev_set_group(indev_keyboard, lv_group_get_default());
    }
#endif // USING_INPUT_DEV_KEYBOARD || USING_TDECK_KEYBOARD

    lv_tick_set_cb(lv_tick_get_callback);
    lv_group_set_default(lv_group_create());
    LILYGO_LOG_D("lv init successfully!");
}

#if defined(LILYGO_SCREEN_CAPTURE_ENABLED) && LILYGO_SCREEN_CAPTURE_ENABLED
bool lv_get_screen_capture(lv_screen_capture_t *capture)
{
    if (!capture || !screen_capture_buf || screen_capture_width == 0 || screen_capture_height == 0) {
        return false;
    }

    capture->pixels = screen_capture_buf;
    capture->width = screen_capture_width;
    capture->height = screen_capture_height;
    capture->rgb565_swapped = screen_capture_rgb565_swapped;
    return true;
}
#endif


extern "C" void lv_mem_init(void)
{
    return; /*Nothing to init*/
}

extern "C" void lv_mem_deinit(void)
{
    return; /*Nothing to deinit*/

}

extern "C" lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    /*Not supported*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported*/
    LV_UNUSED(pool);
    return;
}

extern "C" void *lv_malloc_core(size_t size)
{
    return ps_malloc(size);
}

extern "C" void *lv_realloc_core(void *p, size_t new_size)
{
    return ps_realloc(p, new_size);
}

extern "C" void lv_free_core(void *p)
{
    free(p);
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}

void lv_set_default_group(lv_group_t *group)
{
    lv_indev_t *cur_drv = NULL;
    for (;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv) {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv)  == LV_INDEV_TYPE_POINTER) {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

lv_indev_t *lv_get_touch_indev()
{
    return indev_touch;
}

lv_indev_t *lv_get_keyboard_indev()
{
    return indev_keyboard;
}

lv_indev_t *lv_get_encoder_indev()
{
    return indev_encoder;
}

#endif
