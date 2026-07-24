/**
 * @file      ui_agent_screen.h
 * @brief     AI Agent Screen - public API
 */
#pragma once
#include <lvgl.h>

typedef enum {
    UI_AGENT_ACTION_CLAUDE_UNINSTALL = 0,
    UI_AGENT_ACTION_CODEX_INSTALL,
    UI_AGENT_ACTION_ITERM_INSTALL,
    UI_AGENT_ACTION_NOTIFIER_INSTALL,
} ui_agent_action_t;

typedef void (*ui_agent_action_cb_t)(ui_agent_action_t action,
                                     const char *tool,
                                     const char *command,
                                     uint32_t request_id);
typedef void (*ui_agent_status_request_cb_t)(uint32_t request_id);

void ui_agent_screen_create(lv_obj_t *parent);
void ui_agent_screen_move(int8_t dir);
void ui_agent_screen_confirm(void);
void ui_agent_screen_append_log(const char *text);
void ui_agent_screen_set_action_cb(ui_agent_action_cb_t cb);
void ui_agent_screen_set_status_request_cb(ui_agent_status_request_cb_t cb);
void ui_agent_screen_request_status(void);
void ui_agent_screen_action_result(uint32_t request_id, bool success);
void ui_agent_screen_set_tool_status(const char *tool,
                                     const char *name,
                                     bool detected,
                                     bool hook_installed,
                                     const char *detail);
int32_t ui_agent_screen_get_daemon_port(void);
lv_group_t *ui_agent_screen_get_group(void);
