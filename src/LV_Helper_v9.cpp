/**
 * @file      LV_Helper_v9.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-02-27
 * @note      Adapt to lvgl 9 version
 */
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

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
static lv_color16_t *buf_sw  = NULL;
#endif

#if ((ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)) && defined(ARDUINO_T_WATCH_S3))  ||   \
    (defined(ARDUINO_T_WATCH_S3_ULTRA))
#define _SWAP_COLORS
#endif

#if defined(ARDUINO_T_LORA_PAGER)
#define _SWAP_COLORS
#endif

static void disp_flush( lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p)
{
    size_t len = lv_area_get_size(area);
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    auto *plane = (LilyGo_Display *)lv_display_get_user_data(disp_drv);

#ifdef _SWAP_COLORS
    lv_draw_sw_rgb565_swap(color_p, len);
#endif

#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA)
    // log_d("x1:%d y1:%d w:%d h:%d", area->x1, area->y1, w, h);
    plane->pushColors(area->x1, area->y1, w, h, (uint16_t *)color_p);
#else
    // log_d("x1:%d y1:%d x2:%d y2:%d", area->x1, area->y1, area->x2 + 1, area->y2 + 1);
    plane->pushColors(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)color_p);
#endif

    lv_display_flush_ready( disp_drv );
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
    static uint8_t last_dir = 0;
    auto *plane = (LilyGo_Display *)lv_indev_get_user_data(drv);
    RotaryMsg_t msg =  plane->getRotary();
    switch (msg.dir) {
    case ROTARY_DIR_UP:
        data->enc_diff = 1;
        break;
    case ROTARY_DIR_DOWN:
        data->enc_diff = -1;
        break;
    default:
        data->state = LV_INDEV_STATE_RELEASED;
        break;
    }
    if (msg.centerBtnPressed) {
        data->state = LV_INDEV_STATE_PRESSED;
    }

    if (last_dir != msg.dir || msg.centerBtnPressed) {
        plane->feedback((void *)drv);
    }
}
#endif //USING_INPUT_DEV_ROTARY

#ifdef USING_INPUT_DEV_KEYBOARD
static void keypad_read(lv_indev_t *drv, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t act_key ;
    char c;
    auto *plane = (LilyGo_Display *)lv_indev_get_user_data(drv);
    int state = plane->getKeyChar(&c);
    if (state == KEYBOARD_PRESSED) {
        act_key = c;
        data->key = act_key;
        data->state = LV_INDEV_STATE_PR;
        plane->feedback((void *)drv);
        return;
    }
    data->state = LV_INDEV_STATE_REL;
    data->key = last_key;
}
#endif //USING_INPUT_DEV_KEYBOARD

static uint32_t lv_tick_get_cb(void)
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

static void lv_display_flush_wait_callback(lv_display_t *disp)
{
    log_d("flush disp finish");
}


void beginLvglHelper(LilyGo_Display &board, bool debug)
{

#ifdef _SWAP_COLORS
    log_d("Using color swap function");
#endif

    lv_init();

#if LV_USE_LOG
    if (debug) {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif

    log_d("lv set display size : Width %u Height:%u", board.width(), board.height());

    bool useDMA = board.useDMA();

    size_t lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);

    if (useDMA) {

        log_d("Using DMA pushColors..");

        lv_buffer_size = (board.width() * board.height() / 6) * sizeof(lv_color16_t);

        // lv_buffer_size = (board.width() * board.height() / 10) * sizeof(lv_color16_t);

        buf = (lv_color16_t *)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        assert(buf);
        buf1 = (lv_color16_t *)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        assert(buf1);

        if (!esp_ptr_dma_capable(buf) || !esp_ptr_dma_capable(buf1)) {
            log_e("Error: Buffers are not DMA-capable!");
        }

    } else {
        log_d("Using Not DMA pushColors..");
        buf = (lv_color16_t *)ps_malloc(lv_buffer_size);
        assert(buf);
        buf1 = (lv_color16_t *)ps_malloc(lv_buffer_size);
        assert(buf1);
    }

    disp_drv = lv_display_create(board.width(), board.height());

    if (board.needFullRefresh()) {
        log_d("lv set double buffer and full refresh");
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    } else {
        log_d("lv set double buffer and partial refresh");
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp_drv, lv_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    }
    lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp_drv, disp_flush);
    lv_display_set_user_data(disp_drv, &board);

    lv_display_set_resolution(disp_drv, board.width(), board.height());

    lv_display_set_rotation(disp_drv, (lv_display_rotation_t)board.getRotation());
    lv_display_add_event_cb(disp_drv, lv_res_changed_cb, LV_EVENT_RESOLUTION_CHANGED, &board);

    /**
    * Set a callback to be used while LVGL is waiting flushing to be finished.
    * It can do any complex logic to wait, including semaphores, mutexes, polling flags, etc.
    * If not set the `disp->flushing` flag is used which can be cleared with `lv_display_flush_ready()`
    * @param disp      pointer to a display
    * @param wait_cb   a callback to call while LVGL is waiting for flush ready.
    *                  If NULL `lv_display_flush_ready()` can be used to signal that flushing is ready.
    */
    lv_display_set_flush_wait_cb(disp_drv, lv_display_flush_wait_callback);


#ifdef USING_INPUT_DEV_TOUCHPAD
    if (board.hasTouch()) {
        log_d("lv register touchpad");
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
        log_d("lv register encoder");
        indev_encoder = lv_indev_create();
        lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev_encoder, lv_encoder_read);
        lv_indev_set_user_data(indev_encoder, &board);
        lv_indev_enable(indev_encoder, true);
        lv_indev_set_display(indev_encoder, disp_drv);
        lv_indev_set_group(indev_encoder, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_ROTARY

#ifdef USING_INPUT_DEV_KEYBOARD
    if (board.hasKeyboard()) {
        log_d("lv register keyboard");
        indev_keyboard = lv_indev_create();
        lv_indev_set_type(indev_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev_keyboard, keypad_read);
        lv_indev_set_user_data(indev_keyboard, &board);
        lv_indev_enable(indev_keyboard, true);
        lv_indev_set_display(indev_keyboard, disp_drv);
        lv_indev_set_group(indev_keyboard, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_KEYBOARD

    lv_tick_set_cb(lv_tick_get_cb);
    lv_group_set_default(lv_group_create());
    log_d("lv init successfully!");
}


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


#endif
