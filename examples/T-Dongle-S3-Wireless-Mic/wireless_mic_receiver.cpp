/**
 * @file      wireless_mic_receiver.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-18
 *
 * Receives G.722 microphone audio over ESP-NOW and exposes it as a composite
 * USB microphone and keyboard. The on-board LED reports link and stream state.
 */
#include <Arduino.h>
#include <USB.h>
#include <USBAudioCard.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/stream_buffer.h>
#include <PCMFlowG722.h>
#include <PCMResample.h>
#ifndef WIRELESS_MIC_DEBUG_ENABLED
#define WIRELESS_MIC_DEBUG_ENABLED 0
#endif
#if WIRELESS_MIC_DEBUG_ENABLED
#include <stdarg.h>
#endif

#include "wireless_mic_protocol.h"

namespace {

using namespace wireless_mic;

static constexpr int kBootPin = 0;
static constexpr int kLedDataPin = 40;
static constexpr int kLedClockPin = 39;
#if WIRELESS_MIC_DEBUG_ENABLED
static constexpr int kDebugUartTxPin = 43;
static constexpr int kDebugUartRxPin = 44;
static constexpr uint32_t kDebugUartBaud = 115200;
#endif
static constexpr uint8_t kLedBrightness = 4;
static constexpr uint32_t kLedBlack = 0x000000;
static constexpr uint32_t kLedRed = 0xFF0000;
static constexpr uint32_t kLedGreen = 0x00FF00;
static constexpr uint32_t kLedBlue = 0x0000FF;
static constexpr uint32_t kLedAmber = 0xFF5000;
static constexpr uint32_t kLedBlinkIntervalMs = 400;
#if WIRELESS_MIC_DEBUG_ENABLED
static constexpr uint32_t kDebugStatsIntervalMs = 1000;
#endif
static constexpr uint16_t kUsbVendorId = 0x303A;
static constexpr uint16_t kUsbProductId = 0x82C2;
static constexpr char kUsbSerialNumber[] = "T-Dongle-S3-WMIC-HID-48K";
static constexpr size_t kAudioQueueDepth = 24;
static constexpr size_t kControlQueueDepth = 6;
static constexpr size_t kUsbFrameSamples = kUsbSampleRate / 1000;
static constexpr size_t kUsbFrameBytes =
    kUsbFrameSamples * kChannels * (kBitsPerSample / 8);
static constexpr size_t kUsbPacketSamples =
    (kFrameSamples * kUsbSampleRate) / kSampleRate;
static constexpr size_t kUsbPacketBytes = kUsbPacketSamples * sizeof(int16_t);
static constexpr size_t kPcmBufferBytes = 16 * kUsbPacketBytes;
static constexpr size_t kPrimeBytes = 3 * kUsbPacketBytes;
static constexpr size_t kLowWaterBytes = 2 * kUsbPacketBytes;
static constexpr size_t kHighWaterBytes = 7 * kUsbPacketBytes;
static constexpr uint32_t kDecodeTaskStackBytes = 8192;
static constexpr uint32_t kUsbTaskStackBytes = 4096;
static constexpr uint32_t kChannelDwellMs = 220;
static constexpr uint32_t kSenderTimeoutMs = 3500;

static_assert(kUsbSampleRate % 1000 == 0,
              "USB sample rate must produce whole millisecond frames");
static_assert((kFrameSamples * kUsbSampleRate) % kSampleRate == 0,
              "Radio frames must resample to a whole number of USB samples");

struct ControlEvent {
    uint8_t source[6];
    uint8_t channel;
    ControlPacket packet;
};

struct AudioEvent {
    uint8_t source[6];
    AudioPacket packet;
};

enum class HidCommand : uint8_t {
    None,
    StartContinuousInput,
    CancelContinuousInput,
};

USBAudioCard s_usb_audio(kUsbSampleRate, UAC_BPS_16,
                         UAC_SPK_NONE, UAC_MIC_MONO);
USBHIDKeyboard s_usb_keyboard;
USBHID s_usb_hid;
G722Decoder s_decoder;
#if WIRELESS_MIC_DEBUG_ENABLED
HardwareSerial &s_debug_uart = Serial0;
#endif

QueueHandle_t s_control_queue = nullptr;
QueueHandle_t s_audio_queue = nullptr;
StreamBufferHandle_t s_pcm_buffer = nullptr;
TaskHandle_t s_decode_task = nullptr;
TaskHandle_t s_usb_task = nullptr;

volatile bool s_usb_active = false;
volatile bool s_stream_active = false;
volatile bool s_pcm_primed = false;
volatile uint16_t s_audio_level = 0;
volatile uint32_t s_packets_received = 0;
volatile uint32_t s_packets_lost = 0;
volatile uint32_t s_packets_dropped = 0;
volatile uint32_t s_pcm_underruns = 0;
volatile uint32_t s_usb_short_writes = 0;
int16_t s_last_usb_pcm[kUsbPacketSamples] = {};
bool s_has_last_usb_pcm = false;

bool s_scanning = true;
bool s_sender_ready = false;
bool s_fatal_error = false;
uint8_t s_channel = kFirstChannel;
uint8_t s_sender[6] = {};
char s_sender_name[kDeviceNameLength] = "Waiting";
uint32_t s_session_id = 0;
uint32_t s_expected_sequence = 0;
uint32_t s_last_sender_ms = 0;
uint32_t s_last_channel_ms = 0;
uint32_t s_last_stats_ms = 0;
#if WIRELESS_MIC_DEBUG_ENABLED
uint32_t s_last_debug_stats_ms = 0;
#endif
uint32_t s_led_color = UINT32_MAX;
HidCommand s_pending_hid_command = HidCommand::None;

void updateStatusLed(uint32_t now);

#if WIRELESS_MIC_DEBUG_ENABLED
void logMessage(const char *level, const char *format, ...)
{
    char message[192];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    s_debug_uart.printf("[%10lu] [%s] %s\r\n",
                        static_cast<unsigned long>(millis()), level, message);
}
#else
#define logMessage(...) ((void)0)
#endif

void setFatalError(const char *reason)
{
    logMessage("ERROR", "%s", reason);
    s_fatal_error = true;
    updateStatusLed(millis());
}

void incrementCounter(volatile uint32_t &counter)
{
    counter = counter + 1;
}

void writeStatusLedByte(uint8_t value)
{
    shiftOut(kLedDataPin, kLedClockPin, MSBFIRST, value);
}

void setStatusLed(uint32_t color)
{
    if (color == s_led_color) return;
    s_led_color = color;

    for (uint8_t i = 0; i < 4; ++i) writeStatusLedByte(0x00);
    writeStatusLedByte(0xE0 | (kLedBrightness & 0x1F));
    writeStatusLedByte(static_cast<uint8_t>(color));
    writeStatusLedByte(static_cast<uint8_t>(color >> 8));
    writeStatusLedByte(static_cast<uint8_t>(color >> 16));
    for (uint8_t i = 0; i < 4; ++i) writeStatusLedByte(0xFF);
}

void updateStatusLed(uint32_t now)
{
    if (s_fatal_error) {
        setStatusLed(kLedRed);
    } else if (!s_sender_ready) {
        bool visible = ((now / kLedBlinkIntervalMs) & 1U) == 0;
        setStatusLed(visible ? kLedAmber : kLedBlack);
    } else if (s_stream_active) {
        setStatusLed(kLedBlue);
    } else {
        setStatusLed(kLedGreen);
    }
}

void initializeStatusLed()
{
    pinMode(kLedDataPin, OUTPUT);
    pinMode(kLedClockPin, OUTPUT);
    digitalWrite(kLedDataPin, LOW);
    digitalWrite(kLedClockPin, LOW);
    setStatusLed(kLedAmber);
}

bool sameAddress(const uint8_t *left, const uint8_t *right)
{
    return left && right && memcmp(left, right, 6) == 0;
}

bool addPeer(const uint8_t *address)
{
    if (esp_now_is_peer_exist(address)) return true;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, address, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void queueHidCommand(HidCommand command)
{
    s_pending_hid_command = command;
}

void sendHidKey(uint8_t modifiers, uint8_t key)
{
    KeyReport pressed = {};
    pressed.modifiers = modifiers;
    pressed.keys[0] = key;
    s_usb_keyboard.sendReport(&pressed);

    KeyReport released = {};
    s_usb_keyboard.sendReport(&released);
}

void processHidCommand()
{
    if (s_pending_hid_command == HidCommand::None || !s_usb_hid.ready()) return;

    HidCommand command = s_pending_hid_command;
    s_pending_hid_command = HidCommand::None;
    if (command == HidCommand::StartContinuousInput) {
        constexpr uint8_t kRightAlt =
            1U << (HID_KEY_ALT_RIGHT - HID_KEY_CONTROL_LEFT);
        sendHidKey(kRightAlt, HID_KEY_SPACE);
    } else {
        sendHidKey(0, HID_KEY_ESCAPE);
    }
}

void removeSender()
{
    if (s_sender_ready) {
        logMessage("WARN", "Sender removed: age=%lums session=%08lx",
                   static_cast<unsigned long>(millis() - s_last_sender_ms),
                   static_cast<unsigned long>(s_session_id));
    }
    if (s_sender_ready && esp_now_is_peer_exist(s_sender)) {
        esp_now_del_peer(s_sender);
    }
    if (s_stream_active) queueHidCommand(HidCommand::CancelContinuousInput);
    memset(s_sender, 0, sizeof(s_sender));
    strncpy(s_sender_name, "Waiting", sizeof(s_sender_name));
    s_sender_ready = false;
    s_stream_active = false;
    s_pcm_primed = false;
    s_session_id = 0;
    if (s_pcm_buffer) xStreamBufferReset(s_pcm_buffer);
    if (s_audio_queue) xQueueReset(s_audio_queue);
    s_scanning = true;
    s_last_stats_ms = 0;
    s_channel = kFirstChannel;
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
    s_last_channel_ms = millis();
}

void sendOffer()
{
    if (!s_sender_ready) return;
    s_last_stats_ms = millis();
    ControlPacket offer = makeControl(PacketType::Offer, s_channel,
                                      "T-Dongle-S3", s_session_id, 0);
    esp_now_send(s_sender, reinterpret_cast<uint8_t *>(&offer), sizeof(offer));
}

void onEspNowReceive(const esp_now_recv_info_t *info,
                     const uint8_t *data, int length)
{
    if (!info || !info->src_addr || !data || length < static_cast<int>(sizeof(PacketHeader))) {
        return;
    }
    const PacketHeader *header = reinterpret_cast<const PacketHeader *>(data);
    if (!validHeader(*header, static_cast<size_t>(length))) return;

    PacketType type = static_cast<PacketType>(header->type);
    if (type == PacketType::Audio) {
        if (length != static_cast<int>(sizeof(AudioPacket)) ||
            !s_sender_ready || !s_stream_active ||
            header->session_id != s_session_id ||
            !sameAddress(info->src_addr, s_sender)) {
            return;
        }
        AudioEvent event = {};
        memcpy(event.source, info->src_addr, sizeof(event.source));
        memcpy(&event.packet, data, sizeof(event.packet));
        if (!s_audio_queue || xQueueSend(s_audio_queue, &event, 0) != pdTRUE) {
            incrementCounter(s_packets_dropped);
        }
        s_last_sender_ms = millis();
        return;
    }

    if (length != static_cast<int>(sizeof(ControlPacket))) return;
    const ControlPacket *packet = reinterpret_cast<const ControlPacket *>(data);
    if (!validDevice(packet->device)) return;

    ControlEvent event = {};
    memcpy(event.source, info->src_addr, sizeof(event.source));
    event.channel = info->rx_ctrl ? info->rx_ctrl->channel : s_channel;
    memcpy(&event.packet, packet, sizeof(event.packet));
    if (s_control_queue) xQueueSend(s_control_queue, &event, 0);
}

void pushPcm(const uint8_t *data, size_t length)
{
    if (!s_usb_active || !s_stream_active || !s_pcm_buffer ||
        !data || length == 0) return;
    size_t space = xStreamBufferSpacesAvailable(s_pcm_buffer);
    if (space < length) {
        uint8_t discard[640];
        size_t need = length - space;
        while (need > 0) {
            size_t take = need < sizeof(discard) ? need : sizeof(discard);
            size_t removed = xStreamBufferReceive(s_pcm_buffer, discard, take, 0);
            if (removed == 0) break;
            need -= removed;
        }
        incrementCounter(s_packets_dropped);
    }
    xStreamBufferSend(s_pcm_buffer, data, length, 0);
}

void decodeTask(void *)
{
    AudioEvent event = {};
    int16_t pcm[kFrameSamples];
    int16_t usb_pcm[kUsbPacketSamples];

    while (true) {
        if (xQueueReceive(s_audio_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (!s_stream_active || event.packet.header.session_id != s_session_id) continue;

        uint32_t sequence = event.packet.header.sequence;
        if (sequence != s_expected_sequence) {
            uint32_t missing = sequence - s_expected_sequence;
            if (missing < 0x80000000UL) {
                uint32_t conceal = missing > 3 ? 3 : missing;
                s_packets_lost += missing;
                if (s_has_last_usb_pcm) {
                    memcpy(usb_pcm, s_last_usb_pcm, sizeof(usb_pcm));
                } else {
                    memset(usb_pcm, 0, sizeof(usb_pcm));
                }
                while (conceal--) {
                    for (size_t i = 0; i < kUsbPacketSamples; ++i) {
                        usb_pcm[i] = static_cast<int16_t>(
                            (static_cast<int32_t>(usb_pcm[i]) * 3) / 4);
                    }
                    pushPcm(reinterpret_cast<uint8_t *>(usb_pcm),
                            sizeof(usb_pcm));
                }
            }
        }
        s_expected_sequence = sequence + 1;

        size_t decoded = s_decoder.decode(event.packet.audio,
                                          sizeof(event.packet.audio),
                                          pcm, kFrameSamples);
        if (decoded != kFrameSamples) {
            incrementCounter(s_packets_dropped);
            continue;
        }

        size_t resampled = PCMResample::linearMonoS16(
            pcm, decoded, usb_pcm, kUsbPacketSamples,
            kSampleRate, kUsbSampleRate);
        if (resampled != kUsbPacketSamples) {
            incrementCounter(s_packets_dropped);
            continue;
        }
        memcpy(s_last_usb_pcm, usb_pcm, sizeof(s_last_usb_pcm));
        s_has_last_usb_pcm = true;

        uint16_t peak = 0;
        for (size_t i = 0; i < decoded; i += 4) {
            int32_t value = pcm[i];
            if (value < 0) value = -value;
            if (value > peak) peak = static_cast<uint16_t>(value);
        }
        s_audio_level = peak;
        pushPcm(reinterpret_cast<uint8_t *>(usb_pcm),
                resampled * sizeof(int16_t));
        incrementCounter(s_packets_received);
    }
}

void usbTask(void *)
{
    TickType_t wake = xTaskGetTickCount();
    uint8_t output[kUsbFrameBytes] = {};
    uint8_t staging[kUsbFrameBytes + sizeof(int16_t)] = {};
    size_t pending_offset = 0;
    size_t pending_length = 0;
    bool was_usb_active = false;

    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1));

        if (!s_usb_active) {
            if (was_usb_active && s_pcm_buffer) xStreamBufferReset(s_pcm_buffer);
            was_usb_active = false;
            s_pcm_primed = false;
            pending_offset = 0;
            pending_length = 0;
            continue;
        }

        if (!was_usb_active) {
            if (s_pcm_buffer) xStreamBufferReset(s_pcm_buffer);
            was_usb_active = true;
            s_pcm_primed = false;
        }

        if (pending_offset < pending_length) {
            uint16_t written = s_usb_audio.write(
                output + pending_offset, pending_length - pending_offset);
            pending_offset += written;
            if (pending_offset < pending_length) {
                incrementCounter(s_usb_short_writes);
                continue;
            }
        }

        pending_offset = 0;
        pending_length = sizeof(output);
        memset(output, 0, sizeof(output));

        if (s_stream_active && s_pcm_buffer) {
            size_t available = xStreamBufferBytesAvailable(s_pcm_buffer);
            if (!s_pcm_primed && available >= kPrimeBytes) s_pcm_primed = true;

            if (s_pcm_primed) {
                size_t consume = kUsbFrameBytes;
                if (available > kHighWaterBytes) consume += sizeof(int16_t);
                else if (available < kLowWaterBytes) consume -= sizeof(int16_t);

                size_t received = xStreamBufferReceive(s_pcm_buffer, staging, consume, 0);
                if (received == consume) {
                    size_t copy = received < sizeof(output) ? received : sizeof(output);
                    memcpy(output, staging, copy);
                    if (copy < sizeof(output) && copy >= sizeof(int16_t)) {
                        memcpy(output + copy, output + copy - sizeof(int16_t),
                               sizeof(int16_t));
                    }
                } else {
                    s_pcm_primed = false;
                    incrementCounter(s_pcm_underruns);
                }
            }
        }

        uint16_t written = s_usb_audio.write(output, sizeof(output));
        pending_offset = written;
        if (pending_offset < pending_length) {
            incrementCounter(s_usb_short_writes);
        }
    }
}

void usbAudioEvent(void *, esp_event_base_t event_base, int32_t event_id,
                   void *event_data)
{
    if (event_base != ARDUINO_USB_AUDIO_CARD_EVENTS ||
        event_id != ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT || !event_data) {
        return;
    }
    auto *data = static_cast<arduino_usb_audio_card_event_data_t *>(event_data);
    if (data->interface_enable.interface == UAC_INTERFACE_MIC) {
        s_usb_active = data->interface_enable.enable;
        if (!s_usb_active) s_pcm_primed = false;
    }
}

void beginSession(const ControlEvent &event)
{
    if (!sameAddress(event.source, s_sender)) return;
    if (s_stream_active &&
        s_session_id == event.packet.header.session_id) {
        return;
    }
    s_session_id = event.packet.header.session_id;
    s_expected_sequence = 0;
    s_packets_received = 0;
    s_packets_lost = 0;
    s_packets_dropped = 0;
    s_pcm_underruns = 0;
    s_usb_short_writes = 0;
    s_audio_level = 0;
    s_has_last_usb_pcm = false;
    s_stream_active = true;
    s_pcm_primed = false;
    s_decoder.reset();
    xQueueReset(s_audio_queue);
    xStreamBufferReset(s_pcm_buffer);
    queueHidCommand(HidCommand::StartContinuousInput);
    logMessage("RADIO", "Session started: id=%08lx channel=%u",
               static_cast<unsigned long>(s_session_id), s_channel);
}

void endSession()
{
    uint32_t stopped_session = s_session_id;
    bool was_active = s_stream_active;
    s_stream_active = false;
    s_pcm_primed = false;
    s_audio_level = 0;
    s_session_id = 0;
    xQueueReset(s_audio_queue);
    xStreamBufferReset(s_pcm_buffer);
    if (was_active) queueHidCommand(HidCommand::CancelContinuousInput);
    if (was_active || stopped_session != 0) {
        logMessage("RADIO", "Session stopped: id=%08lx",
                   static_cast<unsigned long>(stopped_session));
    }
}

void sendStats(uint32_t now)
{
    if (!s_sender_ready || now - s_last_stats_ms < 1000) return;
    StatsPacket packet = makeStats(
        s_session_id, 0, s_packets_received, s_packets_lost,
        s_packets_dropped, s_pcm_underruns, s_usb_short_writes);
    if (esp_now_send(s_sender, reinterpret_cast<uint8_t *>(&packet),
                     sizeof(packet)) == ESP_OK) {
        s_last_stats_ms = now;
    }
}

void processControlEvents()
{
    ControlEvent event = {};
    while (xQueueReceive(s_control_queue, &event, 0) == pdTRUE) {
        PacketType type = static_cast<PacketType>(event.packet.header.type);
        if (type == PacketType::Discover) {
            if (s_sender_ready && !sameAddress(event.source, s_sender)) continue;
            if (!addPeer(event.source)) continue;
            memcpy(s_sender, event.source, sizeof(s_sender));
            strncpy(s_sender_name, event.packet.device.name,
                    sizeof(s_sender_name) - 1);
            s_sender_name[sizeof(s_sender_name) - 1] = '\0';
            s_channel = event.channel;
            s_sender_ready = true;
            s_scanning = false;
            s_last_sender_ms = millis();
            logMessage("RADIO",
                       "Sender paired: %02X:%02X:%02X:%02X:%02X:%02X channel=%u name=%s",
                       s_sender[0], s_sender[1], s_sender[2], s_sender[3],
                       s_sender[4], s_sender[5], s_channel, s_sender_name);
            sendOffer();
            continue;
        }
        if (!s_sender_ready || !sameAddress(event.source, s_sender)) continue;
        s_last_sender_ms = millis();
        if (type == PacketType::Start) beginSession(event);
        else if (type == PacketType::Stop) endSession();
        else if (type == PacketType::Heartbeat &&
                 event.packet.header.session_id == 0 && s_stream_active) {
            endSession();
        }
    }
}

bool initializeRadio()
{
    WiFi.mode(WIFI_STA);
    uint32_t start = millis();
    while (!WiFi.STA.started() && millis() - start < 1000) delay(10);
    if (!WiFi.STA.started()) {
        logMessage("ERROR", "Wi-Fi station failed to start");
        return false;
    }
    esp_err_t result = esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        logMessage("ERROR", "Set Wi-Fi channel failed: err=%d", result);
        return false;
    }
    result = esp_now_init();
    if (result != ESP_OK) {
        logMessage("ERROR", "ESP-NOW initialization failed: err=%d", result);
        return false;
    }
    result = esp_now_register_recv_cb(onEspNowReceive);
    if (result != ESP_OK) {
        logMessage("ERROR", "ESP-NOW receive callback failed: err=%d", result);
        return false;
    }
    uint8_t address[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, address);
    logMessage("RADIO",
               "ESP-NOW ready: %02X:%02X:%02X:%02X:%02X:%02X scan-channel=%u",
               address[0], address[1], address[2], address[3], address[4],
               address[5], s_channel);
    return true;
}

} // namespace

void setup()
{
#if WIRELESS_MIC_DEBUG_ENABLED
    s_debug_uart.begin(kDebugUartBaud, SERIAL_8N1,
                       kDebugUartRxPin, kDebugUartTxPin);
    delay(20);
    logMessage("BOOT", "Wireless microphone receiver");
    logMessage("BOOT", "Debug UART0: TX=GPIO%d RX=GPIO%d baud=%lu",
               kDebugUartTxPin, kDebugUartRxPin,
               static_cast<unsigned long>(kDebugUartBaud));
#endif

    pinMode(kBootPin, INPUT_PULLUP);
    initializeStatusLed();

    s_control_queue = xQueueCreate(kControlQueueDepth, sizeof(ControlEvent));
    s_audio_queue = xQueueCreate(kAudioQueueDepth, sizeof(AudioEvent));
    s_pcm_buffer = xStreamBufferCreate(kPcmBufferBytes, 1);
    if (!s_control_queue || !s_audio_queue || !s_pcm_buffer) {
        setFatalError("Failed to allocate radio or PCM buffers");
        return;
    }
    if (!initializeRadio()) {
        setFatalError("Radio initialization failed");
        return;
    }

    PCMFormat format = {};
    format.sampleRate = kSampleRate;
    format.channels = kChannels;
    format.bitsPerSample = kBitsPerSample;
    if (!s_decoder.begin(format)) {
        setFatalError("G.722 decoder initialization failed");
        return;
    }

    BaseType_t decode_created = xTaskCreatePinnedToCore(
        decodeTask, "wmic_decode", kDecodeTaskStackBytes,
        nullptr, 10, &s_decode_task, 0);
    BaseType_t usb_created = xTaskCreatePinnedToCore(
        usbTask, "wmic_usb", kUsbTaskStackBytes,
        nullptr, 11, &s_usb_task, 1);
    if (decode_created != pdPASS || usb_created != pdPASS) {
        setFatalError("Audio task creation failed");
        return;
    }

    if (!USB.VID(kUsbVendorId) || !USB.PID(kUsbProductId)) {
        setFatalError("USB VID/PID configuration failed");
        return;
    }
    USB.manufacturerName("LILYGO");
    USB.productName("LILYGO Wireless Microphone");
    USB.serialNumber(kUsbSerialNumber);
    s_usb_audio.onEvent(usbAudioEvent);
    if (!s_usb_audio.begin()) {
        setFatalError("USB Audio Class initialization failed");
        return;
    }
    s_usb_keyboard.begin();
    logMessage(
        "USB",
        "Starting UAC microphone: vid=%04X pid=%04X rate=%lu channels=%u bits=%u serial=%s",
        kUsbVendorId, kUsbProductId,
        static_cast<unsigned long>(kUsbSampleRate), kChannels, kBitsPerSample,
        kUsbSerialNumber);
    if (!USB.begin()) {
        setFatalError("TinyUSB initialization failed");
        return;
    }
    logMessage("BOOT", "Receiver initialization complete");
}

void loop()
{
    uint32_t now = millis();
    updateStatusLed(now);
    if (s_fatal_error) {
        delay(20);
        return;
    }

    processControlEvents();
    processHidCommand();
    sendStats(now);

#if WIRELESS_MIC_DEBUG_ENABLED
    if (now - s_last_debug_stats_ms >= kDebugStatsIntervalMs) {
        s_last_debug_stats_ms = now;
        size_t pcm_bytes = s_pcm_buffer ?
                           xStreamBufferBytesAvailable(s_pcm_buffer) : 0;
        UBaseType_t queued = s_audio_queue ?
                             uxQueueMessagesWaiting(s_audio_queue) : 0;
        logMessage(
            "STAT",
            "usb=%u sender=%u stream=%u ch=%u session=%08lx rx=%lu lost=%lu drop=%lu underrun=%lu short=%lu pcm=%u queue=%u",
            s_usb_active ? 1U : 0U, s_sender_ready ? 1U : 0U,
            s_stream_active ? 1U : 0U, s_channel,
            static_cast<unsigned long>(s_session_id),
            static_cast<unsigned long>(s_packets_received),
            static_cast<unsigned long>(s_packets_lost),
            static_cast<unsigned long>(s_packets_dropped),
            static_cast<unsigned long>(s_pcm_underruns),
            static_cast<unsigned long>(s_usb_short_writes),
            static_cast<unsigned>(pcm_bytes), static_cast<unsigned>(queued));
    }
#endif

    if (s_scanning && now - s_last_channel_ms >= kChannelDwellMs) {
        s_last_channel_ms = now;
        ++s_channel;
        if (s_channel > kLastChannel) s_channel = kFirstChannel;
        esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
    }
    if (s_sender_ready && now - s_last_sender_ms > kSenderTimeoutMs) {
        removeSender();
    }
    if (digitalRead(kBootPin) == LOW) {
        delay(30);
        if (digitalRead(kBootPin) == LOW) {
            removeSender();
            while (digitalRead(kBootPin) == LOW) delay(10);
        }
    }
    delay(5);
}
