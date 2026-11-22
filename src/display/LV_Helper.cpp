/**
 * @file      LV_Helper.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 * Connects LilyGo display and input devices to LVGL 8.
 */
#include "LilyGoLog.h"
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
static lv_rotary_diagnostics_t rotary_diagnostics;
static int8_t rotary_last_direction;
static volatile bool rotary_activity_pending;
static LilyGo_Display *rotary_plane;
static volatile bool encoder_input_suppressed;


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
static void update_rotary_diagnostics(const RotaryMsg_t &msg)
{
    rotary_diagnostics.button_pressed = msg.centerBtnPressed;
    if (msg.centerBtnClicked) {
        rotary_diagnostics.button_clicks++;
    }
    if (msg.enc_diff == 0) return;

    const uint32_t now = millis();
    const int8_t direction = msg.enc_diff > 0 ? 1 : -1;
    const uint16_t magnitude = (uint16_t)(msg.enc_diff > 0 ? msg.enc_diff : -(int32_t)msg.enc_diff);

    if (rotary_diagnostics.event_count > 0) {
        rotary_diagnostics.last_interval_ms = now - rotary_diagnostics.last_event_ms;
        if (direction != rotary_last_direction) {
            rotary_diagnostics.direction_changes++;
            if (rotary_diagnostics.last_interval_ms <= 80) {
                rotary_diagnostics.rapid_reverse_events++;
            }
        }
    }

    rotary_diagnostics.position += msg.enc_diff;
    if (msg.enc_diff > 0) {
        rotary_diagnostics.positive_steps += magnitude;
    } else {
        rotary_diagnostics.negative_steps += magnitude;
    }
    rotary_diagnostics.event_count++;
    if (magnitude > 1) rotary_diagnostics.multi_step_events++;
    if (magnitude > rotary_diagnostics.max_abs_diff) rotary_diagnostics.max_abs_diff = magnitude;
    rotary_diagnostics.last_diff = msg.enc_diff;
    rotary_diagnostics.last_event_ms = now;
    rotary_last_direction = direction;
}

static void lv_encoder_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    auto *plane = static_cast<LilyGo_Display *>(indev_drv->user_data);
    RotaryMsg_t msg = plane->getRotary();
    data->enc_diff = msg.enc_diff;
    data->state = msg.centerBtnPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (encoder_input_suppressed) {
        plane->clearRotaryMsg();
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        rotary_activity_pending = false;
        return;
    }
    update_rotary_diagnostics(msg);
    if (msg.enc_diff != 0) {
        rotary_activity_pending = true;
    }
    if (msg.enc_diff != 0 || msg.centerBtnClicked || msg.centerBtnReleased) {
        plane->feedback((void *)indev_drv);
    }
}
#endif //USING_INPUT_DEV_ROTARY

void lv_reset_rotary_diagnostics()
{
    memset(&rotary_diagnostics, 0, sizeof(rotary_diagnostics));
    rotary_last_direction = 0;
    rotary_activity_pending = false;
}

bool lv_consume_encoder_activity()
{
    const bool pending = rotary_activity_pending;
    rotary_activity_pending = false;
    return pending;
}

void lv_set_encoder_input_suppressed(bool suppressed)
{
    encoder_input_suppressed = suppressed;
    rotary_activity_pending = false;
    if (rotary_plane) {
        rotary_plane->clearRotaryMsg();
    }
    if (suppressed && encoder_indev) {
        lv_indev_wait_release(encoder_indev);
    }
}

bool lv_get_rotary_diagnostics(lv_rotary_diagnostics_t *snapshot)
{
    if (!snapshot) return false;
    *snapshot = rotary_diagnostics;
    return encoder_indev != NULL;
}

#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD)
static void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t act_key ;
    char c;
    int state = static_cast<LilyGo_Display *>(indev_drv->user_data)->getKeyChar(&c);
    if (state == KEYBOARD_PRESSED) {
        // log_d("Keyboard Pressed %c\n", c);
        act_key = c;
        last_key = act_key;
        data->key = act_key;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
    data->key = last_key;
}
#endif // USING_INPUT_DEV_KEYBOARD || USING_TDECK_KEYBOARD

#if LV_USE_LOG
static bool lv_debug_enabled;
void lv_log_print_g_cb(const char *buf)
{
    (void)buf;
    if (lv_debug_enabled) {
        LILYGO_LOG_PRINTF("%s", buf);
    }
}
#endif //LV_USE_LOG

static void keypad_feedback_cb(lv_indev_drv_t *indev_drv, uint8_t code)
{
    if (indev_drv->type == LV_INDEV_TYPE_KEYPAD) {
        LILYGO_LOG_D("Type:%u code:%u\n", indev_drv->type, code);
        static_cast<LilyGo_Display *>(indev_drv->user_data)->feedback();
    }
}

void beginLvglHelper(LilyGo_Display &display, bool debug)
{
    rotary_plane = &display;
    size_t lv_buffer_size = display.width() * display.height() * sizeof(lv_color_t);
    LILYGO_LOG_D("lv buffer size : %lu , width:%u height:%u", lv_buffer_size, display.width(), display.height());

    lv_init();

    lv_group_set_default(lv_group_create());

#if LV_USE_LOG
    lv_debug_enabled = LILYGO_DEBUG_ENABLED && debug;
    lv_log_register_print_cb(lv_log_print_g_cb);
#endif //LV_USE_LOG

    buf = (lv_color_t *)ps_malloc(lv_buffer_size);
    assert(buf);

    buf1 = (lv_color_t *)ps_malloc(lv_buffer_size);
    assert(buf);

    LILYGO_LOG_I("lv buffer alloc successfully!");

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
        LILYGO_LOG_D("lv register touchpad");
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
        LILYGO_LOG_D("lv register encoder");
        lv_indev_drv_init(&encoder_drv);
        encoder_drv.type = LV_INDEV_TYPE_ENCODER;
        encoder_drv.read_cb = lv_encoder_read;
        encoder_drv.user_data = &display;
        encoder_indev = lv_indev_drv_register(&encoder_drv);
        lv_indev_set_group(encoder_indev, lv_group_get_default());
    }
#endif //USING_INPUT_DEV_ROTARY

#if defined(USING_INPUT_DEV_KEYBOARD) || defined(USING_TDECK_KEYBOARD)
    if (display.hasKeyboard()) {
        LILYGO_LOG_D("lv register keyboard");
        lv_indev_drv_init(&keypad_drv);
        keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
        keypad_drv.read_cb = keypad_read;
        keypad_drv.user_data = &display;
        keypad_drv.feedback_cb = keypad_feedback_cb;
        kb_indev = lv_indev_drv_register(&keypad_drv);
        lv_indev_set_group(kb_indev, lv_group_get_default());
    }
#endif // USING_INPUT_DEV_KEYBOARD || USING_TDECK_KEYBOARD

    LILYGO_LOG_D("lv init successfully!");
}

#endif  //LVGL_VERSION_MAJOR
