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
 *  1. Setup page: pick a device nickname and the ESP-NOW channel.
 *     When WiFi is already connected the channel is locked to the
 *     WiFi channel (ESP-NOW must share the radio channel).
 *  2. Talk page: a contact list (peer nicknames discovered through the
 *     ESP-NOW handshake) on the left and the PTT/status panel on the right.
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

// Local identity / radio config chosen on the setup page.
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
static lv_obj_t        *s_talker_label = NULL;
static lv_obj_t        *s_contact_list = NULL;
static lv_obj_t        *s_contact_count_label = NULL;
static lv_obj_t        *s_peer_count_label = NULL;
static lv_obj_t        *s_contacts_overlay = NULL;
static lv_obj_t        *s_back_btn = NULL;
static lv_obj_t        *s_pair_btn = NULL;

// Mic "ripple" indicator (talk/receive visualisation)
static lv_obj_t        *s_mic_icon   = NULL;
static lv_obj_t        *s_ripple[2]  = {NULL, NULL};
static constexpr int    kMicDia      = 64;   // centre mic circle diameter
static constexpr int    kRippleGrow  = 36;   // how far the rings expand

// Setup-page widgets
static lv_obj_t        *s_nick_ta    = NULL;
static lv_obj_t        *s_chan_dd    = NULL;
#ifdef USING_TOUCHPAD
static lv_obj_t        *s_keyboard   = NULL;
#endif

static AudioInputIf    *audioInput   = NULL;
static AudioOutputIf   *audioOutput  = NULL;
static bool             s_espnow_ready = false;
static bool             s_codec_ready = false;
static bool             s_audio_flow_ready = false;
static bool             s_audio_input_open = false;
static bool             s_audio_output_open = false;
static bool             s_showing_talk = false;

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
            LILYGO_LOG_E("Failed to initialize ESP-NOW broadcast peer");
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
        LILYGO_LOG_E("WalkieBroadcastPeer allocation failed");
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
        LILYGO_LOG_E("esp_now_init failed");
        return false;
    }
    if (esp_now_register_recv_cb(on_esp_now_recv) != ESP_OK) {
        LILYGO_LOG_E("esp_now_register_recv_cb failed");
        esp_now_deinit();
        return false;
    }

    esp_now_peer_info_t peer = {};
    peer.channel = 0;            // 0 = use the current WiFi channel
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    memcpy(peer.peer_addr, s_broadcast_mac, 6);

    if (esp_now_add_peer(&peer) != ESP_OK) {
        LILYGO_LOG_E("esp_now_add_peer failed");
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
        lv_label_set_text_fmt(s_peer_count_label, LV_SYMBOL_CALL " Pair %u", count);
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
    if (s_back_btn) {
        lv_obj_set_style_bg_color(s_back_btn, UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_color(s_back_btn, UI_COLOR_TRACK, 0);
    }
    if (s_pair_btn) {
        lv_obj_set_style_bg_color(s_pair_btn, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_color(s_pair_btn, UI_COLOR_ACCENT_DIM,
                                  LV_STATE_PRESSED);
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
    if (millis() - s_announce_ms >= kAnnounceMs) {
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
        caption = "IDLE";
        accent  = UI_COLOR_TEXT_SECONDARY;
        active  = false;
        break;
    }

    lv_label_set_text(s_status_label, caption);
    lv_obj_set_style_text_color(s_status_label,
                                active ? accent : UI_COLOR_TEXT_SECONDARY, 0);

    // Mic button colour: lit with the accent while active, dark when idle.
    if (s_ptt_btn) {
        lv_obj_set_style_bg_color(s_ptt_btn,
                                  active ? accent : UI_COLOR_CARD_BG, 0);
        lv_obj_set_style_border_color(s_ptt_btn, UI_COLOR_ACCENT, 0);
    }
    if (s_mic_icon) {
        lv_obj_set_style_image_recolor(s_mic_icon,
                                       active ? lv_color_white() : UI_COLOR_ACCENT,
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
        int dia = kMicDia + kRippleGrow * ph / 100;        // grow outward
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
            lv_label_set_text(s_talker_label, "On air");
        } else {
            lv_label_set_text(s_talker_label, "");
        }
    }
}

static void reset_talk_widgets()
{
    s_ptt_btn      = NULL;
    s_status_label = NULL;
    s_talker_label = NULL;
    s_contact_list = NULL;
    s_contact_count_label = NULL;
    s_peer_count_label = NULL;
    s_back_btn = NULL;
    s_pair_btn = NULL;
    s_mic_icon     = NULL;
    s_ripple[0]    = NULL;
    s_ripple[1]    = NULL;
}

static void reset_setup_widgets()
{
    s_nick_ta      = NULL;
    s_chan_dd      = NULL;
}

static void delete_setup_keyboard()
{
#ifdef USING_TOUCHPAD
    if (s_keyboard) {
        lv_obj_delete(s_keyboard);
        s_keyboard = NULL;
    }
#endif
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

static void walkie_stop()
{
    if (!s_started) return;
    s_started = false;
    walkie_release_resources();
}

static void build_setup_ui(lv_obj_t *parent);
static void close_contacts_overlay();

static void return_to_setup()
{
    delete_walkie_timer();
    walkie_stop();
    close_contacts_overlay();
    reset_talk_widgets();
    delete_setup_keyboard();
    reset_setup_widgets();
    s_showing_talk = false;

    if (!s_page) return;
    lv_obj_clean(s_page);
    build_setup_ui(s_page);
}

// ============================================================
// Back button handler (full cleanup)
// ============================================================
static void back_event_handler(lv_event_t *e)
{
    if (s_showing_talk) {
        return_to_setup();
        return;
    }

    delete_walkie_timer();
    walkie_stop();
    walkie_release_resources();
    close_contacts_overlay();

    // Leaving via the back button while still on the setup page: make sure the
    // keypad indev is disabled so menu navigation is not captured by it.
    disable_keyboard();
    delete_setup_keyboard();

    // Delete LVGL widgets
    if (s_page) {
        ui_destroy_app_page(s_page);
        s_page = NULL;
    }
    reset_talk_widgets();
    reset_setup_widgets();
    s_showing_talk = false;

    menu_show();
}

// ============================================================
// PTT button event handler (works for both encoder & touch)
// ============================================================
static void ptt_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED && s_tx_task) {
        s_ptt_active = true;
        if (s_mic_icon) {
            lv_obj_set_style_image_recolor(s_mic_icon, lv_color_white(), 0);
        }
        xTaskNotifyGive(s_tx_task);
    } else if (code == LV_EVENT_RELEASED ||
               code == LV_EVENT_CLICKED) {
        s_ptt_active = false;
    }
}

static void close_contacts_overlay()
{
    if (s_contacts_overlay) {
        lv_obj_delete(s_contacts_overlay);
        s_contacts_overlay = NULL;
    }
    s_contact_list = NULL;
    s_contact_count_label = NULL;
}

static void contacts_close_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    close_contacts_overlay();
}

static lv_obj_t *create_action_button(lv_obj_t *parent, int w, int h,
                                      const char *text, lv_event_cb_t cb,
                                      bool primary, lv_obj_t **btn_out)
{
    ui_styles_init();

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    if (primary) {
        lv_obj_add_style(btn, &ui_styles.accent_btn, 0);
    }
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    if (!primary) {
        lv_obj_set_style_border_color(btn, UI_COLOR_TRACK, 0);
    }
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    ui_add_accent_focus_style(btn);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    if (btn_out) {
        *btn_out = btn;
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, primary ? lv_color_white() : UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(label);
    return label;
}

static void peers_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    close_contacts_overlay();

    int scr_w = lv_disp_get_hor_res(NULL);
    int scr_h = lv_disp_get_ver_res(NULL);
    int panel_w = scr_w - 48;
    int panel_h = scr_h - 52;
    if (panel_w > 320) panel_w = 320;
    if (panel_h > 170) panel_h = 170;
    if (panel_w < 220) panel_w = scr_w - 16;
    if (panel_h < 130) panel_h = scr_h - 32;

    s_contacts_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_contacts_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_contacts_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_contacts_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_contacts_overlay, 0, 0);
    lv_obj_set_style_radius(s_contacts_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_contacts_overlay, 0, 0);
    lv_obj_remove_flag(s_contacts_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_contacts_overlay);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_set_style_bg_color(panel, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    apply_walkie_scrollbar_style(panel);

    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_set_size(title_row, lv_pct(100), 28);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_style_pad_column(title_row, 8, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, LV_SYMBOL_CALL " Peers");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(title, 1);

    s_contact_count_label = lv_label_create(title_row);
    lv_obj_set_style_text_color(s_contact_count_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_contact_count_label, &lv_font_montserrat_12, 0);

    lv_obj_t *close_btn = lv_btn_create(title_row);
    lv_obj_set_size(close_btn, 28, 28);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_border_color(close_btn, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    ui_add_accent_focus_style(close_btn);
    lv_obj_add_event_cb(close_btn, contacts_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(close_label);

    s_contact_list = lv_obj_create(panel);
    lv_obj_set_width(s_contact_list, lv_pct(100));
    lv_obj_set_flex_grow(s_contact_list, 1);
    lv_obj_set_style_pad_all(s_contact_list, 4, 0);
    lv_obj_set_style_pad_row(s_contact_list, 4, 0);
    lv_obj_set_style_border_width(s_contact_list, 0, 0);
    lv_obj_set_style_bg_opa(s_contact_list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_contact_list, LV_FLEX_FLOW_COLUMN);
    apply_walkie_scrollbar_style(s_contact_list);
    rebuild_contact_list();

    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, close_btn);
        lv_group_focus_obj(close_btn);
    }
}

static void setup_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    return_to_setup();
}

// ============================================================
// Talk page: primary PTT surface, with contacts on demand
// ============================================================
static void build_walkie_ui(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    apply_walkie_scrollbar_style(parent);

    // Status label
    s_status_label = lv_label_create(cont);
    lv_label_set_text(s_status_label, "IDLE");
    lv_obj_set_size(s_status_label, lv_pct(100), 18);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_12, 0);

    int stage = kMicDia + kRippleGrow;
    lv_obj_t *mic_stage = lv_obj_create(cont);
    lv_obj_set_size(mic_stage, stage, stage);
    lv_obj_set_style_bg_opa(mic_stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mic_stage, 0, 0);
    lv_obj_set_style_pad_all(mic_stage, 0, 0);
    lv_obj_remove_flag(mic_stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mic_stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(mic_stage);

    // Ripple rings
    for (int i = 0; i < 2; i++) {
        s_ripple[i] = lv_obj_create(mic_stage);
        lv_obj_set_size(s_ripple[i], kMicDia, kMicDia);
        lv_obj_set_style_radius(s_ripple[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ripple[i], 0, 0);
        lv_obj_set_style_bg_color(s_ripple[i], UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(s_ripple[i], LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(s_ripple[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_ripple[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(s_ripple[i]);
    }

    // PTT button
    s_ptt_btn = lv_btn_create(mic_stage);
    lv_obj_set_size(s_ptt_btn, kMicDia, kMicDia);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE,
                            LV_STATE_PRESSED | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_ptt_btn, LV_RADIUS_CIRCLE,
                            LV_STATE_PRESSED | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_transform_width(s_ptt_btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(s_ptt_btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_ptt_btn, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(s_ptt_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ptt_btn, 2, 0);
    lv_obj_set_style_border_color(s_ptt_btn, UI_COLOR_ACCENT, 0);
    lv_obj_center(s_ptt_btn);
    lv_obj_add_event_cb(s_ptt_btn, ptt_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_ptt_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    ui_add_accent_focus_style(s_ptt_btn);

    s_mic_icon = lv_image_create(s_ptt_btn);
    lv_image_set_src(s_mic_icon, &img_microphone);
    lv_obj_center(s_mic_icon);
    lv_obj_set_style_image_recolor(s_mic_icon, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_image_recolor_opa(s_mic_icon, LV_OPA_COVER, 0);

    // Talker label
    s_talker_label = lv_label_create(cont);
    lv_label_set_text(s_talker_label, "");
    lv_obj_set_size(s_talker_label, lv_pct(100), 18);
    lv_label_set_long_mode(s_talker_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_talker_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_talker_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_talker_label, &lv_font_montserrat_12, 0);

    lv_obj_t *actions = lv_obj_create(cont);
    lv_obj_set_size(actions, lv_pct(100), 30);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    create_action_button(actions, 96, 30, LV_SYMBOL_LEFT " Back",
                         setup_btn_cb, false, &s_back_btn);
    s_peer_count_label = create_action_button(actions, 112, 30,
                                             LV_SYMBOL_CALL " Pair 0",
                                             peers_btn_cb, true, &s_pair_btn);

    apply_walkie_action_theme();
    update_contact_count_labels();
}

// ============================================================
// Bring up the talk session, then swap the setup page for the talk page
// ============================================================
static lv_obj_t *s_err_mbox = NULL;
static void show_error_and_back(lv_obj_t *parent, const char *msg);

static bool start_session()
{
    bool wifi_connected = (hw_get_wifi_status() == WL_CONNECTED);

    // --- Init G.722 codec ---
    PCMFormat fmt;
    fmt.sampleRate    = kSampleRate;
    fmt.channels      = kChannels;
    fmt.bitsPerSample = kBitsPerSample;
    bool enc_ready = s_enc.begin(fmt);
    bool dec_ready = enc_ready && s_dec.begin(fmt);
    if (!enc_ready || !dec_ready) {
        LILYGO_LOG_E("G.722 codec init failed");
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
            LILYGO_LOG_E("Failed to open audio input");
            walkie_release_resources();
            return false;
        }
        s_audio_input_open = true;
    }

    if (audioOutput) {
        if(!audioOutput->open(kBitsPerSample, kChannels, kSampleRate)){
            LILYGO_LOG_E("Failed to open audio output");
            walkie_release_resources();
            return false;
        }else{
            LILYGO_LOG_I("Audio output opened: %dHz, %d-bit, %d channels",
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
        LILYGO_LOG_E("Failed to create walkie tasks");
        walkie_release_resources();
        return false;
    }

    s_started     = true;
    s_announce_ms = 0;     // announce immediately on the first timer tick
    return true;
}

static void start_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_started) return;

    // Capture the nickname.
    const char *nick = s_nick_ta ? lv_textarea_get_text(s_nick_ta) : "";
    if (nick && nick[0]) {
        strncpy(s_nickname, nick, kNickMax - 1);
        s_nickname[kNickMax - 1] = '\0';
    }

    // Capture the channel (unless WiFi already pinned it).
    if (s_chan_dd && !lv_obj_has_state(s_chan_dd, LV_STATE_DISABLED)) {
        s_channel = (uint8_t)lv_dropdown_get_selected(s_chan_dd) + 1;
    }

    // Make sure the keypad indev is back off before we leave the setup page.
    disable_keyboard();

    delete_setup_keyboard();

    if (!start_session()) {
        show_error_and_back(s_page, "Failed to start session.");
        return;
    }

    // Swap the setup content for the talk page.
    lv_obj_clean(s_page);
    reset_setup_widgets();
    s_showing_talk = true;
    build_walkie_ui(s_page);

    // Start UI updates (status + handshake + contact list).
    s_timer = lv_timer_create(walkie_timer_cb, 100, NULL);
}

// Route physical keyboard input into the nickname field.
//
// On devices with a rotary + physical keyboard (e.g. T-LoRa-Pager) the keypad
// indev is off until a textarea enters edit mode. We mirror that here: a rotary
// click toggles edit mode and enables/disables the keypad, and pressing ENTER
// on the keyboard confirms and leaves edit mode (so the user is never stuck in
// the field). On touch devices an on-screen keyboard is shown instead.
static void nick_ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_indev_type_t type = lv_indev_get_type(indev);
    bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);

    if (code == LV_EVENT_KEY) {
        lv_key_t key = *(lv_key_t *)lv_event_get_param(e);
        if (key == LV_KEY_ENTER) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
            lv_event_stop_processing(e);
            return;
        }
    }

    if (type == LV_INDEV_TYPE_ENCODER) {
        if (code == LV_EVENT_CLICKED && edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        } else if (code == LV_EVENT_FOCUSED && edited) {
            enable_keyboard();
        }
    }
#ifdef USING_TOUCHPAD
    else if (type == LV_INDEV_TYPE_POINTER && s_keyboard) {
        if (code == LV_EVENT_CLICKED) {
            lv_keyboard_set_textarea(s_keyboard, ta);
            lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
}

// ============================================================
// Setup page: nickname + ESP-NOW channel
// ============================================================
static void build_setup_ui(lv_obj_t *parent)
{
    bool wifi_connected = (hw_get_wifi_status() == WL_CONNECTED);
    if (wifi_connected) {
        // ESP-NOW must share the radio channel with the active WiFi link.
        uint8_t primary = kWifiChannel;
        wifi_second_chan_t second;
        if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
            s_channel = primary;
        }
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 8, 0);
    apply_walkie_scrollbar_style(parent);
    apply_walkie_scrollbar_style(cont);

    int scr_w = lv_disp_get_hor_res(NULL);
    bool stacked = scr_w < 420;

    lv_obj_t *settings_card = ui_create_card(cont, "Session");
    lv_obj_set_width(settings_card, lv_pct(100));

    s_nick_ta = lv_textarea_create(lv_obj_create(settings_card));
    lv_obj_set_width(s_nick_ta, stacked ? 112 : 160);
    lv_obj_set_height(s_nick_ta, 32);
    lv_textarea_set_one_line(s_nick_ta, true);
    lv_textarea_set_max_length(s_nick_ta, kNickMax - 1);
    lv_textarea_set_text(s_nick_ta, s_nickname);
    lv_obj_add_event_cb(s_nick_ta, nick_ta_event_cb, LV_EVENT_ALL, NULL);
    ui_create_card_item(settings_card, LV_SYMBOL_EDIT, "Name", s_nick_ta);

    s_chan_dd = lv_dropdown_create(lv_obj_create(settings_card));
    lv_obj_set_width(s_chan_dd, 96);
    lv_obj_set_height(s_chan_dd, 32);
    lv_dropdown_set_options(s_chan_dd,
                            "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14");
    lv_dropdown_set_selected(s_chan_dd,
                             s_channel >= 1 ? s_channel - 1 : 0);
    if (wifi_connected) {
        lv_obj_add_state(s_chan_dd, LV_STATE_DISABLED);
    }
    ui_create_card_item(settings_card, LV_SYMBOL_WIFI,
                        wifi_connected ? "WiFi Ch" : "Channel", s_chan_dd);

    // ---- Start button ----
    lv_obj_t *start_row = lv_obj_create(settings_card);
    lv_obj_set_size(start_row, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(start_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(start_row, 0, 0);
    lv_obj_set_style_pad_all(start_row, 0, 0);
    lv_obj_set_style_margin_top(start_row, 8, 0);
    lv_obj_set_flex_flow(start_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(start_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(start_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *start_btn = lv_btn_create(start_row);
    lv_obj_set_width(start_btn, scr_w / 3);
    lv_obj_set_height(start_btn, 36);
    lv_obj_add_style(start_btn, &ui_styles.accent_btn, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_set_style_border_width(start_btn, 0, 0);
    ui_add_accent_focus_style(start_btn);
    lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_center(start_label);
    lv_obj_set_style_text_color(start_label, lv_color_white(), 0);

#ifdef USING_TOUCHPAD
    s_keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_set_style_bg_color(s_keyboard, UI_COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_keyboard, lv_color_white(), 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CANCEL, NULL);
#endif
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
        LILYGO_LOG_PRINTLN("Audio output not initialized");
        return;
    }
    audioInput = instance.getAudioInput();
    if (!audioInput) {
        LILYGO_LOG_PRINTLN("Audio input not initialized");
        return;
    }

    // --- Build the setup page; the talk session starts on "Start" ---
    s_page = ui_create_app_page(parent, "Walkie Talkie", back_event_handler);
    build_setup_ui(s_page);
}

void ui_walkie_exit(lv_obj_t *parent)
{
    // Cleanup is done in back_event_handler
}

app_t ui_walkie_main = {
    .setup_func_cb = ui_walkie_enter,
    .exit_func_cb  = ui_walkie_exit,
    .user_data     = nullptr,
};
#endif 
