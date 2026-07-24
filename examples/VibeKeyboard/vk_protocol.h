#pragma once

#include <Arduino.h>

#define VK_DEVICE_NAME "VibeKeyboard"
#define VK_MAX_SESSIONS 32
#define VK_MAX_NOTIFICATIONS 32
#define VK_MAX_SETUP_TOOLS 4
#define VK_MAX_YOLO_RULES 16
#define VK_MAX_YOLO_RULE_LENGTH 96

enum vk_button_id_t : uint8_t {
    VK_BUTTON_DELETE = 0,
    VK_BUTTON_CANCEL = 1,
    VK_BUTTON_MODE = 2,
    VK_BUTTON_SESSION = 3,
    VK_BUTTON_SEND = 4,
    VK_BUTTON_VOICE = 5,
};

enum vk_knob_direction_t : uint8_t {
    VK_KNOB_CLOCKWISE = 0,
    VK_KNOB_COUNTER_CLOCKWISE = 1,
};

enum vk_permission_action_t : uint8_t {
    VK_PERMISSION_ALLOW = 0,
    VK_PERMISSION_DENY = 1,
    VK_PERMISSION_ALWAYS = 2,
};

enum vk_session_status_t : uint8_t {
    VK_STATUS_THINKING = 0,
    VK_STATUS_TOOL_USE = 1,
    VK_STATUS_WRITING = 2,
    VK_STATUS_DONE = 3,
    VK_STATUS_ERROR = 4,
    VK_STATUS_IDLE = 5,
    VK_STATUS_PERMISSION_NEEDED = 6,
};

struct vk_session_info_t {
    uint16_t id;
    uint8_t status;
    bool has_permission_request;
    char name[36];
    char model[24];
    char context[10];
    char cost[12];
    char tokens[12];
    char prompt[180];
};

struct vk_notification_info_t {
    uint32_t id;
    uint16_t session_id;
    uint8_t status;
    bool read;
    char session_name[36];
    char description[120];
    char time[8];
};

struct vk_setup_tool_status_t {
    bool detected;
    bool hook_installed;
    char id[24];
    char name[32];
    char detail[80];
};

struct vk_yolo_config_t {
    bool active;
    bool notify_auto_allow;
    uint8_t allow_count;
    uint8_t deny_count;
    char allow[VK_MAX_YOLO_RULES][VK_MAX_YOLO_RULE_LENGTH];
    char deny[VK_MAX_YOLO_RULES][VK_MAX_YOLO_RULE_LENGTH];
};

enum vk_downlink_type_t : uint8_t {
    VK_DOWNLINK_NONE = 0,
    VK_DOWNLINK_SESSION_LIST,
    VK_DOWNLINK_SESSION_STATUS,
    VK_DOWNLINK_PERMISSION_REQUEST,
    VK_DOWNLINK_DISMISS_PERMISSION,
    VK_DOWNLINK_NOTIFICATION_LIST,
    VK_DOWNLINK_PLAY_SOUND,
    VK_DOWNLINK_SET_VOLUME,
    VK_DOWNLINK_SET_MUTED,
    VK_DOWNLINK_SET_SOUND_MAPPING,
    VK_DOWNLINK_SETUP_ACTION_RESULT,
    VK_DOWNLINK_SESSION_LIST_CLEAR,
    VK_DOWNLINK_SESSION_UPSERT,
    VK_DOWNLINK_SESSION_REMOVE,
    VK_DOWNLINK_SETUP_STATUS_UPDATE,
    VK_DOWNLINK_TIME_SYNC,
    VK_DOWNLINK_YOLO_CONFIG_UPDATE,
};

struct vk_downlink_event_t {
    vk_downlink_type_t type;
    uint16_t session_id;
    uint16_t active_session_id;
    uint8_t status;
    uint8_t active_index;
    uint8_t session_count;
    uint8_t notification_count;
    uint8_t setup_tool_count;
    uint8_t sound_type;
    uint8_t volume;
    uint32_t request_id;
    uint64_t unix_time_ms;
    int16_t utc_offset_minutes;
    bool success;
    bool active;
    bool muted;
    vk_session_info_t session;
    vk_session_info_t sessions[VK_MAX_SESSIONS];
    vk_notification_info_t notifications[VK_MAX_NOTIFICATIONS];
    vk_setup_tool_status_t setup_tools[VK_MAX_SETUP_TOOLS];
    vk_yolo_config_t yolo;
    char action_desc[120];
    char sound_id[32];
};

const char *vk_status_text(uint8_t status);
bool vk_decode_downlink(const uint8_t *data, size_t len, vk_downlink_event_t *event);
size_t vk_encode_button_press(uint8_t button, uint8_t *out, size_t out_len);
size_t vk_encode_button_release(uint8_t button, uint8_t *out, size_t out_len);
size_t vk_encode_knob_rotate(uint8_t direction, uint8_t steps, uint8_t *out, size_t out_len);
size_t vk_encode_knob_press(uint8_t *out, size_t out_len);
size_t vk_encode_knob_release(uint8_t *out, size_t out_len);
size_t vk_encode_session_switch(uint16_t session_id, uint8_t *out, size_t out_len);
size_t vk_encode_permission_response(uint16_t session_id,
                                     uint8_t action,
                                     uint8_t *out,
                                     size_t out_len);
size_t vk_encode_setup_action_request(uint32_t request_id,
                                      uint8_t action_id,
                                      const char *tool,
                                      const char *command,
                                      uint16_t daemon_port,
                                      uint8_t *out,
                                      size_t out_len);
size_t vk_encode_setup_status_request(uint32_t request_id,
                                      uint16_t daemon_port,
                                      uint8_t *out,
                                      size_t out_len);
size_t vk_encode_time_sync_request(uint8_t *out, size_t out_len);
size_t vk_encode_yolo_config_request(uint8_t *out, size_t out_len);
size_t vk_encode_yolo_config_set(bool active,
                                 bool notify_auto_allow,
                                 const char *allow_rules,
                                 const char *deny_rules,
                                 uint8_t *out,
                                 size_t out_len);
