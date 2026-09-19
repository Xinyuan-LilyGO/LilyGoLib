/**
 * @file      ui_navigation.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-08-25
 * 
 */
#include "ui_define.h"

#if FACTORY_HAS_TOUCH_INPUT

static const int32_t EDGE_WIDTH_PX = 20;
static const uint32_t SWIPE_TIMEOUT_MS = 1000;
static const int32_t INDICATOR_MIN_DIAMETER_PX = 64;
static const int32_t INDICATOR_MAX_DIAMETER_PX = 88;
static const int32_t INDICATOR_MARGIN_PX = 8;
static const int32_t INDICATOR_ICON_Y_OFFSET_PX = 2;
static const int32_t INDICATOR_START_DISTANCE_PX = 10;
static const int32_t INDICATOR_VISIBLE_DIVISOR = 3;
static const uint32_t INDICATOR_HIDE_MS = 140;

struct edge_swipe_t {
    lv_obj_t *root;
    lv_obj_t *back_target;
    lv_event_code_t back_event;
    lv_indev_t *pointer;
    lv_point_t start;
    uint32_t started_at;
    int32_t min_distance;
    int direction;
    lv_obj_t *indicator;
    lv_obj_t *indicator_arrow;
    lv_timer_t *tracking_timer;
    bool threshold_feedback_sent;
};

static bool edge_swipe_contains(lv_obj_t *root, lv_obj_t *obj)
{
    for (; obj; obj = lv_obj_get_parent(obj)) {
        if (obj == root) return true;
    }
    return false;
}

static void edge_swipe_indicator_x_anim(void *obj, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)obj, x);
}

static void edge_swipe_stop_tracking(edge_swipe_t *state)
{
    if (state->tracking_timer) {
        lv_timer_delete(state->tracking_timer);
        state->tracking_timer = NULL;
    }
}

static void edge_swipe_hide_indicator(edge_swipe_t *state, bool accepted)
{
    if (!state->indicator) return;

    lv_obj_t *indicator = state->indicator;
    int32_t width = lv_display_get_horizontal_resolution(NULL);
    int32_t current_x = lv_obj_get_x(indicator);
    int32_t target_x = current_x + state->direction * 8;
    if (!accepted) {
        int32_t diameter = lv_obj_get_width(indicator);
        target_x = state->direction > 0 ? -diameter : width;
    }

    state->indicator = NULL;
    state->indicator_arrow = NULL;

    lv_anim_t indicator_anim;
    lv_anim_init(&indicator_anim);
    lv_anim_set_var(&indicator_anim, indicator);
    lv_anim_set_values(&indicator_anim, current_x, target_x);
    lv_anim_set_duration(&indicator_anim, INDICATOR_HIDE_MS);
    lv_anim_set_path_cb(&indicator_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&indicator_anim, edge_swipe_indicator_x_anim);
    lv_anim_start(&indicator_anim);
    lv_obj_fade_out(indicator, INDICATOR_HIDE_MS, 0);
    lv_obj_delete_delayed(indicator, INDICATOR_HIDE_MS + 20);
}

static void edge_swipe_create_indicator(edge_swipe_t *state, lv_display_t *display)
{
    if (state->indicator) lv_obj_delete(state->indicator);

    lv_obj_t *layer = lv_display_get_layer_top(display);
    state->indicator = lv_obj_create(layer);
    if (!state->indicator) return;
    lv_obj_set_size(state->indicator, INDICATOR_MIN_DIAMETER_PX,
                    INDICATOR_MIN_DIAMETER_PX);
    lv_obj_set_style_bg_color(state->indicator, UI_COLOR_ACCENT_DIM, 0);
    lv_obj_set_style_bg_opa(state->indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->indicator, 0, 0);
    lv_obj_set_style_radius(state->indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(state->indicator, 0, 0);
    lv_obj_remove_flag(state->indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(state->indicator, LV_OBJ_FLAG_CLICKABLE);

    state->indicator_arrow = lv_label_create(state->indicator);
    if (!state->indicator_arrow) {
        lv_obj_delete(state->indicator);
        state->indicator = NULL;
        return;
    }
    lv_label_set_text(state->indicator_arrow,
                      state->direction > 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(state->indicator_arrow, lv_color_white(), 0);
    lv_obj_set_style_text_font(state->indicator_arrow, &lv_font_montserrat_22, 0);
    lv_obj_center(state->indicator_arrow);
}

static bool edge_swipe_update_indicator(edge_swipe_t *state, lv_indev_t *indev,
        const lv_point_t &point)
{
    int32_t inward = (point.x - state->start.x) * state->direction;
    int32_t vertical = LV_ABS(point.y - state->start.y);
    inward = LV_MAX(inward, 0);

    int32_t progress = LV_CLAMP(0, inward * 256 / state->min_distance, 256);
    bool threshold_reached = progress == 256 && inward >= vertical * 2;
    if (threshold_reached && !state->threshold_feedback_sent) {
        state->threshold_feedback_sent = true;
        hw_feedback();
    }

    if (!state->indicator) return threshold_reached;

    lv_display_t *display = lv_indev_get_display(indev);
    int32_t width = lv_display_get_horizontal_resolution(display);
    int32_t height = lv_display_get_vertical_resolution(display);
    if (inward < INDICATOR_START_DISTANCE_PX) {
        lv_obj_add_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
        return threshold_reached;
    }

    int32_t diameter = INDICATOR_MIN_DIAMETER_PX +
                       progress * (INDICATOR_MAX_DIAMETER_PX -
                                   INDICATOR_MIN_DIAMETER_PX) / 256;
    int32_t visible_width = diameter * progress / INDICATOR_VISIBLE_DIVISOR / 256;
    int32_t x = state->direction > 0 ? -(diameter - visible_width) :
                width - visible_width;
    int32_t y = LV_CLAMP(INDICATOR_MARGIN_PX,
                         point.y - diameter / 2,
                         height - diameter - INDICATOR_MARGIN_PX);

    lv_obj_remove_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(state->indicator, -1);
    lv_obj_set_size(state->indicator, diameter, diameter);
    lv_obj_set_pos(state->indicator, x, y);
    lv_obj_set_style_opa(state->indicator, progress * LV_OPA_90 / 256, 0);
    lv_obj_set_style_bg_opa(state->indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(state->indicator,
                              threshold_reached ? UI_COLOR_ACCENT : UI_COLOR_ACCENT_DIM, 0);
    if (state->indicator_arrow) {
        lv_obj_update_layout(state->indicator_arrow);
        int32_t icon_offset = state->direction > 0 ? (diameter - visible_width) / 2 :
                              -(diameter - visible_width) / 2;
        lv_obj_set_style_transform_rotation(state->indicator_arrow, 0, 0);
        lv_obj_align(state->indicator_arrow, LV_ALIGN_CENTER,
                     icon_offset, INDICATOR_ICON_Y_OFFSET_PX);
    }
    return threshold_reached;
}

static void edge_swipe_cancel(edge_swipe_t *state, lv_indev_t *indev)
{
    if (state->pointer != indev) return;
    state->pointer = NULL;
    edge_swipe_stop_tracking(state);
    edge_swipe_hide_indicator(state, false);
}

static void edge_swipe_tracking_timer_cb(lv_timer_t *timer)
{
    edge_swipe_t *state = (edge_swipe_t *)lv_timer_get_user_data(timer);
    if (!state->pointer) {
        edge_swipe_stop_tracking(state);
        return;
    }
    if (lv_tick_elaps(state->started_at) > SWIPE_TIMEOUT_MS ||
            !lv_obj_is_visible(state->root)) {
        edge_swipe_cancel(state, state->pointer);
        return;
    }

    lv_point_t point;
    lv_indev_get_point(state->pointer, &point);
    edge_swipe_update_indicator(state, state->pointer, point);
}

static void edge_swipe_input_cb(lv_event_t *e)
{
    edge_swipe_t *state = (edge_swipe_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DELETE || code == LV_EVENT_INDEV_RESET || code == LV_EVENT_LONG_PRESSED) {
        edge_swipe_cancel(state, indev);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;

    lv_obj_t *target = (lv_obj_t *)lv_event_get_param(e);
    if (code == LV_EVENT_PRESSED) {
        edge_swipe_stop_tracking(state);
        state->pointer = NULL;
        if (!lv_obj_is_visible(state->root) || !edge_swipe_contains(state->root, target)) return;

        lv_point_t point;
        lv_indev_get_point(indev, &point);
        int32_t width = lv_display_get_horizontal_resolution(lv_indev_get_display(indev));
        if (point.x < 0 || point.x >= width) return;
        if (point.x < EDGE_WIDTH_PX) state->direction = 1;
        else if (point.x >= width - EDGE_WIDTH_PX) state->direction = -1;
        else return;

        state->pointer = indev;
        state->start = point;
        state->started_at = lv_tick_get();
        state->min_distance = LV_CLAMP(40, width / 5, 80);
        state->threshold_feedback_sent = false;
        edge_swipe_create_indicator(state, lv_indev_get_display(indev));
        edge_swipe_update_indicator(state, indev, point);
        // Input-device callbacks do not receive PRESSING events; poll only during this gesture.
        state->tracking_timer = lv_timer_create(edge_swipe_tracking_timer_cb, 16, state);
        return;
    }

    if (state->pointer != indev) return;
    if (lv_tick_elaps(state->started_at) > SWIPE_TIMEOUT_MS ||
            !lv_obj_is_visible(state->root) || !edge_swipe_contains(state->root, target) ||
            !lv_obj_is_valid(state->back_target) || lv_obj_has_state(state->back_target, LV_STATE_DISABLED)) {
        edge_swipe_cancel(state, indev);
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    bool accepted = edge_swipe_update_indicator(state, indev, point);
    state->pointer = NULL;
    edge_swipe_stop_tracking(state);
    edge_swipe_hide_indicator(state, accepted);
    if (!accepted) return;

    // Reset before navigation: the handler may delete the page, and this release must not click a widget.
    lv_obj_t *back_target = state->back_target;
    lv_event_code_t back_event = state->back_event;
    lv_indev_reset(indev, NULL);
    lv_obj_send_event(back_target, back_event, indev);
}

static void edge_swipe_delete_cb(lv_event_t *e)
{
    edge_swipe_t *state = (edge_swipe_t *)lv_event_get_user_data(e);
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        lv_indev_remove_event_cb_with_user_data(indev, edge_swipe_input_cb, state);
    }
    edge_swipe_stop_tracking(state);
    if (state->indicator) {
        lv_obj_delete(state->indicator);
        state->indicator = NULL;
    }
    lv_free(state);
}

#endif

void ui_enable_edge_swipe_back(lv_obj_t *root, lv_obj_t *back_target, lv_event_code_t back_event)
{
#if FACTORY_HAS_TOUCH_INPUT
    if (!root || !back_target) return;
    edge_swipe_t *state = (edge_swipe_t *)lv_malloc_zeroed(sizeof(edge_swipe_t));
    if (!state) return;
    state->root = root;
    state->back_target = back_target;
    state->back_event = back_event;
    lv_obj_add_event_cb(root, edge_swipe_delete_cb, LV_EVENT_DELETE, state);

    // Input-device events also cover scrollable children without changing their event bubbling flags.
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(indev, edge_swipe_input_cb, LV_EVENT_ALL, state);
        }
    }
#else
    LV_UNUSED(root);
    LV_UNUSED(back_target);
    LV_UNUSED(back_event);
#endif
}
