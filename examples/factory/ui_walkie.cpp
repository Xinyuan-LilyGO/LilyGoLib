/**
 * @file      ui_walkie.cpp
 * @author
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 * ESP-NOW Walkie-Talkie (PTT) App
 *
 * Uses PCMFlowG722 G.722 wideband audio codec for real-time voice
 * communication over ESP-NOW broadcast. Half-duplex operation:
 *  - Hold PTT (encoder center button or touch) to talk
 *  - Release to listen for incoming audio
 *
 * Flow:
 *  1. Open directly onto the listening/PTT dashboard.
 *  2. Use the header settings action to edit the nickname and radio channel.
 *     An active WiFi connection locks ESP-NOW to its channel.
 *  3. Open the peers dialog to see nearby stations.
 *
 * Handshake: each device periodically broadcasts a small "hello" packet
 * carrying its nickname so peers can populate their contact list and show
 * who is currently transmitting.
 *
 * Frame: 20ms G.722 @ 64kbps = 160 bytes per ESP-NOW packet
 *
 * Requirements:
 *  - PCMFlow library (https://github.com/lbuque/PCMFlow)
 *  - PCMFlowG722 library (https://github.com/tanakamasayuki/PCMFlowG722)
 */
#include <LilyGoLog.h>
#include "ui_define.h"
#include "esp_arduino_version.h"

#define WALKIE_EXCLUDE_ARDUINO_CORE4 \
    (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(4,0,0))

#define WALKIE_USE_ESPNOW_CLASS \
    (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3,3,0))

/*&& (defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK_V2)) */
#if !defined(EXCLUDE_WALKIE) && !WALKIE_EXCLUDE_ARDUINO_CORE4

#include <esp_wifi.h>
#include <PCMFlow.h>
#include <PCMFlowG722.h>

#if WALKIE_USE_ESPNOW_CLASS
#include <ESP32_NOW.h>
#include <new>
#else
#include <esp_now.h>
#endif

using namespace std;

LV_IMG_DECLARE(img_microphone);

// ============================================================
// Constants
// ============================================================
static constexpr uint16_t    kSampleRate    = 16000;
static constexpr uint8_t     kChannels      = 1;
static constexpr uint8_t     kBitsPerSample = 16;
static constexpr size_t      kFrameSamples  = 320;   // 20ms @ 16kHz
static constexpr size_t      kFrameBytes    = 160;   // G.722: 2 PCM -> 1 byte
static constexpr uint8_t     kWifiChannel   = 1;     // default when WiFi is offline
static constexpr size_t      kRxQueueDepth  = 8;
static constexpr uint32_t    kTaskStack     = 4096;
static constexpr UBaseType_t kTaskPrio      = 10;

static constexpr size_t      kNickMax       = 20;    // incl. terminating null
static const char            kHelloMagic[4] = {'W', 'L', 'K', 'H'};
static constexpr uint32_t    kAnnounceMs    = 2000;  // hello broadcast period
static constexpr uint32_t    kContactTtlMs  = 15000; // drop peers gone this long
static constexpr size_t      kHelloQueueLen = 8;

static const uint8_t s_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Wire formats ------------------------------------------
// Hello packet length differs from kFrameBytes so the receiver can tell
// control packets and audio frames apart purely by length + magic.
typedef struct __attribute__((packed)) {
    char magic[4];
    char nickname[kNickMax];
} walkie_hello_t;

// Queued peer announcement passed from the ESP-NOW callback to the UI timer.
typedef struct {
    uint8_t mac[6];
    char    nickname[kNickMax];
} walkie_peer_t;

// A discovered contact tracked by the UI.
typedef struct {
    uint8_t  mac[6];
    char     nickname[kNickMax];
    uint32_t last_seen_ms;
} walkie_contact_t;

// ============================================================
// Module state
// ============================================================
typedef enum {
    WALKIE_STATE_IDLE,
    WALKIE_STATE_TRANSMIT,
    WALKIE_STATE_RECEIVE,
} walkie_state_t;

static walkie_state_t   s_state      = WALKIE_STATE_IDLE;
static volatile bool    s_ptt_active = false;
static uint32_t         s_last_rx_ms = 0;

// Local identity / radio config retained across app visits.
static char             s_nickname[kNickMax] = "Walkie_01";
static uint8_t          s_channel    = kWifiChannel;

// Last station that sent us audio (for the "who is talking" display).
static uint8_t          s_talk_mac[6] = {0};
static volatile bool    s_have_talker = false;

// Discovered peers (owned by the UI thread / timer only).
static std::vector<walkie_contact_t> s_contacts;
static uint32_t         s_announce_ms = 0;

// G.722 codec instances
static G722Encoder      s_enc;
static G722Decoder      s_dec;

// PCMFlow playback pipeline (jitter buffer + format conversion).
static PCMFlow          s_audio;
static constexpr size_t kAudioBufferFrames = 2048;

// FreeRTOS
static TaskHandle_t     s_tx_task    = NULL;
static TaskHandle_t     s_rx_task    = NULL;
static QueueHandle_t    s_rx_queue   = NULL;
static QueueHandle_t    s_hello_queue = NULL;
static SemaphoreHandle_t s_codec_mtx = NULL;

// Lifecycle guard: true once the talk session (codec/ESP-NOW/tasks) is up.
static bool             s_started    = false;

// LVGL
static lv_timer_t      *s_timer      = NULL;
static lv_obj_t        *s_page       = NULL;
static lv_obj_t        *s_ptt_btn    = NULL;
static lv_obj_t        *s_status_label = NULL;
static lv_obj_t        *s_status_dot = NULL;
static lv_obj_t        *s_channel_label = NULL;
static lv_obj_t        *s_settings_btn = NULL;
static lv_obj_t        *s_talker_label = NULL;
static lv_obj_t        *s_contact_list = NULL;
static lv_obj_t        *s_contact_count_label = NULL;
static lv_obj_t        *s_peer_count_label = NULL;
static lv_obj_t        *s_contacts_overlay = NULL;
static lv_obj_t        *s_pair_btn = NULL;

// Mic "ripple" indicator (talk/receive visualisation)
static lv_obj_t        *s_mic_icon   = NULL;
static lv_obj_t        *s_ripple[2]  = {NULL, NULL};
static int              s_mic_dia = 64;
static constexpr int    kRippleGrow = 16;

// Settings dialog and its input widgets
static lv_obj_t        *s_settings_box = NULL;
static lv_obj_t        *s_nick_card = NULL;
static lv_obj_t        *s_settings_hint = NULL;
static bool             s_keyboard_transition = false;
static bool             s_settings_closing = false;
static lv_obj_t        *s_nick_ta    = NULL;
static lv_obj_t        *s_chan_dd    = NULL;
static lv_obj_t        *s_keyboard   = NULL;
static ui_soft_keyboard_lift_t s_keyboard_lift = {NULL, NULL, -1};

static AudioInputIf    *audioInput   = NULL;
static AudioOutputIf   *audioOutput  = NULL;
static bool             s_espnow_ready = false;
static bool             s_codec_ready = false;
static bool             s_audio_flow_ready = false;
static bool             s_audio_input_open = false;
static bool             s_audio_output_open = false;

#if WALKIE_USE_ESPNOW_CLASS
class WalkieBroadcastPeer : public ESP_NOW_Peer {
public:
    explicit WalkieBroadcastPeer(uint8_t channel)
        : ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, channel, WIFI_IF_STA, nullptr) {}

    ~WalkieBroadcastPeer()
    {
        remove();
    }

    bool begin()
    {
        if (!ESP_NOW.begin() || !add()) {
            LILYGO_LOG_E("[Walkie] Failed to initialize ESP-NOW broadcast peer");
            return false;
        }
        return true;
    }

    bool send_message(const uint8_t *data, size_t len)
    {
        return send(data, len) == len;
    }
};

static WalkieBroadcastPeer *s_broadcast_peer = nullptr;
#endif

// ============================================================
// ESP-NOW compatibility layer
// ============================================================
static void on_esp_now_recv_common(const uint8_t *mac, const uint8_t *data, int len)
{
    if (!mac || !data) return;

    // Control packet: peer announcing its nickname.
    if (len == (int)sizeof(walkie_hello_t) &&
        memcmp(data, kHelloMagic, sizeof(kHelloMagic)) == 0) {
        const walkie_hello_t *hello = (const walkie_hello_t *)data;
        walkie_peer_t peer = {};
        memcpy(peer.mac, mac, 6);
        memcpy(peer.nickname, hello->nickname, kNickMax);
        peer.nickname[kNickMax - 1] = '\0';
        BaseType_t woken = pdFALSE;
        if (s_hello_queue) xQueueSendFromISR(s_hello_queue, &peer, &woken);
        return;
    }

    // Audio frame.
    if (len != (int)kFrameBytes) return;
    if (s_ptt_active) return;  // skip while transmitting
    if (!s_rx_queue) return;

    memcpy(s_talk_mac, mac, 6);
    s_have_talker = true;

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_rx_queue, data, &woken);

    s_state      = WALKIE_STATE_RECEIVE;
    s_last_rx_ms = millis();
}

#if WALKIE_USE_ESPNOW_CLASS
static void on_esp_now_new_peer(const esp_now_recv_info_t *info,
                                const uint8_t *data, int len, void *arg)
{
    if (!info) return;
    on_esp_now_recv_common(info->src_addr, data, len);
}
#elif (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3,0,0))
static void on_esp_now_recv(const esp_now_recv_info_t *info,
                            const uint8_t *data, int len)
{
    if (!info) return;
    on_esp_now_recv_common(info->src_addr, data, len);
}
#else
static void on_esp_now_recv(const uint8_t *mac, const uint8_t *data, int len)
{
    on_esp_now_recv_common(mac, data, len);
}
#endif

static bool setup_espnow(uint8_t channel, bool wifi_connected)
{
    // When WiFi is connected we must not retune the radio: ESP-NOW simply
    // rides on the WiFi channel. Otherwise bring up a bare STA on `channel`.
    if (!wifi_connected) {
        WiFi.mode(WIFI_STA);
#if WALKIE_USE_ESPNOW_CLASS
        WiFi.setChannel(channel, WIFI_SECOND_CHAN_NONE);
        uint32_t started_at = millis();
        while (!WiFi.STA.started() && millis() - started_at < 1000) {
            delay(10);
        }
#else
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
#endif
    }

#if WALKIE_USE_ESPNOW_CLASS
    if (s_broadcast_peer) {
        delete s_broadcast_peer;
        s_broadcast_peer = nullptr;
    }
    s_broadcast_peer = new (std::nothrow) WalkieBroadcastPeer(channel);
    if (!s_broadcast_peer) {
        LILYGO_LOG_E("[Walkie] WalkieBroadcastPeer allocation failed");
        return false;
    }
    if (!s_broadcast_peer->begin()) {
        delete s_broadcast_peer;
        s_broadcast_peer = nullptr;
        ESP_NOW.end();
        return false;
    }
    ESP_NOW.onNewPeer(on_esp_now_new_peer, nullptr);
#else
    if (esp_now_init() != ESP_OK) {
        LILYGO_LOG_E("[Walkie] esp_now_init failed");
        return false;
    }
    if (esp_now_register_recv_cb(on_esp_now_recv) != ESP_OK) {
        LILYGO_LOG_E("[Walkie] esp_now_register_recv_cb failed");
        esp_now_deinit();
        return false;
    }

    esp_now_peer_info_t peer = {};
    peer.channel = 0;            // 0 = use the current WiFi channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, s_broadcast_mac, 6);

    if (esp_now_add_peer(&peer) != ESP_OK) {
        LILYGO_LOG_E("[Walkie] esp_now_add_peer failed");
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        return false;
    }
#endif

    s_espnow_ready = true;
    return true;
}

static void teardown_espnow()
{
    if (!s_espnow_ready) return;
    s_espnow_ready = false;

#if WALKIE_USE_ESPNOW_CLASS
    if (s_broadcast_peer) {
        delete s_broadcast_peer;
        s_broadcast_peer = nullptr;
    }
    ESP_NOW.end();
#else
    esp_now_unregister_recv_cb();
    esp_now_deinit();
#endif
}

static bool walkie_espnow_send(const uint8_t *data, size_t len)
{
#if WALKIE_USE_ESPNOW_CLASS
    return s_broadcast_peer && s_broadcast_peer->send_message(data, len);
#else
    return esp_now_send(s_broadcast_mac, data, len) == ESP_OK;
#endif
}

// Broadcast our nickname so peers can list us / label our transmissions.
static void send_hello()
{
    walkie_hello_t hello = {};
    memcpy(hello.magic, kHelloMagic, sizeof(kHelloMagic));
    strncpy(hello.nickname, s_nickname, kNickMax - 1);
    walkie_espnow_send((const uint8_t *)&hello, sizeof(hello));
}

// ============================================================
// TX task: capture -> encode -> ESP-NOW send
// ============================================================
static void walkie_tx_task(void *arg)
{
    int16_t pcm[kFrameSamples];
    uint8_t g722[kFrameBytes];

    while (1) {
        // Wait for PTT press notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        s_state = WALKIE_STATE_TRANSMIT;

        while (s_ptt_active) {
            // Read 20ms PCM from microphone (blocks ~20ms)
            xSemaphoreTake(s_codec_mtx, portMAX_DELAY);
            if(audioInput){
                audioInput->read((uint8_t *)pcm, sizeof(pcm));
            }
            xSemaphoreGive(s_codec_mtx);

            // G.722 encode: 320 PCM samples -> 160 bytes
            size_t encoded = s_enc.encode(pcm, kFrameSamples, g722, sizeof(g722));
            if (encoded > 0) {
                walkie_espnow_send(g722, encoded);
            }
        }

        s_state = WALKIE_STATE_IDLE;
    }
}

// ============================================================
// RX task: receive -> decode -> play
// ============================================================
static void walkie_rx_task(void *arg)
{
    uint8_t g722[kFrameBytes];
    int16_t pcm[kFrameSamples];

    while (1) {
        if (xQueueReceive(s_rx_queue, g722, portMAX_DELAY) != pdTRUE) continue;

        // Skip if currently transmitting (stale frame)
        if (s_ptt_active) continue;

        // Enqueue the encoded bytes into the decoder's internal FIFO
        // (pcm == nullptr). PCMFlow pulls and decodes them in pump().
        s_dec.decode(g722, sizeof(g722), nullptr, 0);

        // Advance the PCMFlow pipeline and drain decoded PCM into the
        // speaker, one frame at a time.
        s_audio.pump();
        while (s_audio.availableFrames() >= kFrameSamples) {
            size_t got = s_audio.readFrames(pcm, kFrameSamples);
            if (got == 0) break;
            xSemaphoreTake(s_codec_mtx, portMAX_DELAY);
            if(audioOutput){
                audioOutput->write((uint8_t *)pcm,got * (kBitsPerSample / 8));
            }
            xSemaphoreGive(s_codec_mtx);
        }
    }
}

// ============================================================
// Contact list helpers (UI thread only)
// ============================================================
static walkie_contact_t *find_contact(const uint8_t *mac)
{
    for (auto &c : s_contacts) {
        if (memcmp(c.mac, mac, 6) == 0) return &c;
    }
    return nullptr;
}

static const char *nickname_for(const uint8_t *mac)
{
    walkie_contact_t *c = find_contact(mac);
    return c ? c->nickname : "Unknown";
}

static void update_contact_count_labels()
{
    unsigned count = (unsigned)s_contacts.size();
    if (s_peer_count_label) {
        lv_label_set_text_fmt(s_peer_count_label, LV_SYMBOL_CALL " Peers %u", count);
    }
    if (s_contact_count_label) {
        lv_label_set_text_fmt(s_contact_count_label, "%u", count);
    }
}

static void apply_walkie_scrollbar_style(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(obj, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(obj, 2, LV_PART_SCROLLBAR);
}

static void apply_walkie_action_theme()
{
    if (s_pair_btn) {
        lv_obj_set_style_bg_color(s_pair_btn, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_text_color(s_pair_btn, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_color(s_pair_btn, UI_COLOR_DIVIDER, 0);
    }
    apply_walkie_scrollbar_style(s_page);
    apply_walkie_scrollbar_style(s_contact_list);
}

static void rebuild_contact_list()
{
    update_contact_count_labels();
    if (!s_contact_list) return;
    lv_obj_clean(s_contact_list);

    if (s_contacts.empty()) {
        lv_obj_t *empty = lv_label_create(s_contact_list);
        lv_label_set_text(empty, "Searching...");
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_SECONDARY, 0);
        return;
    }

    for (auto &c : s_contacts) {
        lv_obj_t *row = lv_label_create(s_contact_list);
        lv_label_set_text_fmt(row, "%s %s", LV_SYMBOL_CALL, c.nickname);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_color(row, UI_COLOR_TEXT_PRIMARY, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    }
}

// Drain queued hello packets, update the contact table and prune stale
// peers. Returns true if the visible list changed.
static bool update_contacts()
{
    bool changed = false;
    walkie_peer_t peer;

    while (s_hello_queue &&
           xQueueReceive(s_hello_queue, &peer, 0) == pdTRUE) {
        walkie_contact_t *c = find_contact(peer.mac);
        if (c) {
            if (strncmp(c->nickname, peer.nickname, kNickMax) != 0) {
                strncpy(c->nickname, peer.nickname, kNickMax);
                changed = true;
            }
            c->last_seen_ms = millis();
        } else {
            walkie_contact_t nc = {};
            memcpy(nc.mac, peer.mac, 6);
            strncpy(nc.nickname, peer.nickname, kNickMax);
            nc.last_seen_ms = millis();
            s_contacts.push_back(nc);
            changed = true;
        }
    }

    // Expire peers we have not heard from in a while.
    uint32_t now = millis();
    for (size_t i = 0; i < s_contacts.size();) {
        if (now - s_contacts[i].last_seen_ms > kContactTtlMs) {
            s_contacts.erase(s_contacts.begin() + i);
            changed = true;
        } else {
            i++;
        }
    }
    return changed;
}

// ============================================================
// LVGL UI update timer (100ms)
// ============================================================
static void walkie_timer_cb(lv_timer_t *t)
{
    // --- Periodic handshake broadcast ---
    if (s_started && millis() - s_announce_ms >= kAnnounceMs) {
        s_announce_ms = millis();
        send_hello();
    }

    // --- Refresh the contact list ---
    if (update_contacts()) {
        rebuild_contact_list();
    } else {
        update_contact_count_labels();
    }

    walkie_state_t st = s_state;

    // Auto-revert RECEIVE -> IDLE after 500ms of silence
    if (st == WALKIE_STATE_RECEIVE && millis() - s_last_rx_ms > 500) {
        s_state = WALKIE_STATE_IDLE;
        st      = WALKIE_STATE_IDLE;
        s_have_talker = false;
    }

    if (!s_status_label) return;

    // Per-state caption + accent colour.
    const char *caption;
    lv_color_t  accent;
    bool        active = true;
    switch (st) {
    case WALKIE_STATE_TRANSMIT:
        caption = "TALKING";
        accent  = UI_COLOR_ACCENT;
        break;
    case WALKIE_STATE_RECEIVE:
        caption = "RECEIVING";
        accent  = UI_COLOR_ACCENT_DIM;
        break;
    default:
        caption = s_started ? "LISTENING" : "OFFLINE";
        accent  = UI_COLOR_TEXT_SECONDARY;
        active  = false;
        break;
    }

    lv_label_set_text(s_status_label, caption);
    lv_obj_set_style_text_color(s_status_label,
                                active ? accent : UI_COLOR_TEXT_SECONDARY, 0);

    if (s_status_dot) {
        lv_obj_set_style_bg_color(s_status_dot, active ? accent : UI_COLOR_TRACK, 0);
    }
    if (s_channel_label) {
        lv_label_set_text_fmt(s_channel_label, "Channel %u", s_channel);
    }

    // Mic button colour: lit with the accent while active, dark when idle.
    if (s_ptt_btn) {
        lv_obj_set_style_bg_color(s_ptt_btn,
                                  active ? accent : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_color(s_ptt_btn, UI_COLOR_ACCENT, 0);
    }
    if (s_mic_icon) {
        lv_obj_set_style_image_recolor(s_mic_icon,
                                       active ? UI_COLOR_TEXT_ON_ACCENT : UI_COLOR_ACCENT,
                                       0);
    }
    apply_walkie_action_theme();

    // Ripple animation: two rings radiating outward and fading, 50% out of
    // phase with each other. Driven straight off this 100ms tick.
    static uint8_t ripple_phase = 0;
    if (active) {
        ripple_phase = (ripple_phase + 7) % 100;
    }
    for (int i = 0; i < 2; i++) {
        if (!s_ripple[i]) continue;
        if (!active) {
            lv_obj_set_style_bg_opa(s_ripple[i], LV_OPA_TRANSP, 0);
            continue;
        }
        int ph  = (ripple_phase + i * 50) % 100;          // 0..99
        int dia = s_mic_dia + kRippleGrow * ph / 100;        // grow outward
        lv_opa_t opa = (lv_opa_t)(LV_OPA_50 * (100 - ph) / 100);  // fade out
        lv_obj_set_size(s_ripple[i], dia, dia);
        lv_obj_center(s_ripple[i]);
        lv_obj_set_style_bg_color(s_ripple[i], accent, 0);
        lv_obj_set_style_bg_opa(s_ripple[i], opa, 0);
    }

    // "Who is talking" line
    if (s_talker_label) {
        if (st == WALKIE_STATE_RECEIVE && s_have_talker) {
            lv_label_set_text_fmt(s_talker_label, "From: %s",
                                  nickname_for(s_talk_mac));
        } else if (st == WALKIE_STATE_TRANSMIT) {
            lv_label_set_text_fmt(s_talker_label, "On air: %s", s_nickname);
        } else {
            lv_label_set_text(s_talker_label, s_nickname);
        }
    }
}

static void reset_talk_widgets()
{
    s_ptt_btn = s_status_label = s_status_dot = s_channel_label = NULL;
    s_settings_btn = s_talker_label = s_contact_list = NULL;
    s_contact_count_label = s_peer_count_label = s_pair_btn = NULL;
    s_mic_icon = s_ripple[0] = s_ripple[1] = NULL;
}

static void hide_settings_keyboard()
{
    if (s_keyboard_transition) return;
    s_keyboard_transition = true;
    ui_soft_keyboard_hide(s_keyboard, &s_keyboard_lift);
    if (s_nick_card) lv_obj_remove_local_style_prop(s_nick_card, LV_STYLE_TEXT_FONT, 0);
    if (s_nick_ta) {
        lv_obj_remove_state(s_nick_ta, LV_STATE_EDITED);
        if (auto *group = lv_obj_get_group(s_nick_ta)) lv_group_set_editing(group, false);
    }
    disable_keyboard();
    s_keyboard_transition = false;
}

static void settings_deleted(lv_event_t *)
{
    s_settings_closing = true;
    hide_settings_keyboard();
    if (s_keyboard) lv_obj_delete(s_keyboard);
    s_settings_box = s_nick_card = s_settings_hint = s_nick_ta = s_chan_dd = s_keyboard = NULL;
    s_keyboard_lift = {NULL, NULL, -1};
    s_settings_closing = false;
}

static void delete_walkie_timer()
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
}

// ============================================================
// Talk-session teardown (idempotent)
// ============================================================
static void walkie_release_resources()
{
    bool had_resources = s_started || s_espnow_ready || s_tx_task || s_rx_task ||
                         s_rx_queue || s_hello_queue || s_codec_mtx ||
                         s_audio_input_open || s_audio_output_open ||
                         s_audio_flow_ready || s_codec_ready;
    if (!had_resources) return;

    s_started = false;

    // Stop TX immediately
    s_ptt_active = false;

    // Stop callbacks before deleting queues that callbacks write to.
    teardown_espnow();

    // Delete FreeRTOS tasks
    if (s_tx_task) { vTaskDelete(s_tx_task); s_tx_task = NULL; }
    if (s_rx_task) { vTaskDelete(s_rx_task); s_rx_task = NULL; }

    // Delete synchronisation primitives
    if (s_rx_queue)    { vQueueDelete(s_rx_queue);    s_rx_queue    = NULL; }
    if (s_hello_queue) { vQueueDelete(s_hello_queue); s_hello_queue = NULL; }
    if (s_codec_mtx)   { vSemaphoreDelete(s_codec_mtx); s_codec_mtx = NULL; }

    // Stop audio codec
    if (s_audio_output_open && audioOutput) {
        audioOutput->close();
        s_audio_output_open = false;
        s_audio_input_open = false;
    } else if (s_audio_input_open && audioInput) {
        audioInput->close();
        s_audio_input_open = false;
    }

    // Tear down PCMFlow pipeline (safe: the RX task that pumps it is gone).
    if (s_audio_flow_ready) {
        s_audio.close();
        s_audio_flow_ready = false;
    }

    // Release G.722 codec resources
    if (s_codec_ready) {
        s_enc.end();
        s_dec.end();
        s_codec_ready = false;
    }

    s_contacts.clear();
    s_state = WALKIE_STATE_IDLE;
    s_have_talker = false;
    s_announce_ms = 0;
    memset(s_talk_mac, 0, sizeof(s_talk_mac));
}

static void close_contacts_overlay();
static lv_obj_t *s_err_mbox = NULL;

static void cleanup_walkie()
{
    delete_walkie_timer();
    walkie_release_resources();
    if (s_settings_box) ui_destroy_settings_page(s_settings_box);
    close_contacts_overlay();
    if (s_err_mbox) {
        destroy_msgbox(s_err_mbox);
        s_err_mbox = NULL;
    }
    disable_keyboard();
    if (s_page) {
        ui_destroy_app_page(s_page);
        s_page = NULL;
    }
    reset_talk_widgets();
}

static void back_event_handler(lv_event_t *)
{
    cleanup_walkie();
    menu_show();
}

// ============================================================
// PTT button event handler (works for both encoder & touch)
// ============================================================
static void ptt_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED && s_tx_task && !s_settings_box && !s_contacts_overlay && !s_err_mbox) {
        s_ptt_active = true;
        if (s_mic_icon) {
            lv_obj_set_style_image_recolor(s_mic_icon, UI_COLOR_TEXT_ON_ACCENT, 0);
        }
        xTaskNotifyGive(s_tx_task);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
               code == LV_EVENT_DEFOCUSED || code == LV_EVENT_DELETE ||
               code == LV_EVENT_CLICKED) {
        s_ptt_active = false;
    }
}

static void close_contacts_overlay()
{
    if (s_contacts_overlay) destroy_msgbox(s_contacts_overlay);
}

static void peers_btn_cb(lv_event_t *)
{
    if (s_contacts_overlay) return;
    s_ptt_active = false;
    s_contacts_overlay = create_msgbox(s_page, "Nearby peers", "", NULL, NULL, NULL);
    lv_obj_add_event_cb(s_contacts_overlay, [](lv_event_t *) {
        s_contacts_overlay = s_contact_list = s_contact_count_label = NULL;
    }, LV_EVENT_DELETE, NULL);
    auto *body = lv_msgbox_get_content(s_contacts_overlay);
    s_contact_list = lv_obj_create(body);
    lv_obj_set_size(s_contact_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_contact_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_contact_list, 0, 0);
    lv_obj_set_style_pad_all(s_contact_list, 0, 0);
    lv_obj_set_style_pad_row(s_contact_list, 8, 0);
    lv_obj_set_flex_flow(s_contact_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_contact_list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    rebuild_contact_list();
    lv_group_focus_obj(body);
}

static lv_obj_t *walkie_panel(lv_obj_t *parent)
{
    auto *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                       LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    return panel;
}

static lv_obj_t *walkie_label(lv_obj_t *parent, const char *text)
{
    auto *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SECONDARY, 0);
    return label;
}

static void mic_stage_resized(lv_event_t *event)
{
    auto *stage = lv_event_get_target_obj(event);
    int side = LV_MIN(lv_obj_get_content_width(stage), lv_obj_get_content_height(stage));
    int diameter = LV_MAX(32, LV_MIN(120, side - kRippleGrow));
    if (!s_ptt_btn) return;
    s_mic_dia = diameter;
    lv_obj_set_size(s_ptt_btn, diameter, diameter);
    if (s_mic_icon) {
        lv_image_set_scale(s_mic_icon, diameter * 110 / img_microphone.header.w);
    }
}

static void mic_controls_resized(lv_event_t *event)
{
    auto *controls = lv_event_get_target_obj(event);
    auto *stage = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
    auto *hint = lv_obj_get_child(controls, -1);
    if (hint == stage) return;
    const auto *font = lv_obj_get_style_text_font(hint, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, "Hold to talk", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_label_set_text(hint, size.x <= lv_obj_get_content_width(controls) ? "Hold to talk" : "Hold");
    int available = lv_obj_get_content_height(controls) - lv_font_get_line_height(font) -
                    lv_obj_get_style_pad_row(controls, LV_PART_MAIN);
    lv_obj_set_height(stage, LV_MAX(32, LV_MIN(136,
                      LV_MIN(available, lv_obj_get_content_width(controls)))));
}

static void build_walkie_ui(lv_obj_t *parent)
{
    const int width = lv_display_get_horizontal_resolution(NULL);
    const int height = lv_display_get_vertical_resolution(NULL);
    const bool horizontal = width >= height;
    const int gap = height <= 240 ? 4 : 10;
    lv_obj_set_style_pad_all(parent, gap, 0);
    lv_obj_set_style_pad_gap(parent, gap, 0);
    lv_obj_set_flex_flow(parent, horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    auto *information = walkie_panel(parent);
    lv_obj_set_size(information, horizontal ? 0 : LV_PCT(100),
                    horizontal ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(information, horizontal ? 3 : 0);
    lv_obj_set_flex_flow(information, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(information, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(information, gap, 0);

    auto *status = walkie_panel(information);
    lv_obj_set_size(status, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 6, 0);
    s_status_dot = lv_obj_create(status);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);
    s_status_label = walkie_label(status, "LISTENING");
    lv_obj_set_width(s_status_label, 0);
    lv_obj_set_flex_grow(s_status_label, 1);

    s_talker_label = walkie_label(information, s_nickname);
    lv_obj_add_style(s_talker_label, &ui_styles.accent_text, 0);
    // Use the shared accent style, allowing live theme changes.
    lv_obj_remove_local_style_prop(s_talker_label, LV_STYLE_TEXT_COLOR, 0);
    s_channel_label = walkie_label(information, "");

    s_pair_btn = lv_button_create(information);
    lv_obj_set_size(s_pair_btn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(s_pair_btn, 5, 0);
    lv_obj_set_style_pad_hor(s_pair_btn, 6, 0);
    lv_obj_set_style_radius(s_pair_btn, 8, 0);
    lv_obj_set_style_border_width(s_pair_btn, 1, 0);
    ui_add_accent_focus_style(s_pair_btn);
    lv_obj_add_event_cb(s_pair_btn, peers_btn_cb, LV_EVENT_CLICKED, NULL);
    s_peer_count_label = lv_label_create(s_pair_btn);
    lv_obj_set_width(s_peer_count_label, LV_PCT(100));
    lv_label_set_long_mode(s_peer_count_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_peer_count_label, LV_TEXT_ALIGN_CENTER, 0);

    auto *controls = walkie_panel(parent);
    lv_obj_set_size(controls, horizontal ? 0 : LV_PCT(100), horizontal ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(controls, horizontal ? 2 : 1);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(controls, gap, 0);
    auto *stage = walkie_panel(controls);
    lv_obj_set_size(stage, LV_PCT(100), 80);
    lv_obj_add_event_cb(stage, mic_stage_resized, LV_EVENT_SIZE_CHANGED, NULL);
    for (auto &ripple : s_ripple) {
        ripple = walkie_panel(stage);
        lv_obj_set_size(ripple, s_mic_dia, s_mic_dia);
        lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);
        lv_obj_center(ripple);
    }
    s_ptt_btn = lv_button_create(stage);
    lv_obj_set_size(s_ptt_btn, s_mic_dia, s_mic_dia);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_ptt_btn, 0, 0);
    lv_obj_set_style_border_width(s_ptt_btn, 2, 0);
    lv_obj_set_style_bg_color(s_ptt_btn, UI_COLOR_ACCENT_DIM, LV_STATE_PRESSED);
    lv_obj_center(s_ptt_btn);
    ui_add_accent_focus_style(s_ptt_btn);
    lv_obj_add_event_cb(s_ptt_btn, ptt_btn_event_cb, LV_EVENT_ALL, NULL);
    s_mic_icon = lv_image_create(s_ptt_btn);
    lv_image_set_src(s_mic_icon, &img_microphone);
    lv_obj_set_style_image_recolor_opa(s_mic_icon, LV_OPA_COVER, 0);
    lv_obj_center(s_mic_icon);
    auto *hint = walkie_label(controls, "Hold to talk");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(controls, mic_controls_resized, LV_EVENT_SIZE_CHANGED, stage);
    lv_obj_update_layout(parent);
    lv_obj_send_event(controls, LV_EVENT_SIZE_CHANGED, NULL);
    lv_obj_update_layout(parent);
    lv_obj_send_event(stage, LV_EVENT_SIZE_CHANGED, NULL);
    lv_group_focus_obj(s_ptt_btn);
}

static void show_error_and_back(lv_obj_t *parent, const char *msg);

static bool start_session()
{
    bool wifi_connected = (hw_get_wifi_status() == WL_CONNECTED);
    if (wifi_connected) {
        uint8_t primary = s_channel;
        wifi_second_chan_t secondary;
        if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) s_channel = primary;
    }

    // --- Init G.722 codec ---
    PCMFormat fmt;
    fmt.sampleRate    = kSampleRate;
    fmt.channels      = kChannels;
    fmt.bitsPerSample = kBitsPerSample;
    bool enc_ready = s_enc.begin(fmt);
    bool dec_ready = enc_ready && s_dec.begin(fmt);
    if (!enc_ready || !dec_ready) {
        LILYGO_LOG_E("[Walkie] G.722 codec init failed");
        if (enc_ready) {
            s_enc.end();
        }
        return false;
    }
    s_codec_ready = true;

    // --- Configure PCMFlow playback pipeline ---
    s_audio.setOutputFormat(fmt);
    s_audio.setBufferFrames(kAudioBufferFrames);
    s_audio.setInputSource(s_dec);
    s_audio_flow_ready = true;

    // --- Init ESP-NOW on the chosen channel ---
    if (!setup_espnow(s_channel, wifi_connected)) {
        walkie_release_resources();
        return false;
    }

    // --- Create FreeRTOS primitives ---
    s_rx_queue    = xQueueCreate(kRxQueueDepth, kFrameBytes);
    s_hello_queue = xQueueCreate(kHelloQueueLen, sizeof(walkie_peer_t));
    s_codec_mtx   = xSemaphoreCreateMutex();
    if (!s_rx_queue || !s_hello_queue || !s_codec_mtx) {
        walkie_release_resources();
        return false;
    }

    // --- Open audio codec (mono, 16kHz, 16-bit) ---
    if (audioInput) {
        if (!audioInput->open(kBitsPerSample, kChannels, kSampleRate)) {
            LILYGO_LOG_E("[Walkie] Failed to open audio input");
            walkie_release_resources();
            return false;
        }
        s_audio_input_open = true;
    }

    if (audioOutput) {
        if(!audioOutput->open(kBitsPerSample, kChannels, kSampleRate)){
            LILYGO_LOG_E("[Walkie] Failed to open audio output");
            walkie_release_resources();
            return false;
        }else{
            LILYGO_LOG_I("[Walkie] Audio output opened: %dHz, %d-bit, %d channels",
                  kSampleRate, kBitsPerSample, kChannels);
            audioOutput->setVolume(85);
            s_audio_output_open = true;
        }
    }

    // --- Create tasks ---
    if (xTaskCreate(walkie_tx_task, "wlk_tx", kTaskStack,
                    NULL, kTaskPrio, &s_tx_task) != pdPASS ||
        xTaskCreate(walkie_rx_task, "wlk_rx", kTaskStack,
                    NULL, kTaskPrio, &s_rx_task) != pdPASS) {
        LILYGO_LOG_E("[Walkie] Failed to create walkie tasks");
        walkie_release_resources();
        return false;
    }

    s_started     = true;
    s_announce_ms = 0;     // announce immediately on the first timer tick
    return true;
}

// Only the shared textarea helper owns encoder edit state. The app handles
// keyboard visibility and lift/restore, avoiding a second click-to-edit toggle.
static void nick_ta_event_cb(lv_event_t *event)
{
    if (s_settings_closing || s_keyboard_transition || ui_soft_keyboard_focus_transition_active()) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED ||
            (code == LV_EVENT_FOCUSED && lv_obj_has_state(s_nick_ta, LV_STATE_EDITED))) {
        enable_keyboard();
        if (s_keyboard) {
            if (!s_keyboard_lift.lifted_obj) {
                lv_obj_set_style_text_font(s_nick_card,
                    lv_obj_get_style_text_font(s_nick_card, LV_PART_MAIN), 0);
            }
            ui_soft_keyboard_show(s_keyboard, s_nick_ta, s_nick_card, &s_keyboard_lift);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL ||
               (code == LV_EVENT_DEFOCUSED && !ui_soft_keyboard_focus_transition_active())) {
        hide_settings_keyboard();
    }
}

static void settings_action_cb(lv_event_t *event)
{
    const char *action = lv_label_get_text(lv_obj_get_child(lv_event_get_target_obj(event), 0));
    if (strcmp(action, "Save") != 0) {
        ui_destroy_settings_page(s_settings_box);
        return;
    }
    const char *name = lv_textarea_get_text(s_nick_ta);
    if (!name || !name[0]) {
        lv_label_set_text(s_settings_hint, "Enter a device name.");
        lv_group_focus_obj(s_nick_ta);
        return;
    }
    char nickname[kNickMax];
    size_t length = strlen(name);
    if (length >= kNickMax) {
        length = kNickMax - 1;
        // The announcement has a byte limit; never split a UTF-8 character.
        while (length && (static_cast<unsigned char>(name[length]) & 0xC0) == 0x80) --length;
    }
    memcpy(nickname, name, length);
    nickname[length] = '\0';
    uint8_t channel = s_channel;
    if (!lv_obj_has_state(s_chan_dd, LV_STATE_DISABLED)) {
        channel = lv_dropdown_get_selected(s_chan_dd) + 1;
    }
    const bool restart = channel != s_channel || !s_started;
    ui_destroy_settings_page(s_settings_box);
    snprintf(s_nickname, sizeof(s_nickname), "%s", nickname);
    if (restart) {
        walkie_release_resources();
        s_channel = channel;
        if (!start_session()) show_error_and_back(s_page, "Failed to start session.");
    }
    if (s_started) send_hello();
    walkie_timer_cb(NULL);
}

static void open_walkie_settings(lv_event_t *)
{
    if (s_settings_box) return;
    s_ptt_active = false;
    s_settings_box = ui_create_settings_page(s_page, "Walkie settings");
    lv_obj_add_event_cb(s_settings_box, settings_deleted, LV_EVENT_DELETE, NULL);
    auto *body = s_settings_box;
    if (lv_obj_get_group(body)) lv_group_remove_obj(body);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_pad_row(body, 8, 0);

    s_nick_card = walkie_panel(body);
    lv_obj_set_size(s_nick_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_nick_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_nick_card, 4, 0);
    lv_obj_set_style_pad_row(s_nick_card, 4, 0);
    // The raised input card must remain opaque over the dashboard.
    lv_obj_set_style_bg_color(s_nick_card, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(s_nick_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_nick_card, 8, 0);
    walkie_label(s_nick_card, "Device name");
    s_nick_ta = lv_textarea_create(s_nick_card);
    lv_obj_set_width(s_nick_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_nick_ta, true);
    lv_textarea_set_max_length(s_nick_ta, kNickMax - 1);
    lv_textarea_set_text(s_nick_ta, s_nickname);
    ui_prepare_textarea_for_encoder(s_nick_ta);
    ui_add_accent_focus_style(s_nick_ta);
    lv_obj_add_event_cb(s_nick_ta, nick_ta_event_cb, LV_EVENT_ALL, NULL);

    auto *channel_card = walkie_panel(body);
    lv_obj_set_size(channel_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(channel_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(channel_card, 4, 0);
    walkie_label(channel_card, "Radio channel");
    s_chan_dd = lv_dropdown_create(channel_card);
    lv_obj_set_width(s_chan_dd, LV_PCT(100));
    lv_dropdown_set_options(s_chan_dd, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14");
    lv_obj_set_style_radius(s_chan_dd, 8, 0);
    ui_add_accent_focus_style(s_chan_dd);
    lv_obj_add_flag(s_chan_dd, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(s_chan_dd, [](lv_event_t *event) {
        lv_obj_scroll_to_view_recursive(lv_event_get_target_obj(event), LV_ANIM_ON);
    }, LV_EVENT_FOCUSED, NULL);
    if (hw_get_wifi_status() == WL_CONNECTED) {
        uint8_t primary = s_channel;
        wifi_second_chan_t secondary;
        if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) s_channel = primary;
        lv_obj_add_state(s_chan_dd, LV_STATE_DISABLED);
    }
    lv_dropdown_set_selected(s_chan_dd, s_channel - 1);
    s_settings_hint = walkie_label(body, lv_obj_has_state(s_chan_dd, LV_STATE_DISABLED) ?
        "Channel follows the WiFi connection." : "Use the same channel on both devices.");
    lv_label_set_long_mode(s_settings_hint, LV_LABEL_LONG_WRAP);
    if (ui_soft_keyboard_should_open()) {
        s_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        if (lv_obj_get_group(s_keyboard)) lv_group_remove_obj(s_keyboard);
        auto ready = [](lv_event_t *) {
            hide_settings_keyboard();
            if (s_nick_ta) lv_group_focus_obj(s_nick_ta);
        };
        lv_obj_add_event_cb(s_keyboard, ready, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(s_keyboard, ready, LV_EVENT_CANCEL, NULL);
    }
    auto *actions = walkie_panel(body);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 8, 0);
    for (const char *text : {"Save", "Cancel"}) {
        auto *button = lv_button_create(actions);
        lv_obj_set_size(button, 0, LV_SIZE_CONTENT); lv_obj_set_flex_grow(button, 1);
        lv_obj_add_style(button, &ui_styles.card, 0);
        lv_obj_set_style_pad_all(button, 8, 0); lv_obj_set_style_radius(button, 8, 0);
        ui_add_accent_focus_style(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(button, settings_action_cb, LV_EVENT_CLICKED, NULL);
        auto *label = walkie_label(button, text);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_group_focus_obj(s_nick_ta);
}

// ============================================================
// Error dialog
// ============================================================
static void err_ok_cb(lv_event_t *e)
{
    if (s_err_mbox) {
        destroy_msgbox(s_err_mbox);
        s_err_mbox = NULL;
    }
    if (!s_page) {
        menu_show();
    }
}

static void show_error_and_back(lv_obj_t *parent, const char *msg)
{
    static const char *btns[] = {"OK", ""};
    s_err_mbox = create_msgbox(lv_scr_act(),
                                "Walkie Error", msg,
                                btns, err_ok_cb, NULL);
}

// ============================================================
// enter / exit
// ============================================================
void ui_walkie_enter(lv_obj_t *parent)
{
    audioOutput = instance.getAudioOutput();
    if (!audioOutput) {
        LILYGO_LOG_PRINTLN("[Walkie] Audio output not initialized");
        return;
    }
    audioInput = instance.getAudioInput();
    if (!audioInput) {
        LILYGO_LOG_PRINTLN("[Walkie] Audio input not initialized");
        return;
    }

    s_page = ui_create_app_page(parent, "Walkie Talkie", back_event_handler);
    ui_styles_init();
    s_settings_btn = ui_app_page_add_header_button(s_page, LV_SYMBOL_SETTINGS, open_walkie_settings);
    build_walkie_ui(s_page);
    if (!start_session()) show_error_and_back(s_page, "Failed to start session.");
    s_timer = lv_timer_create(walkie_timer_cb, 100, NULL);
    walkie_timer_cb(NULL);
}

void ui_walkie_exit(lv_obj_t *parent)
{
    cleanup_walkie();
}

app_t ui_walkie_main = {
    .setup_func_cb = ui_walkie_enter,
    .exit_func_cb  = ui_walkie_exit,
    .user_data     = nullptr,
};
#endif 
