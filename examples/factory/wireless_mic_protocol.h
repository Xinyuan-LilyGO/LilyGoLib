/**
 * @file      wireless_mic_protocol.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-18
 *
 * Defines the versioned ESP-NOW control and G.722 audio packets shared by the
 * wireless microphone sender and USB receiver.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace wireless_mic {

static constexpr uint32_t kMagic = 0x43494D57UL; // "WMIC" on the wire.
static constexpr uint8_t kVersion = 1;
static constexpr uint32_t kSampleRate = 16000;
static constexpr uint32_t kUsbSampleRate = 48000;
static constexpr uint8_t kChannels = 1;
static constexpr uint8_t kBitsPerSample = 16;
static constexpr size_t kFrameSamples = 320;
static constexpr size_t kEncodedFrameBytes = 160;
static constexpr uint8_t kDefaultChannel = 6;
static constexpr uint8_t kFirstChannel = 1;
static constexpr uint8_t kLastChannel = 13;
static constexpr uint32_t kFrameDurationMs = 20;
static constexpr size_t kDeviceNameLength = 20;

enum class PacketType : uint8_t {
    Discover = 1,
    Offer = 2,
    Start = 3,
    Audio = 4,
    Stop = 5,
    Heartbeat = 6,
    Stats = 7,
};

enum class Codec : uint8_t {
    G722 = 1,
};

struct __attribute__((packed)) PacketHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t payload_size;
    uint32_t session_id;
    uint32_t sequence;
};

struct __attribute__((packed)) DevicePayload {
    uint32_t sample_rate;
    uint16_t frame_samples;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint8_t codec;
    uint8_t channel;
    char name[kDeviceNameLength];
};

struct __attribute__((packed)) ControlPacket {
    PacketHeader header;
    DevicePayload device;
};

struct __attribute__((packed)) AudioPacket {
    PacketHeader header;
    uint8_t audio[kEncodedFrameBytes];
};

struct __attribute__((packed)) StatsPayload {
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t packets_dropped;
    uint32_t pcm_underruns;
    uint32_t usb_short_writes;
};

struct __attribute__((packed)) StatsPacket {
    PacketHeader header;
    StatsPayload stats;
};

static_assert(sizeof(PacketHeader) == 16, "Wireless microphone header changed");
static_assert(sizeof(AudioPacket) <= 250, "Audio packet must fit ESP-NOW v1");
static_assert(sizeof(StatsPacket) <= 250, "Stats packet must fit ESP-NOW v1");

inline PacketHeader makeHeader(PacketType type, uint16_t payload_size,
                               uint32_t session_id, uint32_t sequence)
{
    PacketHeader header = {};
    header.magic = kMagic;
    header.version = kVersion;
    header.type = static_cast<uint8_t>(type);
    header.payload_size = payload_size;
    header.session_id = session_id;
    header.sequence = sequence;
    return header;
}

inline ControlPacket makeControl(PacketType type, uint8_t channel,
                                 const char *name, uint32_t session_id = 0,
                                 uint32_t sequence = 0)
{
    ControlPacket packet = {};
    packet.header = makeHeader(type, sizeof(packet.device), session_id, sequence);
    packet.device.sample_rate = kSampleRate;
    packet.device.frame_samples = kFrameSamples;
    packet.device.channels = kChannels;
    packet.device.bits_per_sample = kBitsPerSample;
    packet.device.codec = static_cast<uint8_t>(Codec::G722);
    packet.device.channel = channel;
    if (name) {
        strncpy(packet.device.name, name, sizeof(packet.device.name) - 1);
    }
    return packet;
}

inline StatsPacket makeStats(uint32_t session_id, uint32_t sequence,
                             uint32_t packets_received, uint32_t packets_lost,
                             uint32_t packets_dropped, uint32_t pcm_underruns,
                             uint32_t usb_short_writes)
{
    StatsPacket packet = {};
    packet.header = makeHeader(PacketType::Stats, sizeof(packet.stats),
                               session_id, sequence);
    packet.stats.packets_received = packets_received;
    packet.stats.packets_lost = packets_lost;
    packet.stats.packets_dropped = packets_dropped;
    packet.stats.pcm_underruns = pcm_underruns;
    packet.stats.usb_short_writes = usb_short_writes;
    return packet;
}

inline bool validHeader(const PacketHeader &header, size_t packet_size)
{
    return header.magic == kMagic && header.version == kVersion &&
           packet_size == sizeof(PacketHeader) + header.payload_size;
}

inline bool validDevice(const DevicePayload &device)
{
    return device.sample_rate == kSampleRate &&
           device.frame_samples == kFrameSamples &&
           device.channels == kChannels &&
           device.bits_per_sample == kBitsPerSample &&
           device.codec == static_cast<uint8_t>(Codec::G722);
}

} // namespace wireless_mic
