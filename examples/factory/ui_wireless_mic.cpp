/**
 * @file      ui_wireless_mic.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-18
 *
 * Captures microphone audio, encodes it as G.722, and streams it to the paired
 * USB receiver over ESP-NOW without requiring Wi-Fi provisioning.
 */
#include "ui_define.h"

#if FACTORY_HAS_WIRELESS_MIC_DEPS

#include "wireless_mic_protocol.h"

#include <PCMFlowG722.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace {

using namespace wireless_mic;

static constexpr size_t kTxQueueDepth = 8;
static constexpr size_t kControlQueueDepth = 4;
static constexpr uint32_t kDiscoverIntervalMs = 350;
static constexpr uint32_t kHeartbeatIntervalMs = 1000;
static constexpr uint32_t kPeerTimeoutMs = 5000;
#if defined(ARDUINO_T_EMBED_PLUS)
static constexpr int32_t kMicGainMinDb = 0;
static constexpr int32_t kMicGainMaxDb = 24;
static constexpr int32_t kMicGainDefaultDb = 12;
#endif

struct TxItem {
    uint8_t destination[6];
    uint16_t size;
    uint8_t data[sizeof(AudioPacket)];
};

struct ControlEvent {
    uint8_t source[6];
    ControlPacket packet;
};

static const uint8_t kBroadcastAddress[6] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static AudioInputIf *s_audio_input = nullptr;
static G722Encoder s_encoder;
static QueueHandle_t s_tx_queue = nullptr;
static QueueHandle_t s_control_queue = nullptr;
static SemaphoreHandle_t s_send_done = nullptr;
static TaskHandle_t s_capture_task = nullptr;
static TaskHandle_t s_radio_task = nullptr;

static volatile bool s_exiting = false;
static volatile bool s_streaming = false;
static volatile bool s_send_success = false;
static volatile uint16_t s_audio_level = 0;
static bool s_radio_ready = false;
static bool s_audio_open = false;
static bool s_peer_ready = false;
static uint8_t s_channel = kDefaultChannel;
static uint8_t s_receiver[6] = {};
static char s_receiver_name[kDeviceNameLength] = "T-Dongle-S3";
static uint32_t s_session_id = 0;
static uint32_t s_audio_sequence = 0;
static uint32_t s_control_sequence = 0;
static uint32_t s_last_peer_ms = 0;
static uint32_t s_last_discover_ms = 0;
static uint32_t s_last_heartbeat_ms = 0;
static uint32_t s_frames_sent = 0;
static uint32_t s_frames_dropped = 0;
static uint32_t s_send_failures = 0;
#if defined(ARDUINO_T_EMBED_PLUS)
static int32_t s_mic_gain_db = kMicGainDefaultDb;
#endif

static lv_obj_t *s_page = nullptr;
static lv_obj_t *s_status_dot = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_receiver_label = nullptr;
static lv_obj_t *s_channel_label = nullptr;
static lv_obj_t *s_stats_label = nullptr;
static lv_obj_t *s_level_bar = nullptr;
static lv_obj_t *s_mic_button = nullptr;
static lv_obj_t *s_mic_icon = nullptr;
#if defined(ARDUINO_T_EMBED_PLUS)
static lv_obj_t *s_gain_label = nullptr;
static lv_obj_t *s_gain_slider = nullptr;
#endif
static lv_timer_t *s_ui_timer = nullptr;

static bool queuePacket(const uint8_t *destination, const void *packet, size_t size)
{
    if (!s_tx_queue || !destination || !packet || size > sizeof(TxItem::data)) {
        return false;
    }
    TxItem item = {};
    memcpy(item.destination, destination, sizeof(item.destination));
    item.size = static_cast<uint16_t>(size);
    memcpy(item.data, packet, size);
    return xQueueSend(s_tx_queue, &item, 0) == pdTRUE;
}

static bool addPeer(const uint8_t *address)
{
    if (!address) return false;
    if (esp_now_is_peer_exist(address)) return true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, address, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

static void onEspNowSent(const esp_now_send_info_t *, esp_now_send_status_t status)
{
    s_send_success = status == ESP_NOW_SEND_SUCCESS;
    if (s_send_done) xSemaphoreGive(s_send_done);
}

static void onEspNowReceive(const esp_now_recv_info_t *info,
                            const uint8_t *data, int length)
{
    if (!info || !info->src_addr || !data ||
        length != static_cast<int>(sizeof(ControlPacket))) {
        return;
    }
    const ControlPacket *packet = reinterpret_cast<const ControlPacket *>(data);
    if (!validHeader(packet->header, sizeof(*packet)) ||
        !validDevice(packet->device) ||
        packet->header.type != static_cast<uint8_t>(PacketType::Offer)) {
        return;
    }

    ControlEvent event = {};
    memcpy(event.source, info->src_addr, sizeof(event.source));
    memcpy(&event.packet, packet, sizeof(event.packet));
    if (s_control_queue) xQueueSend(s_control_queue, &event, 0);
}

static void radioTask(void *)
{
    TxItem item = {};
    while (!s_exiting) {
        if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (s_exiting) break;

        while (xSemaphoreTake(s_send_done, 0) == pdTRUE) {}
        esp_err_t result = esp_now_send(item.destination, item.data, item.size);
        if (result != ESP_OK ||
            xSemaphoreTake(s_send_done, pdMS_TO_TICKS(60)) != pdTRUE ||
            !s_send_success) {
            ++s_send_failures;
        }
    }
    s_radio_task = nullptr;
    vTaskDelete(nullptr);
}

static bool readAudioFrame(int16_t *pcm)
{
    size_t received = 0;
    uint8_t *bytes = reinterpret_cast<uint8_t *>(pcm);
    const size_t wanted = kFrameSamples * sizeof(int16_t);
    while (received < wanted && !s_exiting && s_streaming) {
        int count = s_audio_input->read(bytes + received, wanted - received);
        if (count <= 0) return false;
        received += static_cast<size_t>(count);
    }
    return received == wanted;
}

static void captureTask(void *)
{
    int16_t pcm[kFrameSamples];
    while (!s_exiting) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        while (s_streaming && !s_exiting) {
            if (!readAudioFrame(pcm)) {
                ++s_frames_dropped;
                continue;
            }

            uint16_t peak = 0;
            for (size_t i = 0; i < kFrameSamples; i += 4) {
                int32_t value = pcm[i];
                if (value < 0) value = -value;
                if (value > peak) peak = static_cast<uint16_t>(value);
            }
            s_audio_level = peak;

            AudioPacket packet = {};
            size_t encoded = s_encoder.encode(pcm, kFrameSamples,
                                              packet.audio, sizeof(packet.audio));
            if (encoded != kEncodedFrameBytes) {
                ++s_frames_dropped;
                continue;
            }
            packet.header = makeHeader(PacketType::Audio, encoded, s_session_id,
                                       s_audio_sequence++);
            if (!queuePacket(s_receiver, &packet, sizeof(packet))) {
                ++s_frames_dropped;
            } else {
                ++s_frames_sent;
            }
        }
        s_audio_level = 0;
    }
    s_capture_task = nullptr;
    vTaskDelete(nullptr);
}

static bool startRadio()
{
    WiFi.mode(WIFI_STA);
    uint32_t start_ms = millis();
    while (!WiFi.STA.started() && millis() - start_ms < 1000) {
        delay(10);
    }
    if (!WiFi.STA.started()) return false;

    if (WiFi.status() == WL_CONNECTED) {
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        if (esp_wifi_get_channel(&s_channel, &secondary) != ESP_OK) {
            s_channel = kDefaultChannel;
        }
    } else {
        s_channel = kDefaultChannel;
        if (esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            return false;
        }
    }

    if (esp_now_init() != ESP_OK) return false;
    if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK ||
        esp_now_register_send_cb(onEspNowSent) != ESP_OK ||
        !addPeer(kBroadcastAddress)) {
        esp_now_deinit();
        return false;
    }
    s_radio_ready = true;
    return true;
}

static void sendControl(PacketType type, const uint8_t *destination)
{
    ControlPacket packet = makeControl(type, s_channel, instance.getName(),
                                       s_session_id, s_control_sequence++);
    queuePacket(destination, &packet, sizeof(packet));
}

static void startStreaming()
{
    if (!s_peer_ready || s_streaming) return;
    s_session_id = esp_random();
    if (s_session_id == 0) s_session_id = 1;
    s_audio_sequence = 0;
    s_encoder.reset();
    sendControl(PacketType::Start, s_receiver);
    s_streaming = true;
    if (s_capture_task) xTaskNotifyGive(s_capture_task);
}

static void stopStreaming()
{
    if (!s_streaming) return;
    s_streaming = false;
    sendControl(PacketType::Stop, s_receiver);
}

static void micButtonEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !s_peer_ready) return;
    if (s_streaming) stopStreaming();
    else startStreaming();
}

#if defined(ARDUINO_T_EMBED_PLUS)
static void gainSliderEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

    s_mic_gain_db = lv_slider_get_value(lv_event_get_target_obj(event));
    if (s_audio_input) {
        s_audio_input->setMicGain(static_cast<float>(s_mic_gain_db));
    }
    if (s_gain_label) {
        lv_label_set_text_fmt(s_gain_label, "Gain +%ld dB",
                              static_cast<long>(s_mic_gain_db));
    }
}
#endif

static void processOffers()
{
    ControlEvent event = {};
    while (s_control_queue && xQueueReceive(s_control_queue, &event, 0) == pdTRUE) {
        if (!addPeer(event.source)) continue;
        memcpy(s_receiver, event.source, sizeof(s_receiver));
        strncpy(s_receiver_name, event.packet.device.name,
                sizeof(s_receiver_name) - 1);
        s_receiver_name[sizeof(s_receiver_name) - 1] = '\0';
        s_peer_ready = true;
        s_last_peer_ms = millis();
    }
}

static void updateUi(lv_timer_t *)
{
    processOffers();
    const uint32_t now = millis();

    if (!s_peer_ready && now - s_last_discover_ms >= kDiscoverIntervalMs) {
        s_last_discover_ms = now;
        sendControl(PacketType::Discover, kBroadcastAddress);
    }
    if (s_peer_ready && now - s_last_heartbeat_ms >= kHeartbeatIntervalMs) {
        s_last_heartbeat_ms = now;
        sendControl(PacketType::Heartbeat, s_receiver);
    }
    if (s_peer_ready && now - s_last_peer_ms > kPeerTimeoutMs + kHeartbeatIntervalMs) {
        stopStreaming();
        if (esp_now_is_peer_exist(s_receiver)) esp_now_del_peer(s_receiver);
        memset(s_receiver, 0, sizeof(s_receiver));
        s_peer_ready = false;
        s_last_discover_ms = 0;
    }

    if (s_mic_button) {
        lv_obj_set_style_bg_color(s_mic_button,
                                  s_streaming ? lv_color_hex(0xD64045) :
                                  (s_peer_ready ? UI_COLOR_ACCENT : lv_color_hex(0x6B7280)),
                                  0);
    }
    if (s_mic_icon) {
        lv_label_set_text(s_mic_icon, s_streaming ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
    }
    if (s_status_dot) {
        lv_obj_set_style_bg_color(s_status_dot,
                                  !s_peer_ready ? lv_color_hex(0xF0A202) :
                                  (s_streaming ? lv_color_hex(0xD64045) : lv_color_hex(0x39B54A)), 0);
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label,
                          !s_peer_ready ? "Searching receiver" :
                          (s_streaming ? "Microphone live" : "Ready"));
    }
    if (s_receiver_label) {
        lv_label_set_text_fmt(s_receiver_label, "%s", s_peer_ready ? s_receiver_name : "T-Dongle-S3");
    }
    if (s_channel_label) {
        lv_label_set_text_fmt(s_channel_label, "CH %u  16 kHz mono", s_channel);
    }
    if (s_stats_label) {
        lv_label_set_text_fmt(s_stats_label, "Frames %lu  Drop %lu  Radio %lu",
                              static_cast<unsigned long>(s_frames_sent),
                              static_cast<unsigned long>(s_frames_dropped),
                              static_cast<unsigned long>(s_send_failures));
    }
    if (s_level_bar) {
        int32_t level = (static_cast<uint32_t>(s_audio_level) * 100U) / 32767U;
        lv_bar_set_value(s_level_bar, level, LV_ANIM_OFF);
    }
}

static void releaseResources()
{
    s_exiting = true;
    s_streaming = false;
    if (s_capture_task) xTaskNotifyGive(s_capture_task);

    uint32_t started = millis();
    while ((s_capture_task || s_radio_task) && millis() - started < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_capture_task) {
        vTaskDelete(s_capture_task);
        s_capture_task = nullptr;
    }
    if (s_radio_task) {
        vTaskDelete(s_radio_task);
        s_radio_task = nullptr;
    }

    if (s_radio_ready) {
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        s_radio_ready = false;
    }
    if (s_audio_open && s_audio_input) {
        s_audio_input->close();
        s_audio_open = false;
    }
    s_encoder.end();

    if (s_tx_queue) vQueueDelete(s_tx_queue);
    if (s_control_queue) vQueueDelete(s_control_queue);
    if (s_send_done) vSemaphoreDelete(s_send_done);
    s_tx_queue = nullptr;
    s_control_queue = nullptr;
    s_send_done = nullptr;
    s_peer_ready = false;
}

static void backEvent(lv_event_t *)
{
    if (s_ui_timer) {
        lv_timer_delete(s_ui_timer);
        s_ui_timer = nullptr;
    }
    releaseResources();
    if (s_page) {
        ui_destroy_app_page(s_page);
        s_page = nullptr;
    }
    menu_show();
}

static lv_obj_t *makeTransparentPanel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void buildUi(lv_obj_t *parent)
{
    int32_t width = lv_display_get_horizontal_resolution(nullptr);
    int32_t height = lv_display_get_vertical_resolution(nullptr);
    bool short_landscape = width >= 300 && height <= 250;
    bool compact = width <= 240 || height <= 180;

    lv_obj_set_style_pad_all(parent, compact ? 4 : 8, 0);
    lv_obj_set_style_pad_gap(parent, compact ? 4 : 8, 0);
    lv_obj_set_flex_flow(parent, short_landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *information = makeTransparentPanel(parent);
    lv_obj_set_size(information, short_landscape ? LV_PCT(58) : LV_PCT(100),
                    short_landscape ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(information, short_landscape ? 1 : 0);
    lv_obj_set_flex_flow(information, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(information, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(information, compact ? 3 : 7, 0);

    lv_obj_t *status_row = makeTransparentPanel(information);
    lv_obj_set_size(status_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 7, 0);

    s_status_dot = lv_obj_create(status_row);
    lv_obj_set_size(s_status_dot, compact ? 9 : 11, compact ? 9 : 11);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_remove_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status_row);
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_status_label,
                               compact ? &lv_font_montserrat_12 : &lv_font_montserrat_16, 0);

    s_receiver_label = lv_label_create(information);
    lv_obj_set_width(s_receiver_label, LV_PCT(100));
    lv_label_set_long_mode(s_receiver_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_receiver_label, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(s_receiver_label,
                               compact ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);

    s_channel_label = lv_label_create(information);
    lv_obj_set_style_text_color(s_channel_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_channel_label, &lv_font_montserrat_12, 0);

    s_level_bar = lv_bar_create(information);
    lv_obj_set_size(s_level_bar, LV_PCT(100), compact ? 8 : 10);
    lv_bar_set_range(s_level_bar, 0, 100);
    lv_obj_set_style_bg_color(s_level_bar, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_level_bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_level_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_level_bar, 3, LV_PART_INDICATOR);

#if defined(ARDUINO_T_EMBED_PLUS)
    lv_obj_t *gain_row = makeTransparentPanel(information);
    lv_obj_set_size(gain_row, LV_PCT(100), compact ? 22 : 28);
    lv_obj_set_flex_flow(gain_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gain_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(gain_row, 0, 0);
    lv_obj_set_style_pad_column(gain_row, 6, 0);

    s_gain_label = lv_label_create(gain_row);
    lv_obj_set_width(s_gain_label, compact ? 68 : 82);
    lv_label_set_text_fmt(s_gain_label, "Gain +%ld dB",
                          static_cast<long>(s_mic_gain_db));
    lv_obj_set_style_text_color(s_gain_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_gain_label,
                               compact ? &lv_font_montserrat_10 : &lv_font_montserrat_12, 0);

    s_gain_slider = lv_slider_create(gain_row);
    lv_obj_set_width(s_gain_slider, 0);
    lv_obj_set_height(s_gain_slider, compact ? 7 : 9);
    lv_obj_set_flex_grow(s_gain_slider, 1);
    lv_slider_set_range(s_gain_slider, kMicGainMinDb, kMicGainMaxDb);
    lv_slider_set_value(s_gain_slider, s_mic_gain_db, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_gain_slider, UI_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_gain_slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_gain_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(s_gain_slider, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_gain_slider, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_gain_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_size(s_gain_slider, compact ? 13 : 16,
                          compact ? 13 : 16, LV_PART_KNOB);
    lv_obj_add_event_cb(s_gain_slider, gainSliderEvent,
                        LV_EVENT_VALUE_CHANGED, nullptr);
#endif

    s_stats_label = lv_label_create(information);
    lv_obj_set_width(s_stats_label, LV_PCT(100));
    lv_label_set_long_mode(s_stats_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_stats_label, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_stats_label, &lv_font_montserrat_10, 0);

    lv_obj_t *controls = makeTransparentPanel(parent);
    lv_obj_set_size(controls, short_landscape ? LV_PCT(38) : LV_PCT(100),
                    short_landscape ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(controls, short_landscape ? 0 : 1);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int32_t button_size = compact ? 64 : (short_landscape ? 82 : 92);
    if (width <= 240) button_size = 76;
    s_mic_button = lv_button_create(controls);
    lv_obj_set_size(s_mic_button, button_size, button_size);
    lv_obj_set_style_radius(s_mic_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_mic_button, 0, 0);
    lv_obj_set_style_shadow_width(s_mic_button, compact ? 6 : 12, 0);
    lv_obj_set_style_shadow_color(s_mic_button, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_shadow_opa(s_mic_button, LV_OPA_30, 0);
    lv_obj_add_event_cb(s_mic_button, micButtonEvent, LV_EVENT_CLICKED, nullptr);
    ui_add_accent_focus_style(s_mic_button);
    if (lv_group_t *group = lv_group_get_default()) {
        lv_group_add_obj(group, s_mic_button);
    }
#if defined(ARDUINO_T_EMBED_PLUS)
    ui_prepare_slider_for_encoder(s_gain_slider);
#endif

    s_mic_icon = lv_label_create(s_mic_button);
    lv_obj_set_style_text_font(s_mic_icon,
                               compact ? &lv_font_montserrat_24 : &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_mic_icon, lv_color_white(), 0);
    lv_obj_center(s_mic_icon);
}

static bool initializeTransport()
{
    s_exiting = false;
    s_streaming = false;
    s_peer_ready = false;
    s_frames_sent = 0;
    s_frames_dropped = 0;
    s_send_failures = 0;
    s_control_sequence = 0;

    s_tx_queue = xQueueCreate(kTxQueueDepth, sizeof(TxItem));
    s_control_queue = xQueueCreate(kControlQueueDepth, sizeof(ControlEvent));
    s_send_done = xSemaphoreCreateBinary();
    if (!s_tx_queue || !s_control_queue || !s_send_done) return false;

    PCMFormat format = {};
    format.sampleRate = kSampleRate;
    format.channels = kChannels;
    format.bitsPerSample = kBitsPerSample;
    if (!s_encoder.begin(format)) return false;

#if defined(ARDUINO_T_EMBED_PLUS)
    s_audio_input->setMicGain(static_cast<float>(s_mic_gain_db));
#endif
    if (!s_audio_input->open(kBitsPerSample, kChannels, kSampleRate)) return false;
    s_audio_open = true;
    if (!startRadio()) return false;

    if (xTaskCreate(captureTask, "wmic_capture", 4096, nullptr, 10,
                    &s_capture_task) != pdPASS ||
        xTaskCreate(radioTask, "wmic_radio", 4096, nullptr, 9,
                    &s_radio_task) != pdPASS) {
        return false;
    }
    return true;
}

} // namespace

void ui_wireless_mic_enter(lv_obj_t *parent)
{
    s_audio_input = instance.getAudioInput();
    if (!s_audio_input) {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, "Microphone unavailable");
        lv_obj_center(label);
        return;
    }

    s_page = ui_create_app_page(parent, "Wireless Mic", backEvent);
    buildUi(s_page);
    if (!initializeTransport()) {
        releaseResources();
        lv_label_set_text(s_status_label, "Initialization failed");
        return;
    }
    s_ui_timer = lv_timer_create(updateUi, 100, nullptr);
    updateUi(nullptr);
}

void ui_wireless_mic_exit(lv_obj_t *) {}

app_t ui_wireless_mic_main = {
    .setup_func_cb = ui_wireless_mic_enter,
    .exit_func_cb = ui_wireless_mic_exit,
    .user_data = nullptr,
};

#endif
