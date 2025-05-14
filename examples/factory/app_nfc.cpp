/**
 * @file      nfc_reader.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-11
 *
 */

#include "app_nfc.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)

enum NFCReaderState {
    ST_POLLING,
    ST_WAIT_RELEASED,
};

static uint8_t          rawBuffer[1024] = {0};
static NdefClass        ndef(&NFCReader);
static NFCReaderState   state = ST_POLLING;
static notify_callback_t ndef_notify_cb = NULL;
static ndef_event_callback_t ndef_event_cb = NULL;

static ReturnCode ndefRecordDumpType(const ndefRecord *record);
static ReturnCode ndefRtdDeviceInfoDump(const ndefType *devInfo, ndefTypeRtdDeviceInfo *devInfoData);
static ReturnCode ndefRtdTextDump(const ndefType *text, ndefRtdText * rtdText);
static ReturnCode ndefRtdUriDump(const ndefType *uri, ndefRtdUri *rtdUri);
static ReturnCode ndefRtdAarDump(const ndefType *aar, ndefConstBuffer *bufAarString);
static ReturnCode ndefMediaWifiDump(const ndefType *wifi, ndefTypeWifi *wifiConfig);

static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation);
static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation);
static ReturnCode ndefMediaVCardDump(const ndefType *vCard);
static ReturnCode ndefRecordDump(const ndefRecord *record, bool verbose);

static void ndefClassHandler()
{
    rfalNfcDevice *nfcDev;
    NFCReader.rfalNfcGetActiveDevice(&nfcDev);

    Serial.print("NDEF ID:");
    for (int i = 0; i < nfcDev->nfcidLen; i++) {
        Serial.print(nfcDev->nfcid[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // See if we can get an NDEF record from it
    ReturnCode  err = ndef.ndefPollerContextInitialization(nfcDev);
    if (err != ST_ERR_NONE) {
        return;
    }

    Serial.println("NDEF context initialized.");

    ndefInfo info;
    err = ndef.ndefPollerNdefDetect(&info);
    if (err != ST_ERR_NONE) {
        Serial.printf("ndefPollerNdefDetect error: %u %s\n", err, ndef.errorToString(err));
        return;
    }

    Serial.println("NDEF detected.");
    uint32_t actual_size = 0;

    memset(rawBuffer, 0, sizeof(rawBuffer));
    err = ndef.ndefPollerReadRawMessage(rawBuffer,  sizeof(rawBuffer), &actual_size);
    if (err != ST_ERR_NONE) {
        return;
    }

    ndefMessage ndefMsg;
    ndefConstBuffer ndefBuf;
    ndefBuf.buffer = rawBuffer;
    ndefBuf.length = actual_size;

    err = ndef.ndefMessageDecode(&ndefBuf, &ndefMsg);
    if (err != ST_ERR_NONE) {
        Serial.printf("Decode message failed. errcode : %d\n", err);
        return;
    }

    // Got an NDEF "message" (a set of NDEF records)
    ndefRecord *record = ndefMessageGetFirstRecord(&ndefMsg);
    while (record != NULL) {
        err = ndefRecordDump(record, false);
        if (err != ST_ERR_NONE) {
            Serial.println("error ....");
            return ;
        }
        record = ndefMessageGetNextRecord(record);
    }
}

static void demoNotif(rfalNfcState st )
{
    if ( st == RFAL_NFC_STATE_WAKEUP_MODE ) {
        Serial.println("Wake Up mode started");
    } else if ( st == RFAL_NFC_STATE_POLL_TECHDETECT ) {
        Serial.println("Wake Up mode terminated. Polling for devices \r\n");
    } else if ( st == RFAL_NFC_STATE_POLL_SELECT ) {
        Serial.println("State poll select");
    } else if ( st == RFAL_NFC_STATE_START_DISCOVERY ) {
        Serial.println("State start discovery");
    } else if (st == RFAL_NFC_STATE_ACTIVATED) {
        if (ndef_notify_cb) {
            ndef_notify_cb();
        }
        ndefClassHandler();
        NFCReader.rfalNfcDeactivate(true);
        NFCReader.rfalNfcaPollerSleep();

#if POLLING
        rfalNfcaSensRes       sensRes;
        rfalNfcaSelRes        selRes;
        rfalNfcDevice *nfcDev;
        NFCReader.rfalNfcGetActiveDevice(&nfcDev);
        /* Loop until tag is removed from the field */
        Serial.print("Operation completed\r\nTag can be removed from the field\r\n");
        NFCReader.rfalNfcaPollerInitialize();
        while (NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes) == ST_ERR_NONE) {
            if (((nfcDev->dev.nfca.type == RFAL_NFCA_T1T) && (!rfalNfcaIsSensResT1T(&sensRes))) ||
                    ((nfcDev->dev.nfca.type != RFAL_NFCA_T1T) && (NFCReader.rfalNfcaPollerSelect(nfcDev->dev.nfca.nfcId1, nfcDev->dev.nfca.nfcId1Len, &selRes) != ST_ERR_NONE))) {
                break;
            }
            Serial.println(".");
            NFCReader.rfalNfcaPollerSleep();
            delay(130);
        }
        Serial.println("Start discovery");
#else
        state = ST_WAIT_RELEASED;
#endif
    }
}



uint32_t interval = 0;

void loopNFCReader()
{
#if POLLING
    NFCReader.rfalNfcWorker();
#else
    switch (state) {
    case ST_POLLING:
        NFCReader.rfalNfcWorker();
        break;
    case ST_WAIT_RELEASED: {
        rfalNfcaSensRes sensRes;
        rfalNfcaSelRes  selRes;
        rfalNfcDevice   *nfcDev;
        NFCReader.rfalNfcGetActiveDevice(&nfcDev);
        NFCReader.rfalNfcaPollerInitialize();
        if (NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes) == ST_ERR_NONE) {
            if (((nfcDev->dev.nfca.type == RFAL_NFCA_T1T) && (!rfalNfcaIsSensResT1T(&sensRes))) ||
                    ((nfcDev->dev.nfca.type != RFAL_NFCA_T1T) &&
                     (NFCReader.rfalNfcaPollerSelect(nfcDev->dev.nfca.nfcId1, nfcDev->dev.nfca.nfcId1Len, &selRes) != ST_ERR_NONE))) {
                state = ST_POLLING;
                Serial.println("Start discovery");
                // break;
                return ;
            }
            if (millis() > interval) {
                Serial.println("Operation completed,Tag can be removed from the field");
                interval = millis() + 1000;
            }
            // Serial.println(".");
            NFCReader.rfalNfcaPollerSleep();
            // delay(130);
        }
    }
    break;
    default:
        break;
    }
#endif
}

static ReturnCode ndefBufferPrint(const char *prefix, const ndefConstBuffer *bufString, const char *suffix)
{
    uint32_t i;
    if ((prefix == NULL) || (bufString == NULL) || (bufString->buffer == NULL) || (suffix  == NULL)) {
        return ST_ERR_PARAM;
    }
    Serial.print(prefix);
    for (i = 0; i < bufString->length; i++) {
        Serial.print((char)bufString->buffer[i]);
    }
    Serial.print(suffix);
    return ST_ERR_NONE;
}

static ReturnCode ndefBuffer8Print(const char *prefix, const ndefConstBuffer8 *bufString, const char *suffix)
{
    ndefConstBuffer buf;
    if (bufString == NULL) {
        return ST_ERR_PARAM;
    }
    buf.buffer = bufString->buffer;
    buf.length = bufString->length;
    return ndefBufferPrint(prefix, &buf, suffix);
}

static bool isPrintableASCII(const uint8_t *str, uint32_t strLen)
{
    uint32_t i;
    if ((str == NULL) || (strLen == 0)) {
        return false;
    }
    for (i = 0; i < strLen; i++) {
        if ((str[i] < 0x20) || (str[i] > 0x7E)) {
            return false;
        }
    }
    return true;
}

static ReturnCode ndefBufferDumpLine(const uint8_t *buffer, const uint32_t offset, uint32_t lineLength, uint32_t remaining)
{
    uint32_t j;
    if (buffer == NULL) {
        return ST_ERR_PARAM;
    }
    Serial.print(" [");
    Serial.print(offset, HEX);
    Serial.print("] ");
    /* Dump hex data */
    for (j = 0; j < remaining; j++) {
        Serial.print(buffer[offset + j], HEX);
        Serial.print(" ");
    }
    /* Fill hex section if needed */
    for (j = 0; j < lineLength - remaining; j++) {
        Serial.print("   ");
    }
    /* Dump characters */
    Serial.print("|");
    for (j = 0; j < remaining; j++) {
        /* Dump only ASCII characters, otherwise replace with a '.' */
        Serial.print((isPrintableASCII(&buffer[offset + j], 1) ? (char)buffer[offset + j] : '.'));
    }
    /* Fill ASCII section if needed */
    for (j = 0; j < lineLength - remaining; j++) {
        Serial.print("  ");
    }
    Serial.print(" |\r\n");
    return ST_ERR_NONE;
}

static ReturnCode ndefBufferDump(const char *string, const ndefConstBuffer *bufPayload, bool verbose)
{
    uint32_t bufferLengthMax = 32;
    const uint32_t lineLength = 8;
    uint32_t displayed;
    uint32_t remaining;
    uint32_t offset;
    if ((string == NULL) || (bufPayload == NULL)) {
        return ST_ERR_PARAM;
    }
    displayed = bufPayload->length;
    remaining = bufPayload->length;
    Serial.print(string);
    Serial.print(" (length ");
    Serial.print(bufPayload->length);
    Serial.print(")\r\n");
    if (bufPayload->buffer == NULL) {
        Serial.print(" <No chunk payload buffer>\r\n");
        return ST_ERR_NONE;
    }
    if (verbose == true) {
        bufferLengthMax = 256;
    }
    if (bufPayload->length > bufferLengthMax) {
        /* Truncate output */
        displayed = bufferLengthMax;
    }
    for (offset = 0; offset < displayed; offset += lineLength) {
        ndefBufferDumpLine(bufPayload->buffer, offset, lineLength, remaining > lineLength ? lineLength : remaining);
        remaining -= lineLength;
    }
    if (displayed < bufPayload->length) {
        Serial.print(" ... (truncated)\r\n");
    }
    return ST_ERR_NONE;
}

static ReturnCode ndefRecordDumpType(const ndefRecord *record)
{
    static ndefTypeRtdDeviceInfo   devInfoData;
    static ndefConstBuffer         bufAarString;
    static ndefTypeWifi            wifiConfig;
    static ndefRtdUri              url;
    static ndefRtdText             rtdText;

    ReturnCode err;
    ndefType   type;
    err = ndef.ndefRecordToType(record, &type);
    if (err != ST_ERR_NONE) {
        return err;
    }
    void *user_data = NULL;
    switch (type.id) {
    case NDEF_TYPE_EMPTY:
        Serial.print(" Empty record\r\n");
        break;
    case NDEF_TYPE_RTD_DEVICE_INFO:
        ndefRtdDeviceInfoDump(&type, &devInfoData);
        user_data = &devInfoData;
        break;
    case NDEF_TYPE_RTD_TEXT:
        ndefRtdTextDump(&type, &rtdText);
        user_data = &rtdText;
        break;
    case NDEF_TYPE_RTD_URI:
        ndefRtdUriDump(&type, &url);
        user_data = &url;
        break;
    case NDEF_TYPE_RTD_AAR:
        ndefRtdAarDump(&type, &bufAarString);
        user_data = &bufAarString;
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        ndefMediaVCardDump(&type);
        break;
    case NDEF_TYPE_MEDIA_WIFI:
        ndefMediaWifiDump(&type, &wifiConfig);
        user_data = &wifiConfig;
        break;
    case NDEF_TYPE_ID_COUNT:
    default:
        break;
    }
    if (ndef_event_cb) {
        ndef_event_cb(type.id, user_data);
    }
    return ST_ERR_NOT_IMPLEMENTED;
}

static ReturnCode ndefRecordDump(const ndefRecord *record, bool verbose)
{
    static uint32_t index;
    const uint8_t *ndefTNFNames[] = {
        (uint8_t *)"Empty",
        (uint8_t *)"NFC Forum well-known type [NFC RTD]",
        (uint8_t *)"Media-type as defined in RFC 2046",
        (uint8_t *)"Absolute URI as defined in RFC 3986",
        (uint8_t *)"NFC Forum external type [NFC RTD]",
        (uint8_t *)"Unknown",
        (uint8_t *)"Unchanged",
        (uint8_t *)"Reserved"
    };
    uint8_t *headerSR = (uint8_t *)"";
    ReturnCode err;

    if (record == NULL) {
        Serial.print("No record\r\n");
        return ST_ERR_NONE;
    }
    if (ndefHeaderIsSetMB(record)) {
        index = 1U;
    } else {
        index++;
    }
    if (verbose == true) {
        headerSR = (uint8_t *)(ndefHeaderIsSetSR(record) ? " - Short Record" : " - Standard Record");
    }
    Serial.print("Record #");
    Serial.print(index);
    Serial.print((char *)headerSR);
    Serial.print("\r\n");
    /* Well-known type dump */
    err = ndefRecordDumpType(record);

#ifdef DEBUG_NDEF
    if (verbose == true) {
        /* Raw dump */
        //Serial.print(" MB:%d ME:%d CF:%d SR:%d IL:%d TNF:%d\r\n", ndefHeaderMB(record), ndefHeaderME(record), ndefHeaderCF(record), ndefHeaderSR(record), ndefHeaderIL(record), ndefHeaderTNF(record));
        Serial.print(" MB ME CF SR IL TNF\r\n");
        Serial.print("  ");
        Serial.print(ndefHeaderMB(record));
        Serial.print("  ");
        Serial.print(ndefHeaderME(record));
        Serial.print("  ");
        Serial.print(ndefHeaderCF(record));
        Serial.print("  ");
        Serial.print(ndefHeaderSR(record));
        Serial.print("  ");
        Serial.print(ndefHeaderIL(record));
        Serial.print("  ");
        Serial.print(ndefHeaderTNF(record));
        Serial.print("\r\n");
    }
    if ((err != ST_ERR_NONE) || (verbose == true)) {
        Serial.print(" Type Name Format: ");
        Serial.print((char *)ndefTNFNames[ndefHeaderTNF(record)]);
        Serial.print("\r\n");
        uint8_t tnf;
        ndefConstBuffer8 bufRecordType;
        ndef.ndefRecordGetType(record, &tnf, &bufRecordType);
        if ((tnf == NDEF_TNF_EMPTY) && (bufRecordType.length == 0U)) {
            Serial.print(" Empty NDEF record\r\n");
        } else {
            ndefBuffer8Print(" Type: \"", &bufRecordType, "\"\r\n");
        }

        if (ndefHeaderIsSetIL(record)) {
            /* ID Length bit set */
            ndefConstBuffer8 bufRecordId;
            ndef.ndefRecordGetId(record, &bufRecordId);
            ndefBuffer8Print(" ID: \"", &bufRecordId, "\"\r\n");
        }

        ndefConstBuffer bufRecordPayload;
        ndef.ndefRecordGetPayload(record, &bufRecordPayload);
        ndefBufferDump(" Payload:", &bufRecordPayload, verbose);
    }
#endif

    return ST_ERR_NONE;
}


static ReturnCode ndefRtdDeviceInfoDump(const ndefType *devInfo, ndefTypeRtdDeviceInfo *devInfoData)
{

    if (devInfo == NULL) {
        return ST_ERR_PARAM;
    }
    if (devInfoData == NULL) {
        return ST_ERR_PARAM;
    }
    if (devInfo->id != NDEF_TYPE_RTD_DEVICE_INFO) {
        return ST_ERR_PARAM;
    }

    ndef.ndefGetRtdDeviceInfo(devInfo, devInfoData);

#ifdef DEBUG_NDEF

    uint32_t type;
    uint32_t i;
    const uint8_t *ndefDeviceInfoName[] = {
        (uint8_t *)"Manufacturer",
        (uint8_t *)"Model",
        (uint8_t *)"Device",
        (uint8_t *)"UUID",
        (uint8_t *)"Firmware version",
    };
    Serial.print(" Device Information:\r\n");
    for (type = 0; type < NDEF_DEVICE_INFO_TYPE_COUNT; type++) {
        if (devInfoData->devInfo[type].buffer != NULL) {
            Serial.print(" - ");
            Serial.print((char *)ndefDeviceInfoName[devInfoData->devInfo[type].type]);
            Serial.print(": ");
            if (type != NDEF_DEVICE_INFO_UUID) {
                for (i = 0; i < devInfoData->devInfo[type].length; i++) {
                    Serial.print(devInfoData->devInfo[type].buffer[i]); /* character */
                }
            } else {
                for (i = 0; i < devInfoData->devInfo[type].length; i++) {
                    Serial.print(devInfoData->devInfo[type].buffer[i], HEX); /* hex number */
                }
            }
            Serial.print("\r\n");
        }
    }
#endif

    return ST_ERR_NONE;
}

static ReturnCode ndefRtdTextDump(const ndefType *text, ndefRtdText * rtdText)
{

    if (text == NULL || rtdText == NULL ) {
        return ST_ERR_PARAM;
    }
    if (text->id != NDEF_TYPE_RTD_TEXT) {
        return ST_ERR_PARAM;
    }

    ndef.ndefGetRtdText(text, &rtdText->utfEncoding, &rtdText->bufLanguageCode, &rtdText->bufSentence);

#ifdef DEBUG_NDEF
    ndefBufferPrint(" Text: \"", &rtdText->bufSentence, "");
    Serial.print("\" (");
    Serial.print((utfEncoding == TEXT_ENCODING_UTF8 ? "UTF8" : "UTF16"));
    Serial.print(",");
    ndefBuffer8Print(" language code \"", &rtdText->bufLanguageCode, "\")\r\n");
#endif
    return ST_ERR_NONE;
}

static ReturnCode ndefRtdUriDump(const ndefType *uri, ndefRtdUri *rtdUri)
{
    if (uri == NULL || rtdUri == NULL) {
        return ST_ERR_PARAM;
    }
    if (uri->id != NDEF_TYPE_RTD_URI) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetRtdUri(uri, &rtdUri->bufProtocol, &rtdUri->bufUriString);
#ifdef DEBUG_NDEF
    ndefBufferPrint("URI: (", &rtdUri->bufProtocol, ")");
    ndefBufferPrint("", &rtdUri->bufUriString, "\r\n");
#endif
    return ST_ERR_NONE;
}

static ReturnCode ndefRtdAarDump(const ndefType *aar, ndefConstBuffer *bufAarString)
{
    if (aar == NULL || bufAarString == NULL) {
        return ST_ERR_PARAM;
    }
    if (aar->id != NDEF_TYPE_RTD_AAR) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetRtdAar(aar, bufAarString);
#ifdef DEBUG_NDEF
    ndefBufferPrint(" AAR Package: ", bufAarString, "\r\n");
#endif
    return ST_ERR_NONE;
}

static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation)
{
    typedef struct {
        uint8_t *vCardString;
        uint8_t *english;
    } ndefTranslate;

    const ndefTranslate translate[] = {
        { (uint8_t *)"N", (uint8_t *)"Name"           },
        { (uint8_t *)"FN", (uint8_t *)"Formatted Name" },
        { (uint8_t *)"ADR", (uint8_t *)"Address"        },
        { (uint8_t *)"TEL", (uint8_t *)"Phone"          },
        { (uint8_t *)"EMAIL", (uint8_t *)"Email"          },
        { (uint8_t *)"TITLE", (uint8_t *)"Title"          },
        { (uint8_t *)"ORG", (uint8_t *)"Org"            },
        { (uint8_t *)"URL", (uint8_t *)"URL"            },
        { (uint8_t *)"PHOTO", (uint8_t *)"Photo"          },
    };

    uint32_t i;

    if ((bufText == NULL) || (bufTranslation == NULL)) {
        return ST_ERR_PROTO;
    }

    for (i = 0; i < SIZEOF_ARRAY(translate); i++) {
        if (ST_BYTECMP(bufText->buffer, translate[i].vCardString, strlen((char *)translate[i].vCardString)) == 0) {
            bufTranslation->buffer = translate[i].english;
            bufTranslation->length = strlen((char *)translate[i].english);

            return ST_ERR_NONE;
        }
    }

    bufTranslation->buffer = bufText->buffer;
    bufTranslation->length = bufText->length;

    return ST_ERR_NONE;
}

static ReturnCode ndefMediaVCardDump(const ndefType *vCard)
{
    ndefConstBuffer bufTypeN     = { (uint8_t *)"N",     strlen((char *)"N")     };
    ndefConstBuffer bufTypeFN    = { (uint8_t *)"FN",    strlen((char *)"FN")    };
    ndefConstBuffer bufTypeADR   = { (uint8_t *)"ADR",   strlen((char *)"ADR")   };
    ndefConstBuffer bufTypeTEL   = { (uint8_t *)"TEL",   strlen((char *)"TEL")   };
    ndefConstBuffer bufTypeEMAIL = { (uint8_t *)"EMAIL", strlen((char *)"EMAIL") };
    ndefConstBuffer bufTypeTITLE = { (uint8_t *)"TITLE", strlen((char *)"TITLE") };
    ndefConstBuffer bufTypeORG   = { (uint8_t *)"ORG",   strlen((char *)"ORG")   };
    ndefConstBuffer bufTypeURL   = { (uint8_t *)"URL",   strlen((char *)"URL")   };
    ndefConstBuffer bufTypePHOTO = { (uint8_t *)"PHOTO", strlen((char *)"PHOTO") };

    const ndefConstBuffer *bufVCardField[] = {
        &bufTypeN,
        &bufTypeFN,
        &bufTypeADR,
        &bufTypeTEL,
        &bufTypeEMAIL,
        &bufTypeTITLE,
        &bufTypeORG,
        &bufTypeURL,
        &bufTypePHOTO,
    };

    uint32_t i;
    const ndefConstBuffer *bufType;
    ndefConstBuffer        bufSubType;
    ndefConstBuffer        bufValue;

    if (vCard == NULL) {
        return ST_ERR_PARAM;
    }

    if (vCard->id != NDEF_TYPE_MEDIA_VCARD) {
        return ST_ERR_PARAM;
    }

    Serial.print(" vCard decoded: \r\n");

    for (i = 0; i < SIZEOF_ARRAY(bufVCardField); i++) {
        /* Requesting vCard field */
        bufType = bufVCardField[i];

        /* Get information from vCard */
        ndef.ndefGetVCard(vCard, bufType, &bufSubType, &bufValue);

        if (bufValue.buffer != NULL) {
            ndefConstBuffer bufTypeTranslate;
            ndefMediaVCardTranslate(bufType, &bufTypeTranslate);

            /* Type */
            ndefBufferPrint(" ", &bufTypeTranslate, "");

            /* Subtype, if any */
            if (bufSubType.buffer != NULL) {
                ndefBufferPrint(" (", &bufSubType, ")");
            }

            /* Value */
            if (ST_BYTECMP(bufType->buffer, bufTypePHOTO.buffer, bufTypePHOTO.length) != 0) {
                ndefBufferPrint(": ", &bufValue, "\r\n");
            } else {
                Serial.print("Photo: <Not displayed>\r\n");
            }
        }
    }

    return ST_ERR_NONE;
}

static ReturnCode ndefMediaWifiDump(const ndefType *wifi, ndefTypeWifi *wifiConfig)
{
    if (wifi == NULL || wifiConfig == NULL) {
        return ST_ERR_PARAM;
    }
    if (wifi->id != NDEF_TYPE_MEDIA_WIFI) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetWifi(wifi, wifiConfig);

#ifdef DEBUG_NDEF
    Serial.print(" Wifi config: \r\n");
    Serial.printf("SSID:%s PASSWORD:%s\n", wifiConfig->bufNetworkSSID.buffer, wifiConfig->bufNetworkKey.buffer);
    ndefBufferDump(" Network SSID:",       &wifiConfig->bufNetworkSSID, false);
    ndefBufferDump(" Network Key:",        &wifiConfig->bufNetworkKey, false);
    Serial.print(" Authentication: ");
    Serial.print(wifiConfig->authentication);
    Serial.print("\r\n");
    Serial.print(" Encryption: ");
    Serial.print(wifiConfig->encryption);
    Serial.print("\r\n");
#endif
    return ST_ERR_NONE;
}

bool beginNFC(notify_callback_t notify_cb, ndef_event_callback_t event_cb)
{
    bool res = false;
    ndef_notify_cb = notify_cb;
    ndef_event_cb = event_cb;
    Serial.print("Initializing NFC: ");
    res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        Serial.println("Failed to find NFC Reader - check your wiring!");
        return false;
    } else {
        Serial.println("Initializing NFC Reader succeeded");
    }
    rfalNfcDiscoverParam discover_params;
    discover_params.devLimit = 1;
    discover_params.techs2Find = RFAL_NFC_POLL_TECH_A;
    discover_params.GBLen = RFAL_NFCDEP_GB_MAX_LEN;
    discover_params.notifyCb = demoNotif;
    discover_params.totalDuration = 1000U;
    discover_params.wakeupEnabled = false;
    Serial.print("Starting discovery mode: ");
    Serial.println(NFCReader.rfalNfcDiscover(&discover_params));
    return true;
}

#endif /*ARDUINO*/
