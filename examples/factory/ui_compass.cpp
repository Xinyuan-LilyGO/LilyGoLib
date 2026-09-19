/**
 * @file      ui_compass.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-08-16
 *
 * Compass — needle image + degree readout + calibration + optional BME280 data.
 * Layout: compass on left, data on right (like sensor page).
 */
#include "ui_define.h"
#include <math.h>
#include <limits.h>

#if !defined(EXCLUDE_COMPASS)

#define POINTER_PIVOT_X 24
#define POINTER_PIVOT_Y 40

static constexpr uint16_t CAL_MIN_SAMPLES = 80;
static constexpr uint16_t CAL_STABLE_TICKS = 100;
static constexpr float CAL_MIN_XY_RANGE_UT = 20.0f;
static constexpr float CAL_MIN_Z_RANGE_UT = 8.0f;
static constexpr float COMPASS_FILTER_DEADBAND_DEG = 0.8f;
static constexpr float COMPASS_RENDER_DEADBAND_DEG = 0.5f;

LV_IMG_DECLARE(img_compass_needle);

static lv_obj_t *page_container = NULL;
static lv_timer_t *timer = NULL;
static lv_obj_t *sensor_unavailable_msgbox = NULL;
static uint32_t timer_interval = 50;
static uint32_t timer_enter_count = 0;
static const uint32_t timer_update_bme_data = (2000 / timer_interval);

static lv_obj_t *dial_obj;
static lv_obj_t *cardinal_labels[4];
static lv_obj_t *pointer_obj;
static lv_obj_t *degree_label;
static lv_obj_t *temperature, *humidity, *pressure, *altitude;
static lv_obj_t *cal_led_obj;
static lv_obj_t *cal_progress_bar;
static lv_obj_t *cal_btn_label;
static int compass_face_size = 0;
static bool compass_filter_ready = false;
static bool compass_render_ready = false;
static float compass_filtered_heading = 0.0f;
static float compass_render_heading = 0.0f;

typedef struct {
    bool active;
    uint32_t samples;
    uint16_t stable_ticks;
    int32_t x_min;
    int32_t x_max;
    int32_t y_min;
    int32_t y_max;
    int32_t z_min;
    int32_t z_max;
    float fx_min;
    float fx_max;
    float fy_min;
    float fy_max;
    float fz_min;
    float fz_max;
    mag_calibration_t previous;
} compass_cal_state_t;

static compass_cal_state_t cal_state;

static bool check_sensor_available(void)
{
    return (hw_get_device_online() & HW_MAGNETOMETER_ONLINE);
}

static float min_float(float a, float b)
{
    return a < b ? a : b;
}

static float normalize_heading(float angle)
{
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    return angle;
}

static float heading_delta(float target, float current)
{
    float delta = target - current;
    while (delta > 180.0f) {
        delta -= 360.0f;
    }
    while (delta < -180.0f) {
        delta += 360.0f;
    }
    return delta;
}

static void reset_compass_filter()
{
    compass_filter_ready = false;
    compass_render_ready = false;
    compass_filtered_heading = 0.0f;
    compass_render_heading = 0.0f;
}

static float smooth_compass_heading(float raw_angle)
{
    raw_angle = normalize_heading(raw_angle);
    if (!compass_filter_ready) {
        compass_filtered_heading = raw_angle;
        compass_render_heading = raw_angle;
        compass_filter_ready = true;
        compass_render_ready = true;
        return raw_angle;
    }

    float delta = heading_delta(raw_angle, compass_filtered_heading);
    float abs_delta = fabsf(delta);
    if (abs_delta >= COMPASS_FILTER_DEADBAND_DEG) {
        float alpha = 0.12f;
        if (abs_delta > 30.0f) {
            alpha = 0.50f;
        } else if (abs_delta > 10.0f) {
            alpha = 0.30f;
        }
        compass_filtered_heading = normalize_heading(compass_filtered_heading + delta * alpha);
    }

    float render_delta = heading_delta(compass_filtered_heading, compass_render_heading);
    if (!compass_render_ready || fabsf(render_delta) >= COMPASS_RENDER_DEADBAND_DEG || abs_delta > 6.0f) {
        compass_render_heading = compass_filtered_heading;
        compass_render_ready = true;
    }
    return compass_render_heading;
}

static const char *heading_cardinal(float angle)
{
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((angle + 22.5f) / 45.0f) & 0x07;
    return dirs[idx];
}

static void create_dial_tick(lv_obj_t *parent, int compass_size, int degree)
{
    bool cardinal = (degree % 90) == 0;
    bool major = (degree % 30) == 0;
    int tick_w = cardinal ? 2 : 1;
    int tick_h = cardinal ? 10 : (major ? 7 : 4);
    int center = compass_size / 2;
    int radius = center - 8;
    float rad = (float)degree * (float)M_PI / 180.0f;
    int x = center + (int)(sinf(rad) * radius);
    int y = center - (int)(cosf(rad) * radius);

    lv_obj_t *tick = lv_obj_create(parent);
    lv_obj_set_size(tick, tick_w, tick_h);
    lv_obj_set_pos(tick, x - tick_w / 2, y - tick_h / 2);
    lv_obj_set_style_bg_color(tick, cardinal ? UI_COLOR_ACCENT : UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_bg_opa(tick, major ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_border_width(tick, 0, 0);
    lv_obj_set_style_radius(tick, 0, 0);
    lv_obj_set_style_pad_all(tick, 0, 0);
    lv_obj_set_style_transform_pivot_x(tick, tick_w / 2, 0);
    lv_obj_set_style_transform_pivot_y(tick, tick_h / 2, 0);
    lv_obj_set_style_transform_rotation(tick, degree * 10, 0);
    lv_obj_remove_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tick, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *create_cardinal_label(lv_obj_t *parent, const char *text, lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void position_cardinal_label(lv_obj_t *label, int compass_size, int degree, float heading)
{
    if (!label || compass_size <= 0) {
        return;
    }
    int center = compass_size / 2;
    int radius = center - (compass_size < 130 ? 24 : 28);
    float screen_degree = (float)degree - heading;
    float rad = screen_degree * (float)M_PI / 180.0f;
    int x = (int)(sinf(rad) * radius);
    int y = -(int)(cosf(rad) * radius);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
}

static void update_cardinal_labels(float heading)
{
    position_cardinal_label(cardinal_labels[0], compass_face_size, 0, heading);
    position_cardinal_label(cardinal_labels[1], compass_face_size, 90, heading);
    position_cardinal_label(cardinal_labels[2], compass_face_size, 180, heading);
    position_cardinal_label(cardinal_labels[3], compass_face_size, 270, heading);
}

static void set_cal_led(bool calibrated)
{
    if (!cal_led_obj) {
        return;
    }
    lv_obj_set_style_bg_color(cal_led_obj, calibrated ? lv_color_hex(0x00D060) : lv_color_hex(0xD02020), 0);
    lv_obj_set_style_shadow_color(cal_led_obj, calibrated ? lv_color_hex(0x00D060) : lv_color_hex(0xD02020), 0);
}

static void reset_cal_ranges()
{
    cal_state.samples = 0;
    cal_state.stable_ticks = 0;
    cal_state.x_min = INT32_MAX;
    cal_state.x_max = INT32_MIN;
    cal_state.y_min = INT32_MAX;
    cal_state.y_max = INT32_MIN;
    cal_state.z_min = INT32_MAX;
    cal_state.z_max = INT32_MIN;
    cal_state.fx_min = 10000.0f;
    cal_state.fx_max = -10000.0f;
    cal_state.fy_min = 10000.0f;
    cal_state.fy_max = -10000.0f;
    cal_state.fz_min = 10000.0f;
    cal_state.fz_max = -10000.0f;
}

static void update_cal_status_idle()
{
    mag_calibration_t cal;
    hw_mag_get_calibration(cal);
    set_cal_led(cal.valid);
    if (cal_progress_bar) {
        lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(cal_progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (cal_btn_label) {
        lv_label_set_text(cal_btn_label, "Cal");
    }
}

static void cancel_calibration()
{
    if (!cal_state.active) {
        return;
    }
    hw_mag_apply_calibration(cal_state.previous);
    cal_state.active = false;
    reset_compass_filter();
    update_cal_status_idle();
}

static void finish_calibration()
{
    mag_calibration_t cal;
    cal.x = (int16_t)((cal_state.x_max + cal_state.x_min) / 2);
    cal.y = (int16_t)((cal_state.y_max + cal_state.y_min) / 2);
    float z_range_ut = (cal_state.fz_max - cal_state.fz_min) * 100.0f;
    if (z_range_ut >= CAL_MIN_Z_RANGE_UT) {
        cal.z = (int16_t)((cal_state.z_max + cal_state.z_min) / 2);
    } else if (cal_state.previous.valid) {
        cal.z = cal_state.previous.z;
    } else {
        cal.z = 0;
    }
    cal.valid = true;

    bool saved = hw_mag_save_calibration(cal);
    cal_state.active = false;
    reset_compass_filter();
    if (cal_progress_bar) {
        lv_bar_set_value(cal_progress_bar, 100, LV_ANIM_OFF);
        lv_obj_add_flag(cal_progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (cal_btn_label) {
        lv_label_set_text(cal_btn_label, "Cal");
    }
    set_cal_led(saved);
}

static void start_calibration()
{
    hw_mag_get_calibration(cal_state.previous);
    mag_calibration_t zero = {0, 0, 0, false};
    hw_mag_apply_calibration(zero);
    reset_compass_filter();
    reset_cal_ranges();
    cal_state.active = true;
    if (cal_progress_bar) {
        lv_obj_remove_flag(cal_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    }
    if (cal_btn_label) {
        lv_label_set_text(cal_btn_label, "Cal");
    }
    set_cal_led(false);
}

static void calibration_button_cb(lv_event_t *e)
{
    (void)e;
    if (!cal_state.active) {
        start_calibration();
    }
}

static void create_calibration_controls(lv_obj_t *parent)
{
    lv_obj_t *cal_btn = lv_btn_create(parent);
    lv_obj_set_size(cal_btn, 88, 20);
    lv_obj_set_style_radius(cal_btn, 2, 0);
    lv_obj_set_style_pad_all(cal_btn, 0, 0);
    lv_obj_set_style_bg_color(cal_btn, UI_COLOR_ACCENT, 0);
    ui_add_accent_focus_style(cal_btn);
    lv_obj_add_event_cb(cal_btn, calibration_button_cb, LV_EVENT_CLICKED, NULL);
    cal_btn_label = lv_label_create(cal_btn);
    lv_obj_set_style_text_font(cal_btn_label, &lv_font_montserrat_10, 0);
    lv_label_set_text(cal_btn_label, "Cal");
    lv_obj_center(cal_btn_label);
}

static void create_calibration_status_widgets(lv_obj_t *root)
{
    if (!root) {
        return;
    }

    cal_led_obj = lv_obj_create(root);
    lv_obj_set_size(cal_led_obj, 9, 9);
    lv_obj_set_style_radius(cal_led_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cal_led_obj, 1, 0);
    lv_obj_set_style_border_color(cal_led_obj, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(cal_led_obj, 5, 0);
    lv_obj_set_style_pad_all(cal_led_obj, 0, 0);
    lv_obj_align(cal_led_obj, LV_ALIGN_TOP_RIGHT, -10, 11);
    lv_obj_remove_flag(cal_led_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cal_led_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(cal_led_obj);

    cal_progress_bar = lv_bar_create(root);
    lv_obj_set_size(cal_progress_bar, LV_PCT(100), 4);
    lv_bar_set_range(cal_progress_bar, 0, 100);
    lv_bar_set_value(cal_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(cal_progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cal_progress_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cal_progress_bar, UI_COLOR_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cal_progress_bar, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cal_progress_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(cal_progress_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(cal_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cal_progress_bar);
}

static void process_calibration_sample(const mag_data_t &mag)
{
    bool changed = false;

    if (mag.raw_x < cal_state.x_min) {
        cal_state.x_min = mag.raw_x;
        changed = true;
    }
    if (mag.raw_x > cal_state.x_max) {
        cal_state.x_max = mag.raw_x;
        changed = true;
    }
    if (mag.raw_y < cal_state.y_min) {
        cal_state.y_min = mag.raw_y;
        changed = true;
    }
    if (mag.raw_y > cal_state.y_max) {
        cal_state.y_max = mag.raw_y;
        changed = true;
    }
    if (mag.raw_z < cal_state.z_min) {
        cal_state.z_min = mag.raw_z;
        changed = true;
    }
    if (mag.raw_z > cal_state.z_max) {
        cal_state.z_max = mag.raw_z;
        changed = true;
    }

    if (mag.field_x < cal_state.fx_min) cal_state.fx_min = mag.field_x;
    if (mag.field_x > cal_state.fx_max) cal_state.fx_max = mag.field_x;
    if (mag.field_y < cal_state.fy_min) cal_state.fy_min = mag.field_y;
    if (mag.field_y > cal_state.fy_max) cal_state.fy_max = mag.field_y;
    if (mag.field_z < cal_state.fz_min) cal_state.fz_min = mag.field_z;
    if (mag.field_z > cal_state.fz_max) cal_state.fz_max = mag.field_z;

    cal_state.samples++;
    cal_state.stable_ticks = changed ? 0 : (uint16_t)(cal_state.stable_ticks + 1);

    float x_range_ut = (cal_state.fx_max - cal_state.fx_min) * 100.0f;
    float y_range_ut = (cal_state.fy_max - cal_state.fy_min) * 100.0f;
    float xy_quality = min_float(x_range_ut / CAL_MIN_XY_RANGE_UT, y_range_ut / CAL_MIN_XY_RANGE_UT);
    if (xy_quality > 1.0f) {
        xy_quality = 1.0f;
    }

    bool enough_xy = x_range_ut >= CAL_MIN_XY_RANGE_UT &&
                     y_range_ut >= CAL_MIN_XY_RANGE_UT &&
                     cal_state.samples >= CAL_MIN_SAMPLES;
    int progress = enough_xy ? 70 + (int)((uint32_t)cal_state.stable_ticks * 30 / CAL_STABLE_TICKS)
                   : (int)(xy_quality * 70.0f);
    if (progress > 100) {
        progress = 100;
    }
    if (cal_progress_bar) {
        lv_bar_set_value(cal_progress_bar, progress, LV_ANIM_OFF);
    }

    if (!enough_xy) {
        set_cal_led(false);
    } else if (cal_state.stable_ticks < CAL_STABLE_TICKS) {
        set_cal_led(false);
    } else {
        finish_calibration();
    }
}

static void update_compass(lv_timer_t *param)
{
#ifdef USING_BME280
    if (timer_enter_count++ >= timer_update_bme_data) {
        timer_enter_count = 0;
        float temp, humi, press, alt;
        hw_bme_get_data(temp, humi, press, alt);
        if (temperature) lv_label_set_text_fmt(temperature, "%.1f°C", temp);
        if (humidity) lv_label_set_text_fmt(humidity, "%.0f%%", humi);
        if (pressure) lv_label_set_text_fmt(pressure, "%.0fhPa", press);
        if (altitude) lv_label_set_text_fmt(altitude, "%.0fm", alt);
    }
#endif
    mag_data_t mag;
    if (!hw_mag_read(mag)) return;
    if (cal_state.active) {
        process_calibration_sample(mag);
    }
    float raw_angle = mag.heading_degrees;
    if (raw_angle < 0) return;
    float angle = smooth_compass_heading(raw_angle);
    if (dial_obj) {
        lv_obj_set_style_transform_rotation(dial_obj, (int)(-angle * 10.0f), 0);
    }
    update_cardinal_labels(angle);
    if (degree_label) {
        int display_degree = (int)(angle + 0.5f);
        if (display_degree >= 360) {
            display_degree -= 360;
        }
        lv_label_set_text_fmt(degree_label, "%s %d°", heading_cardinal(angle), display_degree);
    }
}

static void back_event_handler(lv_event_t *e)
{
    if (timer) {
        lv_timer_delete(timer);
        timer = NULL;
    }
    cancel_calibration();
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    dial_obj = NULL;
    cardinal_labels[0] = NULL;
    cardinal_labels[1] = NULL;
    cardinal_labels[2] = NULL;
    cardinal_labels[3] = NULL;
    compass_face_size = 0;
    pointer_obj = NULL;
    degree_label = NULL;
    temperature = NULL;
    humidity = NULL;
    pressure = NULL;
    altitude = NULL;
    cal_led_obj = NULL;
    cal_progress_bar = NULL;
    cal_btn_label = NULL;
    reset_compass_filter();
    hw_mag_enable(false);
    menu_show();
}

static void sensor_unavailable_msgbox_cb(lv_event_t *e)
{
    (void)e;
    if (sensor_unavailable_msgbox) {
        destroy_msgbox(sensor_unavailable_msgbox);
        sensor_unavailable_msgbox = NULL;
    }
    menu_show();
}

static void show_sensor_unavailable_msgbox(void)
{
    if (sensor_unavailable_msgbox) {
        return;
    }

    static const char *btns[] = {"OK", ""};
    sensor_unavailable_msgbox = create_msgbox(
                                    lv_scr_act(),
                                    "Sensor",
                                    "Magnetometer not detected.",
                                    btns,
                                    sensor_unavailable_msgbox_cb,
                                    NULL);
}

void ui_compass_enter(lv_obj_t *parent)
{
    if (!check_sensor_available()) {
        show_sensor_unavailable_msgbox();
        return;
    }

    hw_mag_enable(true);
    reset_compass_filter();

    page_container = ui_create_app_page(parent, "Compass", back_event_handler);
    create_calibration_status_widgets((lv_obj_t *)lv_obj_get_user_data(page_container));

    bool small = is_screen_small();

    /* Card fills content area */
    lv_obj_t *card = ui_create_card(page_container, NULL);
    lv_obj_set_height(card, lv_pct(100));
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Row container: compass on left, data on right */
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left: Compass ── */
    int compass_size = small ? 120 : 150;

    lv_obj_t *compass_cont = lv_obj_create(row);
    lv_obj_set_size(compass_cont, compass_size, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(compass_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compass_cont, 0, 0);
    lv_obj_set_style_radius(compass_cont, 0, 0);
    lv_obj_set_style_pad_all(compass_cont, 0, 0);
    lv_obj_set_style_pad_row(compass_cont, small ? 3 : 5, 0);
    lv_obj_set_flex_flow(compass_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(compass_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(compass_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(compass_cont, LV_OBJ_FLAG_CLICKABLE);

    degree_label = lv_label_create(compass_cont);
    lv_obj_set_style_text_color(degree_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(degree_label, small ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
    lv_obj_set_width(degree_label, compass_size);
    lv_label_set_long_mode(degree_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(degree_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(degree_label, "N 0°");

    lv_obj_t *compass_face = lv_obj_create(compass_cont);
    compass_face_size = compass_size;
    lv_obj_set_size(compass_face, compass_size, compass_size);
    lv_obj_set_style_bg_opa(compass_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compass_face, 0, 0);
    lv_obj_set_style_radius(compass_face, 0, 0);
    lv_obj_set_style_pad_all(compass_face, 0, 0);
    lv_obj_remove_flag(compass_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(compass_face, LV_OBJ_FLAG_CLICKABLE);

    dial_obj = lv_obj_create(compass_face);
    lv_obj_set_size(dial_obj, compass_size, compass_size);
    lv_obj_set_style_bg_opa(dial_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dial_obj, 0, 0);
    lv_obj_set_style_radius(dial_obj, 0, 0);
    lv_obj_set_style_pad_all(dial_obj, 0, 0);
    lv_obj_set_style_transform_pivot_x(dial_obj, compass_size / 2, 0);
    lv_obj_set_style_transform_pivot_y(dial_obj, compass_size / 2, 0);
    lv_obj_center(dial_obj);
    lv_obj_remove_flag(dial_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dial_obj, LV_OBJ_FLAG_CLICKABLE);

    /* Compass ring */
    lv_obj_t *ring = lv_obj_create(dial_obj);
    lv_obj_set_size(ring, compass_size, compass_size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_center(ring);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    for (int deg = 0; deg < 360; deg += 10) {
        create_dial_tick(dial_obj, compass_size, deg);
    }

    cardinal_labels[0] = create_cardinal_label(compass_face, "N", UI_COLOR_ACCENT, &lv_font_montserrat_14);
    cardinal_labels[1] = create_cardinal_label(compass_face, "E", UI_COLOR_TEXT_SECONDARY, &lv_font_montserrat_12);
    cardinal_labels[2] = create_cardinal_label(compass_face, "S", UI_COLOR_TEXT_SECONDARY, &lv_font_montserrat_12);
    cardinal_labels[3] = create_cardinal_label(compass_face, "W", UI_COLOR_TEXT_SECONDARY, &lv_font_montserrat_12);
    update_cardinal_labels(0.0f);

    /* Needle remains fixed at the front; the dial rotates underneath it. */
    pointer_obj = lv_image_create(compass_face);
    lv_image_set_src(pointer_obj, &img_compass_needle);
    lv_obj_set_style_image_recolor(pointer_obj, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(pointer_obj, LV_OPA_COVER, 0);

    lv_obj_align(pointer_obj, LV_ALIGN_CENTER, 0, -15);
    lv_image_set_pivot(pointer_obj, POINTER_PIVOT_X, POINTER_PIVOT_Y);
    lv_image_set_rotation(pointer_obj, 0);

    /* Center dot */
    lv_obj_t *dot = lv_obj_create(compass_face);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_center(dot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    /* ── Right: Data column ── */
    lv_obj_t *data_col = lv_obj_create(row);
    lv_obj_set_size(data_col, 90, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(data_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(data_col, 0, 0);
    lv_obj_set_style_radius(data_col, 0, 0);
    lv_obj_set_style_pad_all(data_col, 0, 0);
    lv_obj_set_style_pad_row(data_col, 2, 0);
    lv_obj_set_flex_flow(data_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(data_col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(data_col, LV_OBJ_FLAG_SCROLLABLE);

#ifdef USING_BME280
    /* BME280 data — fixed-width labels to prevent layout jitter */
    lv_obj_t *lbl;

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Temp");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    temperature = lv_label_create(data_col);
    lv_label_set_text(temperature, "--°C");
    lv_obj_set_style_text_color(temperature, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(temperature, &lv_font_montserrat_12, 0);
    lv_obj_set_width(temperature, 80);
    lv_label_set_long_mode(temperature, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(temperature, LV_TEXT_ALIGN_RIGHT, 0);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Humi");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    humidity = lv_label_create(data_col);
    lv_label_set_text(humidity, "--%");
    lv_obj_set_style_text_color(humidity, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(humidity, &lv_font_montserrat_12, 0);
    lv_obj_set_width(humidity, 80);
    lv_label_set_long_mode(humidity, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(humidity, LV_TEXT_ALIGN_RIGHT, 0);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Press");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    pressure = lv_label_create(data_col);
    lv_label_set_text(pressure, "--hPa");
    lv_obj_set_style_text_color(pressure, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(pressure, &lv_font_montserrat_12, 0);
    lv_obj_set_width(pressure, 80);
    lv_label_set_long_mode(pressure, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(pressure, LV_TEXT_ALIGN_RIGHT, 0);

    lbl = lv_label_create(data_col);
    lv_label_set_text(lbl, "Alt");
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    altitude = lv_label_create(data_col);
    lv_label_set_text(altitude, "--m");
    lv_obj_set_style_text_color(altitude, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(altitude, &lv_font_montserrat_12, 0);
    lv_obj_set_width(altitude, 80);
    lv_label_set_long_mode(altitude, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(altitude, LV_TEXT_ALIGN_RIGHT, 0);
#endif

    create_calibration_controls(data_col);
    update_cal_status_idle();
    timer = lv_timer_create(update_compass, timer_interval, NULL);
}

void ui_compass_exit(lv_obj_t *parent)
{
}

app_t ui_compass_main = {
    .setup_func_cb = ui_compass_enter,
    .exit_func_cb  = ui_compass_exit,
    .user_data     = nullptr,
};

#endif
