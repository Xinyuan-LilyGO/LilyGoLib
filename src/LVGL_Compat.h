/**
 * Small LVGL compatibility helpers shared by LilyGoLib applications.
 */
#pragma once

#include <lvgl.h>

/* LVGL 9.6 marks the generic flag functions deprecated, but keeps them for
 * compatibility. Keep the existing LilyGoLib call sites valid on both 9.5
 * and 9.6 while containing the deprecation diagnostic in this wrapper. */
#if defined(LVGL_VERSION_MAJOR) && LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static inline void lilygo_lv_obj_add_flag_compat(lv_obj_t *obj, lv_obj_flag_t flags)
{
    lv_obj_add_flag(obj, flags);
}

static inline void lilygo_lv_obj_remove_flag_compat(lv_obj_t *obj, lv_obj_flag_t flags)
{
    lv_obj_remove_flag(obj, flags);
}

static inline bool lilygo_lv_obj_has_flag_compat(const lv_obj_t *obj, lv_obj_flag_t flags)
{
    return lv_obj_has_flag(obj, flags);
}

static inline lv_obj_t *lilygo_lv_menu_create_compat(lv_obj_t *parent)
{
    return lv_menu_create(parent);
}

static inline lv_obj_t *lilygo_lv_menu_cont_create_compat(lv_obj_t *parent)
{
    return lv_menu_cont_create(parent);
}

static inline void lilygo_lv_menu_set_mode_root_back_button_compat(
    lv_obj_t *obj, lv_menu_mode_root_back_button_t mode)
{
    lv_menu_set_mode_root_back_button(obj, mode);
}

static inline void lilygo_lv_textarea_set_align_compat(lv_obj_t *obj, lv_text_align_t align)
{
    lv_textarea_set_align(obj, align);
}
#pragma GCC diagnostic pop

#define lv_obj_add_flag    lilygo_lv_obj_add_flag_compat
#define lv_obj_remove_flag lilygo_lv_obj_remove_flag_compat
#define lv_obj_has_flag    lilygo_lv_obj_has_flag_compat
#define lv_menu_create     lilygo_lv_menu_create_compat
#define lv_menu_cont_create lilygo_lv_menu_cont_create_compat
#define lv_menu_set_mode_root_back_button lilygo_lv_menu_set_mode_root_back_button_compat
#define lv_textarea_set_align lilygo_lv_textarea_set_align_compat
#endif
