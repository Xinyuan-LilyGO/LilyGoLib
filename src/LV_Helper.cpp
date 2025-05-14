/**
 * @file      LV_Helper.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */
#include "LV_Helper.h"

#if LVGL_VERSION_MAJOR == 8


static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_indev_drv_t encoder_drv;
static lv_indev_drv_t keypad_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf  = NULL;
static lv_color_t *buf1  = NULL;
static lv_indev_t *kb_indev;
static lv_indev_t *encoder_indev;
static lv_indev_t *touchpad_indev;


void updateLvglHelper()
{
    lv_disp_drv_update(lv_disp_get_default(), &disp_drv);
}

/* Display flushing */
void disp_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );
    auto *plane = (LilyGo_Display *)disp->user_data;
#if defined(ARDUINO_T_LORA_PAGER)
    plane->pushColors(area->x1, area->y1, w, h, (uint16_t *)color_p);
#else
    plane->pushColors(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)color_p);
#endif
    lv_disp_flush_ready( disp );
}

#ifdef USING_INPUT_DEV_TOUCHPAD
void touchpad_read( lv_indev_drv_t *indev_driver, lv_indev_data_t *data )
{
    static int16_t x = 0, y = 0;
    uint8_t touched = static_cast<LilyGo_Display *>(indev_driver->user_data)->getPoint(&x, &y, 1);
    if ( touched > 0 ) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
#endif //USING_INPUT_DEV_TOUCHPAD

#ifdef USING_INPUT_DEV_ROTARY
static void lv_encoder_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static uint8_t last_dir = 0;
    RotaryMsg_t msg = static_cast<LilyGo_Display *>(indev_drv->user_data)->getRotary();
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
        plane->feedback((void*)drv);
    }
}
#endif //USING_INPUT_DEV_ROTARY

#ifdef USING_INPUT_DEV_KEYBOARD
static void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t act_key ;
    char c;
    int state = static_cast<LilyGo_Display *>(indev_drv->user_data)->getKeyChar(&c);
    if (state == KEYBOARD_PRESSED) {
        // log_d("Keyboard Pressed %c\n", c);
        act_key = c;
        data->key = act_key;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
    data->key = last_key;
}
#endif //USING_INPUT_DEV_KEYBOARD

#if LV_USE_LOG
void lv_log_print_g_cb(const char *buf)
{
    Serial.println(buf);
    Serial.flush();
}
#endif //LV_USE_LOG

static void keypad_feedback_cb(lv_indev_drv_t *indev_drv, uint8_t code)
{
    if (indev_drv->type == LV_INDEV_TYPE_KEYPAD) {
        log_d("Type:%u code:%u\n", indev_drv->type, code);
        static_cast<LilyGo_Display *>(indev_drv->user_data)->feedback();
    }
}

void beginLvglHelper(LilyGo_Display &display, bool debug)
{
    size_t lv_buffer_size = display.width() * display.height() * sizeof(lv_color_t);
    log_d("lv buffer size : %lu , width:%u height:%u", lv_buffer_size, display.width(), display.height());

    lv_init();

    lv_group_set_default(lv_group_create());

#if LV_USE_LOG
    if (debug) {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif //LV_USE_LOG

    buf = (lv_color_t *)ps_malloc(lv_buffer_size);
    assert(buf);

    buf1 = (lv_color_t *)ps_malloc(lv_buffer_size);
    assert(buf);

    log_i("lv buffer alloc successfully!");

    lv_disp_draw_buf_init( &draw_buf, buf, buf1, lv_buffer_size);
    /*Initialize the display*/
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = display.width();
    disp_drv.ver_res = display.height();
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = &display;
    disp_drv.full_refresh = display.needFullRefresh();
    lv_disp_drv_register( &disp_drv );

#ifdef USING_INPUT_DEV_TOUCHPAD
    if (display.hasTouch()) {
        log_d("lv register touchpad");
        /*Initialize the input device driver*/
        lv_indev_drv_init( &indev_drv );
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touchpad_read;
        indev_drv.user_data = &display;
        touchpad_indev = lv_indev_drv_register( &indev_drv );
    }
#endif //USING_INPUT_DEV_TOUCHPAD

#ifdef USING_INPUT_DEV_ROTARY
    if (display.hasEncoder()) {
        log_d("lv register encoder");
        lv_indev_drv_init(&encoder_drv);
        encoder_drv.type = LV_INDEV_TYPE_ENCODER;
        encoder_drv.read_cb = lv_encoder_read;
        encoder_drv.user_data = &display;
        encoder_indev = lv_indev_drv_register(&encoder_drv);
        lv_indev_set_group(encoder_indev, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_ROTARY

#ifdef USING_INPUT_DEV_KEYBOARD
    if (display.hasKeyboard()) {
        log_d("lv register keyboard");
        lv_indev_drv_init(&keypad_drv);
        keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
        keypad_drv.read_cb = keypad_read;
        keypad_drv.user_data = &display;
        keypad_drv.feedback_cb = keypad_feedback_cb;
        kb_indev = lv_indev_drv_register(&keypad_drv);
        lv_indev_set_group(kb_indev, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_KEYBOARD

    log_d("lv init successfully!");
}

#endif  //LVGL_VERSION_MAJOR

