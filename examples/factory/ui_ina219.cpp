 /**
 * @file      ui_ina219.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-22
 * INA219 voltage/current/power dashboard.
 */
#include "ui_define.h"

#if !defined(EXCLUDE_INA219)

#include <math.h>

#if defined(ARDUINO) && __has_include(<Wire.h>) && __has_include(<Adafruit_INA219.h>)
#include <Wire.h>
#include <Adafruit_INA219.h>
#define INA219_APP_HAS_DRIVER 1
#else
#define INA219_APP_HAS_DRIVER 0
#endif

#define INA219_I2C_ADDR             0x40
#define INA219_SAMPLE_MS            350
#define INA219_TREND_POINTS         44
#define INA219_MAX_VOLTAGE_MV       32000

static const uint16_t shunt_milliohm_options[] = {100, 50, 20, 10, 5};
#define INA219_SHUNT_OPTION_COUNT (sizeof(shunt_milliohm_options) / sizeof(shunt_milliohm_options[0]))

typedef struct {
    float bus_v;
    float shunt_mv;
    float load_v;
    float current_ma;
    float power_mw;
} ina219_sample_t;

static lv_obj_t *page_container = NULL;
static lv_obj_t *sensor_msgbox = NULL;
static lv_timer_t *sample_timer = NULL;

static lv_obj_t *voltage_arc = NULL;
static lv_obj_t *current_arc = NULL;
static lv_obj_t *power_bar = NULL;
static lv_obj_t *status_led = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *voltage_value_label = NULL;
static lv_obj_t *current_value_label = NULL;
static lv_obj_t *power_value_label = NULL;
static lv_obj_t *load_label = NULL;
static lv_obj_t *bus_label = NULL;
static lv_obj_t *shunt_label = NULL;
static lv_obj_t *shunt_dropdown = NULL;
static lv_obj_t *peak_current_label = NULL;
static lv_obj_t *peak_power_label = NULL;
static lv_obj_t *avg_power_label = NULL;
static lv_obj_t *direction_label = NULL;
static lv_obj_t *trend_chart = NULL;
static lv_obj_t *trend_voltage_label = NULL;
static lv_obj_t *trend_current_label = NULL;
static lv_chart_series_t *trend_voltage_series = NULL;
static lv_chart_series_t *trend_current_series = NULL;

static float filtered_voltage = 0.0f;
static float filtered_current = 0.0f;
static float filtered_power = 0.0f;
static float filtered_load = 0.0f;
static float peak_current = 0.0f;
static float peak_power = 0.0f;
static float avg_power = 0.0f;
static uint32_t avg_count = 0;
static uint8_t selected_shunt_idx = 0;
static bool filter_ready = false;
static bool ina219_ready = false;

#if INA219_APP_HAS_DRIVER
static Adafruit_INA219 ina219(INA219_I2C_ADDR);
#endif

static int32_t clamp_i32(int32_t value, int32_t min_v, int32_t max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static const char *format_voltage(float voltage_v, char *buf, size_t len)
{
    snprintf(buf, len, "%.2f V", voltage_v);
    return buf;
}

static const char *format_current(float current_ma, char *buf, size_t len)
{
    float abs_ma = fabsf(current_ma);
    if (abs_ma >= 1000.0f) {
        snprintf(buf, len, "%.2f A", current_ma / 1000.0f);
    } else {
        snprintf(buf, len, "%.0f mA", current_ma);
    }
    return buf;
}

static const char *format_power(float power_mw, char *buf, size_t len)
{
    float abs_mw = fabsf(power_mw);
    if (abs_mw >= 1000.0f) {
        snprintf(buf, len, "%.2f W", power_mw / 1000.0f);
    } else {
        snprintf(buf, len, "%.0f mW", power_mw);
    }
    return buf;
}

static const char *format_millivolt(float mv, char *buf, size_t len)
{
    snprintf(buf, len, "%.2f mV", mv);
    return buf;
}

static uint16_t current_shunt_milliohm(void)
{
    if (selected_shunt_idx >= INA219_SHUNT_OPTION_COUNT) {
        selected_shunt_idx = 0;
    }
    return shunt_milliohm_options[selected_shunt_idx];
}

static int32_t current_scale_max_ma(void)
{
    return clamp_i32(320000 / (int32_t)current_shunt_milliohm(), 1000, 64000);
}

static int32_t power_scale_max_mw(void)
{
    return current_scale_max_ma() * 32;
}

static void set_status(const char *text, lv_color_t color)
{
    if (status_label) {
        lv_label_set_text(status_label, text);
        lv_obj_set_style_text_color(status_label, color, 0);
    }
    if (status_led) {
        lv_obj_set_style_bg_color(status_led, color, 0);
        lv_obj_set_style_shadow_color(status_led, color, 0);
    }
}

static void reset_runtime_state(void)
{
    filtered_voltage = 0.0f;
    filtered_current = 0.0f;
    filtered_power = 0.0f;
    filtered_load = 0.0f;
    peak_current = 0.0f;
    peak_power = 0.0f;
    avg_power = 0.0f;
    avg_count = 0;
    filter_ready = false;
}

static void clear_widget_refs(void)
{
    voltage_arc = NULL;
    current_arc = NULL;
    power_bar = NULL;
    status_led = NULL;
    status_label = NULL;
    voltage_value_label = NULL;
    current_value_label = NULL;
    power_value_label = NULL;
    load_label = NULL;
    bus_label = NULL;
    shunt_label = NULL;
    shunt_dropdown = NULL;
    peak_current_label = NULL;
    peak_power_label = NULL;
    avg_power_label = NULL;
    direction_label = NULL;
    trend_chart = NULL;
    trend_voltage_label = NULL;
    trend_current_label = NULL;
    trend_voltage_series = NULL;
    trend_current_series = NULL;
}

static void stop_sampling(void)
{
    if (sample_timer) {
        lv_timer_del(sample_timer);
        sample_timer = NULL;
    }
#if INA219_APP_HAS_DRIVER
    if (ina219_ready) {
        ina219.powerSave(true);
    }
#endif
    ina219_ready = false;
}

static void close_page(bool show_menu)
{
    stop_sampling();
    if (sensor_msgbox) {
        destroy_msgbox(sensor_msgbox);
        sensor_msgbox = NULL;
    }
    if (page_container) {
        ui_destroy_app_page(page_container);
        page_container = NULL;
    }
    clear_widget_refs();
    reset_runtime_state();
    if (show_menu) {
        menu_show();
    }
}

static void sensor_msgbox_cb(lv_event_t *e)
{
    (void)e;
    close_page(false);
    menu_show();
}

static void show_sensor_unavailable_msgbox(void)
{
    if (sensor_msgbox) return;
    static const char *btns[] = {"OK", ""};
    sensor_msgbox = create_msgbox(
                        lv_scr_act(),
                        "INA219",
#if INA219_APP_HAS_DRIVER
                        "INA219 is not built into this board.\n"
                        "Connect and power an external INA219 module on the I2C pins to use this test.\n"
                        "No module was found at address 0x40. Check SDA/SCL, VCC/GND, and wiring.",
#else
                        "INA219 is not built into this board; an external INA219 module is required.\n"
                        "INA219 support is not enabled in this firmware.",
#endif
                        btns,
                        sensor_msgbox_cb,
                        NULL);
}

static bool probe_sensor(void)
{
#if INA219_APP_HAS_DRIVER
    Wire.beginTransmission(INA219_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        ina219_ready = false;
        return false;
    }

    if (!ina219.begin(&Wire)) {
        ina219_ready = false;
        return false;
    }

    ina219.setCalibration_32V_2A();
    ina219.powerSave(false);
    ina219_ready = true;
    return true;
#else
    ina219_ready = false;
    return false;
#endif
}

static bool read_sample(ina219_sample_t *sample)
{
    if (!sample || !ina219_ready) return false;
#if INA219_APP_HAS_DRIVER
    sample->bus_v = ina219.getBusVoltage_V();
    bool bus_ok = ina219.success();
    sample->shunt_mv = ina219.getShuntVoltage_mV();
    bool shunt_ok = ina219.success();
    sample->load_v = sample->bus_v + sample->shunt_mv / 1000.0f;

    uint16_t shunt_mohm = current_shunt_milliohm();
    sample->current_ma = sample->shunt_mv * 1000.0f / (float)shunt_mohm;
    sample->power_mw = sample->load_v * sample->current_ma;

    return bus_ok && shunt_ok &&
           isfinite(sample->bus_v) &&
           isfinite(sample->shunt_mv) &&
           isfinite(sample->current_ma) &&
           isfinite(sample->power_mw);
#else
    return false;
#endif
}

static void update_trend(float voltage_v, float current_ma)
{
    if (trend_chart && trend_voltage_series && trend_current_series) {
        lv_chart_set_next_value(trend_chart,
                                trend_voltage_series,
                                clamp_i32((int32_t)(voltage_v * 1000.0f), 0, INA219_MAX_VOLTAGE_MV));
        lv_chart_set_next_value(trend_chart,
                                trend_current_series,
                                clamp_i32((int32_t)fabsf(current_ma), 0, current_scale_max_ma()));
    }

    char buf[32];
    if (trend_voltage_label) {
        lv_label_set_text_fmt(trend_voltage_label, "V %s", format_voltage(voltage_v, buf, sizeof(buf)));
    }
    if (trend_current_label) {
        lv_label_set_text_fmt(trend_current_label, "I %s", format_current(current_ma, buf, sizeof(buf)));
    }
}

static void clear_trend_display(void)
{
    if (trend_chart && trend_voltage_series && trend_current_series) {
        lv_chart_set_axis_range(trend_chart, LV_CHART_AXIS_SECONDARY_Y, 0, current_scale_max_ma());
        lv_chart_set_all_values(trend_chart, trend_voltage_series, LV_CHART_POINT_NONE);
        lv_chart_set_all_values(trend_chart, trend_current_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(trend_chart);
    }
    if (trend_voltage_label) {
        lv_label_set_text(trend_voltage_label, "V --");
    }
    if (trend_current_label) {
        lv_label_set_text(trend_current_label, "I --");
    }
}

static void update_dashboard(const ina219_sample_t *sample)
{
    if (!sample) return;

    if (!filter_ready) {
        filtered_voltage = sample->bus_v;
        filtered_current = sample->current_ma;
        filtered_power = sample->power_mw;
        filtered_load = sample->load_v;
        filter_ready = true;
    } else {
        const float alpha = 0.32f;
        filtered_voltage += (sample->bus_v - filtered_voltage) * alpha;
        filtered_current += (sample->current_ma - filtered_current) * alpha;
        filtered_power += (sample->power_mw - filtered_power) * alpha;
        filtered_load += (sample->load_v - filtered_load) * alpha;
    }

    float abs_current = fabsf(filtered_current);
    float abs_power = fabsf(filtered_power);
    int32_t max_current_ma = current_scale_max_ma();
    int32_t max_power_mw = power_scale_max_mw();
    if (abs_current > peak_current) peak_current = abs_current;
    if (abs_power > peak_power) peak_power = abs_power;
    avg_power = (avg_power * avg_count + abs_power) / (float)(avg_count + 1);
    avg_count++;

    if (voltage_arc) {
        lv_arc_set_value(voltage_arc,
                         clamp_i32((int32_t)(filtered_load * 1000.0f), 0, INA219_MAX_VOLTAGE_MV));
    }
    if (current_arc) {
        lv_arc_set_value(current_arc,
                         clamp_i32((int32_t)abs_current, 0, max_current_ma));
    }
    if (power_bar) {
        lv_bar_set_value(power_bar,
                         clamp_i32((int32_t)(abs_power * 1000.0f / max_power_mw), 0, 1000),
                         LV_ANIM_ON);
    }

    char buf[32];
    if (voltage_value_label) {
        lv_label_set_text(voltage_value_label, format_voltage(filtered_load, buf, sizeof(buf)));
    }
    if (current_value_label) {
        lv_label_set_text(current_value_label, format_current(filtered_current, buf, sizeof(buf)));
    }
    if (power_value_label) {
        lv_label_set_text(power_value_label, format_power(filtered_power, buf, sizeof(buf)));
    }
    if (load_label) {
        lv_label_set_text_fmt(load_label, "LOAD %s", format_voltage(filtered_load, buf, sizeof(buf)));
    }
    if (bus_label) {
        lv_label_set_text(bus_label, format_voltage(filtered_voltage, buf, sizeof(buf)));
    }
    if (shunt_label) {
        lv_label_set_text(shunt_label, format_millivolt(sample->shunt_mv, buf, sizeof(buf)));
    }
    if (peak_current_label) {
        lv_label_set_text(peak_current_label, format_current(peak_current, buf, sizeof(buf)));
    }
    if (peak_power_label) {
        lv_label_set_text(peak_power_label, format_power(peak_power, buf, sizeof(buf)));
    }
    if (avg_power_label) {
        lv_label_set_text(avg_power_label, format_power(avg_power, buf, sizeof(buf)));
    }
    if (direction_label) {
        lv_label_set_text(direction_label, filtered_current < -1.0f ? "Reverse" : "Forward");
        lv_obj_set_style_text_color(direction_label,
                                    filtered_current < -1.0f ? lv_color_hex(0xFFB800) : UI_COLOR_TEXT_PRIMARY,
                                    0);
    }

    if (abs_current > max_current_ma * 0.95f || abs_power > max_power_mw * 0.95f) {
        set_status("LIMIT", lv_color_hex(0xFF445A));
    } else if (abs_current > max_current_ma * 0.70f || abs_power > max_power_mw * 0.70f) {
        set_status("HIGH LOAD", lv_color_hex(0xFFB800));
    } else {
        set_status("LIVE", UI_COLOR_ACCENT);
    }

    update_trend(filtered_load, filtered_current);
}

static void sample_timer_cb(lv_timer_t *t)
{
    (void)t;
    ina219_sample_t sample = {};
    if (!read_sample(&sample)) {
        set_status("READ FAIL", lv_color_hex(0xFF445A));
        return;
    }
    update_dashboard(&sample);
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    ina219_sample_t sample = {};
    if (read_sample(&sample)) {
        update_dashboard(&sample);
    } else {
        set_status("READ FAIL", lv_color_hex(0xFF445A));
    }
}

static void shunt_dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    selected_shunt_idx = lv_dropdown_get_selected(dd);
    if (selected_shunt_idx >= INA219_SHUNT_OPTION_COUNT) selected_shunt_idx = 0;
    if (current_arc) {
        lv_arc_set_range(current_arc, 0, current_scale_max_ma());
    }
    if (trend_chart) {
        lv_chart_set_axis_range(trend_chart, LV_CHART_AXIS_SECONDARY_Y, 0, current_scale_max_ma());
    }
    reset_runtime_state();
    clear_trend_display();
    refresh_btn_cb(NULL);
}

static void back_event_handler(lv_event_t *e)
{
    (void)e;
    close_page(true);
}

static lv_obj_t *create_center_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
    return label;
}

static lv_obj_t *create_meter_arc(lv_obj_t *parent, lv_color_t color,
                                  int32_t max_value, const char *title)
{
    (void)title;
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 92, 92);
    lv_arc_set_range(arc, 0, max_value);
    lv_arc_set_value(arc, 0);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_t *value = create_center_label(arc, "--", &lv_font_montserrat_14, UI_COLOR_TEXT_PRIMARY);
    return value;
}

static void create_meter_caption(lv_obj_t *parent, lv_obj_t *arc, const char *title, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align_to(lbl, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, -3);
}

static lv_obj_t *add_info_row(lv_obj_t *card, const char *icon, const char *title,
                              const char *value, lv_obj_t **out_label)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    if (out_label) *out_label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    return row;
}

static void create_dashboard_card(void)
{
    lv_obj_t *card = ui_create_card(page_container, NULL);
    lv_obj_set_height(card, 158);
    lv_obj_set_layout(card, LV_LAYOUT_NONE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x24121D), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x31415A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 14, 0);
    lv_obj_set_style_shadow_color(card, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "LIVE POWER");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    status_led = lv_obj_create(card);
    lv_obj_set_size(status_led, 9, 9);
    lv_obj_set_style_radius(status_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_led, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(status_led, 0, 0);
    lv_obj_set_style_shadow_width(status_led, 8, 0);
    lv_obj_set_style_shadow_color(status_led, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(status_led, LV_OPA_70, 0);
    lv_obj_align(status_led, LV_ALIGN_TOP_RIGHT, -8, 8);

    status_label = lv_label_create(card);
    lv_label_set_text(status_label, "LIVE");
    lv_obj_set_style_text_color(status_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(status_label, status_led, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    voltage_value_label = create_meter_arc(card, lv_color_hex(0x36D7FF),
                                           INA219_MAX_VOLTAGE_MV, "VOLTAGE");
    voltage_arc = lv_obj_get_parent(voltage_value_label);
    lv_obj_align(voltage_arc, LV_ALIGN_LEFT_MID, 6, -2);
    create_meter_caption(card, voltage_arc, "VOLTAGE", lv_color_hex(0x36D7FF));

    current_value_label = create_meter_arc(card, lv_color_hex(0xFFB800),
                                           current_scale_max_ma(), "CURRENT");
    current_arc = lv_obj_get_parent(current_value_label);
    lv_obj_align(current_arc, LV_ALIGN_RIGHT_MID, -6, -2);
    create_meter_caption(card, current_arc, "CURRENT", lv_color_hex(0xFFB800));

    power_value_label = lv_label_create(card);
    lv_label_set_text(power_value_label, "--");
    lv_obj_set_style_text_color(power_value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(power_value_label, &lv_font_montserrat_24, 0);
    lv_obj_align(power_value_label, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t *power_caption = lv_label_create(card);
    lv_label_set_text(power_caption, "POWER");
    lv_obj_set_style_text_color(power_caption, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(power_caption, &lv_font_montserrat_12, 0);
    lv_obj_align_to(power_caption, power_value_label, LV_ALIGN_OUT_BOTTOM_MID, 0, -1);

    power_bar = lv_bar_create(card);
    lv_obj_set_size(power_bar, 104, 6);
    lv_bar_set_range(power_bar, 0, 1000);
    lv_bar_set_value(power_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(power_bar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(power_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(power_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(power_bar, 0, LV_PART_INDICATOR);
    lv_obj_align(power_bar, LV_ALIGN_CENTER, 0, 32);

    load_label = lv_label_create(card);
    lv_label_set_text(load_label, "LOAD --");
    lv_obj_set_style_text_color(load_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(load_label, &lv_font_montserrat_12, 0);
    lv_obj_align(load_label, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void create_trend_card(void)
{
    lv_obj_t *card = ui_create_card(page_container, "Realtime V/I");

    lv_obj_t *legend = lv_obj_create(card);
    lv_obj_set_size(legend, LV_PCT(100), 18);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_radius(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_style_pad_column(legend, 10, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(legend, LV_OBJ_FLAG_SCROLLABLE);

    trend_voltage_label = lv_label_create(legend);
    lv_label_set_text(trend_voltage_label, "V --");
    lv_obj_set_style_text_color(trend_voltage_label, lv_color_hex(0x36D7FF), 0);
    lv_obj_set_style_text_font(trend_voltage_label, &lv_font_montserrat_12, 0);

    trend_current_label = lv_label_create(legend);
    lv_label_set_text(trend_current_label, "I --");
    lv_obj_set_style_text_color(trend_current_label, lv_color_hex(0xFFB800), 0);
    lv_obj_set_style_text_font(trend_current_label, &lv_font_montserrat_12, 0);

    trend_chart = lv_chart_create(card);
    lv_obj_set_size(trend_chart, LV_PCT(100), 76);
    lv_chart_set_type(trend_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(trend_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(trend_chart, INA219_TREND_POINTS);
    lv_chart_set_axis_range(trend_chart, LV_CHART_AXIS_PRIMARY_Y, 0, INA219_MAX_VOLTAGE_MV);
    lv_chart_set_axis_range(trend_chart, LV_CHART_AXIS_SECONDARY_Y, 0, current_scale_max_ma());
    lv_chart_set_div_line_count(trend_chart, 4, 5);
    lv_obj_set_style_bg_color(trend_chart, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(trend_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(trend_chart, 1, 0);
    lv_obj_set_style_border_color(trend_chart, lv_color_hex(0x31415A), 0);
    lv_obj_set_style_radius(trend_chart, 0, 0);
    lv_obj_set_style_pad_all(trend_chart, 4, 0);
    lv_obj_set_style_line_width(trend_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(trend_chart, lv_color_hex(0x26323E), LV_PART_MAIN);
    lv_obj_set_style_line_width(trend_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(trend_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(trend_chart, LV_OBJ_FLAG_SCROLLABLE);

    trend_voltage_series = lv_chart_add_series(trend_chart, lv_color_hex(0x36D7FF), LV_CHART_AXIS_PRIMARY_Y);
    trend_current_series = lv_chart_add_series(trend_chart, lv_color_hex(0xFFB800), LV_CHART_AXIS_SECONDARY_Y);
    clear_trend_display();
}

static void create_details_card(void)
{
    lv_obj_t *card = ui_create_card(page_container, "Details");
    add_info_row(card, LV_SYMBOL_BATTERY_FULL, "Bus", "--", &bus_label);
    add_info_row(card, LV_SYMBOL_SETTINGS, "Shunt", "--", &shunt_label);

    shunt_dropdown = lv_dropdown_create(lv_obj_create(card));
    lv_dropdown_set_options(shunt_dropdown, "100 mOhm\n50 mOhm\n20 mOhm\n10 mOhm\n5 mOhm");
    lv_dropdown_set_selected(shunt_dropdown, selected_shunt_idx);
    lv_obj_set_width(shunt_dropdown, 100);
    lv_obj_set_height(shunt_dropdown, 32);
    lv_obj_add_event_cb(shunt_dropdown, shunt_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_card_item(card, LV_SYMBOL_SETTINGS, "Shunt R", shunt_dropdown);

    add_info_row(card, LV_SYMBOL_UPLOAD, "Peak I", "--", &peak_current_label);
    add_info_row(card, LV_SYMBOL_POWER, "Peak P", "--", &peak_power_label);
    add_info_row(card, LV_SYMBOL_REFRESH, "Avg P", "--", &avg_power_label);
    add_info_row(card, LV_SYMBOL_LOOP, "Flow", "--", &direction_label);
    ui_create_card_button(card, LV_SYMBOL_REFRESH, "Sample", "Refresh", refresh_btn_cb);
}

void ui_ina219_enter(lv_obj_t *parent)
{
    reset_runtime_state();
    if (!probe_sensor()) {
        show_sensor_unavailable_msgbox();
        return;
    }

    page_container = ui_create_app_page(parent, "INA219", back_event_handler);
    create_dashboard_card();
    create_trend_card();
    create_details_card();

    ina219_sample_t sample = {};
    if (read_sample(&sample)) {
        update_dashboard(&sample);
    } else {
        set_status("READ FAIL", lv_color_hex(0xFF445A));
    }

    sample_timer = lv_timer_create(sample_timer_cb, INA219_SAMPLE_MS, NULL);
}

void ui_ina219_exit(lv_obj_t *parent)
{
    (void)parent;
    close_page(false);
}

app_t ui_ina219_main = {
    .setup_func_cb = ui_ina219_enter,
    .exit_func_cb = ui_ina219_exit,
    .user_data = NULL,
};

#endif
