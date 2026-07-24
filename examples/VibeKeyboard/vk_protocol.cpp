#include "vk_protocol.h"

#include <math.h>
#include <string.h>

static const uint8_t TAG_BUTTON_PRESS = 0x01;
static const uint8_t TAG_BUTTON_RELEASE = 0x02;
static const uint8_t TAG_KNOB_ROTATE = 0x03;
static const uint8_t TAG_KNOB_PRESS = 0x04;
static const uint8_t TAG_KNOB_RELEASE = 0x05;
static const uint8_t TAG_PERMISSION_RESPONSE = 0x06;
static const uint8_t TAG_SESSION_SWITCH = 0x07;
static const uint8_t TAG_SETUP_ACTION_REQUEST = 0x08;
static const uint8_t TAG_SETUP_STATUS_REQUEST = 0x09;
static const uint8_t TAG_TIME_SYNC_REQUEST = 0x0A;
static const uint8_t TAG_YOLO_CONFIG_REQUEST = 0x0B;
static const uint8_t TAG_YOLO_CONFIG_SET = 0x0C;

static const uint8_t TAG_SESSION_LIST_UPDATE = 0x81;
static const uint8_t TAG_SESSION_STATUS_CHANGE = 0x82;
static const uint8_t TAG_PERMISSION_REQUEST = 0x83;
static const uint8_t TAG_SET_LED = 0x84;
static const uint8_t TAG_SET_KNOB_RING = 0x85;
static const uint8_t TAG_PLAY_SOUND = 0x86;
static const uint8_t TAG_DISMISS_PERMISSION = 0x87;
static const uint8_t TAG_FRAME_DATA = 0x88;
static const uint8_t TAG_NOTIFICATION_LIST_UPDATE = 0x89;
static const uint8_t TAG_SET_VOLUME = 0x8A;
static const uint8_t TAG_SET_MUTED = 0x8B;
static const uint8_t TAG_SET_SOUND_MAPPING = 0x8C;
static const uint8_t TAG_SETUP_ACTION_RESULT = 0x8D;
static const uint8_t TAG_SESSION_LIST_CLEAR = 0x8E;
static const uint8_t TAG_SESSION_UPSERT = 0x8F;
static const uint8_t TAG_SESSION_REMOVE = 0x90;
static const uint8_t TAG_SETUP_STATUS_UPDATE = 0x91;
static const uint8_t TAG_TIME_SYNC = 0x92;
static const uint8_t TAG_YOLO_CONFIG_UPDATE = 0x93;

struct reader_t {
    const uint8_t *data;
    size_t len;
    size_t off;
};

static bool read_u8(reader_t *r, uint8_t *v)
{
    if (r->off + 1 > r->len) return false;
    *v = r->data[r->off++];
    return true;
}

static bool read_u16(reader_t *r, uint16_t *v)
{
    if (r->off + 2 > r->len) return false;
    *v = (uint16_t)r->data[r->off] | ((uint16_t)r->data[r->off + 1] << 8);
    r->off += 2;
    return true;
}

static bool read_i16(reader_t *r, int16_t *v)
{
    uint16_t raw = 0;
    if (!read_u16(r, &raw)) return false;
    *v = (int16_t)raw;
    return true;
}

static bool read_u32(reader_t *r, uint32_t *v)
{
    if (r->off + 4 > r->len) return false;
    *v = (uint32_t)r->data[r->off] |
         ((uint32_t)r->data[r->off + 1] << 8) |
         ((uint32_t)r->data[r->off + 2] << 16) |
         ((uint32_t)r->data[r->off + 3] << 24);
    r->off += 4;
    return true;
}

static bool read_u64(reader_t *r, uint64_t *v)
{
    if (r->off + 8 > r->len) return false;
    uint64_t out = 0;
    for (uint8_t i = 0; i < 8; i++) {
        out |= ((uint64_t)r->data[r->off + i]) << (8 * i);
    }
    r->off += 8;
    *v = out;
    return true;
}

static bool read_f64(reader_t *r, double *v)
{
    uint64_t bits = 0;
    if (!read_u64(r, &bits)) return false;
    memcpy(v, &bits, sizeof(bits));
    return true;
}

static bool read_string(reader_t *r, char *dst, size_t dst_len)
{
    uint16_t slen = 0;
    if (!read_u16(r, &slen)) return false;
    if (r->off + slen > r->len) return false;
    if (dst && dst_len > 0) {
        size_t n = min((size_t)slen, dst_len - 1);
        memcpy(dst, r->data + r->off, n);
        dst[n] = '\0';
    }
    r->off += slen;
    return true;
}

static bool write_u16(uint8_t *out, size_t out_len, size_t *off, uint16_t v)
{
    if (!out || !off || *off + 2 > out_len) return false;
    out[(*off)++] = v & 0xFF;
    out[(*off)++] = v >> 8;
    return true;
}

static bool write_u32(uint8_t *out, size_t out_len, size_t *off, uint32_t v)
{
    if (!out || !off || *off + 4 > out_len) return false;
    out[(*off)++] = v & 0xFF;
    out[(*off)++] = (v >> 8) & 0xFF;
    out[(*off)++] = (v >> 16) & 0xFF;
    out[(*off)++] = (v >> 24) & 0xFF;
    return true;
}

static bool write_string(uint8_t *out, size_t out_len, size_t *off, const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    if (len > 0xFFFF || !write_u16(out, out_len, off, (uint16_t)len)) return false;
    if (*off + len > out_len) return false;
    memcpy(out + *off, s, len);
    *off += len;
    return true;
}

static void format_k(uint64_t value, char *dst, size_t len)
{
    if (value >= 1000000ULL) {
        snprintf(dst, len, "%llum", (unsigned long long)((value + 500000ULL) / 1000000ULL));
    } else if (value >= 1000ULL) {
        snprintf(dst, len, "%lluk", (unsigned long long)((value + 500ULL) / 1000ULL));
    } else {
        snprintf(dst, len, "%llu", (unsigned long long)value);
    }
}

static void format_context(uint8_t pct, char *dst, size_t len)
{
    if (pct > 0) {
        snprintf(dst, len, "%u%%", pct);
    } else {
        snprintf(dst, len, "--");
    }
}

static void format_cost(double cost, char *dst, size_t len)
{
    if (isnan(cost) || cost <= 0.0) {
        snprintf(dst, len, "$0.00");
    } else {
        snprintf(dst, len, "$%.2f", cost);
    }
}

static void format_time(uint64_t ts, char *dst, size_t len)
{
    if (ts == 0) {
        snprintf(dst, len, "--:--");
        return;
    }
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(dst, len, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

const char *vk_status_text(uint8_t status)
{
    switch (status) {
    case VK_STATUS_THINKING:
        return "THINKING";
    case VK_STATUS_TOOL_USE:
        return "TOOL";
    case VK_STATUS_WRITING:
        return "WRITING";
    case VK_STATUS_DONE:
        return "DONE";
    case VK_STATUS_ERROR:
        return "ERROR";
    case VK_STATUS_PERMISSION_NEEDED:
        return "PERM";
    case VK_STATUS_IDLE:
    default:
        return "IDLE";
    }
}

static bool decode_session(reader_t *r, vk_session_info_t *s)
{
    uint64_t tokens_in = 0;
    uint64_t tokens_out = 0;
    uint64_t ignored_u64 = 0;
    double cost = 0.0;
    uint8_t pct = 0;
    char ignored[4];
    char last_ai[180];

    memset(s, 0, sizeof(*s));
    if (!read_u16(r, &s->id)) return false;
    if (!read_string(r, s->name, sizeof(s->name))) return false;
    if (!read_u8(r, &s->status)) return false;
    uint8_t has_perm = 0;
    if (!read_u8(r, &has_perm)) return false;
    s->has_permission_request = has_perm != 0;
    if (!read_string(r, ignored, sizeof(ignored))) return false;       // source
    if (!read_string(r, ignored, sizeof(ignored))) return false;       // cwd
    if (!read_string(r, ignored, sizeof(ignored))) return false;       // permission_mode
    if (!read_string(r, s->model, sizeof(s->model))) return false;
    if (!read_u64(r, &tokens_in)) return false;
    if (!read_u64(r, &tokens_out)) return false;
    if (!read_f64(r, &cost)) return false;
    if (!read_u8(r, &pct)) return false;
    if (!read_string(r, s->prompt, sizeof(s->prompt))) return false;   // last_message
    if (!read_string(r, last_ai, sizeof(last_ai))) return false;
    if (s->prompt[0] == '\0' && last_ai[0] != '\0') {
        strlcpy(s->prompt, last_ai, sizeof(s->prompt));
    }
    if (!read_string(r, ignored, sizeof(ignored))) return false;       // bundle_id
    if (!read_string(r, ignored, sizeof(ignored))) return false;       // session_tty
    if (!read_u64(r, &ignored_u64)) return false;                      // started_at
    if (!read_u64(r, &ignored_u64)) return false;                      // last_activity

    if (s->name[0] == '\0') snprintf(s->name, sizeof(s->name), "SESSION %u", s->id);
    if (s->model[0] == '\0') snprintf(s->model, sizeof(s->model), "--");
    if (s->prompt[0] == '\0') snprintf(s->prompt, sizeof(s->prompt), "Waiting for activity.");
    format_context(pct, s->context, sizeof(s->context));
    format_cost(cost, s->cost, sizeof(s->cost));
    format_k(tokens_in + tokens_out, s->tokens, sizeof(s->tokens));
    return true;
}

static bool decode_notification(reader_t *r, vk_notification_info_t *n)
{
    uint64_t ts = 0;
    uint8_t read = 0;
    memset(n, 0, sizeof(*n));
    if (!read_u32(r, &n->id)) return false;
    if (!read_u16(r, &n->session_id)) return false;
    if (!read_string(r, n->session_name, sizeof(n->session_name))) return false;
    if (!read_u8(r, &n->status)) return false;
    if (!read_string(r, n->description, sizeof(n->description))) return false;
    if (!read_u64(r, &ts)) return false;
    if (!read_u8(r, &read)) return false;
    n->read = read != 0;
    format_time(ts, n->time, sizeof(n->time));
    return true;
}

static bool decode_setup_tool_status(reader_t *r, vk_setup_tool_status_t *tool)
{
    uint8_t detected = 0;
    uint8_t hook_installed = 0;

    memset(tool, 0, sizeof(*tool));
    if (!read_string(r, tool->id, sizeof(tool->id))) return false;
    if (!read_string(r, tool->name, sizeof(tool->name))) return false;
    if (!read_u8(r, &detected)) return false;
    if (!read_u8(r, &hook_installed)) return false;
    if (!read_string(r, tool->detail, sizeof(tool->detail))) return false;

    tool->detected = detected != 0;
    tool->hook_installed = hook_installed != 0;
    return true;
}

static bool decode_yolo_config(reader_t *r, vk_yolo_config_t *config)
{
    uint8_t active = 0;
    uint8_t notify_auto_allow = 0;
    uint8_t allow_count = 0;
    uint8_t deny_count = 0;

    memset(config, 0, sizeof(*config));
    if (!read_u8(r, &active) ||
        !read_u8(r, &notify_auto_allow) ||
        !read_u8(r, &allow_count)) {
        return false;
    }
    config->active = active != 0;
    config->notify_auto_allow = notify_auto_allow != 0;
    config->allow_count = min(allow_count, (uint8_t)VK_MAX_YOLO_RULES);
    for (uint8_t i = 0; i < allow_count; i++) {
        char ignored[1];
        char *dst = i < VK_MAX_YOLO_RULES ? config->allow[i] : ignored;
        size_t dst_len = i < VK_MAX_YOLO_RULES ? VK_MAX_YOLO_RULE_LENGTH : sizeof(ignored);
        if (!read_string(r, dst, dst_len)) return false;
    }
    if (!read_u8(r, &deny_count)) return false;
    config->deny_count = min(deny_count, (uint8_t)VK_MAX_YOLO_RULES);
    for (uint8_t i = 0; i < deny_count; i++) {
        char ignored[1];
        char *dst = i < VK_MAX_YOLO_RULES ? config->deny[i] : ignored;
        size_t dst_len = i < VK_MAX_YOLO_RULES ? VK_MAX_YOLO_RULE_LENGTH : sizeof(ignored);
        if (!read_string(r, dst, dst_len)) return false;
    }
    return true;
}

bool vk_decode_downlink(const uint8_t *data, size_t len, vk_downlink_event_t *event)
{
    if (!data || len == 0 || !event) return false;
    memset(event, 0, sizeof(*event));

    reader_t r = { data, len, 1 };
    uint8_t tag = data[0];

    switch (tag) {
    case TAG_SESSION_LIST_UPDATE: {
        uint8_t count = 0;
        if (!read_u8(&r, &count)) return false;
        event->type = VK_DOWNLINK_SESSION_LIST;
        event->session_count = min(count, (uint8_t)VK_MAX_SESSIONS);
        for (uint8_t i = 0; i < count; i++) {
            vk_session_info_t tmp;
            vk_session_info_t *dst = i < VK_MAX_SESSIONS ? &event->sessions[i] : &tmp;
            if (!decode_session(&r, dst)) return false;
        }
        if (!read_u8(&r, &event->active_index)) return false;
        if (event->session_count == 0) {
            event->active_index = 0;
        } else if (event->active_index >= event->session_count) {
            event->active_index = event->session_count - 1;
        }
        return true;
    }
    case TAG_SESSION_STATUS_CHANGE:
        event->type = VK_DOWNLINK_SESSION_STATUS;
        return read_u16(&r, &event->session_id) && read_u8(&r, &event->status);
    case TAG_PERMISSION_REQUEST:
        event->type = VK_DOWNLINK_PERMISSION_REQUEST;
        return read_u16(&r, &event->session_id) &&
               read_string(&r, event->action_desc, sizeof(event->action_desc));
    case TAG_DISMISS_PERMISSION:
        event->type = VK_DOWNLINK_DISMISS_PERMISSION;
        return read_u16(&r, &event->session_id);
    case TAG_NOTIFICATION_LIST_UPDATE: {
        uint8_t count = 0;
        if (!read_u8(&r, &count)) return false;
        event->type = VK_DOWNLINK_NOTIFICATION_LIST;
        event->notification_count = min(count, (uint8_t)VK_MAX_NOTIFICATIONS);
        for (uint8_t i = 0; i < count; i++) {
            vk_notification_info_t tmp;
            vk_notification_info_t *dst = i < VK_MAX_NOTIFICATIONS ? &event->notifications[i] : &tmp;
            if (!decode_notification(&r, dst)) return false;
        }
        return true;
    }
    case TAG_PLAY_SOUND:
        event->type = VK_DOWNLINK_PLAY_SOUND;
        return read_u8(&r, &event->sound_type);
    case TAG_SETUP_ACTION_RESULT: {
        uint8_t success = 0;
        event->type = VK_DOWNLINK_SETUP_ACTION_RESULT;
        if (!read_u32(&r, &event->request_id) ||
            !read_u8(&r, &success)) {
            return false;
        }
        event->success = success != 0;
        return true;
    }
    case TAG_SESSION_LIST_CLEAR:
        event->type = VK_DOWNLINK_SESSION_LIST_CLEAR;
        return read_u16(&r, &event->active_session_id);
    case TAG_SESSION_UPSERT: {
        uint8_t active = 0;
        event->type = VK_DOWNLINK_SESSION_UPSERT;
        if (!decode_session(&r, &event->session) ||
            !read_u8(&r, &active)) {
            return false;
        }
        event->session_id = event->session.id;
        event->active = active != 0;
        return true;
    }
    case TAG_SESSION_REMOVE:
        event->type = VK_DOWNLINK_SESSION_REMOVE;
        return read_u16(&r, &event->session_id);
    case TAG_SETUP_STATUS_UPDATE: {
        uint8_t count = 0;
        if (!read_u32(&r, &event->request_id) ||
            !read_u8(&r, &count)) {
            return false;
        }
        event->type = VK_DOWNLINK_SETUP_STATUS_UPDATE;
        event->setup_tool_count = min(count, (uint8_t)VK_MAX_SETUP_TOOLS);
        for (uint8_t i = 0; i < count; i++) {
            vk_setup_tool_status_t tmp;
            vk_setup_tool_status_t *dst = i < VK_MAX_SETUP_TOOLS ?
                                          &event->setup_tools[i] : &tmp;
            if (!decode_setup_tool_status(&r, dst)) return false;
        }
        return true;
    }
    case TAG_TIME_SYNC:
        event->type = VK_DOWNLINK_TIME_SYNC;
        return read_u64(&r, &event->unix_time_ms) &&
               read_i16(&r, &event->utc_offset_minutes);
    case TAG_YOLO_CONFIG_UPDATE:
        event->type = VK_DOWNLINK_YOLO_CONFIG_UPDATE;
        return decode_yolo_config(&r, &event->yolo);
    case TAG_SET_LED:
        return r.off + 5 <= r.len;
    case TAG_SET_KNOB_RING:
        return r.off + 3 <= r.len;
    case TAG_FRAME_DATA: {
        uint16_t ignored = 0;
        uint32_t pixel_len = 0;
        if (!read_u16(&r, &ignored)) return false;
        if (!read_u16(&r, &ignored)) return false;
        if (!read_u32(&r, &pixel_len)) return false;
        return r.off + pixel_len <= r.len;
    }
    case TAG_SET_VOLUME:
        event->type = VK_DOWNLINK_SET_VOLUME;
        return read_u8(&r, &event->volume);
    case TAG_SET_MUTED: {
        uint8_t muted = 0;
        event->type = VK_DOWNLINK_SET_MUTED;
        if (!read_u8(&r, &muted)) return false;
        event->muted = muted != 0;
        return true;
    }
    case TAG_SET_SOUND_MAPPING:
        event->type = VK_DOWNLINK_SET_SOUND_MAPPING;
        return read_u8(&r, &event->sound_type) &&
               read_string(&r, event->sound_id, sizeof(event->sound_id));
    default:
        return false;
    }
}

size_t vk_encode_button_press(uint8_t button, uint8_t *out, size_t out_len)
{
    if (!out || out_len < 2) return 0;
    out[0] = TAG_BUTTON_PRESS;
    out[1] = button;
    return 2;
}

size_t vk_encode_button_release(uint8_t button, uint8_t *out, size_t out_len)
{
    if (!out || out_len < 2) return 0;
    out[0] = TAG_BUTTON_RELEASE;
    out[1] = button;
    return 2;
}

size_t vk_encode_knob_rotate(uint8_t direction, uint8_t steps, uint8_t *out, size_t out_len)
{
    if (!out || out_len < 3) return 0;
    out[0] = TAG_KNOB_ROTATE;
    out[1] = direction;
    out[2] = steps;
    return 3;
}

size_t vk_encode_knob_press(uint8_t *out, size_t out_len)
{
    if (!out || out_len < 1) return 0;
    out[0] = TAG_KNOB_PRESS;
    return 1;
}

size_t vk_encode_knob_release(uint8_t *out, size_t out_len)
{
    if (!out || out_len < 1) return 0;
    out[0] = TAG_KNOB_RELEASE;
    return 1;
}

size_t vk_encode_session_switch(uint16_t session_id, uint8_t *out, size_t out_len)
{
    if (!out || out_len < 3) return 0;
    out[0] = TAG_SESSION_SWITCH;
    out[1] = session_id & 0xFF;
    out[2] = session_id >> 8;
    return 3;
}

size_t vk_encode_permission_response(uint16_t session_id,
                                     uint8_t action,
                                     uint8_t *out,
                                     size_t out_len)
{
    if (!out || out_len < 4) return 0;
    out[0] = TAG_PERMISSION_RESPONSE;
    out[1] = session_id & 0xFF;
    out[2] = session_id >> 8;
    out[3] = action;
    return 4;
}

size_t vk_encode_setup_action_request(uint32_t request_id,
                                      uint8_t action_id,
                                      const char *tool,
                                      const char *command,
                                      uint16_t daemon_port,
                                      uint8_t *out,
                                      size_t out_len)
{
    if (!out || out_len < 10) return 0;

    size_t off = 0;
    out[off++] = TAG_SETUP_ACTION_REQUEST;
    if (!write_u32(out, out_len, &off, request_id)) return 0;
    if (off + 1 > out_len) return 0;
    out[off++] = action_id;
    if (!write_string(out, out_len, &off, tool)) return 0;
    if (!write_string(out, out_len, &off, command)) return 0;
    if (!write_u16(out, out_len, &off, daemon_port)) return 0;
    return off;
}

size_t vk_encode_setup_status_request(uint32_t request_id,
                                      uint16_t daemon_port,
                                      uint8_t *out,
                                      size_t out_len)
{
    if (!out || out_len < 7) return 0;

    size_t off = 0;
    out[off++] = TAG_SETUP_STATUS_REQUEST;
    if (!write_u32(out, out_len, &off, request_id)) return 0;
    if (!write_u16(out, out_len, &off, daemon_port)) return 0;
    return off;
}

size_t vk_encode_time_sync_request(uint8_t *out, size_t out_len)
{
    if (!out || out_len < 1) return 0;
    out[0] = TAG_TIME_SYNC_REQUEST;
    return 1;
}

size_t vk_encode_yolo_config_request(uint8_t *out, size_t out_len)
{
    if (!out || out_len < 1) return 0;
    out[0] = TAG_YOLO_CONFIG_REQUEST;
    return 1;
}

static bool write_rule_lines(const char *text,
                             uint8_t *out,
                             size_t out_len,
                             size_t *off)
{
    if (!text) text = "";
    if (!out || !off || *off >= out_len) return false;

    size_t count_pos = (*off)++;
    uint8_t count = 0;
    const char *line = text;
    while (*line != '\0' && count < VK_MAX_YOLO_RULES) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        while (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            if (len > 0xFFFF || *off + 2 + len > out_len) return false;
            if (!write_u16(out, out_len, off, (uint16_t)len)) return false;
            memcpy(out + *off, line, len);
            *off += len;
            count++;
        }
        if (!end) break;
        line = end + 1;
    }
    out[count_pos] = count;
    return true;
}

size_t vk_encode_yolo_config_set(bool active,
                                 bool notify_auto_allow,
                                 const char *allow_rules,
                                 const char *deny_rules,
                                 uint8_t *out,
                                 size_t out_len)
{
    if (!out || out_len < 5) return 0;
    size_t off = 0;
    out[off++] = TAG_YOLO_CONFIG_SET;
    out[off++] = active ? 1 : 0;
    out[off++] = notify_auto_allow ? 1 : 0;
    if (!write_rule_lines(allow_rules, out, out_len, &off) ||
        !write_rule_lines(deny_rules, out, out_len, &off)) {
        return 0;
    }
    return off;
}
