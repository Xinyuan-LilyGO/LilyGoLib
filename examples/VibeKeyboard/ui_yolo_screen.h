/**
 * @file      ui_yolo_screen.h
 * @brief     YOLO Screen - public API
 */
#pragma once
#include <lvgl.h>

typedef void (*ui_yolo_config_cb_t)(bool active,
                                    bool notify_auto_allow,
                                    const char *allow_rules,
                                    const char *deny_rules);

void ui_yolo_screen_create(lv_obj_t *parent);
void ui_yolo_screen_move(int8_t dir);
void ui_yolo_screen_confirm(void);
void ui_yolo_screen_submit(void);
void ui_yolo_screen_set_config(bool active,
                               bool notify_auto_allow,
                               const char *allow_rules,
                               const char *deny_rules);
void ui_yolo_screen_set_config_cb(ui_yolo_config_cb_t callback);
void ui_yolo_screen_update_detection(const char *class_name, uint8_t confidence);
lv_group_t *ui_yolo_screen_get_group(void);
