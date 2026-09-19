/**
 * @file      ui_sensor.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"

#if !defined(EXCLUDE_IMU)

typedef struct {
    lv_obj_t *roll;
    lv_obj_t *pitch;
    lv_obj_t *heading;
} imu_label_t;

typedef struct {
    lv_obj_t *steps;
    lv_obj_t *activity;
    lv_obj_t *orientation;
    lv_obj_t *step_events;
    lv_obj_t *last_event;
    lv_obj_t *tap_events;
    lv_obj_t *motion_events;
    lv_obj_t *accel_xyz;
    lv_obj_t *g_force;
    lv_obj_t *peak_g;
} bma_label_t;

static imu_label_t label_imu;
static bma_label_t label_bma;
static lv_obj_t *page_container = NULL;
static lv_timer_t *timer = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *bma_orientation_led = NULL;

static lv_obj_t *card_value_label(lv_obj_t *row)
{
    if (!row) return NULL;
    uint32_t child_count = lv_obj_get_child_count(row);
    if (child_count == 0) return NULL;
    return lv_obj_get_child(row, child_count - 1);
}

static const char *bma_activity_text(bma_activity_t activity)
{
    switch (activity) {
    case BMA_ACTIVITY_STATIONARY:
        return "Still";
    case BMA_ACTIVITY_WALKING:
        return "Walking";
    case BMA_ACTIVITY_RUNNING:
        return "Running";
    default:
        return "Unknown";
    }
}

static const char *bma_event_text(bma_sensor_event_t event)
{
    switch (event) {
    case BMA_SENSOR_EVENT_STEP:
        return "Step";
    case BMA_SENSOR_EVENT_SINGLE_TAP:
        return "Single Tap";
    case BMA_SENSOR_EVENT_DOUBLE_TAP:
        return "Double Tap";
    case BMA_SENSOR_EVENT_TRIPLE_TAP:
        return "Triple Tap";
    case BMA_SENSOR_EVENT_ACTIVITY:
        return "Activity";
    case BMA_SENSOR_EVENT_TILT:
        return "Tilt";
    case BMA_SENSOR_EVENT_ANY_MOTION:
        return "Motion";
    case BMA_SENSOR_EVENT_NO_MOTION:
        return "No Motion";
    case BMA_SENSOR_EVENT_DATA_READY:
        return "Data Ready";
    default:
        return "--";
    }
}

static const char *bma_orientation_text(uint8_t orientation, uint8_t reverse)
{
    if (reverse) return "Face Down";
    switch (orientation) {
    case LV_ALIGN_TOP_LEFT:
        return "Top Left";
    case LV_ALIGN_TOP_RIGHT:
        return "Top Right";
    case LV_ALIGN_BOTTOM_LEFT:
        return "Bottom Left";
    case LV_ALIGN_BOTTOM_RIGHT:
        return "Bottom Right";
    case LV_ALIGN_CENTER:
        return "Face Up";
    default:
        return "--";
    }
}

static void bma_reset_cb(lv_event_t *e)
{
    hw_feedback();
    hw_reset_bma_sensor_stats();
}

static void bma_update_timer_cb(lv_timer_t *t)
{
    bma_sensor_snapshot_t snapshot;
    hw_get_bma_sensor_snapshot(snapshot);
    if (!snapshot.valid) return;

    char buf[64];
    if (label_bma.steps) {
        lv_label_set_text_fmt(label_bma.steps, "%lu", (unsigned long)snapshot.step_count);
    }
    if (label_bma.activity) {
        lv_label_set_text(label_bma.activity, bma_activity_text(snapshot.activity));
    }
    if (label_bma.orientation) {
        lv_label_set_text(label_bma.orientation,
                          bma_orientation_text(snapshot.orientation, snapshot.reverse));
    }
    if (label_bma.step_events) {
        lv_label_set_text_fmt(label_bma.step_events, "%lu", (unsigned long)snapshot.step_events);
    }
    if (label_bma.last_event) {
        lv_label_set_text(label_bma.last_event, bma_event_text(snapshot.last_event));
    }
    if (label_bma.tap_events) {
        snprintf(buf, sizeof(buf), "S %lu  D %lu",
                 (unsigned long)snapshot.single_taps,
                 (unsigned long)snapshot.double_taps);
        lv_label_set_text(label_bma.tap_events, buf);
    }
    if (label_bma.motion_events) {
        snprintf(buf, sizeof(buf), "Move %lu  Tilt %lu",
                 (unsigned long)snapshot.motion_events,
                 (unsigned long)snapshot.tilt_events);
        lv_label_set_text(label_bma.motion_events, buf);
    }
    if (label_bma.accel_xyz) {
        if (snapshot.accel_valid) {
            snprintf(buf, sizeof(buf), "%.1f %.1f %.1f",
                     snapshot.accel_x, snapshot.accel_y, snapshot.accel_z);
            lv_label_set_text(label_bma.accel_xyz, buf);
        } else {
            lv_label_set_text(label_bma.accel_xyz, "--");
        }
    }
    if (label_bma.g_force) {
        snprintf(buf, sizeof(buf), "%.2fg", snapshot.magnitude / 9.80665f);
        lv_label_set_text(label_bma.g_force, buf);
    }
    if (label_bma.peak_g) {
        snprintf(buf, sizeof(buf), "%.2fg", snapshot.peak_magnitude / 9.80665f);
        lv_label_set_text(label_bma.peak_g, buf);
    }

    if (bma_orientation_led) {
        lv_obj_align(bma_orientation_led,
                     snapshot.accel_valid ? static_cast<lv_align_t>(snapshot.orientation) : LV_ALIGN_CENTER,
                     0, 0);
        lv_led_set_color(bma_orientation_led,
                         snapshot.reverse ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN));
    }
}

static void back_event_handler(lv_event_t *e)
{
    hw_unregister_imu_process();

    if (timer) {
        lv_timer_del(timer); timer = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }

    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }
    memset(&label_bma, 0, sizeof(label_bma));
    bma_orientation_led = NULL;

    menu_show();
}

void ui_sensor_enter(lv_obj_t *parent)
{
    hw_register_imu_process();

    page_container = ui_create_app_page(parent, "Sensor", back_event_handler);

#if  defined(USING_BHI260_SENSOR)
    /* ── IMU display ── */
    lv_obj_t *card = ui_create_card(page_container, "IMU");
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    bool small = is_screen_small();
    int arc_size = small ? 80 : 140;
    int ball_size = small ? 20 : 36;

    lv_obj_t *lbl;

#if defined(ARDUINO_T_WATCH_S3_ULTRA)
    /* Watch-Ultra: circle centered in upper third, data below */
    int screen_h = lv_display_get_vertical_resolution(NULL);

    /* Remove padding and flex layout for absolute positioning */
    lv_obj_set_style_pad_all(page_container, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    /* Disable flex so lv_obj_align works */
    lv_obj_set_layout(card, LV_LAYOUT_NONE);

    lv_obj_t *arc = lv_arc_create(card);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, arc_size, arc_size);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, screen_h / 3 - arc_size / 2);

    lv_obj_t *led1 = lv_obj_create(arc);
    lv_obj_set_size(led1, ball_size, ball_size);
    lv_obj_set_style_radius(led1, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(led1, &ui_styles.accent_btn, 0);
    lv_obj_set_style_shadow_width(led1, 12, 0);
    lv_obj_set_style_shadow_color(led1, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(led1, LV_OPA_40, 0);
    lv_obj_center(led1);

    /* Data labels — below the circle, centered */
    int data_y = screen_h / 3 + arc_size / 2 + 16;
    int label_w = small ? 80 : 100;

    lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Roll");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, -label_w / 2, data_y);

    label_imu.roll = lv_label_create(card);
    lv_label_set_text(label_imu.roll, "0.0");
    lv_obj_add_style(label_imu.roll, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.roll, &lv_font_montserrat_20, 0);
    lv_obj_align(label_imu.roll, LV_ALIGN_TOP_MID, label_w / 2, data_y);

    lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Pitch");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, -label_w / 2, data_y + 35);

    label_imu.pitch = lv_label_create(card);
    lv_label_set_text(label_imu.pitch, "0.0");
    lv_obj_add_style(label_imu.pitch, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.pitch, &lv_font_montserrat_20, 0);
    lv_obj_align(label_imu.pitch, LV_ALIGN_TOP_MID, label_w / 2, data_y + 35);

    lbl = lv_label_create(card);
    lv_label_set_text(lbl, "Heading");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, -label_w / 2, data_y + 70);

    label_imu.heading = lv_label_create(card);
    lv_label_set_text(label_imu.heading, "0.0");
    lv_obj_add_style(label_imu.heading, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.heading, &lv_font_montserrat_20, 0);
    lv_obj_align(label_imu.heading, LV_ALIGN_TOP_MID, label_w / 2, data_y + 70);

#else
    /* Other devices: arc left, data right in a row */
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *arc_cont = lv_obj_create(row);
    lv_obj_set_size(arc_cont, arc_size + 4, arc_size + 4);
    lv_obj_set_style_bg_opa(arc_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_cont, 0, 0);
    lv_obj_set_style_radius(arc_cont, 0, 0);
    lv_obj_set_style_pad_all(arc_cont, 0, 0);
    lv_obj_remove_flag(arc_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(arc_cont, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *arc = lv_arc_create(arc_cont);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, arc_size, arc_size);
    lv_obj_center(arc);

    lv_obj_t *led1 = lv_obj_create(arc);
    lv_obj_set_size(led1, ball_size, ball_size);
    lv_obj_set_style_radius(led1, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(led1, &ui_styles.accent_btn, 0);
    lv_obj_set_style_shadow_width(led1, 8, 0);
    lv_obj_set_style_shadow_color(led1, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(led1, LV_OPA_40, 0);
    lv_obj_center(led1);

    lv_obj_t *data_col = lv_obj_create(row);
    lv_obj_set_height(data_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(data_col, 1);
    lv_obj_set_style_bg_opa(data_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(data_col, 0, 0);
    lv_obj_set_style_radius(data_col, 0, 0);
    lv_obj_set_style_pad_all(data_col, 0, 0);
    lv_obj_set_style_pad_row(data_col, 4, 0);
    lv_obj_set_flex_flow(data_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(data_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Roll");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    label_imu.roll = lv_label_create(data_col);
    lv_label_set_text(label_imu.roll, "0.0");
    lv_obj_add_style(label_imu.roll, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.roll, &lv_font_montserrat_16, 0);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Pitch");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    label_imu.pitch = lv_label_create(data_col);
    lv_label_set_text(label_imu.pitch, "0.0");
    lv_obj_add_style(label_imu.pitch, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.pitch, &lv_font_montserrat_16, 0);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Heading");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    label_imu.heading = lv_label_create(data_col);
    lv_label_set_text(label_imu.heading, "0.0");
    lv_obj_add_style(label_imu.heading, &ui_styles.accent_text, 0);
    lv_obj_set_style_text_font(label_imu.heading, &lv_font_montserrat_16, 0);
#endif

    timer = lv_timer_create([](lv_timer_t *t) {
        lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
        imu_params_t param;
        hw_get_imu_params(param);
        lv_label_set_text_fmt(label_imu.roll, "%.1f", param.roll);
        lv_label_set_text_fmt(label_imu.pitch, "%.1f", param.pitch);
        lv_label_set_text_fmt(label_imu.heading, "%.1f", param.heading);

        float posY = param.roll * 2;
        float posX = param.pitch;
        /* Constrain ball within the arc using actual object sizes */
        lv_obj_t *arc_parent = lv_obj_get_parent(obj); /* the arc object */
        int arc_r = lv_obj_get_width(arc_parent) / 2;
        int ball_r = lv_obj_get_width(obj) / 2;
        int max_move = arc_r - ball_r - 2;
        if (max_move < 10) max_move = 10;
#ifdef ARDUINO
        posX = constrain(posX, -max_move, max_move);
        posY = constrain(posY, -max_move, max_move);
#endif
        lv_obj_align(obj, LV_ALIGN_CENTER, posX, posY);

    }, 100, led1);
#endif // USING_BHI260_SENSOR


#ifdef USING_BMA423_SENSOR

    memset(&label_bma, 0, sizeof(label_bma));
    bma_orientation_led = NULL;
    bool small = is_screen_small();

    lv_obj_t *card = ui_create_card(page_container, "Orientation");
    lv_obj_t *cont = lv_obj_create(card);
    lv_obj_set_size(cont, small ? 72 : 120, small ? 72 : 120);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cont);

    bma_orientation_led = lv_led_create(cont);
    lv_led_set_brightness(bma_orientation_led, 150);
    lv_led_set_color(bma_orientation_led, lv_palette_main(LV_PALETTE_GREEN));
    lv_obj_align(bma_orientation_led, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Position", "--");
    label_bma.orientation = card_value_label(info_row);

    card = ui_create_card(page_container, "Motion");
    info_row = ui_create_card_info(card, LV_SYMBOL_LIST, "Steps", "0");
    label_bma.steps = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Activity", "Unknown");
    label_bma.activity = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Step Events", "0");
    label_bma.step_events = card_value_label(info_row);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Counters", "Reset", bma_reset_cb);

    card = ui_create_card(page_container, "Gesture");
    info_row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "Last", "--");
    label_bma.last_event = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Taps", "S 0  D 0");
    label_bma.tap_events = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Motion", "Move 0  Tilt 0");
    label_bma.motion_events = card_value_label(info_row);

    card = ui_create_card(page_container, "Accel");
    info_row = ui_create_card_info(card, LV_SYMBOL_SETTINGS, "XYZ", "--");
    label_bma.accel_xyz = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "G Force", "0.00g");
    label_bma.g_force = card_value_label(info_row);
    info_row = ui_create_card_info(card, LV_SYMBOL_REFRESH, "Peak", "0.00g");
    label_bma.peak_g = card_value_label(info_row);

    timer = lv_timer_create(bma_update_timer_cb, 250, NULL);
    if (timer) {
        lv_timer_ready(timer);
    }

#endif // USING_BMA423_SENSOR

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t*e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif

}

void ui_sensor_exit(lv_obj_t *parent)
{

}

app_t ui_sensor_main = {
    .setup_func_cb = ui_sensor_enter,
    .exit_func_cb = ui_sensor_exit,
    .user_data = nullptr,
};

#endif /* EXCLUDE_IMU */
