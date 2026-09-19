/**
 * @file      LilyGoNfcService.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-25
 *
 */
#include "nfc/LilyGoNfcService.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)

#include <stdio.h>
#include <string.h>
#include <demo_ce.h>

extern RfalNfcClass NFCReader;

static LilyGoNfcService *activeService = nullptr;
static const uint32_t kReleaseCheckIntervalMs = 180U;
static const uint8_t kReleaseMissingLimit = 3U;
static const uint16_t kDefaultEmulationDiscoveryMs = 1000U;
static const uint16_t kWifiAuthWpa2Psk = 0x0020U;
static const uint16_t kWifiEncAes = 0x0010U;

static void safeCopy(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dstSize, "%s", src);
}

static void appendText(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0 || !src) return;
    const size_t used = strlen(dst);
    if (used >= dstSize - 1) return;
    snprintf(dst + used, dstSize - used, "%s", src);
}

static bool appendBytes(uint8_t *buf, uint32_t bufLen, uint32_t *pos,
                        const uint8_t *data, uint32_t len)
{
    if (!buf || !pos || ((len > 0U) && !data) || ((*pos + len) > bufLen)) {
        return false;
    }
    if (len > 0U) {
        memcpy(&buf[*pos], data, len);
        *pos += len;
    }
    return true;
}

static bool appendByte(uint8_t *buf, uint32_t bufLen, uint32_t *pos, uint8_t value)
{
    return appendBytes(buf, bufLen, pos, &value, 1U);
}

static bool appendU16BE(uint8_t *buf, uint32_t bufLen, uint32_t *pos, uint16_t value)
{
    const uint8_t tmp[2] = {
        static_cast<uint8_t>(value >> 8U),
        static_cast<uint8_t>(value & 0xFFU)
    };
    return appendBytes(buf, bufLen, pos, tmp, sizeof(tmp));
}

static bool appendWpsAttributeHeader(uint8_t *buf, uint32_t bufLen, uint32_t *pos,
                                     uint16_t tag, uint16_t len)
{
    return appendU16BE(buf, bufLen, pos, tag) && appendU16BE(buf, bufLen, pos, len);
}

static bool appendWpsAttributeBytes(uint8_t *buf, uint32_t bufLen, uint32_t *pos,
                                    uint16_t tag, const uint8_t *data, uint16_t len)
{
    return appendWpsAttributeHeader(buf, bufLen, pos, tag, len) &&
           appendBytes(buf, bufLen, pos, data, len);
}

static bool appendWpsAttributeU8(uint8_t *buf, uint32_t bufLen, uint32_t *pos,
                                 uint16_t tag, uint8_t value)
{
    return appendWpsAttributeHeader(buf, bufLen, pos, tag, 1U) &&
           appendByte(buf, bufLen, pos, value);
}

static bool appendWpsAttributeU16(uint8_t *buf, uint32_t bufLen, uint32_t *pos,
                                  uint16_t tag, uint16_t value)
{
    return appendWpsAttributeHeader(buf, bufLen, pos, tag, 2U) &&
           appendU16BE(buf, bufLen, pos, value);
}

static bool startsWith(const char *text, const char *prefix)
{
    if (!text || !prefix) return false;
    const size_t prefixLen = strlen(prefix);
    return strncmp(text, prefix, prefixLen) == 0;
}

static uint8_t detectUriPrefix(const char *uri, const char **rest)
{
    if (!uri) {
        if (rest) *rest = "";
        return 0x00U;
    }

    struct PrefixMap {
        const char *prefix;
        uint8_t code;
    };

    static const PrefixMap prefixes[] = {
        {"http://www.", 0x01U},
        {"https://www.", 0x02U},
        {"http://", 0x03U},
        {"https://", 0x04U},
        {"tel:", 0x05U},
        {"mailto:", 0x06U},
    };

    for (uint8_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (startsWith(uri, prefixes[i].prefix)) {
            if (rest) *rest = uri + strlen(prefixes[i].prefix);
            return prefixes[i].code;
        }
    }

    if (rest) *rest = uri;
    return 0x00U;
}

static bool finishShortNdefFile(uint8_t *file, uint32_t fileLen, uint32_t *pos,
                                uint32_t messageLen)
{
    if (!file || !pos || *pos > fileLen || messageLen > 255U ||
            messageLen + 2U != *pos) {
        return false;
    }
    file[0] = static_cast<uint8_t>(messageLen >> 8U);
    file[1] = static_cast<uint8_t>(messageLen & 0xFFU);
    return true;
}

static bool buildUriNdefFile(uint8_t *file, uint32_t fileLen, const char *uri,
                             uint32_t *ndefFileLen)
{
    if (!file || !ndefFileLen || !uri || uri[0] == '\0') return false;

    const char *rest = nullptr;
    const uint8_t prefixCode = detectUriPrefix(uri, &rest);
    const uint32_t restLen = strlen(rest);
    const uint32_t payloadLen = 1U + restLen;
    const uint32_t messageLen = 3U + 1U + payloadLen;
    uint32_t pos = 2U;

    if (payloadLen > 255U || (messageLen + 2U) > fileLen) return false;

    if (!appendByte(file, fileLen, &pos, 0xD1U) ||
            !appendByte(file, fileLen, &pos, 0x01U) ||
            !appendByte(file, fileLen, &pos, static_cast<uint8_t>(payloadLen)) ||
            !appendByte(file, fileLen, &pos, 'U') ||
            !appendByte(file, fileLen, &pos, prefixCode) ||
            !appendBytes(file, fileLen, &pos, reinterpret_cast<const uint8_t *>(rest), restLen)) {
        return false;
    }

    if (!finishShortNdefFile(file, fileLen, &pos, messageLen)) return false;
    *ndefFileLen = pos;
    return true;
}

static bool buildTextNdefFile(uint8_t *file, uint32_t fileLen, const char *text,
                              uint32_t *ndefFileLen)
{
    if (!file || !ndefFileLen || !text) return false;

    static const char lang[] = "en";
    const uint32_t textLen = strlen(text);
    const uint32_t payloadLen = 1U + (sizeof(lang) - 1U) + textLen;
    const uint32_t messageLen = 3U + 1U + payloadLen;
    uint32_t pos = 2U;

    if (textLen == 0U || payloadLen > 255U || (messageLen + 2U) > fileLen) return false;

    if (!appendByte(file, fileLen, &pos, 0xD1U) ||
            !appendByte(file, fileLen, &pos, 0x01U) ||
            !appendByte(file, fileLen, &pos, static_cast<uint8_t>(payloadLen)) ||
            !appendByte(file, fileLen, &pos, 'T') ||
            !appendByte(file, fileLen, &pos, static_cast<uint8_t>(sizeof(lang) - 1U)) ||
            !appendBytes(file, fileLen, &pos, reinterpret_cast<const uint8_t *>(lang), sizeof(lang) - 1U) ||
            !appendBytes(file, fileLen, &pos, reinterpret_cast<const uint8_t *>(text), textLen)) {
        return false;
    }

    if (!finishShortNdefFile(file, fileLen, &pos, messageLen)) return false;
    *ndefFileLen = pos;
    return true;
}

static bool buildWifiOobPayload(uint8_t *buf, uint32_t bufLen, const char *ssid,
                                const char *password, uint16_t authType,
                                uint16_t encType, uint32_t *payloadLen)
{
    static const uint8_t macSkip[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!buf || !payloadLen || !ssid || ssid[0] == '\0') return false;

    const char *key = password ? password : "";
    const uint16_t ssidLen = static_cast<uint16_t>(strlen(ssid));
    const uint16_t passwordLen = static_cast<uint16_t>(strlen(key));
    const uint16_t credentialLen = static_cast<uint16_t>(
                                       5U +
                                       4U + ssidLen +
                                       6U +
                                       6U +
                                       4U + passwordLen +
                                       10U);
    uint32_t pos = 0U;

    if (!appendWpsAttributeU8(buf, bufLen, &pos, 0x104AU, 0x10U) ||
            !appendWpsAttributeHeader(buf, bufLen, &pos, 0x100EU, credentialLen) ||
            !appendWpsAttributeU8(buf, bufLen, &pos, 0x1026U, 0x01U) ||
            !appendWpsAttributeBytes(buf, bufLen, &pos, 0x1045U,
                                     reinterpret_cast<const uint8_t *>(ssid), ssidLen) ||
            !appendWpsAttributeU16(buf, bufLen, &pos, 0x1003U, authType) ||
            !appendWpsAttributeU16(buf, bufLen, &pos, 0x100FU, encType) ||
            !appendWpsAttributeBytes(buf, bufLen, &pos, 0x1027U,
                                     reinterpret_cast<const uint8_t *>(key), passwordLen) ||
            !appendWpsAttributeBytes(buf, bufLen, &pos, 0x1020U, macSkip, sizeof(macSkip))) {
        return false;
    }

    *payloadLen = pos;
    return true;
}

static bool buildWifiNdefFile(uint8_t *file, uint32_t fileLen, const char *ssid,
                              const char *password, uint16_t authType,
                              uint16_t encType, uint32_t *ndefFileLen)
{
    static const uint8_t mimeType[] = "application/vnd.wfa.wsc";
    uint8_t payload[160];
    uint32_t payloadLen = 0U;
    uint32_t pos = 2U;

    if (!buildWifiOobPayload(payload, sizeof(payload), ssid, password,
                             authType, encType, &payloadLen)) {
        return false;
    }

    const uint32_t messageLen = 3U + (sizeof(mimeType) - 1U) + payloadLen;
    if (payloadLen > 255U || messageLen > 255U || (messageLen + 2U) > fileLen) {
        return false;
    }

    if (!appendByte(file, fileLen, &pos, 0xD2U) ||
            !appendByte(file, fileLen, &pos, static_cast<uint8_t>(sizeof(mimeType) - 1U)) ||
            !appendByte(file, fileLen, &pos, static_cast<uint8_t>(payloadLen)) ||
            !appendBytes(file, fileLen, &pos, mimeType, sizeof(mimeType) - 1U) ||
            !appendBytes(file, fileLen, &pos, payload, payloadLen)) {
        return false;
    }

    if (!finishShortNdefFile(file, fileLen, &pos, messageLen)) return false;
    *ndefFileLen = pos;
    return true;
}

static void copyBufferAsText(char *dst, size_t dstSize, const uint8_t *buffer, uint32_t length)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!buffer || length == 0) return;

    const size_t limit = (length < (dstSize - 1)) ? length : (dstSize - 1);
    for (size_t i = 0; i < limit; ++i) {
        const uint8_t c = buffer[i];
        dst[i] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '.';
    }
    dst[limit] = '\0';
}

static void copyConstBuffer(char *dst, size_t dstSize, const ndefConstBuffer &buf)
{
    copyBufferAsText(dst, dstSize, buf.buffer, buf.length);
}

static void copyConstBuffer8(char *dst, size_t dstSize, const ndefConstBuffer8 &buf)
{
    copyBufferAsText(dst, dstSize, buf.buffer, buf.length);
}

static void bytesToHex(const uint8_t *bytes, uint32_t length, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!bytes || length == 0) return;

    size_t pos = 0;
    for (uint32_t i = 0; i < length && pos < dstSize - 1; ++i) {
        const int written = snprintf(dst + pos, dstSize - pos, "%02X%s", bytes[i], (i + 1 < length) ? " " : "");
        if (written <= 0) break;
        pos += static_cast<size_t>(written);
        if (pos >= dstSize - 4 && i + 1 < length) {
            snprintf(dst + ((dstSize > 4) ? dstSize - 4 : 0), (dstSize > 4) ? 4 : dstSize, "...");
            break;
        }
    }
}

static const char *rfalDeviceTypeName(rfalNfcDevType type)
{
    switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
    case RFAL_NFC_POLL_TYPE_NFCA:
        return "NFC-A";
    case RFAL_NFC_LISTEN_TYPE_NFCB:
    case RFAL_NFC_POLL_TYPE_NFCB:
        return "NFC-B";
    case RFAL_NFC_LISTEN_TYPE_NFCF:
    case RFAL_NFC_POLL_TYPE_NFCF:
        return "NFC-F";
    case RFAL_NFC_LISTEN_TYPE_NFCV:
    case RFAL_NFC_POLL_TYPE_NFCV:
        return "NFC-V";
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
        return "ST25TB";
    case RFAL_NFC_LISTEN_TYPE_AP2P:
    case RFAL_NFC_POLL_TYPE_AP2P:
        return "AP2P";
    default:
        return "Unknown";
    }
}

static const char *rfalInterfaceName(rfalNfcRfInterface interfaceType)
{
    switch (interfaceType) {
    case RFAL_NFC_INTERFACE_RF:
        return "RF";
    case RFAL_NFC_INTERFACE_ISODEP:
        return "ISO-DEP";
    case RFAL_NFC_INTERFACE_NFCDEP:
        return "NFC-DEP";
    default:
        return "Unknown";
    }
}

static const char *ndefDeviceName(ndefDeviceType type)
{
    switch (type) {
    case NDEF_DEV_T1T:
        return "T1T";
    case NDEF_DEV_T2T:
        return "T2T";
    case NDEF_DEV_T3T:
        return "T3T";
    case NDEF_DEV_T4T:
        return "T4T";
    case NDEF_DEV_T5T:
        return "T5T";
    case NDEF_DEV_NONE:
    default:
        return "--";
    }
}

static const char *nfcaListenTypeName(rfalNfcaListenDeviceType type)
{
    switch (type) {
    case RFAL_NFCA_T1T:
        return "NFC-A Type 1";
    case RFAL_NFCA_T2T:
        return "NFC-A Type 2";
    case RFAL_NFCA_T4T:
        return "NFC-A Type 4";
    case RFAL_NFCA_NFCDEP:
        return "NFC-A P2P";
    case RFAL_NFCA_T4T_NFCDEP:
        return "NFC-A Type 4/P2P";
    default:
        return "NFC-A";
    }
}

static bool nfcaLooksMifareClassic(const rfalNfcaListenDevice &nfca)
{
    return (nfca.selRes.sak & 0x08U) != 0U;
}

static bool isNdefNotAvailable(ReturnCode err)
{
    return err == ST_ERR_NOTSUPP ||
           err == ST_ERR_REQUEST ||
           err == ST_ERR_NOTFOUND ||
           err == ST_ERR_PROTO;
}

static bool isPresenceResponse(ReturnCode err)
{
    return err == ST_ERR_NONE ||
           err == ST_ERR_RF_COLLISION ||
           err == ST_ERR_PAR ||
           err == ST_ERR_CRC ||
           err == ST_ERR_FRAMING ||
           err == ST_ERR_PROTO;
}

static bool preparePresencePolling(ReturnCode initErr)
{
    if (initErr != ST_ERR_NONE) {
        return false;
    }

    RfalRfClass *rf = NFCReader.getRfalRf();
    if (!rf) {
        return false;
    }

    return rf->rfalFieldOnAndStartGT() == ST_ERR_NONE;
}

static const char *ndefStateName(ndefState state)
{
    switch (state) {
    case NDEF_STATE_INITIALIZED:
        return "Empty";
    case NDEF_STATE_READWRITE:
        return "ReadWrite";
    case NDEF_STATE_READONLY:
        return "ReadOnly";
    case NDEF_STATE_INVALID:
    default:
        return "Invalid";
    }
}

static const char *wifiAuthenticationName(uint8_t authentication)
{
    switch (authentication) {
    case NDEF_WIFI_AUTHENTICATION_NONE:
        return "Open";
    case NDEF_WIFI_AUTHENTICATION_WPAPSK:
        return "WPA-PSK";
    case NDEF_WIFI_AUTHENTICATION_SHARED:
        return "Shared";
    case NDEF_WIFI_AUTHENTICATION_WPA:
        return "WPA";
    case NDEF_WIFI_AUTHENTICATION_WPA2:
        return "WPA2";
    case NDEF_WIFI_AUTHENTICATION_WPA2PSK:
        return "WPA2-PSK";
    default:
        return "Unknown";
    }
}

static void fillGenericRecord(NdefClass &ndef, const ndefRecord *record, LilyGoNfcRecord &out)
{
    uint8_t tnf = 0;
    ndefConstBuffer8 recordType = {};
    ndefConstBuffer payload = {};

    safeCopy(out.title, sizeof(out.title), "Raw");
    safeCopy(out.value, sizeof(out.value), "--");
    safeCopy(out.detail, sizeof(out.detail), "");

    if (ndef.ndefRecordGetType(record, &tnf, &recordType) == ST_ERR_NONE) {
        char typeText[64];
        copyConstBuffer8(typeText, sizeof(typeText), recordType);
        snprintf(out.detail, sizeof(out.detail), "TNF %u  Type %s", tnf, typeText[0] ? typeText : "--");
    }

    if (ndef.ndefRecordGetPayload(record, &payload) == ST_ERR_NONE && payload.buffer && payload.length) {
        bytesToHex(payload.buffer, payload.length, out.value, sizeof(out.value));
    }
}

static void appendVCardField(NdefClass &ndef, const ndefType *vCard, const char *field,
                             const char *label, char *dst, size_t dstSize)
{
    ndefConstBuffer bufType = { reinterpret_cast<uint8_t *>(const_cast<char *>(field)),
                                static_cast<uint32_t>(strlen(field)) };
    ndefConstBuffer subType = {};
    ndefConstBuffer value = {};

    ndef.ndefGetVCard(vCard, &bufType, &subType, &value);
    if (!value.buffer || value.length == 0) return;

    char valueText[LILYGO_NFC_MAX_TEXT_LEN];
    copyConstBuffer(valueText, sizeof(valueText), value);

    if (dst[0] != '\0') appendText(dst, dstSize, "\n");
    appendText(dst, dstSize, label);
    appendText(dst, dstSize, ": ");
    appendText(dst, dstSize, valueText);
}

LilyGoNfcService LilyGoNfc;

LilyGoNfcService::LilyGoNfcService() :
    _ndef(&NFCReader),
    _mode(LILYGO_NFC_MODE_IDLE),
    _emulationRxData(nullptr),
    _emulationRxLen(nullptr),
    _emulationNdefFileLen(0),
    _running(false),
    _activeHandled(false),
    _hasNdefContext(false),
    _activeCardValid(false),
    _activeNfcaValid(false),
    _deactivateRequested(false),
    _emulationExchangePrimed(false),
    _emulationReaderActive(false),
    _activeType(RFAL_NFC_LISTEN_TYPE_NFCA),
    _activeUidLen(0),
    _lastPresenceCheckMs(0),
    _missingPresenceCount(0)
{
    memset(&_config, 0, sizeof(_config));
    memset(&_emulationConfig, 0, sizeof(_emulationConfig));
    memset(&_result, 0, sizeof(_result));
    memset(&_emulationStatus, 0, sizeof(_emulationStatus));
    memset(&_emulationDiscover, 0, sizeof(_emulationDiscover));
    memset(_activeUid, 0, sizeof(_activeUid));
    memset(&_activeNfca, 0, sizeof(_activeNfca));
    memset(_rawBuffer, 0, sizeof(_rawBuffer));
    memset(_emulationNdefFile, 0, sizeof(_emulationNdefFile));
    memset(_emulationTxBuf, 0, sizeof(_emulationTxBuf));
    _emulationNfcfNfcid2[0] = 0x02;
    _emulationNfcfNfcid2[1] = 0xFE;
    _emulationNfcfNfcid2[2] = 0x11;
    _emulationNfcfNfcid2[3] = 0x22;
    _emulationNfcfNfcid2[4] = 0x33;
    _emulationNfcfNfcid2[5] = 0x44;
    _emulationNfcfNfcid2[6] = 0x55;
    _emulationNfcfNfcid2[7] = 0x66;
    _result.state = LILYGO_NFC_STATE_IDLE;
    _emulationStatus.state = LILYGO_NFC_STATE_IDLE;
}

bool LilyGoNfcService::beginReader(LilyGoNfcEventCallback callback, void *userData)
{
    LilyGoNfcReaderConfig config = {};
    config.technologies = RFAL_NFC_POLL_TECH_A |
                          RFAL_NFC_POLL_TECH_B |
                          RFAL_NFC_POLL_TECH_F |
                          RFAL_NFC_POLL_TECH_V
#if defined(RFAL_NFC_POLL_TECH_ST25TB)
                          | RFAL_NFC_POLL_TECH_ST25TB
#endif
                          ;
    config.discoveryDurationMs = 1000U;
    config.wakeupEnabled = false;
    config.callback = callback;
    config.userData = userData;
    return beginReader(config);
}

bool LilyGoNfcService::beginReader(const LilyGoNfcReaderConfig &config)
{
    stop();

    _mode = LILYGO_NFC_MODE_READER;
    _config = config;
    if (_config.technologies == RFAL_NFC_TECH_NONE) {
        _config.technologies = RFAL_NFC_POLL_TECH_A;
    }
    if (_config.discoveryDurationMs == 0) {
        _config.discoveryDurationMs = 1000U;
    }

    activeService = this;
    _running = true;
    _activeHandled = false;
    _hasNdefContext = false;
    _activeCardValid = false;
    _activeNfcaValid = false;
    _deactivateRequested = false;
    _activeUidLen = 0;
    _missingPresenceCount = 0;
    resetResult();
    _result.state = LILYGO_NFC_STATE_STARTING;

    ReturnCode err = NFCReader.rfalNfcInitialize();
    if (err != ST_ERR_NONE) {
        setError(err, "rfalNfcInitialize failed");
        _running = false;
        _mode = LILYGO_NFC_MODE_IDLE;
        _result.state = LILYGO_NFC_STATE_ERROR;
        emit(LILYGO_NFC_EVENT_START_FAILED);
        if (activeService == this) activeService = nullptr;
        return false;
    }

    if (!startDiscovery()) {
        _running = false;
        _mode = LILYGO_NFC_MODE_IDLE;
        _result.state = LILYGO_NFC_STATE_ERROR;
        emit(LILYGO_NFC_EVENT_START_FAILED);
        if (activeService == this) activeService = nullptr;
        return false;
    }

    _result.state = LILYGO_NFC_STATE_POLLING;
    emit(LILYGO_NFC_EVENT_STARTED);
    return true;
}

bool LilyGoNfcService::beginEmulationUrl(const char *url,
        LilyGoNfcEmulationTech tech,
        LilyGoNfcEmulationCallback callback,
        void *userData)
{
    LilyGoNfcEmulationConfig config = {};
    config.kind = LILYGO_NFC_EMULATION_URL;
    config.tech = tech;
    config.templateName = "URL";
    config.url = url;
    config.discoveryDurationMs = kDefaultEmulationDiscoveryMs;
    config.callback = callback;
    config.userData = userData;
    return beginEmulation(config);
}

bool LilyGoNfcService::beginEmulationText(const char *text,
        LilyGoNfcEmulationTech tech,
        LilyGoNfcEmulationCallback callback,
        void *userData)
{
    LilyGoNfcEmulationConfig config = {};
    config.kind = LILYGO_NFC_EMULATION_TEXT;
    config.tech = tech;
    config.templateName = "Text";
    config.text = text;
    config.discoveryDurationMs = kDefaultEmulationDiscoveryMs;
    config.callback = callback;
    config.userData = userData;
    return beginEmulation(config);
}

bool LilyGoNfcService::beginEmulationWifi(const char *ssid,
        const char *password,
        LilyGoNfcEmulationTech tech,
        LilyGoNfcEmulationCallback callback,
        void *userData)
{
    LilyGoNfcEmulationConfig config = {};
    config.kind = LILYGO_NFC_EMULATION_WIFI;
    config.tech = tech;
    config.templateName = "WiFi";
    config.wifiSsid = ssid;
    config.wifiPassword = password;
    config.wifiAuthenticationType = kWifiAuthWpa2Psk;
    config.wifiEncryptionType = kWifiEncAes;
    config.discoveryDurationMs = kDefaultEmulationDiscoveryMs;
    config.callback = callback;
    config.userData = userData;
    return beginEmulation(config);
}

bool LilyGoNfcService::beginEmulation(const LilyGoNfcEmulationConfig &config)
{
    stop();

    _mode = LILYGO_NFC_MODE_EMULATION;
    _emulationConfig = config;
    if (_emulationConfig.discoveryDurationMs == 0U) {
        _emulationConfig.discoveryDurationMs = kDefaultEmulationDiscoveryMs;
    }
    if (_emulationConfig.wifiAuthenticationType == 0U &&
            _emulationConfig.kind == LILYGO_NFC_EMULATION_WIFI) {
        _emulationConfig.wifiAuthenticationType = kWifiAuthWpa2Psk;
    }
    if (_emulationConfig.wifiEncryptionType == 0U &&
            _emulationConfig.kind == LILYGO_NFC_EMULATION_WIFI) {
        _emulationConfig.wifiEncryptionType = kWifiEncAes;
    }

    resetEmulationStatus();
    _emulationStatus.state = LILYGO_NFC_STATE_STARTING;
    _emulationStatus.kind = _emulationConfig.kind;
    _emulationStatus.tech = _emulationConfig.tech;
    _emulationStatus.experimental =
        (_emulationConfig.tech == LILYGO_NFC_EMULATION_TECH_NFCA_T4T);
    safeCopy(_emulationStatus.templateName, sizeof(_emulationStatus.templateName),
             _emulationConfig.templateName ? _emulationConfig.templateName :
             lilygoNfcEmulationKindName(_emulationConfig.kind));

    activeService = this;
    _running = true;
    _emulationExchangePrimed = false;
    _emulationReaderActive = false;
    _emulationRxData = nullptr;
    _emulationRxLen = nullptr;

    if (!configureEmulationNdef(_emulationConfig)) {
        setEmulationError(ST_ERR_PARAM, "Invalid NDEF template");
        _emulationStatus.state = LILYGO_NFC_STATE_ERROR;
        _running = false;
        _mode = LILYGO_NFC_MODE_IDLE;
        emitEmulation(LILYGO_NFC_EVENT_START_FAILED);
        if (activeService == this) activeService = nullptr;
        return false;
    }

    ReturnCode err = NFCReader.rfalNfcInitialize();
    if (err != ST_ERR_NONE) {
        setEmulationError(err, "rfalNfcInitialize failed");
        _emulationStatus.state = LILYGO_NFC_STATE_ERROR;
        _running = false;
        _mode = LILYGO_NFC_MODE_IDLE;
        emitEmulation(LILYGO_NFC_EVENT_START_FAILED);
        if (activeService == this) activeService = nullptr;
        return false;
    }

    uint16_t techs = RFAL_NFC_LISTEN_TECH_F;
    if (_emulationConfig.tech == LILYGO_NFC_EMULATION_TECH_NFCA_T4T) {
        techs = RFAL_NFC_LISTEN_TECH_A;
    } else if (_emulationConfig.tech == LILYGO_NFC_EMULATION_TECH_MIXED) {
        techs = RFAL_NFC_LISTEN_TECH_A | RFAL_NFC_LISTEN_TECH_F;
        _emulationStatus.experimental = true;
    }

    configureEmulationDiscovery(techs);
    demoCeSetNdefFile(_emulationNdefFile, _emulationNdefFileLen);
    demoCeInit(_emulationNfcfNfcid2);

    if (!startEmulationDiscovery()) {
        _emulationStatus.state = LILYGO_NFC_STATE_ERROR;
        _running = false;
        _mode = LILYGO_NFC_MODE_IDLE;
        emitEmulation(LILYGO_NFC_EVENT_START_FAILED);
        if (activeService == this) activeService = nullptr;
        return false;
    }

    _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;
    emitEmulation(LILYGO_NFC_EVENT_EMULATION_STARTED);
    return true;
}

void LilyGoNfcService::loop()
{
    if (!_running) return;

    if (_mode == LILYGO_NFC_MODE_EMULATION) {
        handleEmulationLoop();
        return;
    }

    if (_result.state == LILYGO_NFC_STATE_WAIT_RELEASE) {
        handleWaitRelease();
        return;
    }

    NFCReader.rfalNfcWorker();

    if (_running && NFCReader.rfalNfcGetState() == RFAL_NFC_STATE_IDLE &&
            _result.state == LILYGO_NFC_STATE_POLLING) {
        startDiscovery();
    }
}

void LilyGoNfcService::stop()
{
    if (!_running) return;

    const LilyGoNfcMode previousMode = _mode;
    NFCReader.rfalNfcDeactivate(false);
    _running = false;
    _mode = LILYGO_NFC_MODE_IDLE;
    _activeHandled = false;
    _hasNdefContext = false;
    _activeCardValid = false;
    _activeNfcaValid = false;
    _deactivateRequested = false;
    _emulationExchangePrimed = false;
    _emulationReaderActive = false;
    _emulationRxData = nullptr;
    _emulationRxLen = nullptr;
    _activeUidLen = 0;
    _missingPresenceCount = 0;
    _result.state = LILYGO_NFC_STATE_IDLE;
    _emulationStatus.state = LILYGO_NFC_STATE_IDLE;
    if (previousMode == LILYGO_NFC_MODE_EMULATION) {
        emitEmulation(LILYGO_NFC_EVENT_STOPPED);
    } else {
        emit(LILYGO_NFC_EVENT_STOPPED);
    }
    if (activeService == this) {
        activeService = nullptr;
    }
}

bool LilyGoNfcService::isRunning() const
{
    return _running;
}

LilyGoNfcState LilyGoNfcService::state() const
{
    if (_mode == LILYGO_NFC_MODE_EMULATION) {
        return _emulationStatus.state;
    }
    return _result.state;
}

const LilyGoNfcReaderResult &LilyGoNfcService::lastResult() const
{
    return _result;
}

const LilyGoNfcEmulationStatus &LilyGoNfcService::emulationStatus() const
{
    return _emulationStatus;
}

void LilyGoNfcService::notifyStatic(rfalNfcState st)
{
    if (activeService) {
        activeService->handleNotify(st);
    }
}

bool LilyGoNfcService::startDiscovery()
{
    rfalNfcDiscoverParam discover = {};
    discover.compMode = RFAL_COMPLIANCE_MODE_NFC;
    discover.devLimit = 1U;
    discover.techs2Find = _config.technologies;
    discover.totalDuration = _config.discoveryDurationMs;
    discover.nfcfBR = RFAL_BR_212;
    discover.ap2pBR = RFAL_BR_424;
    discover.GBLen = 0U;
    discover.notifyCb = notifyStatic;
    discover.wakeupEnabled = _config.wakeupEnabled;
    discover.wakeupConfigDefault = true;

    ReturnCode err = NFCReader.rfalNfcDiscover(&discover);
    if (err != ST_ERR_NONE) {
        setError(err, "rfalNfcDiscover failed");
        return false;
    }

    _result.state = LILYGO_NFC_STATE_POLLING;
    _activeHandled = false;
    _hasNdefContext = false;
    _activeCardValid = false;
    _activeNfcaValid = false;
    _deactivateRequested = false;
    _activeUidLen = 0;
    _missingPresenceCount = 0;
    return true;
}

void LilyGoNfcService::configureEmulationDiscovery(uint16_t techs)
{
    static const uint8_t nfcid1[RFAL_NFCID1_TRIPLE_LEN] = {
        0x5F, 'L', 'G', 'O', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t pmm[8] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x00
    };

    memset(&_emulationDiscover, 0, sizeof(_emulationDiscover));
    _emulationDiscover.compMode = RFAL_COMPLIANCE_MODE_NFC;
    _emulationDiscover.techs2Find = techs;
    _emulationDiscover.totalDuration = _emulationConfig.discoveryDurationMs;
    _emulationDiscover.devLimit = 1U;
    _emulationDiscover.nfcfBR = RFAL_BR_212;
    _emulationDiscover.ap2pBR = RFAL_BR_424;
    _emulationDiscover.notifyCb = notifyStatic;

    _emulationDiscover.lmConfigPA.nfcidLen = RFAL_LM_NFCID_LEN_04;
    memcpy(_emulationDiscover.lmConfigPA.nfcid, nfcid1, sizeof(nfcid1));
    _emulationDiscover.lmConfigPA.SENS_RES[0] = 0x02;
    _emulationDiscover.lmConfigPA.SENS_RES[1] = 0x00;
    _emulationDiscover.lmConfigPA.SEL_RES = 0x20;

    _emulationDiscover.lmConfigPF.SC[0] = 0x12;
    _emulationDiscover.lmConfigPF.SC[1] = 0xFC;
    _emulationDiscover.lmConfigPF.SENSF_RES[0] = RFAL_NFCF_CMD_POLLING_RES;
    memcpy(&_emulationDiscover.lmConfigPF.SENSF_RES[1], _emulationNfcfNfcid2, RFAL_NFCF_NFCID2_LEN);
    memcpy(&_emulationDiscover.lmConfigPF.SENSF_RES[9], pmm, sizeof(pmm));
    _emulationDiscover.lmConfigPF.SENSF_RES[17] = 0x00;
    _emulationDiscover.lmConfigPF.SENSF_RES[18] = 0x00;
}

bool LilyGoNfcService::startEmulationDiscovery()
{
    _emulationExchangePrimed = false;
    _emulationReaderActive = false;
    _emulationRxData = nullptr;
    _emulationRxLen = nullptr;
    _emulationStatus.readerActive = false;
    safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology), "--");
    safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface), "--");

    ReturnCode err = NFCReader.rfalNfcDiscover(&_emulationDiscover);
    if (err != ST_ERR_NONE) {
        setEmulationError(err, "rfalNfcDiscover failed");
        _emulationStatus.state = LILYGO_NFC_STATE_ERROR;
        return false;
    }

    _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;
    return true;
}

void LilyGoNfcService::handleEmulationLoop()
{
    NFCReader.rfalNfcWorker();

    switch (NFCReader.rfalNfcGetState()) {
    case RFAL_NFC_STATE_ACTIVATED: {
        bool activatedNow = false;
        if (!_emulationReaderActive) {
            activatedNow = true;
            _emulationReaderActive = true;
            _emulationStatus.readerActive = true;
            _emulationStatus.readCount++;
            _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;

            rfalNfcDevice *dev = nullptr;
            if (NFCReader.rfalNfcGetActiveDevice(&dev) == ST_ERR_NONE && dev) {
                safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology),
                         rfalDeviceTypeName(dev->type));
                safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface),
                         rfalInterfaceName(dev->rfInterface));
            }
        }
        if (!_emulationExchangePrimed && !primeEmulationExchange()) {
            break;
        }
        if (activatedNow) {
            emitEmulation(LILYGO_NFC_EVENT_EMULATION_ACTIVATED);
        }
        break;
    }

    case RFAL_NFC_STATE_DATAEXCHANGE_DONE:
        handleEmulationDataExchangeDone();
        break;

    case RFAL_NFC_STATE_LISTEN_SLEEP:
        if (_emulationReaderActive || _emulationExchangePrimed) {
            _emulationReaderActive = false;
            _emulationExchangePrimed = false;
            _emulationRxData = nullptr;
            _emulationRxLen = nullptr;
            _emulationStatus.readerActive = false;
            _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;
            safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology), "--");
            safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface), "--");
            emitEmulation(LILYGO_NFC_EVENT_EMULATION_RELEASED);
        }
        NFCReader.rfalNfcDeactivate(true);
        break;

    case RFAL_NFC_STATE_IDLE:
        if (_emulationReaderActive || _emulationExchangePrimed) {
            _emulationReaderActive = false;
            _emulationExchangePrimed = false;
            _emulationRxData = nullptr;
            _emulationRxLen = nullptr;
            _emulationStatus.readerActive = false;
            _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;
            safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology), "--");
            safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface), "--");
            emitEmulation(LILYGO_NFC_EVENT_EMULATION_RELEASED);
        }
        if (_running && !startEmulationDiscovery()) {
            _running = false;
            _mode = LILYGO_NFC_MODE_IDLE;
            emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
            if (activeService == this) activeService = nullptr;
        }
        break;

    default:
        break;
    }
}

bool LilyGoNfcService::primeEmulationExchange()
{
    ReturnCode err = NFCReader.rfalNfcDataExchangeStart(nullptr, 0U,
                     &_emulationRxData, &_emulationRxLen,
                     RFAL_FWT_NONE);
    if (err != ST_ERR_NONE) {
        setEmulationError(err, "Prime exchange failed");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return false;
    }

    _emulationExchangePrimed = true;
    (void)NFCReader.rfalNfcDataExchangeGetStatus();
    return true;
}

bool LilyGoNfcService::waitForNextEmulationFrame()
{
    ReturnCode err = NFCReader.rfalNfcDataExchangeStart(nullptr, 0U,
                     &_emulationRxData, &_emulationRxLen,
                     RFAL_FWT_NONE);
    if (err != ST_ERR_NONE) {
        setEmulationError(err, "Wait frame failed");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return false;
    }

    _emulationExchangePrimed = true;
    return true;
}

void LilyGoNfcService::handleEmulationDataExchangeDone()
{
    ReturnCode err = NFCReader.rfalNfcDataExchangeGetStatus();
    if (err == ST_ERR_BUSY) {
        return;
    }

    if (err == ST_ERR_SLEEP_REQ || err == ST_ERR_RELEASE_REQ || err == ST_ERR_LINK_LOSS) {
        _emulationExchangePrimed = false;
        _emulationReaderActive = false;
        _emulationRxData = nullptr;
        _emulationRxLen = nullptr;
        _emulationStatus.readerActive = false;
        _emulationStatus.state = LILYGO_NFC_STATE_EMULATING;
        safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology), "--");
        safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface), "--");
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_RELEASED);
        NFCReader.rfalNfcDeactivate(true);
        return;
    }

    if (err != ST_ERR_NONE) {
        setEmulationError(err, "Data exchange failed");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return;
    }

    rfalNfcDevice *dev = nullptr;
    if ((NFCReader.rfalNfcGetActiveDevice(&dev) != ST_ERR_NONE) || !dev ||
            !_emulationRxData || !_emulationRxLen) {
        setEmulationError(ST_ERR_REQUEST, "No active reader context");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return;
    }

    uint16_t inLen = *_emulationRxLen;
    uint16_t outLen = 0U;
    const bool isNfcaT4t = (dev->type == RFAL_NFC_POLL_TYPE_NFCA) &&
                           (dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP);
    const bool isNfcfT3t = (dev->type == RFAL_NFC_POLL_TYPE_NFCF) &&
                           (dev->rfInterface == RFAL_NFC_INTERFACE_RF);

    if (isNfcaT4t) {
        outLen = demoCeT4T(_emulationRxData, inLen, _emulationTxBuf, sizeof(_emulationTxBuf));
    } else if (isNfcfT3t) {
        inLen = rfalConvBitsToBytes(inLen);
        outLen = demoCeT3T(_emulationRxData, inLen, _emulationTxBuf, sizeof(_emulationTxBuf));
    } else {
        setEmulationError(ST_ERR_NOTSUPP, "Unsupported reader interface");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return;
    }

    if (outLen == 0U) {
        if (isNfcfT3t) {
            (void)waitForNextEmulationFrame();
            return;
        }

        setEmulationError(ST_ERR_NOTSUPP, "No emulation response");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return;
    }

    err = NFCReader.rfalNfcDataExchangeStart(_emulationTxBuf, outLen,
            &_emulationRxData, &_emulationRxLen,
            RFAL_FWT_NONE);
    if (err != ST_ERR_NONE) {
        setEmulationError(err, "Response exchange failed");
        NFCReader.rfalNfcDeactivate(true);
        _emulationExchangePrimed = false;
        emitEmulation(LILYGO_NFC_EVENT_EMULATION_ERROR);
        return;
    }

    _emulationStatus.exchangeCount++;
    emitEmulation(LILYGO_NFC_EVENT_EMULATION_EXCHANGE);
}

void LilyGoNfcService::handleNotify(rfalNfcState st)
{
    if (!_running || _mode != LILYGO_NFC_MODE_READER) return;

    if (st == RFAL_NFC_STATE_ACTIVATED && !_activeHandled) {
        handleActivated();
    }
}

void LilyGoNfcService::handleActivated()
{
    _activeHandled = true;
    _result.state = LILYGO_NFC_STATE_READING;

    rfalNfcDevice *dev = nullptr;
    ReturnCode err = NFCReader.rfalNfcGetActiveDevice(&dev);
    if (err != ST_ERR_NONE || !dev) {
        setError(err, "No active NFC device");
        emit(LILYGO_NFC_EVENT_NDEF_ERROR);
        NFCReader.rfalNfcDeactivate(true);
        _result.state = LILYGO_NFC_STATE_POLLING;
        return;
    }

    resetResult();
    _result.state = LILYGO_NFC_STATE_READING;
    _activeCardValid = true;
    _activeType = dev->type;
    _activeUidLen = 0;
    memset(_activeUid, 0, sizeof(_activeUid));
    _activeNfcaValid = false;
    memset(&_activeNfca, 0, sizeof(_activeNfca));
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA || dev->type == RFAL_NFC_POLL_TYPE_NFCA) {
        memcpy(&_activeNfca, &dev->dev.nfca, sizeof(_activeNfca));
        _activeNfcaValid = true;
    }

    _result.uidLen = (dev->nfcidLen < LILYGO_NFC_MAX_UID_LEN) ? dev->nfcidLen : LILYGO_NFC_MAX_UID_LEN;
    if (dev->nfcid && _result.uidLen) {
        memcpy(_result.uid, dev->nfcid, _result.uidLen);
        memcpy(_activeUid, dev->nfcid, _result.uidLen);
        _activeUidLen = _result.uidLen;
        bytesToHex(dev->nfcid, dev->nfcidLen, _result.uidText, sizeof(_result.uidText));
    } else {
        safeCopy(_result.uidText, sizeof(_result.uidText), "--");
    }
    safeCopy(_result.technology, sizeof(_result.technology), rfalDeviceTypeName(dev->type));
    safeCopy(_result.interfaceName, sizeof(_result.interfaceName), rfalInterfaceName(dev->rfInterface));

    rfalNfcDevice ndefDevice = *dev;
    const bool ndefCandidate = describeCard(dev, &ndefDevice);
    if (!ndefCandidate) {
        _result.ndefAttempted = false;
        _result.ndefSupported = false;
        _result.lastError = ST_ERR_NONE;
        safeCopy(_result.errorText, sizeof(_result.errorText), "");
        safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "UID only");
        emit(LILYGO_NFC_EVENT_CARD_DETECTED);
        enterWaitRelease();
        return;
    }

    emit(LILYGO_NFC_EVENT_CARD_DETECTED);
    readNdef(&ndefDevice);
}

void LilyGoNfcService::handleWaitRelease()
{
    const uint32_t now = millis();
    if (now - _lastPresenceCheckMs < kReleaseCheckIntervalMs) {
        return;
    }
    _lastPresenceCheckMs = now;

    if (isActiveCardStillPresent()) {
        _missingPresenceCount = 0;
        return;
    }
    if (++_missingPresenceCount < kReleaseMissingLimit) {
        return;
    }

    _hasNdefContext = false;
    _activeCardValid = false;
    _activeNfcaValid = false;
    _activeHandled = false;
    _activeUidLen = 0;
    _missingPresenceCount = 0;
    _result.state = LILYGO_NFC_STATE_POLLING;
    emit(LILYGO_NFC_EVENT_CARD_RELEASED);

    if (!_deactivateRequested) {
        NFCReader.rfalNfcDeactivate(true);
    }
    _deactivateRequested = false;
}

void LilyGoNfcService::enterWaitRelease()
{
    _result.state = LILYGO_NFC_STATE_WAIT_RELEASE;
    _lastPresenceCheckMs = 0;
    _missingPresenceCount = 0;

    if (!_deactivateRequested) {
        NFCReader.rfalNfcDeactivate(false);
        _deactivateRequested = true;
    }
}

bool LilyGoNfcService::isActiveCardStillPresent()
{
    if (!_activeCardValid) {
        if (_hasNdefContext) {
            return _ndef.ndefPollerCheckPresence() == ST_ERR_NONE;
        }
        return false;
    }

    switch (_activeType) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
    case RFAL_NFC_POLL_TYPE_NFCA: {
        rfalNfcaSensRes sensRes = {};
        rfalNfcaSelRes selRes = {};

        if (!preparePresencePolling(NFCReader.rfalNfcaPollerInitialize())) {
            return false;
        }
        const ReturnCode err = NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes);
        if (!isPresenceResponse(err)) {
            return false;
        }

        if (err == ST_ERR_NONE && _activeNfcaValid && _activeNfca.type == RFAL_NFCA_T1T) {
            if (!rfalNfcaIsSensResT1T(&sensRes)) {
                return false;
            }
        } else if (_activeNfcaValid) {
            (void)NFCReader.rfalNfcaPollerSelect(_activeNfca.nfcId1, _activeNfca.nfcId1Len, &selRes);
        }

        NFCReader.rfalNfcaPollerSleep();
        return true;
    }

    case RFAL_NFC_LISTEN_TYPE_NFCB:
    case RFAL_NFC_POLL_TYPE_NFCB: {
        rfalNfcbSensbRes sensbRes = {};
        uint8_t sensbResLen = 0;

        if (!preparePresencePolling(NFCReader.rfalNfcbPollerInitialize())) {
            return false;
        }
        const ReturnCode err = NFCReader.rfalNfcbPollerCheckPresence(RFAL_NFCB_SENS_CMD_ALLB_REQ,
                                                                      RFAL_NFCB_SLOT_NUM_1,
                                                                      &sensbRes,
                                                                      &sensbResLen);
        if (!isPresenceResponse(err)) {
            return false;
        }
        if (err == ST_ERR_NONE && _activeUidLen == RFAL_NFCB_NFCID0_LEN &&
                memcmp(sensbRes.nfcid0, _activeUid, RFAL_NFCB_NFCID0_LEN) != 0) {
            return false;
        }
        return true;
    }

    case RFAL_NFC_LISTEN_TYPE_NFCF:
    case RFAL_NFC_POLL_TYPE_NFCF:
        if (!preparePresencePolling(NFCReader.rfalNfcfPollerInitialize(RFAL_BR_212))) {
            return false;
        }
        return isPresenceResponse(NFCReader.rfalNfcfPollerCheckPresence());

    case RFAL_NFC_LISTEN_TYPE_NFCV:
    case RFAL_NFC_POLL_TYPE_NFCV: {
        rfalNfcvInventoryRes invRes = {};

        if (!preparePresencePolling(NFCReader.rfalNfcvPollerInitialize())) {
            return false;
        }
        const ReturnCode err = NFCReader.rfalNfcvPollerCheckPresence(&invRes);
        if (!isPresenceResponse(err)) {
            return false;
        }
        if (err == ST_ERR_NONE && _activeUidLen == RFAL_NFCV_UID_LEN &&
                memcmp(invRes.UID, _activeUid, RFAL_NFCV_UID_LEN) != 0) {
            return false;
        }
        return true;
    }

    case RFAL_NFC_LISTEN_TYPE_ST25TB: {
        uint8_t chipId = 0;

        if (!preparePresencePolling(NFCReader.rfalSt25tbPollerInitialize())) {
            return false;
        }
        return isPresenceResponse(NFCReader.rfalSt25tbPollerCheckPresence(&chipId));
    }

    default:
        if (_hasNdefContext) {
            return _ndef.ndefPollerCheckPresence() == ST_ERR_NONE;
        }
        return false;
    }
}

bool LilyGoNfcService::describeCard(rfalNfcDevice *dev, rfalNfcDevice *ndefDev)
{
    if (!dev || !ndefDev) {
        return false;
    }

    *ndefDev = *dev;
    _result.ndefSupported = false;
    safeCopy(_result.cardType, sizeof(_result.cardType), rfalDeviceTypeName(dev->type));
    safeCopy(_result.cardInfo, sizeof(_result.cardInfo), "--");
    safeCopy(_result.tagType, sizeof(_result.tagType), "--");

    switch (dev->type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA: {
        const rfalNfcaListenDevice &nfca = dev->dev.nfca;
        const uint8_t sak = nfca.selRes.sak;

        if (nfcaLooksMifareClassic(nfca)) {
            _result.mifareClassic = true;
            safeCopy(_result.cardType, sizeof(_result.cardType), "MIFARE Classic");
            safeCopy(_result.tagType, sizeof(_result.tagType), "MIFARE");
            snprintf(_result.cardInfo, sizeof(_result.cardInfo),
                     "ISO14443A  ATQA %02X %02X  SAK 0x%02X  UID %uB",
                     nfca.sensRes.anticollisionInfo, nfca.sensRes.platformInfo,
                     sak, _result.uidLen);
            return false;
        }

        safeCopy(_result.cardType, sizeof(_result.cardType), nfcaListenTypeName(nfca.type));
        snprintf(_result.cardInfo, sizeof(_result.cardInfo),
                 "ISO14443A  ATQA %02X %02X  SAK 0x%02X  UID %uB",
                 nfca.sensRes.anticollisionInfo, nfca.sensRes.platformInfo,
                 sak, _result.uidLen);

        switch (nfca.type) {
        case RFAL_NFCA_T1T:
            safeCopy(_result.tagType, sizeof(_result.tagType), "T1T");
            return false;
        case RFAL_NFCA_T2T:
            safeCopy(_result.tagType, sizeof(_result.tagType), "T2T");
            _result.ndefSupported = true;
            return true;
        case RFAL_NFCA_T4T:
            safeCopy(_result.tagType, sizeof(_result.tagType), "T4T");
            _result.ndefSupported = true;
            return true;
        case RFAL_NFCA_T4T_NFCDEP:
            safeCopy(_result.tagType, sizeof(_result.tagType), "T4T");
            ndefDev->dev.nfca.type = RFAL_NFCA_T4T;
            _result.ndefSupported = true;
            return true;
        case RFAL_NFCA_NFCDEP:
        default:
            safeCopy(_result.tagType, sizeof(_result.tagType), "P2P");
            return false;
        }
    }

    case RFAL_NFC_LISTEN_TYPE_NFCB:
        safeCopy(_result.cardType, sizeof(_result.cardType), "NFC-B Type 4");
        safeCopy(_result.tagType, sizeof(_result.tagType), "T4T");
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "ISO14443B / ISO-DEP  ID %uB", _result.uidLen);
        _result.ndefSupported = true;
        return true;

    case RFAL_NFC_LISTEN_TYPE_NFCF:
        if (rfalNfcfIsNfcDepSupported(&dev->dev.nfcf)) {
            safeCopy(_result.cardType, sizeof(_result.cardType), "NFC-F P2P");
            safeCopy(_result.tagType, sizeof(_result.tagType), "P2P");
            snprintf(_result.cardInfo, sizeof(_result.cardInfo), "NFCID2 %uB; NFC-DEP capable", _result.uidLen);
            return false;
        }
        safeCopy(_result.cardType, sizeof(_result.cardType), "FeliCa/NFC-F Type 3");
        safeCopy(_result.tagType, sizeof(_result.tagType), "T3T");
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "FeliCa  NFCID2 %uB", _result.uidLen);
        _result.ndefSupported = true;
        return true;

    case RFAL_NFC_LISTEN_TYPE_NFCV:
        safeCopy(_result.cardType, sizeof(_result.cardType), "NFC-V Type 5");
        safeCopy(_result.tagType, sizeof(_result.tagType), "T5T");
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "ISO15693  UID %uB", _result.uidLen);
        _result.ndefSupported = true;
        return true;

    case RFAL_NFC_LISTEN_TYPE_ST25TB:
        safeCopy(_result.cardType, sizeof(_result.cardType), "ST25TB");
        safeCopy(_result.tagType, sizeof(_result.tagType), "--");
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "ST proprietary tag  UID %uB", _result.uidLen);
        return false;

    case RFAL_NFC_LISTEN_TYPE_AP2P:
        safeCopy(_result.cardType, sizeof(_result.cardType), "Active P2P");
        safeCopy(_result.tagType, sizeof(_result.tagType), "P2P");
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "Active peer-to-peer  NFCID %uB", _result.uidLen);
        return false;

    default:
        snprintf(_result.cardInfo, sizeof(_result.cardInfo), "ID %uB", _result.uidLen);
        return false;
    }
}

void LilyGoNfcService::readNdef(rfalNfcDevice *dev)
{
    _result.ndefAttempted = true;
    ReturnCode err = _ndef.ndefPollerContextInitialization(dev);
    if (err != ST_ERR_NONE) {
        _result.ndefSupported = false;
        if (isNdefNotAvailable(err)) {
            _result.lastError = ST_ERR_NONE;
            safeCopy(_result.errorText, sizeof(_result.errorText), "");
            safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "No NDEF");
            emit(LILYGO_NFC_EVENT_NDEF_UNSUPPORTED);
        } else {
            setError(err, "NDEF context failed");
            safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "Error");
            emit(LILYGO_NFC_EVENT_NDEF_ERROR);
        }
        enterWaitRelease();
        return;
    }

    _hasNdefContext = true;
    safeCopy(_result.tagType, sizeof(_result.tagType), ndefDeviceName(_ndef.type));

    ndefInfo info = {};
    err = _ndef.ndefPollerNdefDetect(&info);
    if (err != ST_ERR_NONE) {
        _result.ndefSupported = false;
        if (isNdefNotAvailable(err)) {
            _result.lastError = ST_ERR_NONE;
            safeCopy(_result.errorText, sizeof(_result.errorText), "");
            safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "No NDEF");
            emit(LILYGO_NFC_EVENT_NDEF_UNSUPPORTED);
        } else {
            setError(err, "NDEF detect failed");
            safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "Error");
            emit(LILYGO_NFC_EVENT_NDEF_ERROR);
        }
        enterWaitRelease();
        return;
    }

    _result.ndefSupported = true;
    _result.hasNdef = (info.state == NDEF_STATE_READWRITE || info.state == NDEF_STATE_READONLY);
    _result.ndefMajorVersion = info.majorVersion;
    _result.ndefMinorVersion = info.minorVersion;
    _result.ndefAreaLen = info.areaLen;
    _result.ndefAvailableLen = info.areaAvalableSpaceLen;
    _result.ndefMessageLen = info.messageLen;
    safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), ndefStateName(info.state));

    if (!_result.hasNdef || info.messageLen == 0) {
        emit(LILYGO_NFC_EVENT_NDEF_READ);
        enterWaitRelease();
        return;
    }

    uint32_t actualSize = 0;
    memset(_rawBuffer, 0, sizeof(_rawBuffer));
    err = _ndef.ndefPollerReadRawMessage(_rawBuffer, sizeof(_rawBuffer), &actualSize);
    if (err != ST_ERR_NONE) {
        setError(err, "NDEF read failed");
        emit(LILYGO_NFC_EVENT_NDEF_ERROR);
        enterWaitRelease();
        return;
    }

    bytesToHex(_rawBuffer, actualSize, _result.rawHexPreview, sizeof(_result.rawHexPreview));
    _result.ndefMessageLen = actualSize;

    ndefMessage message = {};
    ndefConstBuffer ndefBuffer = {};
    ndefBuffer.buffer = _rawBuffer;
    ndefBuffer.length = actualSize;

    err = _ndef.ndefMessageDecode(&ndefBuffer, &message);
    if (err != ST_ERR_NONE) {
        setError(err, "NDEF decode failed");
        emit(LILYGO_NFC_EVENT_NDEF_ERROR);
        enterWaitRelease();
        return;
    }

    ndefRecord *record = ndefMessageGetFirstRecord(&message);
    while (record && _result.recordCount < LILYGO_NFC_MAX_RECORDS) {
        appendRecord(record);
        record = ndefMessageGetNextRecord(record);
    }

    emit(LILYGO_NFC_EVENT_NDEF_READ);
    enterWaitRelease();
}

bool LilyGoNfcService::appendRecord(const ndefRecord *record)
{
    if (!record || _result.recordCount >= LILYGO_NFC_MAX_RECORDS) {
        return false;
    }

    LilyGoNfcRecord &out = _result.records[_result.recordCount];
    memset(&out, 0, sizeof(out));
    out.index = _result.recordCount + 1;
    out.kind = LILYGO_NFC_RECORD_UNKNOWN;
    out.ndefType = NDEF_TYPE_ID_COUNT;
    safeCopy(out.title, sizeof(out.title), "Unknown");
    safeCopy(out.value, sizeof(out.value), "--");

    ndefType type = {};
    ReturnCode err = _ndef.ndefRecordToType(record, &type);
    if (err != ST_ERR_NONE) {
        fillGenericRecord(_ndef, record, out);
        _result.recordCount++;
        return true;
    }

    out.ndefType = type.id;

    switch (type.id) {
    case NDEF_TYPE_EMPTY:
        out.kind = LILYGO_NFC_RECORD_EMPTY;
        safeCopy(out.title, sizeof(out.title), "Empty");
        safeCopy(out.value, sizeof(out.value), "--");
        break;

    case NDEF_TYPE_RTD_DEVICE_INFO: {
        ndefTypeRtdDeviceInfo deviceInfo = {};
        _ndef.ndefGetRtdDeviceInfo(&type, &deviceInfo);
        out.kind = LILYGO_NFC_RECORD_DEVICE_INFO;
        safeCopy(out.title, sizeof(out.title), "Device");
        out.value[0] = '\0';
        for (uint8_t i = 0; i < NDEF_DEVICE_INFO_TYPE_COUNT; ++i) {
            if (!deviceInfo.devInfo[i].buffer || deviceInfo.devInfo[i].length == 0) continue;
            char item[64];
            copyBufferAsText(item, sizeof(item), deviceInfo.devInfo[i].buffer, deviceInfo.devInfo[i].length);
            if (out.value[0] != '\0') appendText(out.value, sizeof(out.value), "\n");
            appendText(out.value, sizeof(out.value), item);
        }
        if (out.value[0] == '\0') safeCopy(out.value, sizeof(out.value), "Device Info");
        break;
    }

    case NDEF_TYPE_RTD_TEXT: {
        uint8_t utfEncoding = 0;
        ndefConstBuffer8 language = {};
        ndefConstBuffer sentence = {};
        _ndef.ndefGetRtdText(&type, &utfEncoding, &language, &sentence);
        out.kind = LILYGO_NFC_RECORD_TEXT;
        safeCopy(out.title, sizeof(out.title), "Text");
        copyConstBuffer(out.value, sizeof(out.value), sentence);

        char lang[16];
        copyConstBuffer8(lang, sizeof(lang), language);
        snprintf(out.detail, sizeof(out.detail), "%s  %s",
                 (utfEncoding == TEXT_ENCODING_UTF8) ? "UTF-8" : "UTF-16",
                 lang[0] ? lang : "--");
        break;
    }

    case NDEF_TYPE_RTD_URI: {
        ndefConstBuffer protocol = {};
        ndefConstBuffer uri = {};
        _ndef.ndefGetRtdUri(&type, &protocol, &uri);
        out.kind = LILYGO_NFC_RECORD_URI;
        safeCopy(out.title, sizeof(out.title), "URL");
        copyConstBuffer(out.value, sizeof(out.value), protocol);
        char uriText[LILYGO_NFC_MAX_TEXT_LEN];
        copyConstBuffer(uriText, sizeof(uriText), uri);
        appendText(out.value, sizeof(out.value), uriText);
        break;
    }

    case NDEF_TYPE_RTD_AAR: {
        ndefConstBuffer aar = {};
        _ndef.ndefGetRtdAar(&type, &aar);
        out.kind = LILYGO_NFC_RECORD_AAR;
        safeCopy(out.title, sizeof(out.title), "AAR");
        copyConstBuffer(out.value, sizeof(out.value), aar);
        break;
    }

    case NDEF_TYPE_MEDIA_VCARD:
        out.kind = LILYGO_NFC_RECORD_VCARD;
        safeCopy(out.title, sizeof(out.title), "vCard");
        out.value[0] = '\0';
        appendVCardField(_ndef, &type, "FN", "Name", out.value, sizeof(out.value));
        appendVCardField(_ndef, &type, "TEL", "Tel", out.value, sizeof(out.value));
        appendVCardField(_ndef, &type, "EMAIL", "Email", out.value, sizeof(out.value));
        appendVCardField(_ndef, &type, "URL", "URL", out.value, sizeof(out.value));
        if (out.value[0] == '\0') safeCopy(out.value, sizeof(out.value), "vCard");
        break;

    case NDEF_TYPE_MEDIA_WIFI: {
        ndefTypeWifi wifi = {};
        _ndef.ndefGetWifi(&type, &wifi);
        out.kind = LILYGO_NFC_RECORD_WIFI;
        safeCopy(out.title, sizeof(out.title), "WiFi");
        copyConstBuffer(out.wifiSsid, sizeof(out.wifiSsid), wifi.bufNetworkSSID);
        copyConstBuffer(out.wifiPassword, sizeof(out.wifiPassword), wifi.bufNetworkKey);
        out.wifiAuthentication = wifi.authentication;
        out.wifiEncryption = wifi.encryption;
        safeCopy(out.value, sizeof(out.value), out.wifiSsid[0] ? out.wifiSsid : "Hidden SSID");
        snprintf(out.detail, sizeof(out.detail), "%s  Password %s",
                 wifiAuthenticationName(wifi.authentication),
                 out.wifiPassword[0] ? "present" : "empty");
        break;
    }

    case NDEF_TYPE_MEDIA:
        out.kind = LILYGO_NFC_RECORD_MEDIA;
        safeCopy(out.title, sizeof(out.title), "Media");
        fillGenericRecord(_ndef, record, out);
        break;

    case NDEF_TYPE_ID_COUNT:
    default:
        fillGenericRecord(_ndef, record, out);
        break;
    }

    _result.recordCount++;
    return true;
}

bool LilyGoNfcService::configureEmulationNdef(const LilyGoNfcEmulationConfig &config)
{
    memset(_emulationNdefFile, 0, sizeof(_emulationNdefFile));
    _emulationNdefFileLen = 0;

    switch (config.kind) {
    case LILYGO_NFC_EMULATION_URL:
        if (!buildUriNdefFile(_emulationNdefFile, sizeof(_emulationNdefFile),
                              config.url, &_emulationNdefFileLen)) {
            return false;
        }
        safeCopy(_emulationStatus.payloadPreview, sizeof(_emulationStatus.payloadPreview), config.url);
        break;

    case LILYGO_NFC_EMULATION_TEXT:
        if (!buildTextNdefFile(_emulationNdefFile, sizeof(_emulationNdefFile),
                               config.text, &_emulationNdefFileLen)) {
            return false;
        }
        safeCopy(_emulationStatus.payloadPreview, sizeof(_emulationStatus.payloadPreview), config.text);
        break;

    case LILYGO_NFC_EMULATION_WIFI: {
        const uint16_t authType = config.wifiAuthenticationType ? config.wifiAuthenticationType : kWifiAuthWpa2Psk;
        const uint16_t encType = config.wifiEncryptionType ? config.wifiEncryptionType : kWifiEncAes;
        if (!buildWifiNdefFile(_emulationNdefFile, sizeof(_emulationNdefFile),
                               config.wifiSsid, config.wifiPassword,
                               authType, encType, &_emulationNdefFileLen)) {
            return false;
        }
        snprintf(_emulationStatus.payloadPreview, sizeof(_emulationStatus.payloadPreview),
                 "SSID %s", config.wifiSsid ? config.wifiSsid : "--");
        break;
    }

    default:
        return false;
    }

    _emulationStatus.ndefFileLen = _emulationNdefFileLen;
    return _emulationNdefFileLen >= 2U;
}

void LilyGoNfcService::emit(LilyGoNfcEvent event)
{
    if (_config.callback) {
        _config.callback(event, _result, _config.userData);
    }
}

void LilyGoNfcService::emitEmulation(LilyGoNfcEvent event)
{
    if (_emulationConfig.callback) {
        _emulationConfig.callback(event, _emulationStatus, _emulationConfig.userData);
    }
}

void LilyGoNfcService::resetResult()
{
    memset(&_result, 0, sizeof(_result));
    _result.state = LILYGO_NFC_STATE_IDLE;
    _result.lastError = ST_ERR_NONE;
    safeCopy(_result.uidText, sizeof(_result.uidText), "--");
    safeCopy(_result.technology, sizeof(_result.technology), "--");
    safeCopy(_result.interfaceName, sizeof(_result.interfaceName), "--");
    safeCopy(_result.tagType, sizeof(_result.tagType), "--");
    safeCopy(_result.cardType, sizeof(_result.cardType), "--");
    safeCopy(_result.cardInfo, sizeof(_result.cardInfo), "--");
    safeCopy(_result.ndefStateText, sizeof(_result.ndefStateText), "--");
}

void LilyGoNfcService::resetEmulationStatus()
{
    memset(&_emulationStatus, 0, sizeof(_emulationStatus));
    _emulationStatus.state = LILYGO_NFC_STATE_IDLE;
    _emulationStatus.lastError = ST_ERR_NONE;
    safeCopy(_emulationStatus.templateName, sizeof(_emulationStatus.templateName), "--");
    safeCopy(_emulationStatus.payloadPreview, sizeof(_emulationStatus.payloadPreview), "--");
    safeCopy(_emulationStatus.activeTechnology, sizeof(_emulationStatus.activeTechnology), "--");
    safeCopy(_emulationStatus.activeInterface, sizeof(_emulationStatus.activeInterface), "--");
}

void LilyGoNfcService::setError(ReturnCode err, const char *fallback)
{
    _result.lastError = err;
    const char *errText = _ndef.errorToString(err);
    if (errText && errText[0]) {
        safeCopy(_result.errorText, sizeof(_result.errorText), errText);
    } else {
        safeCopy(_result.errorText, sizeof(_result.errorText), fallback ? fallback : "NFC error");
    }
}

void LilyGoNfcService::setEmulationError(ReturnCode err, const char *fallback)
{
    _emulationStatus.lastError = err;
    const char *errText = _ndef.errorToString(err);
    if (errText && errText[0]) {
        safeCopy(_emulationStatus.errorText, sizeof(_emulationStatus.errorText), errText);
    } else {
        safeCopy(_emulationStatus.errorText, sizeof(_emulationStatus.errorText),
                 fallback ? fallback : "NFC emulation error");
    }
}

const char *lilygoNfcStateName(LilyGoNfcState state)
{
    switch (state) {
    case LILYGO_NFC_STATE_IDLE:
        return "Idle";
    case LILYGO_NFC_STATE_STARTING:
        return "Starting";
    case LILYGO_NFC_STATE_POLLING:
        return "Polling";
    case LILYGO_NFC_STATE_READING:
        return "Reading";
    case LILYGO_NFC_STATE_WAIT_RELEASE:
        return "Wait release";
    case LILYGO_NFC_STATE_EMULATING:
        return "Emulating";
    case LILYGO_NFC_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

const char *lilygoNfcEventName(LilyGoNfcEvent event)
{
    switch (event) {
    case LILYGO_NFC_EVENT_STARTED:
        return "Started";
    case LILYGO_NFC_EVENT_START_FAILED:
        return "Start failed";
    case LILYGO_NFC_EVENT_CARD_DETECTED:
        return "Card detected";
    case LILYGO_NFC_EVENT_NDEF_READ:
        return "NDEF read";
    case LILYGO_NFC_EVENT_NDEF_UNSUPPORTED:
        return "No NDEF";
    case LILYGO_NFC_EVENT_NDEF_ERROR:
        return "NDEF error";
    case LILYGO_NFC_EVENT_CARD_RELEASED:
        return "Card released";
    case LILYGO_NFC_EVENT_STOPPED:
        return "Stopped";
    case LILYGO_NFC_EVENT_EMULATION_STARTED:
        return "Emulation started";
    case LILYGO_NFC_EVENT_EMULATION_ACTIVATED:
        return "Reader detected";
    case LILYGO_NFC_EVENT_EMULATION_EXCHANGE:
        return "Data exchange";
    case LILYGO_NFC_EVENT_EMULATION_RELEASED:
        return "Reader released";
    case LILYGO_NFC_EVENT_EMULATION_ERROR:
        return "Emulation error";
    default:
        return "Unknown";
    }
}

const char *lilygoNfcRecordKindName(LilyGoNfcRecordKind kind)
{
    switch (kind) {
    case LILYGO_NFC_RECORD_EMPTY:
        return "Empty";
    case LILYGO_NFC_RECORD_TEXT:
        return "Text";
    case LILYGO_NFC_RECORD_URI:
        return "URL";
    case LILYGO_NFC_RECORD_WIFI:
        return "WiFi";
    case LILYGO_NFC_RECORD_VCARD:
        return "vCard";
    case LILYGO_NFC_RECORD_DEVICE_INFO:
        return "Device";
    case LILYGO_NFC_RECORD_AAR:
        return "AAR";
    case LILYGO_NFC_RECORD_MEDIA:
        return "Media";
    case LILYGO_NFC_RECORD_UNKNOWN:
    default:
        return "Unknown";
    }
}

const char *lilygoNfcEmulationKindName(LilyGoNfcEmulationKind kind)
{
    switch (kind) {
    case LILYGO_NFC_EMULATION_URL:
        return "URL";
    case LILYGO_NFC_EMULATION_TEXT:
        return "Text";
    case LILYGO_NFC_EMULATION_WIFI:
        return "WiFi";
    default:
        return "Unknown";
    }
}

const char *lilygoNfcEmulationTechName(LilyGoNfcEmulationTech tech)
{
    switch (tech) {
    case LILYGO_NFC_EMULATION_TECH_NFCF_T3T:
        return "NFC-F Type 3";
    case LILYGO_NFC_EMULATION_TECH_NFCA_T4T:
        return "NFC-A Type 4";
    case LILYGO_NFC_EMULATION_TECH_MIXED:
        return "Mixed A/F";
    default:
        return "Unknown";
    }
}

#endif
