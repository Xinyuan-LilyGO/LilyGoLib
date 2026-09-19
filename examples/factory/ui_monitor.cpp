/**
 * @file      ui_monitor.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"
#include <math.h>

typedef enum {
    ROW_STATE,
    ROW_PMIC,
    ROW_GAUGE,
    ROW_USB_PRESENT,
    ROW_BATTERY_PRESENT,
    ROW_OTG,
    ROW_VBUS_MV,
    ROW_VBUS_MA,
    ROW_SYS_MV,
    ROW_BATTERY_MV,
    ROW_BATTERY_MA,
    ROW_BATTERY_PERCENT,
    ROW_POWER_W,
    ROW_AVG_POWER_MW,
    ROW_TIME_EMPTY,
    ROW_TIME_FULL,
    ROW_REMAINING_CAPACITY,
    ROW_FULL_CAPACITY,
    ROW_DESIGN_CAPACITY,
    ROW_STANDBY_CURRENT,
    ROW_MAX_LOAD_CURRENT,
    ROW_TEMPERATURE,
    ROW_NTC,
    ROW_CHARGE_FAULT,
} monitor_row_id_t;

typedef struct {
    monitor_row_id_t id;
    lv_obj_t *label;
} monitor_row_label_t;

static const uint8_t MONITOR_ROW_MAX = 32;
static monitor_row_label_t row_labels[MONITOR_ROW_MAX];
static uint8_t row_label_count = 0;

static lv_obj_t *page_container = NULL;
static lv_timer_t *timer = NULL;
static lv_obj_t *quit_btn = NULL;
static lv_obj_t *overview_percent_label = NULL;
static lv_obj_t *overview_state_label = NULL;
static lv_obj_t *overview_source_label = NULL;
static lv_obj_t *overview_bar = NULL;

static void monitor_reset_labels()
{
    row_label_count = 0;
    overview_percent_label = NULL;
    overview_state_label = NULL;
    overview_source_label = NULL;
    overview_bar = NULL;
    for (uint8_t i = 0; i < MONITOR_ROW_MAX; ++i) {
        row_labels[i].label = NULL;
    }
}

static void monitor_register_label(monitor_row_id_t id, lv_obj_t *label)
{
    if (!label || row_label_count >= MONITOR_ROW_MAX) {
        return;
    }
    row_labels[row_label_count].id = id;
    row_labels[row_label_count].label = label;
    row_label_count++;
}

static lv_obj_t *monitor_find_label(monitor_row_id_t id)
{
    for (uint8_t i = 0; i < row_label_count; ++i) {
        if (row_labels[i].id == id) {
            return row_labels[i].label;
        }
    }
    return NULL;
}

static void monitor_set_row_text(monitor_row_id_t id, const char *text)
{
    lv_obj_t *label = monitor_find_label(id);
    if (label) {
        lv_label_set_text(label, text ? text : "--");
    }
}

static lv_obj_t *monitor_add_row(lv_obj_t *card, monitor_row_id_t id,
                                 const char *icon, const char *title,
                                 const char *value)
{
    lv_obj_t *row = ui_create_card_info(card, icon, title, value);
    lv_obj_t *label = lv_obj_get_child(row, lv_obj_get_child_count(row) - 1);
    monitor_register_label(id, label);
    return row;
}

static bool metric_valid(const power_monitor_metric_t &metric)
{
    return metric.valid && !isnan(metric.value) && !isinf(metric.value);
}

static void format_voltage(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%.3f V", metric.value / 1000.0f);
}

static void format_current(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    if (fabsf(metric.value) >= 1000.0f) {
        snprintf(buf, len, "%.2f A", metric.value / 1000.0f);
    } else {
        snprintf(buf, len, "%.0f mA", metric.value);
    }
}

static void format_percent(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%.0f%%", metric.value);
}

static void format_temperature(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%.1f C", metric.value);
}

static void format_power_w(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%.2f W", metric.value);
}

static void format_power_mw(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    if (fabsf(metric.value) >= 1000.0f) {
        snprintf(buf, len, "%.2f W", metric.value / 1000.0f);
    } else {
        snprintf(buf, len, "%.0f mW", metric.value);
    }
}

static void format_capacity(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%.0f mAh", metric.value);
}

static void format_time(char *buf, size_t len, const power_monitor_metric_t &metric)
{
    if (!metric_valid(metric)) {
        snprintf(buf, len, "--");
        return;
    }
    uint32_t minutes = static_cast<uint32_t>(metric.value + 0.5f);
    if (minutes >= 60) {
        snprintf(buf, len, "%lu h %02lu m",
                 static_cast<unsigned long>(minutes / 60),
                 static_cast<unsigned long>(minutes % 60));
    } else {
        snprintf(buf, len, "%lu min", static_cast<unsigned long>(minutes));
    }
}

static const char *present_text(bool valid, bool present)
{
    if (!valid) {
        return "--";
    }
    return present ? "Present" : "Absent";
}

static bool has_rail_metrics(const power_monitor_snapshot_t &snapshot)
{
    return metric_valid(snapshot.vbus_mv) ||
           metric_valid(snapshot.vbus_ma) ||
           metric_valid(snapshot.sys_mv) ||
           metric_valid(snapshot.battery_mv) ||
           metric_valid(snapshot.battery_ma);
}

static bool has_fuel_metrics(const power_monitor_snapshot_t &snapshot)
{
    return metric_valid(snapshot.battery_percent) ||
           metric_valid(snapshot.instantaneous_power_w) ||
           metric_valid(snapshot.average_power_mw) ||
           metric_valid(snapshot.time_to_empty_min) ||
           metric_valid(snapshot.time_to_full_min) ||
           metric_valid(snapshot.remaining_capacity_mah) ||
           metric_valid(snapshot.full_charge_capacity_mah) ||
           metric_valid(snapshot.design_capacity_mah) ||
           metric_valid(snapshot.standby_current_ma) ||
           metric_valid(snapshot.max_load_current_ma);
}

static bool has_health_metrics(const power_monitor_snapshot_t &snapshot)
{
    return metric_valid(snapshot.temperature_c) ||
           metric_valid(snapshot.battery_temperature_c) ||
           snapshot.ntc_state[0] != '\0' ||
           snapshot.charge_fault;
}

static void monitor_update_overview(const power_monitor_snapshot_t &snapshot)
{
    char buf[48];
    if (overview_percent_label) {
        format_percent(buf, sizeof(buf), snapshot.battery_percent);
        lv_label_set_text(overview_percent_label, buf);
        if (metric_valid(snapshot.battery_percent) && snapshot.battery_percent.value <= 20.0f) {
            lv_obj_set_style_text_color(overview_percent_label, UI_COLOR_WARNING, 0);
        } else {
            lv_obj_set_style_text_color(overview_percent_label, UI_COLOR_TEXT_PRIMARY, 0);
        }
    }
    if (overview_state_label) {
        lv_label_set_text(overview_state_label, snapshot.charge_state[0] ? snapshot.charge_state : "--");
        lv_obj_set_style_text_color(overview_state_label,
                                    snapshot.charge_fault ? UI_COLOR_WARNING : UI_COLOR_TEXT_SECONDARY,
                                    0);
    }
    if (overview_source_label) {
        snprintf(buf, sizeof(buf), "%s / %s",
                 snapshot.pmic_name[0] ? snapshot.pmic_name : "No PMIC",
                 snapshot.gauge_name[0] ? snapshot.gauge_name : "No gauge");
        lv_label_set_text(overview_source_label, buf);
    }
    if (overview_bar) {
        int32_t value = metric_valid(snapshot.battery_percent)
                        ? static_cast<int32_t>(snapshot.battery_percent.value + 0.5f)
                        : 0;
        value = constrain(value, 0, 100);
        lv_bar_set_value(overview_bar, value, LV_ANIM_ON);
        lv_obj_set_style_bg_color(overview_bar,
                                  value <= 20 ? UI_COLOR_WARNING : UI_COLOR_ACCENT,
                                  LV_PART_INDICATOR);
    }
}

static void monitor_update_rows(const power_monitor_snapshot_t &snapshot)
{
    char buf[48];

    monitor_set_row_text(ROW_STATE, snapshot.charge_state);
    monitor_set_row_text(ROW_PMIC, snapshot.pmic_name[0] ? snapshot.pmic_name : "--");
    monitor_set_row_text(ROW_GAUGE, snapshot.gauge_name[0] ? snapshot.gauge_name : "--");
    monitor_set_row_text(ROW_USB_PRESENT, present_text(snapshot.vbus_present_valid, snapshot.vbus_present));
    monitor_set_row_text(ROW_BATTERY_PRESENT, present_text(snapshot.battery_present_valid, snapshot.battery_present));
    monitor_set_row_text(ROW_OTG, snapshot.otg_supported ? (snapshot.otg_enabled ? "Enabled" : "Disabled") : "--");

    format_voltage(buf, sizeof(buf), snapshot.vbus_mv);
    monitor_set_row_text(ROW_VBUS_MV, buf);
    format_current(buf, sizeof(buf), snapshot.vbus_ma);
    monitor_set_row_text(ROW_VBUS_MA, buf);
    format_voltage(buf, sizeof(buf), snapshot.sys_mv);
    monitor_set_row_text(ROW_SYS_MV, buf);
    format_voltage(buf, sizeof(buf), snapshot.battery_mv);
    monitor_set_row_text(ROW_BATTERY_MV, buf);
    format_current(buf, sizeof(buf), snapshot.battery_ma);
    monitor_set_row_text(ROW_BATTERY_MA, buf);

    format_percent(buf, sizeof(buf), snapshot.battery_percent);
    monitor_set_row_text(ROW_BATTERY_PERCENT, buf);
    format_power_w(buf, sizeof(buf), snapshot.instantaneous_power_w);
    monitor_set_row_text(ROW_POWER_W, buf);
    format_power_mw(buf, sizeof(buf), snapshot.average_power_mw);
    monitor_set_row_text(ROW_AVG_POWER_MW, buf);
    format_time(buf, sizeof(buf), snapshot.time_to_empty_min);
    monitor_set_row_text(ROW_TIME_EMPTY, buf);
    format_time(buf, sizeof(buf), snapshot.time_to_full_min);
    monitor_set_row_text(ROW_TIME_FULL, buf);
    format_capacity(buf, sizeof(buf), snapshot.remaining_capacity_mah);
    monitor_set_row_text(ROW_REMAINING_CAPACITY, buf);
    format_capacity(buf, sizeof(buf), snapshot.full_charge_capacity_mah);
    monitor_set_row_text(ROW_FULL_CAPACITY, buf);
    format_capacity(buf, sizeof(buf), snapshot.design_capacity_mah);
    monitor_set_row_text(ROW_DESIGN_CAPACITY, buf);
    format_current(buf, sizeof(buf), snapshot.standby_current_ma);
    monitor_set_row_text(ROW_STANDBY_CURRENT, buf);
    format_current(buf, sizeof(buf), snapshot.max_load_current_ma);
    monitor_set_row_text(ROW_MAX_LOAD_CURRENT, buf);

    format_temperature(buf, sizeof(buf), snapshot.temperature_c);
    monitor_set_row_text(ROW_TEMPERATURE, buf);
    monitor_set_row_text(ROW_NTC, snapshot.ntc_state[0] ? snapshot.ntc_state : "--");
    monitor_set_row_text(ROW_CHARGE_FAULT, snapshot.charge_fault ? "Fault" : "Normal");
}

static void monitor_refresh()
{
    power_monitor_snapshot_t snapshot;
    hw_get_power_monitor_snapshot(snapshot);
    monitor_update_overview(snapshot);
    monitor_update_rows(snapshot);
}

static void back_event_handler(lv_event_t *e)
{
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
    monitor_reset_labels();
    menu_show();
}

static void monitor_create_overview(lv_obj_t *parent, const power_monitor_snapshot_t &snapshot)
{
    lv_obj_t *card = ui_create_card(parent, NULL);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t *top = lv_obj_create(card);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    overview_percent_label = lv_label_create(top);
    lv_label_set_text(overview_percent_label, "--");
    lv_obj_set_style_text_color(overview_percent_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(overview_percent_label, is_screen_small() ? &lv_font_montserrat_20 : &lv_font_montserrat_24, 0);

    lv_obj_t *state_box = lv_obj_create(top);
    lv_obj_set_size(state_box, LV_PCT(55), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(state_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state_box, 0, 0);
    lv_obj_set_style_pad_all(state_box, 0, 0);
    lv_obj_set_flex_flow(state_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    overview_state_label = lv_label_create(state_box);
    lv_label_set_text(overview_state_label, "--");
    lv_obj_set_style_text_color(overview_state_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(overview_state_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(overview_state_label, &lv_font_montserrat_12, 0);

    overview_source_label = lv_label_create(state_box);
    lv_label_set_text(overview_source_label, "--");
    lv_obj_set_style_text_color(overview_source_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(overview_source_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(overview_source_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(overview_source_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(overview_source_label, LV_PCT(100));

    overview_bar = lv_bar_create(card);
    lv_obj_set_size(overview_bar, LV_PCT(100), 8);
    lv_bar_set_range(overview_bar, 0, 100);
    lv_obj_set_style_radius(overview_bar, 0, 0);
    lv_obj_set_style_radius(overview_bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(overview_bar, UI_COLOR_TRACK, 0);
    lv_obj_set_style_bg_color(overview_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);

    monitor_update_overview(snapshot);
}

void ui_monitor_enter(lv_obj_t *parent)
{
    monitor_reset_labels();
    page_container = ui_create_app_page(parent, "Monitor", back_event_handler);

    power_monitor_snapshot_t snapshot;
    hw_get_power_monitor_snapshot(snapshot);

    monitor_create_overview(page_container, snapshot);

    lv_obj_t *card = ui_create_card(page_container, "Status");
    monitor_add_row(card, ROW_STATE, LV_SYMBOL_POWER, "State", "--");
    monitor_add_row(card, ROW_PMIC, LV_SYMBOL_SETTINGS, "PMIC", "--");
    monitor_add_row(card, ROW_GAUGE, LV_SYMBOL_BATTERY_FULL, "Gauge", "--");
    monitor_add_row(card, ROW_USB_PRESENT, LV_SYMBOL_USB, "USB", "--");
    monitor_add_row(card, ROW_BATTERY_PRESENT, LV_SYMBOL_BATTERY_FULL, "Battery", "--");
    if (snapshot.otg_supported) {
        monitor_add_row(card, ROW_OTG, LV_SYMBOL_POWER, "OTG", "--");
    }

    if (has_rail_metrics(snapshot)) {
        card = ui_create_card(page_container, "Rails");
        if (metric_valid(snapshot.vbus_mv)) {
            monitor_add_row(card, ROW_VBUS_MV, LV_SYMBOL_USB, "VBUS", "--");
        }
        if (metric_valid(snapshot.vbus_ma)) {
            monitor_add_row(card, ROW_VBUS_MA, LV_SYMBOL_USB, "VBUS Current", "--");
        }
        if (metric_valid(snapshot.sys_mv)) {
            monitor_add_row(card, ROW_SYS_MV, LV_SYMBOL_POWER, "System", "--");
        }
        if (metric_valid(snapshot.battery_mv)) {
            monitor_add_row(card, ROW_BATTERY_MV, LV_SYMBOL_BATTERY_FULL, "Battery V", "--");
        }
        if (metric_valid(snapshot.battery_ma)) {
            monitor_add_row(card, ROW_BATTERY_MA, LV_SYMBOL_BATTERY_FULL, "Battery I", "--");
        }
    }

    if (has_fuel_metrics(snapshot)) {
        card = ui_create_card(page_container, "Fuel");
        if (metric_valid(snapshot.battery_percent)) {
            monitor_add_row(card, ROW_BATTERY_PERCENT, LV_SYMBOL_BATTERY_FULL, "SOC", "--");
        }
        if (metric_valid(snapshot.instantaneous_power_w)) {
            monitor_add_row(card, ROW_POWER_W, LV_SYMBOL_POWER, "Power", "--");
        }
        if (metric_valid(snapshot.average_power_mw)) {
            monitor_add_row(card, ROW_AVG_POWER_MW, LV_SYMBOL_POWER, "Avg Power", "--");
        }
        if (metric_valid(snapshot.time_to_empty_min)) {
            monitor_add_row(card, ROW_TIME_EMPTY, LV_SYMBOL_REFRESH, "To Empty", "--");
        }
        if (metric_valid(snapshot.time_to_full_min)) {
            monitor_add_row(card, ROW_TIME_FULL, LV_SYMBOL_REFRESH, "To Full", "--");
        }
        if (metric_valid(snapshot.remaining_capacity_mah)) {
            monitor_add_row(card, ROW_REMAINING_CAPACITY, LV_SYMBOL_BATTERY_FULL, "Remaining", "--");
        }
        if (metric_valid(snapshot.full_charge_capacity_mah)) {
            monitor_add_row(card, ROW_FULL_CAPACITY, LV_SYMBOL_BATTERY_FULL, "Full Cap", "--");
        }
        if (metric_valid(snapshot.design_capacity_mah)) {
            monitor_add_row(card, ROW_DESIGN_CAPACITY, LV_SYMBOL_SETTINGS, "Design Cap", "--");
        }
        if (metric_valid(snapshot.standby_current_ma)) {
            monitor_add_row(card, ROW_STANDBY_CURRENT, LV_SYMBOL_POWER, "Standby I", "--");
        }
        if (metric_valid(snapshot.max_load_current_ma)) {
            monitor_add_row(card, ROW_MAX_LOAD_CURRENT, LV_SYMBOL_POWER, "Max Load I", "--");
        }
    }

    if (has_health_metrics(snapshot)) {
        card = ui_create_card(page_container, "Health");
        if (metric_valid(snapshot.temperature_c)) {
            monitor_add_row(card, ROW_TEMPERATURE, LV_SYMBOL_SETTINGS, "Temp", "--");
        }
        monitor_add_row(card, ROW_NTC, LV_SYMBOL_SETTINGS, "NTC", "--");
        monitor_add_row(card, ROW_CHARGE_FAULT, LV_SYMBOL_WARNING, "Fault", "--");
    }

    monitor_update_rows(snapshot);

    timer = lv_timer_create([](lv_timer_t *t) {
        (void)t;
        monitor_refresh();
    }, 1000, NULL);

#ifdef USING_TOUCHPAD
    quit_btn  = create_floating_button([](lv_event_t *e) {
        hw_feedback();
        back_event_handler(e);
    }, NULL);
#endif
}

void ui_monitor_exit(lv_obj_t *parent)
{
    (void)parent;
}

app_t ui_monitor_main = {
    .setup_func_cb = ui_monitor_enter,
    .exit_func_cb = ui_monitor_exit,
    .user_data = nullptr,
};
