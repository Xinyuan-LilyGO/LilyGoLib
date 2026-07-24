/**
 * @file      VibeKeyboard.ino
 * @brief     Multi-screen LVGL demo for T-LoRa-Pager with modal navigation
 *
 * Main Screens (cycle with encoder center / CAP):
 *   0 - Vibe Keyboard Screen (main hub)
 *   1 - Setup Screen
 *   2 - AI Agent Screen
 *   3 - Sound Screen
 *   4 - YOLO Screen
 *   5 - Keys Screen
 *   6 - About Screen
 *
 * Modal Screens:
 *   Select Session  - encoder center from Vibe Keyboard
 *   Notify          - Fn key from Vibe Keyboard
 *   Notify Detail   - encoder center from Notify
 *
 * Key map follows the Ardot Bottom Bar labels on each screen:
 *   ↑ / ↓      - scroll or move current selection
 *   ← / BS/ESC - back where the current Bottom Bar shows BACK
 *   ● / ENTER  - confirm where the current Bottom Bar shows CONFIRM
 *   Fn         - Notify from Vibe Keyboard
 *   SPC        - Voice on Vibe Keyboard, ALLOW on Notify Detail
 *   CAP        - Setup from Vibe Keyboard
 *   x / ENTER  - DENY on Notify Detail
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <NimBLEDevice.h>
#include <time.h>
#include <sys/time.h>

#include "ui_vibe_keyboard.h"
#include "vibe_input.h"
#include "ui_select_session.h"
#include "ui_notify_screen.h"
#include "ui_notify_detail_screen.h"
#include "ui_setup_screen.h"
#include "ui_agent_screen.h"
#include "ui_sound_screen.h"
#include "ui_yolo_screen.h"
#include "ui_keys_screen.h"
#include "ui_about_screen.h"
#include "vk_protocol.h"
#include "vk_sound.h"

#define SCREEN_COUNT      7

enum ModalScreen {
    MODAL_NONE = 0,
    MODAL_SELECT_SESSION,
    MODAL_NOTIFY,
    MODAL_NOTIFY_DETAIL
};

static lv_obj_t   *screens[SCREEN_COUNT] = {};
static int8_t      current_screen        = 0;
static ModalScreen active_modal          = MODAL_NONE;
static lv_group_t *screen_groups[SCREEN_COUNT] = {};

static lv_obj_t *scr_select_session = NULL;
static lv_obj_t *scr_notify         = NULL;
static lv_obj_t *scr_notify_detail  = NULL;

static NimBLEServer *s_vibe_server = NULL;
static NimBLECharacteristic *s_event_char = NULL;

#define VK_DOWNLINK_QUEUE_DEPTH 8
#define VK_DOWNLINK_MAX_PACKET  2048
#define VK_DOWNLINK_LOG_PREVIEW 32
#define VK_TIME_SYNC_RETRY_MS    (60UL * 1000UL)
#define VK_TIME_SYNC_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)

// Keep enabled while checking multi-row layout/scrolling on the device.
// A real BLE notification snapshot replaces these entries.
#ifndef VK_NOTIFY_SCREEN_DEMO
#define VK_NOTIFY_SCREEN_DEMO 1
#endif

struct vk_raw_downlink_t {
    uint16_t len;
    uint8_t data[VK_DOWNLINK_MAX_PACKET];
};

static portMUX_TYPE s_downlink_mux = portMUX_INITIALIZER_UNLOCKED;
static vk_raw_downlink_t s_downlink_queue[VK_DOWNLINK_QUEUE_DEPTH];
static uint8_t s_downlink_head = 0;
static uint8_t s_downlink_tail = 0;
static uint32_t s_downlink_drop_count = 0;
static uint32_t s_downlink_reject_count = 0;
static uint32_t s_downlink_reject_len = 0;
static vk_downlink_event_t s_downlink_decode_event;

static vk_session_info_t s_sessions[VK_MAX_SESSIONS];
static uint8_t s_session_count = 0;
static uint16_t s_selected_session_id = 0;
static bool s_applying_downlink = false;
static vk_notification_info_t s_notifications[VK_MAX_NOTIFICATIONS];
static uint8_t s_notification_count = 0;
static uint16_t s_detail_session_id = 0;
static uint16_t s_resolved_permission_sessions[VK_MAX_NOTIFICATIONS];
static uint8_t s_resolved_permission_count = 0;
static bool s_notify_modal_pending = false;
static bool s_clock_synced = false;
static int16_t s_utc_offset_minutes = 0;
static bool s_time_request_scheduled = false;
static uint32_t s_next_time_request_at = 0;

// ── forward declarations ───────────────────────────────────────────────
static lv_group_t *group_for_screen(int8_t idx);
static void create_screen_key_group(int8_t idx);
static void load_screen(int8_t idx, lv_scr_load_anim_t anim);
static void open_modal(ModalScreen modal, lv_obj_t *scr, lv_group_t *grp);
static void close_modal(void);
static void handle_current_back(void);
static void handle_current_move(int8_t dir);
static void handle_current_confirm(void);
static void screen_key_event_cb(lv_event_t *e);
static void on_agent_action(ui_agent_action_t action,
                            const char *tool,
                            const char *command,
                            uint32_t request_id);
static void process_agent_remote_rx(void);
static void begin_vibe_ble(void);
static void sync_status_bar_ui(void);
static void process_vibe_downlinks(void);
static void refresh_session_ui(void);
static void refresh_notification_ui(void);
static void open_notify_modal_if_needed(void);
static void send_button_press(uint8_t button);
static void send_button_release(uint8_t button);
static void send_session_switch_by_index(int index);
static void send_permission_response(uint16_t session_id, uint8_t action);
static bool send_setup_action(uint32_t request_id,
                              uint8_t action_id,
                              const char *tool,
                              const char *command,
                              uint16_t daemon_port);
static bool send_setup_status_request(uint32_t request_id, uint16_t daemon_port);
static bool send_time_sync_request(void);
static bool send_yolo_config_request(void);
static bool send_yolo_config(bool active,
                             bool notify_auto_allow,
                             const char *allow_rules,
                             const char *deny_rules);
static void on_yolo_config_changed(bool active,
                                   bool notify_auto_allow,
                                   const char *allow_rules,
                                   const char *deny_rules);
static bool apply_time_sync(uint64_t unix_time_ms, int16_t utc_offset_minutes);
static void on_agent_status_request(uint32_t request_id);
static void select_session_by_id(uint16_t session_id, bool fallback);
static void clear_sessions(uint16_t active_session_id);
static void upsert_session(const vk_session_info_t *session, bool active);
static void remove_session(uint16_t session_id);
static void dismiss_permission(uint16_t session_id);
static bool is_permission_resolved(uint16_t session_id);
static void mark_permission_resolved(uint16_t session_id);
static void clear_permission_resolved(uint16_t session_id);

static void on_keyboard_feedback(void *args)
{
    (void)args;
    vk_sound_play_event(VK_SOUND_CLICK);
}

// ── Modal helpers ──────────────────────────────────────────────────────

static void open_modal(ModalScreen modal, lv_obj_t *scr, lv_group_t *grp)
{
    active_modal = modal;
    lv_set_default_group(grp);
    if (modal == MODAL_SELECT_SESSION) {
        ui_select_session_focus_current();
    }
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void close_modal(void)
{
    active_modal = MODAL_NONE;
    if (s_notify_modal_pending && s_notification_count > 0) {
        s_notify_modal_pending = false;
        open_modal(MODAL_NOTIFY, scr_notify, ui_notify_screen_get_group());
        return;
    }
    lv_set_default_group(group_for_screen(current_screen));
    lv_screen_load_anim(screens[current_screen], LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

// ── Notify Detail callbacks ────────────────────────────────────────────

static void on_notify_detail_back(void)
{
    active_modal = MODAL_NOTIFY;
    lv_set_default_group(ui_notify_screen_get_group());
    lv_screen_load_anim(scr_notify, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

static void resolve_notify_detail(uint8_t action)
{
    if (s_detail_session_id != 0) {
        uint16_t resolved_session_id = s_detail_session_id;
        send_permission_response(resolved_session_id, action);
        mark_permission_resolved(resolved_session_id);
        dismiss_permission(resolved_session_id);
        refresh_session_ui();
        refresh_notification_ui();
    }

    if (s_notification_count == 0) {
        close_modal();
        return;
    }

    active_modal = MODAL_NOTIFY;
    lv_set_default_group(ui_notify_screen_get_group());
    lv_screen_load_anim(scr_notify, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

static void on_notify_detail_allow(void)
{
    resolve_notify_detail(VK_PERMISSION_ALLOW);
}

static void on_notify_detail_deny(void)
{
    resolve_notify_detail(VK_PERMISSION_DENY);
}

// ── Notify Screen: open detail when encoder confirms ──────────────────

static void on_notify_select(uint8_t row)
{
    if (row >= s_notification_count) return;
    vk_notification_info_t *n = &s_notifications[row];
    s_detail_session_id = n->session_id;
    ui_notify_detail_screen_set_data("PERM REQUEST", "vk", n->session_name,
                                     n->time, "ACTION", n->description, "Tool");
    active_modal = MODAL_NOTIFY_DETAIL;
    lv_set_default_group(ui_notify_detail_screen_get_group());
    lv_screen_load_anim(scr_notify_detail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// ── Select Session confirm callback ───────────────────────────────────

static void on_select_session_confirm(int sel)
{
    if (sel < 0 || sel >= s_session_count) {
        close_modal();
        return;
    }
    ui_vibe_keyboard_select(sel);
    send_session_switch_by_index(sel);
    close_modal();
}

// ── Vibe protocol BLE bridge ─────────────────────────────────────────

static void print_ble_received_message(const uint8_t *data, size_t len)
{
    size_t preview_len = len < VK_DOWNLINK_LOG_PREVIEW ? len : VK_DOWNLINK_LOG_PREVIEW;
    Serial.printf("[BLE RX] len=%u", (unsigned)len);
    if (data && len > 0) {
        Serial.printf(" tag=0x%02X", (unsigned)data[0]);
    }

    Serial.printf(" text=\"");
    for (size_t i = 0; data && i < preview_len; i++) {
        char c = (char)data[i];
        if (c >= 0x20 && c <= 0x7E) {
            Serial.printf("%c", c);
        } else {
            Serial.printf(".");
        }
    }

    Serial.printf("\" hex=");
    for (size_t i = 0; data && i < preview_len; i++) {
        Serial.printf("%02X", (unsigned)data[i]);
        if (i + 1 < preview_len) {
            Serial.printf(" ");
        }
    }
    if (preview_len < len) Serial.printf(" ...");
    Serial.printf("\n");
}

class VibeServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        (void)server;
        (void)connInfo;
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        NimBLEDevice::startAdvertising();
    }
};

class VibeCmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
    {
        (void)connInfo;
        std::string value = characteristic->getValue();
        if (value.empty() || value.size() > VK_DOWNLINK_MAX_PACKET) {
            portENTER_CRITICAL(&s_downlink_mux);
            s_downlink_reject_count++;
            s_downlink_reject_len = (uint32_t)value.size();
            portEXIT_CRITICAL(&s_downlink_mux);
            return;
        }

        portENTER_CRITICAL(&s_downlink_mux);
        uint8_t next = (uint8_t)((s_downlink_head + 1) % VK_DOWNLINK_QUEUE_DEPTH);
        if (next != s_downlink_tail) {
            vk_raw_downlink_t *slot = &s_downlink_queue[s_downlink_head];
            slot->len = (uint16_t)value.size();
            memcpy(slot->data, value.data(), value.size());
            s_downlink_head = next;
        } else {
            s_downlink_drop_count++;
        }
        portEXIT_CRITICAL(&s_downlink_mux);
    }
};

static VibeServerCallbacks s_server_callbacks;
static VibeCmdCallbacks s_cmd_callbacks;

static void begin_vibe_ble(void)
{
    NimBLEDevice::init(VK_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    s_vibe_server = NimBLEDevice::createServer();
    s_vibe_server->setCallbacks(&s_server_callbacks, false);

    NimBLEService *service = s_vibe_server->createService("5a5f5b5e-1234-5678-abcd-000000000001");
    NimBLECharacteristic *cmd = service->createCharacteristic(
        "5a5f5b5e-1234-5678-abcd-000000000002",
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    s_event_char = service->createCharacteristic(
        "5a5f5b5e-1234-5678-abcd-000000000003",
        NIMBLE_PROPERTY::NOTIFY);
    cmd->setCallbacks(&s_cmd_callbacks);
    service->start();

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(service->getUUID());
    advertising->setName(VK_DEVICE_NAME);
    advertising->start();
}

static bool send_uplink(const uint8_t *payload, size_t len)
{
    if (!s_vibe_server || s_vibe_server->getConnectedCount() == 0 ||
        !s_event_char || !payload || len == 0) {
        return false;
    }
    s_event_char->notify(payload, len);
    return true;
}

static void sync_status_bar_ui(void)
{
    static bool last_connected = false;
    static bool ble_initialized = false;
    static bool time_initialized = false;
    static uint32_t last_time_update = 0;
    bool connected = s_vibe_server && s_vibe_server->getConnectedCount() > 0;

    if (!ble_initialized || connected != last_connected) {
        ui_vibe_keyboard_set_ble_connected(connected);
        last_connected = connected;
        ble_initialized = true;

        s_time_request_scheduled = connected;
        s_next_time_request_at = millis() + 1000;
    }

    uint32_t now = millis();
    if (connected && s_time_request_scheduled &&
        (int32_t)(now - s_next_time_request_at) >= 0) {
        if (send_time_sync_request()) {
            uint32_t delay_ms = s_clock_synced ?
                                VK_TIME_SYNC_INTERVAL_MS : VK_TIME_SYNC_RETRY_MS;
            s_next_time_request_at = now + delay_ms;
        }
    }

    if (!time_initialized || now - last_time_update >= 1000) {
        struct tm timeinfo;
        char time_text[6] = "--:--";
        if (s_clock_synced) {
            time_t local_epoch = time(NULL) + (time_t)s_utc_offset_minutes * 60;
            gmtime_r(&local_epoch, &timeinfo);
            strftime(time_text, sizeof(time_text), "%H:%M", &timeinfo);
        } else if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
            instance.rtc.getDateTime(&timeinfo);
            strftime(time_text, sizeof(time_text), "%H:%M", &timeinfo);
        }
        ui_vibe_keyboard_update(time_text, NULL, NULL, NULL, NULL, NULL, NULL);
        last_time_update = now;
        time_initialized = true;
    }
}

static void send_button_press(uint8_t button)
{
    uint8_t payload[4];
    size_t len = vk_encode_button_press(button, payload, sizeof(payload));
    send_uplink(payload, len);
}

static void send_button_release(uint8_t button)
{
    uint8_t payload[4];
    size_t len = vk_encode_button_release(button, payload, sizeof(payload));
    send_uplink(payload, len);
}

static void send_session_switch_by_index(int index)
{
    if (s_applying_downlink) return;
    if (index < 0 || index >= s_session_count) return;
    uint16_t session_id = s_sessions[index].id;
    if (session_id == 0 || session_id == s_selected_session_id) return;
    s_selected_session_id = session_id;
    uint8_t payload[4];
    size_t len = vk_encode_session_switch(session_id, payload, sizeof(payload));
    send_uplink(payload, len);
}

static void send_permission_response(uint16_t session_id, uint8_t action)
{
    uint8_t payload[6];
    size_t len = vk_encode_permission_response(session_id, action, payload, sizeof(payload));
    send_uplink(payload, len);
}

static bool send_setup_action(uint32_t request_id,
                              uint8_t action_id,
                              const char *tool,
                              const char *command,
                              uint16_t daemon_port)
{
    uint8_t payload[192];
    size_t len = vk_encode_setup_action_request(request_id,
                                                action_id,
                                                tool,
                                                command,
                                                daemon_port,
                                                payload,
                                                sizeof(payload));
    return send_uplink(payload, len);
}

static bool send_setup_status_request(uint32_t request_id, uint16_t daemon_port)
{
    uint8_t payload[8];
    size_t len = vk_encode_setup_status_request(request_id,
                                                daemon_port,
                                                payload,
                                                sizeof(payload));
    return send_uplink(payload, len);
}

static bool send_time_sync_request(void)
{
    uint8_t payload[1];
    size_t len = vk_encode_time_sync_request(payload, sizeof(payload));
    return send_uplink(payload, len);
}

static bool send_yolo_config_request(void)
{
    uint8_t payload[1];
    size_t len = vk_encode_yolo_config_request(payload, sizeof(payload));
    return send_uplink(payload, len);
}

static bool send_yolo_config(bool active,
                             bool notify_auto_allow,
                             const char *allow_rules,
                             const char *deny_rules)
{
    uint8_t payload[512];
    size_t len = vk_encode_yolo_config_set(active,
                                           notify_auto_allow,
                                           allow_rules,
                                           deny_rules,
                                           payload,
                                           sizeof(payload));
    if (len == 0) {
        Serial.println("[YOLO] config is too large to send");
        return false;
    }
    return send_uplink(payload, len);
}

static void on_yolo_config_changed(bool active,
                                   bool notify_auto_allow,
                                   const char *allow_rules,
                                   const char *deny_rules)
{
    if (s_applying_downlink) return;
    if (!send_yolo_config(active, notify_auto_allow, allow_rules, deny_rules)) {
        Serial.printf("{\"type\":\"yolo_config\",\"active\":%s,"
                      "\"notify_auto_allow\":%s}\n",
                      active ? "true" : "false",
                      notify_auto_allow ? "true" : "false");
    }
}

static void join_yolo_rules(const char rules[][VK_MAX_YOLO_RULE_LENGTH],
                            uint8_t count,
                            char *out,
                            size_t out_len)
{
    if (!out || out_len == 0) return;
    out[0] = '\0';
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) strlcat(out, "\n", out_len);
        strlcat(out, rules[i], out_len);
    }
}

static bool apply_time_sync(uint64_t unix_time_ms, int16_t utc_offset_minutes)
{
    uint64_t seconds = unix_time_ms / 1000ULL;
    time_t epoch_seconds = (time_t)seconds;
    if (epoch_seconds < 0 || (uint64_t)epoch_seconds != seconds) {
        return false;
    }

    struct timeval tv;
    tv.tv_sec = epoch_seconds;
    tv.tv_usec = (suseconds_t)((unix_time_ms % 1000ULL) * 1000ULL);
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }

    s_utc_offset_minutes = utc_offset_minutes;
    s_clock_synced = true;
    s_time_request_scheduled = true;
    s_next_time_request_at = millis() + VK_TIME_SYNC_INTERVAL_MS;

    if (instance.getDeviceProbe() & HW_RTC_ONLINE) {
        struct tm utc_time;
        if (gmtime_r(&epoch_seconds, &utc_time) &&
            utc_time.tm_year >= 100 && utc_time.tm_year <= 199) {
            instance.rtc.setDateTime((uint16_t)(utc_time.tm_year + 1900),
                                     (uint8_t)(utc_time.tm_mon + 1),
                                     (uint8_t)utc_time.tm_mday,
                                     (uint8_t)utc_time.tm_hour,
                                     (uint8_t)utc_time.tm_min,
                                     (uint8_t)utc_time.tm_sec);
        }
    }

    Serial.printf("[TIME] synced unix_ms=%llu utc_offset_min=%d\n",
                  (unsigned long long)unix_time_ms,
                  (int)utc_offset_minutes);
    return true;
}

static int find_session_index(uint16_t session_id)
{
    for (uint8_t i = 0; i < s_session_count; i++) {
        if (s_sessions[i].id == session_id) return i;
    }
    return -1;
}

static void select_session_by_id(uint16_t session_id, bool fallback)
{
    int idx = find_session_index(session_id);
    if (idx < 0 && fallback) {
        idx = s_session_count > 0 ? 0 : -1;
    }
    if (idx < 0) return;

    s_selected_session_id = s_sessions[idx].id;
    ui_vibe_keyboard_select(idx);
    ui_select_session_select(idx);
}

static void clear_sessions(uint16_t active_session_id)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    s_session_count = 0;
    s_selected_session_id = active_session_id;
    refresh_session_ui();
}

static void upsert_session(const vk_session_info_t *session, bool active)
{
    if (!session || session->id == 0) return;

    int idx = find_session_index(session->id);
    if (idx < 0) {
        if (s_session_count >= VK_MAX_SESSIONS) {
            return;
        }
        idx = s_session_count++;
    }

    s_sessions[idx] = *session;
    if (active || s_selected_session_id == session->id ||
        (s_selected_session_id == 0 && s_session_count == 1)) {
        s_selected_session_id = session->id;
    }

    refresh_session_ui();
    select_session_by_id(s_selected_session_id, true);
}

static void remove_session(uint16_t session_id)
{
    int idx = find_session_index(session_id);
    if (idx < 0) return;

    bool removed_active = s_selected_session_id == session_id;
    for (uint8_t i = (uint8_t)idx; i + 1 < s_session_count; i++) {
        s_sessions[i] = s_sessions[i + 1];
    }
    s_session_count--;
    memset(&s_sessions[s_session_count], 0, sizeof(s_sessions[s_session_count]));

    if (removed_active) {
        s_selected_session_id = s_session_count > 0 ? s_sessions[0].id : 0;
    }

    refresh_session_ui();
    select_session_by_id(s_selected_session_id, true);
}

#if VK_NOTIFY_SCREEN_DEMO
static void seed_notification_demo(void)
{
    static const struct {
        uint16_t session_id;
        uint8_t status;
        const char *session_name;
        const char *description;
        const char *time;
    } demo[] = {
        { 101, VK_STATUS_PERMISSION_NEEDED, "BUILD AGENT", "Allow running the PlatformIO build", "10:21" },
        { 102, VK_STATUS_PERMISSION_NEEDED, "CODE REVIEW", "Review changes in ui_notify_screen.cpp", "10:22" },
        { 103, VK_STATUS_PERMISSION_NEEDED, "TEST RUNNER", "Allow executing the notification tests", "10:23" },
        { 104, VK_STATUS_ERROR,             "DEVICE LOG", "Serial monitor reported a test error", "10:24" },
        { 105, VK_STATUS_PERMISSION_NEEDED, "DOCS AGENT", "Allow updating the example README", "10:25" },
        { 106, VK_STATUS_PERMISSION_NEEDED, "RELEASE", "Confirm the generated firmware artifact", "10:26" },
    };

    static_assert(sizeof(demo) / sizeof(demo[0]) <= VK_MAX_NOTIFICATIONS,
                  "Notification demo exceeds VK_MAX_NOTIFICATIONS");

    s_notification_count = (uint8_t)(sizeof(demo) / sizeof(demo[0]));
    for (uint8_t i = 0; i < s_notification_count; i++) {
        vk_notification_info_t *n = &s_notifications[i];
        memset(n, 0, sizeof(*n));
        n->id = 1000U + i;
        n->session_id = demo[i].session_id;
        n->status = demo[i].status;
        lv_snprintf(n->session_name, sizeof(n->session_name), "%s", demo[i].session_name);
        lv_snprintf(n->description, sizeof(n->description), "%s", demo[i].description);
        lv_snprintf(n->time, sizeof(n->time), "%s", demo[i].time);
    }

    Serial.printf("[NOTIFY DEMO] seeded %u notifications\n",
                  (unsigned)s_notification_count);
}
#endif

static bool is_permission_resolved(uint16_t session_id)
{
    for (uint8_t i = 0; i < s_resolved_permission_count; i++) {
        if (s_resolved_permission_sessions[i] == session_id) return true;
    }
    return false;
}

static void mark_permission_resolved(uint16_t session_id)
{
    if (session_id == 0 || is_permission_resolved(session_id)) return;
    if (s_resolved_permission_count >= VK_MAX_NOTIFICATIONS) {
        memmove(&s_resolved_permission_sessions[0],
                &s_resolved_permission_sessions[1],
                sizeof(s_resolved_permission_sessions[0]) * (VK_MAX_NOTIFICATIONS - 1));
        s_resolved_permission_count = VK_MAX_NOTIFICATIONS - 1;
    }
    s_resolved_permission_sessions[s_resolved_permission_count++] = session_id;
}

static void clear_permission_resolved(uint16_t session_id)
{
    for (uint8_t i = 0; i < s_resolved_permission_count; i++) {
        if (s_resolved_permission_sessions[i] != session_id) continue;
        for (uint8_t j = i; j + 1 < s_resolved_permission_count; j++) {
            s_resolved_permission_sessions[j] = s_resolved_permission_sessions[j + 1];
        }
        s_resolved_permission_count--;
        s_resolved_permission_sessions[s_resolved_permission_count] = 0;
        return;
    }
}

static bool upsert_permission_notification(uint16_t session_id, const char *desc)
{
    int idx = -1;
    bool is_new = false;
    for (uint8_t i = 0; i < s_notification_count; i++) {
        if (s_notifications[i].session_id == session_id) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (s_notification_count >= VK_MAX_NOTIFICATIONS) {
            idx = VK_MAX_NOTIFICATIONS - 1;
        } else {
            idx = s_notification_count++;
        }
        is_new = true;
    }

    vk_notification_info_t *n = &s_notifications[idx];
    memset(n, 0, sizeof(*n));
    n->id = millis();
    n->session_id = session_id;
    n->status = VK_STATUS_PERMISSION_NEEDED;
    snprintf(n->time, sizeof(n->time), "%02u:%02u",
             (unsigned)((millis() / 3600000UL) % 24),
             (unsigned)((millis() / 60000UL) % 60));
    int sidx = find_session_index(session_id);
    if (sidx >= 0) {
        lv_snprintf(n->session_name, sizeof(n->session_name), "%s", s_sessions[sidx].name);
        s_sessions[sidx].has_permission_request = true;
        s_sessions[sidx].status = VK_STATUS_PERMISSION_NEEDED;
    } else {
        lv_snprintf(n->session_name, sizeof(n->session_name), "SESSION %u", session_id);
    }
    lv_snprintf(n->description, sizeof(n->description), "%s", desc ? desc : "Permission request");
    return is_new;
}

static void dismiss_permission(uint16_t session_id)
{
    for (uint8_t i = 0; i < s_session_count; i++) {
        if (s_sessions[i].id == session_id) {
            s_sessions[i].has_permission_request = false;
            if (s_sessions[i].status == VK_STATUS_PERMISSION_NEEDED) {
                s_sessions[i].status = VK_STATUS_IDLE;
            }
        }
    }
    for (uint8_t i = 0; i < s_notification_count; i++) {
        if (s_notifications[i].session_id != session_id) continue;
        for (uint8_t j = i; j + 1 < s_notification_count; j++) {
            s_notifications[j] = s_notifications[j + 1];
        }
        s_notification_count--;
        memset(&s_notifications[s_notification_count], 0,
               sizeof(s_notifications[s_notification_count]));
        break;
    }
    if (s_detail_session_id == session_id) s_detail_session_id = 0;
}

static void refresh_session_ui(void)
{
    uint8_t main_count = s_session_count ? s_session_count : 1;
    ui_vibe_keyboard_set_session_count(main_count);
    ui_select_session_set_session_count(s_session_count);

    for (uint8_t i = 0; i < s_session_count; i++) {
        ui_vibe_keyboard_set_session(i,
                                     s_sessions[i].name,
                                     s_sessions[i].model,
                                     s_sessions[i].context,
                                     s_sessions[i].cost,
                                     s_sessions[i].tokens,
                                     vk_status_text(s_sessions[i].status),
                                     s_sessions[i].prompt);
        ui_select_session_set_row(i,
                                  s_sessions[i].status == VK_STATUS_ERROR ? "err" : "ok",
                                  s_sessions[i].status != VK_STATUS_ERROR,
                                  s_sessions[i].name,
                                  vk_status_text(s_sessions[i].status),
                                  s_sessions[i].prompt);
    }

    if (s_session_count == 0) {
        ui_vibe_keyboard_set_session(0, "NO SESSION", "--", "--", "$0.00",
                                     "0", "IDLE", "Waiting for daemon session data.");
    }
}

static void refresh_notification_ui(void)
{
    char count_buf[24];
    snprintf(count_buf, sizeof(count_buf), "%u in %u sessions",
             (unsigned)s_notification_count, (unsigned)s_notification_count);
    ui_notify_screen_set_count(count_buf);

    for (uint8_t i = 0; i < VK_MAX_NOTIFICATIONS; i++) {
        if (i < s_notification_count) {
            ui_notify_screen_set_row(i,
                                     s_notifications[i].status == VK_STATUS_ERROR ? "err" : "ok",
                                     s_notifications[i].status != VK_STATUS_ERROR,
                                     s_notifications[i].session_name,
                                     s_notifications[i].description,
                                     "(1)",
                                     i == ui_notify_screen_get_selected());
        } else {
            ui_notify_screen_set_row(i, "ok", true, "", "", "", i == ui_notify_screen_get_selected());
        }
    }
}

static void open_notify_modal_if_needed(void)
{
    if (s_notification_count == 0) {
        s_notify_modal_pending = false;
        return;
    }

    if (active_modal != MODAL_NONE) {
        if (active_modal != MODAL_NOTIFY && active_modal != MODAL_NOTIFY_DETAIL) {
            s_notify_modal_pending = true;
        }
        return;
    }

    s_notify_modal_pending = false;
    open_modal(MODAL_NOTIFY, scr_notify, ui_notify_screen_get_group());
}

static void apply_downlink_event(const vk_downlink_event_t *event)
{
    switch (event->type) {
    case VK_DOWNLINK_SESSION_LIST:
        s_session_count = event->session_count;
        for (uint8_t i = 0; i < s_session_count; i++) {
            s_sessions[i] = event->sessions[i];
        }
        if (s_session_count > 0 && event->active_index < s_session_count) {
            s_selected_session_id = s_sessions[event->active_index].id;
        } else {
            s_selected_session_id = 0;
        }
        refresh_session_ui();
        select_session_by_id(s_selected_session_id, true);
        break;
    case VK_DOWNLINK_SESSION_STATUS: {
        int idx = find_session_index(event->session_id);
        if (idx >= 0) {
            s_sessions[idx].status = event->status;
            refresh_session_ui();
            select_session_by_id(s_selected_session_id, true);
        }
        break;
    }
    case VK_DOWNLINK_PERMISSION_REQUEST: {
        clear_permission_resolved(event->session_id);
        bool is_new_notification =
            upsert_permission_notification(event->session_id, event->action_desc);
        refresh_session_ui();
        refresh_notification_ui();
        if (is_new_notification) open_notify_modal_if_needed();
        break;
    }
    case VK_DOWNLINK_DISMISS_PERMISSION:
        dismiss_permission(event->session_id);
        refresh_session_ui();
        refresh_notification_ui();
        break;
    case VK_DOWNLINK_NOTIFICATION_LIST: {
        uint8_t filtered_count = 0;
        memset(s_notifications, 0, sizeof(s_notifications));
        for (uint8_t i = 0; i < event->notification_count; i++) {
            if (is_permission_resolved(event->notifications[i].session_id)) continue;
            s_notifications[filtered_count++] = event->notifications[i];
        }
        s_notification_count = filtered_count;
        Serial.printf("[BLE RX] notifications=%u shown=%u\n",
                      (unsigned)event->notification_count,
                      (unsigned)s_notification_count);
        refresh_notification_ui();
        // Periodic snapshots update the list without stealing the active screen.
        break;
    }
    case VK_DOWNLINK_PLAY_SOUND:
        vk_sound_play_event(event->sound_type);
        break;
    case VK_DOWNLINK_SET_VOLUME:
        vk_sound_set_volume(event->volume);
        ui_sound_screen_refresh();
        break;
    case VK_DOWNLINK_SET_MUTED:
        vk_sound_set_muted(event->muted);
        ui_sound_screen_refresh();
        break;
    case VK_DOWNLINK_SET_SOUND_MAPPING:
        if (vk_sound_set_mapping_id(event->sound_type, event->sound_id)) {
            ui_sound_screen_refresh();
        }
        break;
    case VK_DOWNLINK_SETUP_ACTION_RESULT:
        ui_agent_screen_action_result(event->request_id, event->success);
        break;
    case VK_DOWNLINK_SETUP_STATUS_UPDATE:
        for (uint8_t i = 0; i < event->setup_tool_count; i++) {
            const vk_setup_tool_status_t *tool = &event->setup_tools[i];
            ui_agent_screen_set_tool_status(tool->id,
                                            tool->name,
                                            tool->detected,
                                            tool->hook_installed,
                                            tool->detail);
        }
        break;
    case VK_DOWNLINK_SESSION_LIST_CLEAR:
        clear_sessions(event->active_session_id);
        break;
    case VK_DOWNLINK_SESSION_UPSERT:
        upsert_session(&event->session, event->active);
        break;
    case VK_DOWNLINK_SESSION_REMOVE:
        remove_session(event->session_id);
        break;
    case VK_DOWNLINK_TIME_SYNC:
        if (!apply_time_sync(event->unix_time_ms, event->utc_offset_minutes)) {
            Serial.println("[TIME] rejected invalid time sync");
        }
        break;
    case VK_DOWNLINK_YOLO_CONFIG_UPDATE: {
        char allow_rules[512];
        char deny_rules[512];
        join_yolo_rules(event->yolo.allow,
                        event->yolo.allow_count,
                        allow_rules,
                        sizeof(allow_rules));
        join_yolo_rules(event->yolo.deny,
                        event->yolo.deny_count,
                        deny_rules,
                        sizeof(deny_rules));
        ui_yolo_screen_set_config(event->yolo.active,
                                  event->yolo.notify_auto_allow,
                                  allow_rules,
                                  deny_rules);
        Serial.printf("[YOLO] synced active=%u notify_auto_allow=%u allow=%u deny=%u\n",
                      event->yolo.active ? 1U : 0U,
                      event->yolo.notify_auto_allow ? 1U : 0U,
                      (unsigned)event->yolo.allow_count,
                      (unsigned)event->yolo.deny_count);
        break;
    }
    default:
        break;
    }
}

static void process_vibe_downlinks(void)
{
    static uint8_t raw[VK_DOWNLINK_MAX_PACKET];
    uint16_t raw_len = 0;
    uint32_t dropped = 0;
    uint32_t rejected = 0;
    uint32_t rejected_len = 0;
    bool has_event = false;

    portENTER_CRITICAL(&s_downlink_mux);
    dropped = s_downlink_drop_count;
    s_downlink_drop_count = 0;
    rejected = s_downlink_reject_count;
    rejected_len = s_downlink_reject_len;
    s_downlink_reject_count = 0;
    portEXIT_CRITICAL(&s_downlink_mux);
    if (dropped > 0) {
        Serial.printf("[BLE RX] queue full, dropped %u packet(s)\n", (unsigned)dropped);
    }
    if (rejected > 0) {
        Serial.printf("[BLE RX] rejected %u invalid packet(s), last_len=%u max=%u\n",
                      (unsigned)rejected,
                      (unsigned)rejected_len,
                      (unsigned)VK_DOWNLINK_MAX_PACKET);
    }

    do {
        portENTER_CRITICAL(&s_downlink_mux);
        has_event = s_downlink_tail != s_downlink_head;
        if (has_event) {
            vk_raw_downlink_t *slot = &s_downlink_queue[s_downlink_tail];
            raw_len = slot->len;
            memcpy(raw, slot->data, raw_len);
            s_downlink_tail = (uint8_t)((s_downlink_tail + 1) % VK_DOWNLINK_QUEUE_DEPTH);
        }
        portEXIT_CRITICAL(&s_downlink_mux);

        if (has_event) {
            print_ble_received_message(raw, raw_len);
            if (vk_decode_downlink(raw, raw_len, &s_downlink_decode_event)) {
                Serial.printf("[BLE RX] decode ok type=%u\n",
                              (unsigned)s_downlink_decode_event.type);
                s_applying_downlink = true;
                apply_downlink_event(&s_downlink_decode_event);
                s_applying_downlink = false;
            } else {
                Serial.printf("[BLE RX] decode failed tag=0x%02X len=%u\n",
                              (unsigned)raw[0],
                              (unsigned)raw_len);
            }
        }
    } while (has_event);
}

// ── Setup Screen confirm callback ─────────────────────────────────────

static void on_setup_confirm(int sel)
{
    static const int8_t target_screens[] = {
        2,  /* AI Agent */
        4,  /* YOLO */
        3,  /* Sound */
        6,  /* About */
    };
    const int count = sizeof(target_screens) / sizeof(target_screens[0]);
    if (sel < 0 || sel >= count) return;
    load_screen(target_screens[sel], LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

// ── Agent remote bridge ────────────────────────────────────────────────

static int32_t json_uint_field(const String &line, const char *field)
{
    String key = String("\"") + field + "\":";
    int start = line.indexOf(key);
    if (start < 0) return -1;
    start += key.length();

    while (start < (int)line.length() && line[start] == ' ') {
        start++;
    }

    int end = start;
    while (end < (int)line.length() && isDigit(line[end])) {
        end++;
    }
    if (end == start) return -1;
    return line.substring(start, end).toInt();
}

static void on_agent_action(ui_agent_action_t action,
                            const char *tool,
                            const char *command,
                            uint32_t request_id)
{
    int32_t port = ui_agent_screen_get_daemon_port();
    if (port < 0 || port > 65535) {
        port = 0;
    }
    if (send_setup_action(request_id,
                          (uint8_t)action,
                          tool,
                          command,
                          (uint16_t)port)) {
        return;
    }

    Serial.printf("{\"type\":\"agent_action\",\"request_id\":%lu,"
                  "\"action_id\":%d,\"tool\":\"%s\",\"command\":\"%s\","
                  "\"daemon_port\":%d}\n",
                  (unsigned long)request_id,
                  (int)action,
                  tool,
                  command,
                  (int)ui_agent_screen_get_daemon_port());
}

static void on_agent_status_request(uint32_t request_id)
{
    int32_t port = ui_agent_screen_get_daemon_port();
    if (port < 0 || port > 65535) {
        port = 0;
    }
    if (send_setup_status_request(request_id, (uint16_t)port)) {
        return;
    }

    Serial.printf("{\"type\":\"agent_status_request\",\"request_id\":%lu,"
                  "\"daemon_port\":%d}\n",
                  (unsigned long)request_id,
                  (int)ui_agent_screen_get_daemon_port());
}

static void handle_agent_remote_line(const String &line)
{
    if (line.indexOf("\"type\":\"agent_action_result\"") < 0) return;

    int32_t request_id = json_uint_field(line, "request_id");
    if (request_id <= 0) return;

    bool success = line.indexOf("\"success\":true") >= 0 ||
                   line.indexOf("\"success\":1") >= 0;
    ui_agent_screen_action_result((uint32_t)request_id, success);
}

static void process_agent_remote_rx(void)
{
    static String line;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;

        if (c == '\n') {
            if (line.length() > 0) {
                handle_agent_remote_line(line);
                line = "";
            }
            continue;
        }

        if (line.length() < 240) {
            line += c;
        } else {
            line = "";
        }
    }
}

// ── Screen loader ──────────────────────────────────────────────────────

static lv_group_t *group_for_screen(int8_t idx)
{
    switch (idx) {
    case 0:
        return ui_vibe_keyboard_get_group();
    case 1:
        return ui_setup_screen_get_group();
    case 2:
        return ui_agent_screen_get_group();
    case 3:
        return ui_sound_screen_get_group();
    case 4:
        return ui_yolo_screen_get_group();
    case 5:
        return ui_keys_screen_get_group();
    case 6:
        return ui_about_screen_get_group();
    default:
        return screen_groups[idx];
    }
}

static void create_screen_key_group(int8_t idx)
{
    screen_groups[idx] = lv_group_create();
    lv_obj_add_flag(screens[idx], LV_OBJ_FLAG_CLICKABLE);
    lv_group_add_obj(screen_groups[idx], screens[idx]);
    lv_group_focus_obj(screens[idx]);
}

static void load_screen(int8_t idx, lv_scr_load_anim_t anim)
{
    current_screen = (idx + SCREEN_COUNT) % SCREEN_COUNT;
    lv_group_t *group = group_for_screen(current_screen);
    if (group) {
        lv_set_default_group(group);
    }
    lv_screen_load_anim(screens[current_screen], anim, 200, 0, false);
    if (current_screen == 2) {
        ui_agent_screen_request_status();
    } else if (current_screen == 4) {
        send_yolo_config_request();
    }
}

// ── LVGL keypad navigation ─────────────────────────────────────────────

static bool is_scroll_up_key(uint32_t key)
{
    return key == LV_KEY_UP;
}

static bool is_scroll_down_key(uint32_t key)
{
    return key == LV_KEY_DOWN;
}

static bool is_back_key(uint32_t key)
{
    return key == LV_KEY_LEFT || key == LV_KEY_BACKSPACE || key == '\b' ||
           key == 0x7F || key == LV_KEY_ESC;
}

static bool is_delete_key(uint32_t key)
{
    return key == LV_KEY_BACKSPACE || key == '\b' || key == 0x7F;
}

static bool is_confirm_key(uint32_t key)
{
    return key == LV_KEY_ENTER || key == '\n' || key == '\r';
}

static void handle_current_back(void)
{
    switch (current_screen) {
    case 1:  /* Setup -> Vibe Keyboard */
        load_screen(0, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
        break;
    case 2:  /* AI Agent -> Setup */
    case 3:  /* Sound -> Setup */
    case 5:  /* Keys -> Setup */
    case 6:  /* About -> Setup */
        load_screen(1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
        break;
    case 4:  /* YOLO -> Setup */
        ui_yolo_screen_submit();
        load_screen(1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
        break;
    default:
        break;
    }
}

static void handle_current_move(int8_t dir)
{
    switch (current_screen) {
    case 0:
        ui_vibe_keyboard_move(dir);
        break;
    case 1:
        ui_setup_screen_move(dir);
        break;
    case 2:
        ui_agent_screen_move(dir);
        break;
    case 3:
        ui_sound_screen_move(dir);
        break;
    case 4:
        ui_yolo_screen_move(dir);
        break;
    case 5:
        if (dir < 0) {
            ui_keys_screen_scroll_up();
        } else {
            ui_keys_screen_scroll_down();
        }
        break;
    case 6:
        ui_about_screen_move(dir);
        break;
    default:
        break;
    }
}

static void handle_current_confirm(void)
{
    switch (current_screen) {
    case 1:
        ui_setup_screen_confirm();
        break;
    case 2:
        ui_agent_screen_confirm();
        break;
    case 3:
        ui_sound_screen_confirm();
        break;
    case 4:
        ui_yolo_screen_confirm();
        break;
    default:
        break;
    }
}

static void handle_key(uint32_t key, bool direct_nav, bool pressed)
{
    static bool voice_pressed = false;

    Serial.printf("key: %lu\n", key);

    if (key == 0x0) {
        if (pressed) {
            if (active_modal == MODAL_NONE && current_screen == 0 && !voice_pressed) {
                voice_pressed = true;
                send_button_press(VK_BUTTON_VOICE);
                Serial.println("[VOICE] pressed");
            }
        } else if (voice_pressed) {
            voice_pressed = false;
            send_button_release(VK_BUTTON_VOICE);
        }
        return;
    }

    if (!pressed) return;

    switch (active_modal) {
        case MODAL_SELECT_SESSION:
            if (direct_nav && is_scroll_up_key(key)) {
                ui_select_session_move(-1);
            } else if (direct_nav && is_scroll_down_key(key)) {
                ui_select_session_move(1);
            } else if (direct_nav && is_confirm_key(key)) {
                on_select_session_confirm(ui_select_session_selected_index());
            } else if (is_back_key(key) || key == VK_KEY_CAP) {
                close_modal();
            }
            return;

        case MODAL_NOTIFY:
            if (direct_nav && is_scroll_up_key(key)) {
                ui_notify_screen_move(-1);
            } else if (direct_nav && is_scroll_down_key(key)) {
                ui_notify_screen_move(1);
            } else if (direct_nav && is_confirm_key(key)) {
                ui_notify_screen_confirm();
            } else if (is_back_key(key)) {
                close_modal();
            }
            return;

        case MODAL_NOTIFY_DETAIL:
            if (is_back_key(key)) {
                on_notify_detail_back();
            } else if (key == 'a' || key == 'A') {
                on_notify_detail_allow();
            } else if (key == 'd' || key == 'D') {
                on_notify_detail_deny();
            }
            return;

        case MODAL_NONE:
        default:
            break;
    }

    if (direct_nav && is_scroll_up_key(key)) {
        handle_current_move(-1);
    } else if (direct_nav && is_scroll_down_key(key)) {
        handle_current_move(1);
    } else if (is_back_key(key)) {
        if (current_screen == 0) {
            if (is_delete_key(key)) {
                send_button_press(VK_BUTTON_DELETE);
            } else {
                send_button_press(VK_BUTTON_CANCEL);
            }
        }
        handle_current_back();
    } else if (direct_nav && is_confirm_key(key)) {
        if (current_screen == 0) {
            send_button_press(VK_BUTTON_SEND);
        }
        handle_current_confirm();
    } else if (key == VK_KEY_CAP && current_screen == 0) {
        load_screen(1, LV_SCR_LOAD_ANIM_MOVE_LEFT);   /* Vibe Keyboard -> Setup */
    } else if (key == VK_KEY_FN && current_screen == 0) {
        send_button_press(VK_BUTTON_SESSION);
        open_modal(MODAL_NOTIFY, scr_notify, ui_notify_screen_get_group());
    }
}

static void screen_key_event_cb(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    bool direct_nav = active_modal != MODAL_NONE || target == screens[current_screen];
    handle_key(lv_event_get_key(e), direct_nav, true);
}

// ── setup ──────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    instance.begin();
    vk_sound_begin();
    instance.setFeedbackCallback(on_keyboard_feedback);
    instance.attachKeyboardFeedback(true);
    beginLvglHelper(instance);
    vk_input_begin(instance);

    // Main screens
    screens[0] = lv_obj_create(NULL);
    ui_vibe_keyboard_create(screens[0]);
    sync_status_bar_ui();
    lv_obj_add_event_cb(screens[0], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[1] = lv_obj_create(NULL);
    ui_setup_screen_create(screens[1]);
    ui_setup_screen_set_confirm_cb(on_setup_confirm);
    lv_obj_add_event_cb(screens[1], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[2] = lv_obj_create(NULL);
    ui_agent_screen_create(screens[2]);
    ui_agent_screen_set_action_cb(on_agent_action);
    ui_agent_screen_set_status_request_cb(on_agent_status_request);
    lv_obj_add_event_cb(screens[2], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[3] = lv_obj_create(NULL);
    ui_sound_screen_create(screens[3]);
    lv_obj_add_event_cb(screens[3], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[4] = lv_obj_create(NULL);
    ui_yolo_screen_create(screens[4]);
    ui_yolo_screen_set_config_cb(on_yolo_config_changed);
    lv_obj_add_event_cb(screens[4], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[5] = lv_obj_create(NULL);
    ui_keys_screen_create(screens[5]);
    lv_obj_add_event_cb(screens[5], screen_key_event_cb, LV_EVENT_KEY, NULL);

    screens[6] = lv_obj_create(NULL);
    ui_about_screen_create(screens[6]);
    lv_obj_add_event_cb(screens[6], screen_key_event_cb, LV_EVENT_KEY, NULL);

    // Modal screens
    scr_select_session = lv_obj_create(NULL);
    ui_select_session_create(scr_select_session);
    ui_select_session_set_confirm_cb(on_select_session_confirm);
    lv_obj_add_event_cb(scr_select_session, screen_key_event_cb, LV_EVENT_KEY, NULL);

    scr_notify = lv_obj_create(NULL);
    ui_notify_screen_create(scr_notify);
    lv_obj_add_event_cb(scr_notify, screen_key_event_cb, LV_EVENT_KEY, NULL);
    ui_notify_screen_set_select_cb(on_notify_select);

    scr_notify_detail = lv_obj_create(NULL);
    ui_notify_detail_screen_create(scr_notify_detail);
    ui_notify_detail_screen_set_callbacks(on_notify_detail_back,
                                          on_notify_detail_allow,
                                          on_notify_detail_deny);
    lv_obj_add_event_cb(scr_notify_detail, screen_key_event_cb, LV_EVENT_KEY, NULL);

    // Wire vibe keyboard open-session button → open select session modal
    ui_vibe_keyboard_set_open_session_cb([]() {
        send_button_press(VK_BUTTON_SESSION);
        open_modal(MODAL_SELECT_SESSION, scr_select_session,
                   ui_select_session_get_group());
    });
    ui_vibe_keyboard_set_selection_cb([](int index) {
        send_session_switch_by_index(index);
    });
    ui_vibe_keyboard_set_voice_cb([](bool pressed) {
        handle_key(0, false, pressed);
    });

    refresh_session_ui();
#if VK_NOTIFY_SCREEN_DEMO
    seed_notification_demo();
#endif
    refresh_notification_ui();

    // Activate the vibe keyboard group
    lv_set_default_group(ui_vibe_keyboard_get_group());

    lv_screen_load(screens[0]);
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Start advertising only after every BLE-driven UI target is ready.
    begin_vibe_ble();
}

// ── loop ───────────────────────────────────────────────────────────────

void loop()
{
    sync_status_bar_ui();
    process_vibe_downlinks();
    process_agent_remote_rx();
    lv_timer_handler();
}
