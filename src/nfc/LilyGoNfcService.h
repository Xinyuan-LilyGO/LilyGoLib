/**
 * @file      LilyGoNfcService.h
 * @brief     Declares the ST25R3916 NFC reader and tag-emulation service.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-25
 *
 */
#pragma once

#if defined(ARDUINO) && \
    (defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_WATCH_S3_ULTRA)) && \
    !defined(USING_ST25R3916)
#define USING_ST25R3916
#endif

#if defined(ARDUINO) && defined(USING_ST25R3916)

#include <Arduino.h>
#include "nfc_include.h"

/** Maximum NFC UID length stored by the service. */
#define LILYGO_NFC_MAX_UID_LEN        16
/** Maximum parsed NDEF records stored in one reader result. */
#define LILYGO_NFC_MAX_RECORDS        8
/** Maximum parsed record text length. */
#define LILYGO_NFC_MAX_TEXT_LEN       192
/** Maximum parsed record detail text length. */
#define LILYGO_NFC_MAX_DETAIL_LEN     160
/** Maximum parsed Wi-Fi field length. */
#define LILYGO_NFC_MAX_WIFI_LEN       64
/** Raw NDEF read buffer length. */
#define LILYGO_NFC_RAW_BUFFER_LEN     1024
/** Hex preview length for raw NDEF data. */
#define LILYGO_NFC_RAW_HEX_PREVIEW    256
/** Maximum generated NDEF payload length for tag emulation. */
#define LILYGO_NFC_MAX_EMULATION_NDEF_LEN 512
/** Maximum emulation payload preview text length. */
#define LILYGO_NFC_MAX_EMULATION_TEXT_LEN 128

/**
 * @brief Current NFC service mode.
 */
enum LilyGoNfcMode : uint8_t {
    LILYGO_NFC_MODE_IDLE = 0, /**< Service is stopped. */
    LILYGO_NFC_MODE_READER,   /**< Reader / poller mode. */
    LILYGO_NFC_MODE_EMULATION, /**< Tag emulation mode. */
};

/**
 * @brief NFC service state-machine states.
 */
enum LilyGoNfcState : uint8_t {
    LILYGO_NFC_STATE_IDLE = 0,     /**< Service is idle. */
    LILYGO_NFC_STATE_STARTING,     /**< Discovery or emulation is starting. */
    LILYGO_NFC_STATE_POLLING,      /**< Reader mode is polling for cards. */
    LILYGO_NFC_STATE_READING,      /**< A detected card is being described or read. */
    LILYGO_NFC_STATE_WAIT_RELEASE, /**< Service is waiting for card removal. */
    LILYGO_NFC_STATE_EMULATING,    /**< Tag emulation is active. */
    LILYGO_NFC_STATE_ERROR,        /**< Service entered an error state. */
};

/**
 * @brief Events emitted by the NFC reader and emulation service.
 */
enum LilyGoNfcEvent : uint8_t {
    LILYGO_NFC_EVENT_STARTED = 0,          /**< Reader mode started. */
    LILYGO_NFC_EVENT_START_FAILED,         /**< Reader or emulation start failed. */
    LILYGO_NFC_EVENT_CARD_DETECTED,        /**< A card was detected. */
    LILYGO_NFC_EVENT_NDEF_READ,            /**< NDEF data was read successfully. */
    LILYGO_NFC_EVENT_NDEF_UNSUPPORTED,     /**< Card does not support the handled NDEF path. */
    LILYGO_NFC_EVENT_NDEF_ERROR,           /**< NDEF read or parse error. */
    LILYGO_NFC_EVENT_CARD_RELEASED,        /**< Detected card was removed. */
    LILYGO_NFC_EVENT_STOPPED,              /**< NFC service stopped. */
    LILYGO_NFC_EVENT_EMULATION_STARTED,    /**< Tag emulation discovery started. */
    LILYGO_NFC_EVENT_EMULATION_ACTIVATED,  /**< A reader activated the emulated tag. */
    LILYGO_NFC_EVENT_EMULATION_EXCHANGE,   /**< Data exchange occurred during emulation. */
    LILYGO_NFC_EVENT_EMULATION_RELEASED,   /**< Reader released the emulated tag. */
    LILYGO_NFC_EVENT_EMULATION_ERROR,      /**< Tag emulation error. */
};

/**
 * @brief Built-in NDEF payload templates for tag emulation.
 */
enum LilyGoNfcEmulationKind : uint8_t {
    LILYGO_NFC_EMULATION_URL = 0, /**< Emulate a URL NDEF record. */
    LILYGO_NFC_EMULATION_TEXT,    /**< Emulate a text NDEF record. */
    LILYGO_NFC_EMULATION_WIFI,    /**< Emulate a Wi-Fi handover NDEF record. */
};

/**
 * @brief NFC technologies used for tag emulation.
 */
enum LilyGoNfcEmulationTech : uint8_t {
    LILYGO_NFC_EMULATION_TECH_NFCF_T3T = 0, /**< NFC-F Type 3 Tag emulation. */
    LILYGO_NFC_EMULATION_TECH_NFCA_T4T,     /**< NFC-A Type 4 Tag emulation. */
    LILYGO_NFC_EMULATION_TECH_MIXED,        /**< Mixed technology discovery. */
};

/**
 * @brief Parsed high-level NDEF record categories.
 */
enum LilyGoNfcRecordKind : uint8_t {
    LILYGO_NFC_RECORD_UNKNOWN = 0,    /**< Unknown record type. */
    LILYGO_NFC_RECORD_EMPTY,          /**< Empty NDEF record. */
    LILYGO_NFC_RECORD_TEXT,           /**< Text record. */
    LILYGO_NFC_RECORD_URI,            /**< URI / URL record. */
    LILYGO_NFC_RECORD_WIFI,           /**< Wi-Fi configuration record. */
    LILYGO_NFC_RECORD_VCARD,          /**< vCard contact record. */
    LILYGO_NFC_RECORD_DEVICE_INFO,    /**< Device information record. */
    LILYGO_NFC_RECORD_AAR,            /**< Android application record. */
    LILYGO_NFC_RECORD_MEDIA,          /**< Media or MIME record. */
};

/**
 * @brief Parsed NDEF record summary.
 */
struct LilyGoNfcRecord {
    LilyGoNfcRecordKind kind;                    /**< High-level record category. */
    ndefTypeId ndefType;                         /**< Raw NDEF type identifier. */
    uint8_t index;                               /**< Record index in the NDEF message. */
    char title[24];                              /**< Short display title. */
    char value[LILYGO_NFC_MAX_TEXT_LEN];         /**< Primary record value. */
    char detail[LILYGO_NFC_MAX_DETAIL_LEN];      /**< Additional decoded detail text. */
    char wifiSsid[LILYGO_NFC_MAX_WIFI_LEN];      /**< Parsed Wi-Fi SSID. */
    char wifiPassword[LILYGO_NFC_MAX_WIFI_LEN];  /**< Parsed Wi-Fi password. */
    uint8_t wifiAuthentication;                  /**< Parsed Wi-Fi authentication type. */
    uint8_t wifiEncryption;                      /**< Parsed Wi-Fi encryption type. */
};

/**
 * @brief Last reader-mode result and parsed card information.
 */
struct LilyGoNfcReaderResult {
    LilyGoNfcState state;                                      /**< Current reader state. */
    ReturnCode lastError;                                      /**< Last RFAL or NDEF error code. */
    char errorText[48];                                        /**< Human-readable error text. */

    uint8_t uid[LILYGO_NFC_MAX_UID_LEN];                       /**< Card UID bytes. */
    uint8_t uidLen;                                            /**< Number of valid UID bytes. */
    char uidText[(LILYGO_NFC_MAX_UID_LEN * 3) + 1];            /**< Hex UID text. */
    char technology[16];                                       /**< Detected NFC technology. */
    char interfaceName[16];                                    /**< Detected RFAL interface name. */
    char tagType[12];                                          /**< Detected NFC Forum tag type. */
    char cardType[32];                                         /**< Human-readable card type. */
    char cardInfo[96];                                         /**< Additional card details. */
    bool mifareClassic;                                        /**< true when the card is MIFARE Classic. */

    bool hasNdef;                                              /**< true when an NDEF context was found. */
    bool ndefAttempted;                                        /**< true when NDEF detection/read was attempted. */
    bool ndefSupported;                                        /**< true when NDEF is supported. */
    uint8_t ndefMajorVersion;                                  /**< NDEF major version. */
    uint8_t ndefMinorVersion;                                  /**< NDEF minor version. */
    uint32_t ndefAreaLen;                                      /**< Total NDEF area length. */
    uint32_t ndefAvailableLen;                                 /**< Available NDEF payload length. */
    uint32_t ndefMessageLen;                                   /**< Parsed NDEF message length. */
    char ndefStateText[16];                                    /**< Human-readable NDEF state. */
    char rawHexPreview[LILYGO_NFC_RAW_HEX_PREVIEW + 1];        /**< Hex preview of raw NDEF data. */

    uint8_t recordCount;                                       /**< Number of parsed records. */
    LilyGoNfcRecord records[LILYGO_NFC_MAX_RECORDS];           /**< Parsed record summaries. */
};

/**
 * @brief Reader-mode event callback.
 * @param event Reader event type.
 * @param result Current reader result snapshot.
 * @param userData User pointer supplied in the reader config.
 */
typedef void (*LilyGoNfcEventCallback)(LilyGoNfcEvent event,
                                       const LilyGoNfcReaderResult &result,
                                       void *userData);

/**
 * @brief Reader-mode configuration.
 */
struct LilyGoNfcReaderConfig {
    uint16_t technologies;               /**< RFAL technology mask used for discovery. */
    uint16_t discoveryDurationMs;        /**< Discovery duration in milliseconds. */
    bool wakeupEnabled;                  /**< true to enable wakeup-mode discovery. */
    LilyGoNfcEventCallback callback;     /**< Optional reader event callback. */
    void *userData;                      /**< User pointer passed to callback. */
};

/**
 * @brief Current tag-emulation status.
 */
struct LilyGoNfcEmulationStatus {
    LilyGoNfcState state;                                   /**< Current emulation state. */
    ReturnCode lastError;                                   /**< Last RFAL or NDEF error code. */
    char errorText[48];                                     /**< Human-readable error text. */
    LilyGoNfcEmulationKind kind;                            /**< Selected emulation payload kind. */
    LilyGoNfcEmulationTech tech;                            /**< Selected emulation technology. */
    char templateName[32];                                  /**< Display name for the active template. */
    char payloadPreview[LILYGO_NFC_MAX_EMULATION_TEXT_LEN]; /**< Human-readable payload preview. */
    char activeTechnology[16];                              /**< Technology currently activated by a reader. */
    char activeInterface[16];                               /**< RFAL interface currently activated by a reader. */
    uint32_t ndefFileLen;                                   /**< Generated NDEF file length in bytes. */
    uint32_t readCount;                                     /**< Number of NDEF reads observed. */
    uint32_t exchangeCount;                                 /**< Number of emulation exchanges observed. */
    bool readerActive;                                      /**< true while a reader is actively connected. */
    bool experimental;                                      /**< true when the selected mode is experimental. */
};

/**
 * @brief Tag-emulation event callback.
 * @param event Emulation event type.
 * @param status Current emulation status snapshot.
 * @param userData User pointer supplied in the emulation config.
 */
typedef void (*LilyGoNfcEmulationCallback)(LilyGoNfcEvent event,
                                           const LilyGoNfcEmulationStatus &status,
                                           void *userData);

/**
 * @brief Tag-emulation configuration.
 */
struct LilyGoNfcEmulationConfig {
    LilyGoNfcEmulationKind kind;           /**< Payload template kind. */
    LilyGoNfcEmulationTech tech;           /**< NFC technology used for emulation. */
    const char *templateName;              /**< Optional display name for the template. */
    const char *url;                       /**< URL payload for URL emulation. */
    const char *text;                      /**< Text payload for text emulation. */
    const char *wifiSsid;                  /**< Wi-Fi SSID for Wi-Fi emulation. */
    const char *wifiPassword;              /**< Wi-Fi password for Wi-Fi emulation. */
    uint16_t wifiAuthenticationType;       /**< NDEF Wi-Fi authentication type. */
    uint16_t wifiEncryptionType;           /**< NDEF Wi-Fi encryption type. */
    uint16_t discoveryDurationMs;          /**< Discovery duration in milliseconds. */
    LilyGoNfcEmulationCallback callback;   /**< Optional emulation event callback. */
    void *userData;                        /**< User pointer passed to callback. */
};

/**
 * @brief Stateful NFC reader and tag-emulation service.
 */
class LilyGoNfcService {
public:
    /**
     * @brief Construct an idle NFC service.
     */
    LilyGoNfcService();

    /**
     * @brief Start reader mode with an explicit configuration.
     * @param config Reader configuration.
     * @return true if reader discovery was started.
     */
    bool beginReader(const LilyGoNfcReaderConfig &config);

    /**
     * @brief Start reader mode with default discovery settings.
     * @param callback Optional reader event callback.
     * @param userData User pointer passed to callback.
     * @return true if reader discovery was started.
     */
    bool beginReader(LilyGoNfcEventCallback callback, void *userData = nullptr);

    /**
     * @brief Start tag emulation with an explicit configuration.
     * @param config Emulation configuration.
     * @return true if emulation discovery was started.
     */
    bool beginEmulation(const LilyGoNfcEmulationConfig &config);

    /**
     * @brief Start URL tag emulation.
     * @param url URL payload.
     * @param tech NFC technology used for emulation.
     * @param callback Optional emulation event callback.
     * @param userData User pointer passed to callback.
     * @return true if emulation discovery was started.
     */
    bool beginEmulationUrl(const char *url,
                           LilyGoNfcEmulationTech tech = LILYGO_NFC_EMULATION_TECH_NFCF_T3T,
                           LilyGoNfcEmulationCallback callback = nullptr,
                           void *userData = nullptr);

    /**
     * @brief Start text tag emulation.
     * @param text Text payload.
     * @param tech NFC technology used for emulation.
     * @param callback Optional emulation event callback.
     * @param userData User pointer passed to callback.
     * @return true if emulation discovery was started.
     */
    bool beginEmulationText(const char *text,
                            LilyGoNfcEmulationTech tech = LILYGO_NFC_EMULATION_TECH_NFCF_T3T,
                            LilyGoNfcEmulationCallback callback = nullptr,
                            void *userData = nullptr);

    /**
     * @brief Start Wi-Fi tag emulation.
     * @param ssid Wi-Fi SSID payload.
     * @param password Wi-Fi password payload.
     * @param tech NFC technology used for emulation.
     * @param callback Optional emulation event callback.
     * @param userData User pointer passed to callback.
     * @return true if emulation discovery was started.
     */
    bool beginEmulationWifi(const char *ssid,
                            const char *password,
                            LilyGoNfcEmulationTech tech = LILYGO_NFC_EMULATION_TECH_NFCF_T3T,
                            LilyGoNfcEmulationCallback callback = nullptr,
                            void *userData = nullptr);

    /**
     * @brief Poll NFC state and dispatch pending reader or emulation work.
     */
    void loop();

    /**
     * @brief Stop reader or emulation mode and return to idle.
     */
    void stop();

    /**
     * @brief Check whether the service is currently active.
     * @return true when reader or emulation mode is running.
     */
    bool isRunning() const;

    /**
     * @brief Get the current NFC service state.
     * @return Current service state.
     */
    LilyGoNfcState state() const;

    /**
     * @brief Get the last reader-mode result.
     * @return Reference to the internal reader result snapshot.
     */
    const LilyGoNfcReaderResult &lastResult() const;

    /**
     * @brief Get the current tag-emulation status.
     * @return Reference to the internal emulation status snapshot.
     */
    const LilyGoNfcEmulationStatus &emulationStatus() const;

private:
    /**
     * @brief Static RFAL notification trampoline.
     * @param st RFAL NFC state reported by the stack.
     */
    static void notifyStatic(rfalNfcState st);

    /**
     * @brief Start reader-mode RFAL discovery.
     * @return true if discovery was started.
     */
    bool startDiscovery();

    /**
     * @brief Start RFAL discovery for tag emulation.
     * @return true if discovery was started.
     */
    bool startEmulationDiscovery();

    /**
     * @brief Handle an RFAL notification in the active mode.
     * @param st RFAL NFC state reported by the stack.
     */
    void handleNotify(rfalNfcState st);

    /**
     * @brief Handle card or reader activation.
     */
    void handleActivated();

    /**
     * @brief Poll active tag-emulation data exchange.
     */
    void handleEmulationLoop();

    /**
     * @brief Prime the first tag-emulation data exchange.
     * @return true if the exchange was started.
     */
    bool primeEmulationExchange();

    /**
     * @brief Wait for another emulation frame after an exchange.
     * @return true if the next frame wait was started.
     */
    bool waitForNextEmulationFrame();

    /**
     * @brief Handle completion of an emulation data exchange.
     */
    void handleEmulationDataExchangeDone();

    /**
     * @brief Poll card presence while waiting for card release.
     */
    void handleWaitRelease();

    /**
     * @brief Enter the wait-release state after a card interaction.
     */
    void enterWaitRelease();

    /**
     * @brief Check whether the active card is still present.
     * @return true when the card appears to be present.
     */
    bool isActiveCardStillPresent();

    /**
     * @brief Fill reader result fields from detected RFAL devices.
     * @param dev Activated device.
     * @param ndefDev Device used for NDEF operations.
     * @return true if the card was described.
     */
    bool describeCard(rfalNfcDevice *dev, rfalNfcDevice *ndefDev);

    /**
     * @brief Read and parse NDEF records from a detected card.
     * @param dev Activated device.
     */
    void readNdef(rfalNfcDevice *dev);

    /**
     * @brief Append one parsed NDEF record to the reader result.
     * @param record NDEF record to summarize.
     * @return true if the record was appended.
     */
    bool appendRecord(const ndefRecord *record);

    /**
     * @brief Build the emulated NDEF file from a configuration.
     * @param config Emulation configuration.
     * @return true if a valid NDEF payload was generated.
     */
    bool configureEmulationNdef(const LilyGoNfcEmulationConfig &config);

    /**
     * @brief Configure RFAL discovery parameters for emulation.
     * @param techs RFAL technology mask.
     */
    void configureEmulationDiscovery(uint16_t techs);

    /**
     * @brief Emit a reader-mode event.
     * @param event Event to emit.
     */
    void emit(LilyGoNfcEvent event);

    /**
     * @brief Emit a tag-emulation event.
     * @param event Event to emit.
     */
    void emitEmulation(LilyGoNfcEvent event);

    /**
     * @brief Reset the reader result structure to default values.
     */
    void resetResult();

    /**
     * @brief Reset the emulation status structure to default values.
     */
    void resetEmulationStatus();

    /**
     * @brief Store a reader-mode error code and fallback text.
     * @param err Error code.
     * @param fallback Fallback text when the RFAL error has no display string.
     */
    void setError(ReturnCode err, const char *fallback);

    /**
     * @brief Store an emulation-mode error code and fallback text.
     * @param err Error code.
     * @param fallback Fallback text when the RFAL error has no display string.
     */
    void setEmulationError(ReturnCode err, const char *fallback);

    /** NDEF parser and builder helper. */
    NdefClass _ndef;
    /** Active service mode. */
    LilyGoNfcMode _mode;
    /** Reader configuration active in reader mode. */
    LilyGoNfcReaderConfig _config;
    /** Emulation configuration active in tag-emulation mode. */
    LilyGoNfcEmulationConfig _emulationConfig;
    /** Last reader result snapshot. */
    LilyGoNfcReaderResult _result;
    /** Current emulation status snapshot. */
    LilyGoNfcEmulationStatus _emulationStatus;
    /** RFAL discovery parameters used for emulation. */
    rfalNfcDiscoverParam _emulationDiscover;
    /** Scratch buffer for raw NFC and NDEF data. */
    uint8_t _rawBuffer[LILYGO_NFC_RAW_BUFFER_LEN];
    /** Generated emulated NDEF file buffer. */
    uint8_t _emulationNdefFile[LILYGO_NFC_MAX_EMULATION_NDEF_LEN];
    /** NFC-F ID used during emulation. */
    uint8_t _emulationNfcfNfcid2[RFAL_NFCF_NFCID2_LEN];
    /** Emulation transmit buffer. */
    uint8_t _emulationTxBuf[RFAL_NFC_RF_BUF_LEN];
    /** Pointer to RFAL-provided emulation receive data. */
    uint8_t *_emulationRxData;
    /** Pointer to RFAL-provided emulation receive length. */
    uint16_t *_emulationRxLen;
    /** Generated emulated NDEF file length. */
    uint32_t _emulationNdefFileLen;
    /** true while reader or emulation mode is running. */
    bool _running;
    /** true after the current activation has been handled. */
    bool _activeHandled;
    /** true when an NDEF context is available for the active card. */
    bool _hasNdefContext;
    /** true when cached active card fields are valid. */
    bool _activeCardValid;
    /** true when cached NFC-A activation data is valid. */
    bool _activeNfcaValid;
    /** true when a deactivate request is pending. */
    bool _deactivateRequested;
    /** true when the emulation exchange has been primed. */
    bool _emulationExchangePrimed;
    /** true while an external reader is connected to the emulated tag. */
    bool _emulationReaderActive;
    /** RFAL type of the active card. */
    rfalNfcDevType _activeType;
    /** UID bytes for the active card. */
    uint8_t _activeUid[LILYGO_NFC_MAX_UID_LEN];
    /** Number of valid bytes in _activeUid. */
    uint8_t _activeUidLen;
    /** Cached NFC-A listener device data. */
    rfalNfcaListenDevice _activeNfca;
    /** millis() timestamp of the last presence check. */
    uint32_t _lastPresenceCheckMs;
    /** Number of consecutive failed presence checks. */
    uint8_t _missingPresenceCount;
};

/** Global NFC service instance. */
extern LilyGoNfcService LilyGoNfc;

/**
 * @brief Convert an NFC state to a display string.
 * @param state State value.
 * @return Static state name.
 */
const char *lilygoNfcStateName(LilyGoNfcState state);

/**
 * @brief Convert an NFC event to a display string.
 * @param event Event value.
 * @return Static event name.
 */
const char *lilygoNfcEventName(LilyGoNfcEvent event);

/**
 * @brief Convert a parsed record kind to a display string.
 * @param kind Record kind.
 * @return Static record kind name.
 */
const char *lilygoNfcRecordKindName(LilyGoNfcRecordKind kind);

/**
 * @brief Convert an emulation payload kind to a display string.
 * @param kind Emulation kind.
 * @return Static emulation kind name.
 */
const char *lilygoNfcEmulationKindName(LilyGoNfcEmulationKind kind);

/**
 * @brief Convert an emulation technology to a display string.
 * @param tech Emulation technology.
 * @return Static technology name.
 */
const char *lilygoNfcEmulationTechName(LilyGoNfcEmulationTech tech);

#endif
