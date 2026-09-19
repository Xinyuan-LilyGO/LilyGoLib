/**
 * @file      GPS.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-07
 *
 */
#include "LilyGoLog.h"
#include "GPS.h"
#include "minmea.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

TinyGPSField::TinyGPSField() :
    _valid(false),
    _lastCommitTime(0)
{
}

uint32_t TinyGPSField::age() const
{
    if (!_valid) {
        return UINT32_MAX;
    }
    return millis() - _lastCommitTime;
}

void TinyGPSField::commit()
{
    _valid = true;
    _lastCommitTime = millis();
}

TinyGPSLocation::TinyGPSLocation() :
    _lat(0.0),
    _lng(0.0)
{
}

void TinyGPSLocation::set(double lat, double lng)
{
    _lat = lat;
    _lng = lng;
    commit();
}

TinyGPSDate::TinyGPSDate() :
    _year(0),
    _month(0),
    _day(0)
{
}

void TinyGPSDate::set(uint16_t year, uint8_t month, uint8_t day)
{
    if (year == 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        return;
    }
    _year = year;
    _month = month;
    _day = day;
    commit();
}

TinyGPSTime::TinyGPSTime() :
    _hour(0),
    _minute(0),
    _second(0)
{
}

void TinyGPSTime::set(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (hour > 23 || minute > 59 || second > 60) {
        return;
    }
    _hour = hour;
    _minute = minute;
    _second = second;
    commit();
}

TinyGPSInteger::TinyGPSInteger() :
    _value(0)
{
}

void TinyGPSInteger::set(uint32_t value)
{
    _value = value;
    commit();
}

TinyGPSHDOP::TinyGPSHDOP() :
    _hdop(0.0)
{
}

void TinyGPSHDOP::set(double value)
{
    _hdop = value;
    commit();
}

TinyGPSAltitude::TinyGPSAltitude() :
    _meters(0.0)
{
}

void TinyGPSAltitude::setMeters(double meters)
{
    _meters = meters;
    commit();
}

TinyGPSSpeed::TinyGPSSpeed() :
    _kmph(0.0)
{
}

void TinyGPSSpeed::setKmph(double kmph)
{
    _kmph = kmph;
    commit();
}

struct uBloxGnssModelInfo { // Structure to hold the module info (uses 341 bytes of RAM)
    char softVersion[30];
    char hardwareVersion[10];
    uint8_t extensionNo = 0;
    char extension[10][30];
} ;

static int getAck(Stream &stream, uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID);


static bool checkL76K(HardwareSerial &SerialGPS, String &gps_model)
{
    SerialGPS.updateBaudRate(9600);
    SerialGPS.setTimeout(10);
    bool result = false;
    uint32_t startTimeout ;
    for (int i = 0; i < 3; ++i) {
        SerialGPS.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
        delay(5);
        // Get version information
        startTimeout = millis() + 3000;
        LILYGO_LOG_D("Try to init L76K . Wait stop .");
        while (SerialGPS.available()) {
            SerialGPS.read();
            if (millis() > startTimeout) {
                LILYGO_LOG_D("Wait L76K stop NMEA timeout,retry again!");
                break;
            }
        };
        SerialGPS.flush();
        delay(200);

        SerialGPS.write("$PCAS06,0*1B\r\n");
        startTimeout = millis() + 500;
        String ver = "";
        while (!SerialGPS.available()) {
            if (millis() > startTimeout) {
                LILYGO_LOG_D("Get L76K Info timeout!");
                break;
            }
        }
        SerialGPS.setTimeout(10);
        ver = SerialGPS.readStringUntil('\n');
        if (ver.startsWith("$GPTXT,01,01,02")) {
            LILYGO_LOG_D("L76K GNSS init succeeded, using L76K GNSS Module");
            gps_model = "L76K";
            result = true;
            break;
        }
        delay(500);
    }
    // Initialize the L76K Chip, use GPS + GLONASS
    // SerialGPS.write("$PCAS04,5*1C\r\n");
    // GPS + BeiDou + GLONASS
    SerialGPS.write("$PCAS04,7*1C\r\n");
    delay(250);
    // only ask for RMC and GGA
    // SerialGPS.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n");
    // All nmea message output
    SerialGPS.write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n");
    delay(250);
    // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
    SerialGPS.write("$PCAS11,3*1E\r\n");
    return result;
}

static bool checkLS550G(HardwareSerial &SerialGPS, String &gps_model)
{
    SerialGPS.updateBaudRate(115200);

    bool result = false;
    bool timeout = false;

    uint32_t startTimeout ;

    for (int i = 0; i < 3; ++i) {
        // Stops GNSS engine.
        SerialGPS.write("$PQTMGNSSSTOP*09\r\n");
        delay(5);
        startTimeout = millis() + 300;
        LILYGO_LOG_D("Try to init LS550G . Wait stop .");
        while (SerialGPS.available()) {
            SerialGPS.read();
            if (millis() > startTimeout) {
                LILYGO_LOG_E("Wait LS550G stop NMEA timeout!");
                timeout = true;
                break;
            }
        };
        if (timeout) {
            timeout = false;
            continue;
        }
        SerialGPS.flush();
        delay(200);

        SerialGPS.write("$PQTMQVER*08\r\n");
        startTimeout = millis() + 500;
        String ver = "";
        while (!SerialGPS.available()) {
            if (millis() > startTimeout) {
                LILYGO_LOG_E("Get LS550G timeout!");
                timeout = true;
                break;
            }
        }
        if (timeout) {
            timeout = false;
            continue;
        }
        SerialGPS.setTimeout(10);

        // Should get string :  $PQTMQVER,OK,1,MODULE,LS550G00AANR01A03S,2025/07/31,18:23:57*49
        ver = SerialGPS.readStringUntil('\n');
        LILYGO_LOG_D("REV = %s", ver.c_str());
        if (ver.startsWith("$PQTMQVER,OK,1,MODULE,LS550G")) {
            LILYGO_LOG_D("REV = %s", ver.c_str());
            LILYGO_LOG_D("LS550G GNSS init succeeded, using LS550G GNSS Module\n");
            gps_model = "LS550G";
            result = true;
            break;
        }
        delay(500);
    }
    // Use GPS + GLONASS + Galileo + BDS + QZSS
    SerialGPS.write("$PQTMCFGCNST,W,1,1,1,1,0,0*2B\r\n");
    delay(50);

    // Set the 1PPS feature configuration:
    SerialGPS.write("$PQTMCFGPPS,W,1,1,100,1,1,0*73\r\n");
    delay(50);

    // Starts GNSS engine
    SerialGPS.write("$PQTMGNSSSTART*51\r\n");

    return result;
}

static bool checkUblox(HardwareSerial &SerialGPS, String &gps_model)
{
    struct uBloxGnssModelInfo info ;

    uint8_t buffer[256];

    //  Get UBlox GPS module version
    uint8_t cfg_get_hw[] =  {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
    SerialGPS.write(cfg_get_hw, sizeof(cfg_get_hw));

    uint16_t len = getAck(SerialGPS, buffer, 256, 0x0A, 0x04);
    if (len) {
        memset((void *)&info, 0, sizeof(info));
        uint16_t position = 0;
        for (int i = 0; i < 30; i++) {
            info.softVersion[i] = buffer[position];
            position++;
        }
        for (int i = 0; i < 10; i++) {
            info.hardwareVersion[i] = buffer[position];
            position++;
        }
        while (len >= position + 30) {
            for (int i = 0; i < 30; i++) {
                info.extension[info.extensionNo][i] = buffer[position];
                position++;
            }
            info.extensionNo++;
            if (info.extensionNo > 9)
                break;
        }

        LILYGO_LOG_I("Module Info : ");
        LILYGO_LOG_I("Soft version: %s", info.softVersion);
        LILYGO_LOG_I("Hard version: %s", info.hardwareVersion);
        LILYGO_LOG_I("Extensions: %d", info.extensionNo);
        for (int i = 0; i < info.extensionNo; i++) {
            LILYGO_LOG_I("%s", info.extension[i]);
        }
        LILYGO_LOG_I("Model:%s", info.extension[2]);

        for (int i = 0; i < info.extensionNo; ++i) {
            if (!strncmp(info.extension[i], "OD=", 3)) {
                // Should get "MIA-M10Q"
                strcpy((char *)buffer, &(info.extension[i][3]));
                LILYGO_LOG_I("GPS Model: %s", (char *)buffer);
                gps_model = (char *)buffer;
            }
        }
        return true;
    }
    return false;
}

static int getAck(Stream &stream, uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint16_t    ubxFrameCounter = 0;
    uint32_t    startTime = millis();
    uint16_t    needRead =  0;

    while (millis() - startTime < 800) {
        while (stream.available()) {
            int c = stream.read();
            switch (ubxFrameCounter) {
            case 0:
                if (c == 0xB5) {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                if (c == 0x62) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                if (c == requestedClass) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                if (c == requestedID) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                needRead |=  (c << 8);
                ubxFrameCounter++;
                break;
            case 6:
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (stream.readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    return 0;
}

static bool nmeaFieldHasValue(const char *sentence, uint8_t fieldIndex)
{
    if (!sentence || sentence[0] != '$') {
        return false;
    }
    uint8_t index = 0;
    const char *p = sentence;
    while (*p && *p != '*') {
        if (*p == ',') {
            index++;
            const char *start = p + 1;
            const char *end = start;
            while (*end && *end != ',' && *end != '*') {
                end++;
            }
            if (index == fieldIndex) {
                return end > start;
            }
        }
        p++;
    }
    return false;
}

static bool nmeaFieldToInt(const char *sentence, uint8_t fieldIndex, int *value)
{
    if (!sentence || sentence[0] != '$' || !value) {
        return false;
    }
    uint8_t index = 0;
    const char *p = sentence;
    while (*p && *p != '*') {
        if (*p == ',') {
            index++;
            const char *start = p + 1;
            const char *end = start;
            while (*end && *end != ',' && *end != '*') {
                end++;
            }
            if (index == fieldIndex) {
                if (end == start || end - start >= 12) {
                    return false;
                }
                char buffer[12];
                memcpy(buffer, start, end - start);
                buffer[end - start] = '\0';
                *value = atoi(buffer);
                return true;
            }
        }
        p++;
    }
    return false;
}

static bool nmeaContains(const char *sentence, const char *text)
{
    if (!sentence || !text) {
        return false;
    }
    return strstr(sentence, text) != NULL;
}

static bool minmeaFloatIsValid(const struct minmea_float &value)
{
    return value.scale != 0 && !isnan(minmea_tofloat(&value));
}

static bool minmeaDateIsValid(const struct minmea_date &date)
{
    return date.year >= 0 && date.month >= 1 && date.month <= 12 && date.day >= 1 && date.day <= 31;
}

static bool minmeaTimeIsValid(const struct minmea_time &time)
{
    return time.hours >= 0 && time.hours <= 23 &&
           time.minutes >= 0 && time.minutes <= 59 &&
           time.seconds >= 0 && time.seconds <= 60;
}

static uint16_t minmeaFullYear(const struct minmea_date &date)
{
    if (date.year >= 1900) {
        return date.year;
    }
    if (date.year >= 80) {
        return 1900 + date.year;
    }
    return 2000 + date.year;
}

static uint8_t gsvSatelliteCountInSentence(const struct minmea_sentence_gsv &frame)
{
    // NMEA 4.x GSV can append signal ID after the satellite groups.
    if (frame.total_msgs <= 0 || frame.msg_nr <= 0 ||
            frame.msg_nr > frame.total_msgs || frame.total_sats <= 0) {
        return 0;
    }

    int firstSatellite = (frame.msg_nr - 1) * 4;
    int count = frame.total_sats - firstSatellite;
    if (count <= 0) {
        return 0;
    }
    if (count > 4) {
        return 4;
    }
    return (uint8_t)count;
}

uint32_t GPS::loop(bool nmea_to_serial)
{
    _nmeaToSerial = nmea_to_serial;
    assert(_stream);
    if (_probeRunning) {
        return charsProcessed();
    }

    while (_stream->available()) {
        int c = _stream->read();
        if (c < 0) {
            continue;
        }
        encode((char)c);
    }
    if (nmea_to_serial) {
        while (Serial.available()) {
            _stream->write(Serial.read());
        }
    }

    expireSatellites();
    return charsProcessed();
}

bool GPS::encode(char c)
{
    if (_nmeaToSerial) {
        Serial.write((uint8_t)c);
    }
    _charsProcessed++;
    return processNMEAChar(c);
}

bool GPS::processNMEAChar(char c)
{
    if (c == '$') {
        _collectingSentence = true;
        _sentenceOffset = 0;
        _sentence[_sentenceOffset++] = c;
        return false;
    }

    if (!_collectingSentence) {
        return false;
    }

    if (c == '\r' || c == '\n') {
        bool parsed = false;
        if (_sentenceOffset > 0) {
            _sentence[_sentenceOffset] = '\0';
            parsed = processNMEASentence(_sentence);
        }
        _collectingSentence = false;
        _sentenceOffset = 0;
        return parsed;
    }

    if (_sentenceOffset >= NMEA_BUFFER_SIZE - 1) {
        _collectingSentence = false;
        _sentenceOffset = 0;
        return false;
    }

    _sentence[_sentenceOffset++] = c;
    return false;
}

bool GPS::processNMEASentence(const char *sentence)
{
    if (!minmea_check(sentence, false)) {
        return false;
    }

    if (strlen(sentence) >= 6 && strncmp(sentence + 3, "TXT", 3) == 0) {
        return processTXT(sentence);
    }

    switch (minmea_sentence_id(sentence, false)) {
    case MINMEA_SENTENCE_RMC:
        return processRMC(sentence);
    case MINMEA_SENTENCE_GGA:
        return processGGA(sentence);
    case MINMEA_SENTENCE_GLL:
        return processGLL(sentence);
    case MINMEA_SENTENCE_GSA:
        return processGSA(sentence);
    case MINMEA_SENTENCE_GSV:
        return processGSV(sentence);
    case MINMEA_SENTENCE_VTG:
        return processVTG(sentence);
    case MINMEA_SENTENCE_ZDA:
        return processZDA(sentence);
    default:
        break;
    }
    return false;
}

bool GPS::processRMC(const char *sentence)
{
    struct minmea_sentence_rmc frame;
    if (!minmea_parse_rmc(&frame, sentence)) {
        return false;
    }

    if (minmeaTimeIsValid(frame.time)) {
        time.set(frame.time.hours, frame.time.minutes, frame.time.seconds);
    }

    if (minmeaDateIsValid(frame.date)) {
        date.set(minmeaFullYear(frame.date), frame.date.month, frame.date.day);
    }

    if (frame.valid &&
            minmeaFloatIsValid(frame.latitude) &&
            minmeaFloatIsValid(frame.longitude)) {
        location.set(minmea_tocoord(&frame.latitude), minmea_tocoord(&frame.longitude));
        markFirstFix();
    }

    if (minmeaFloatIsValid(frame.speed)) {
        speed.setKmph(minmea_tofloat(&frame.speed) * 1.852);
    }

    return true;
}

bool GPS::processGGA(const char *sentence)
{
    struct minmea_sentence_gga frame;
    if (!minmea_parse_gga(&frame, sentence)) {
        return false;
    }

    if (minmeaTimeIsValid(frame.time)) {
        time.set(frame.time.hours, frame.time.minutes, frame.time.seconds);
    }

    if (frame.fix_quality > 0 &&
            minmeaFloatIsValid(frame.latitude) &&
            minmeaFloatIsValid(frame.longitude)) {
        location.set(minmea_tocoord(&frame.latitude), minmea_tocoord(&frame.longitude));
        markFirstFix();
    }

    satellites.set(frame.satellites_tracked < 0 ? 0 : frame.satellites_tracked);

    if (minmeaFloatIsValid(frame.hdop)) {
        hdop.set(minmea_tofloat(&frame.hdop));
    }

    if (frame.altitude_units == 'M' && minmeaFloatIsValid(frame.altitude)) {
        altitude.setMeters(minmea_tofloat(&frame.altitude));
    }

    return true;
}

bool GPS::processGLL(const char *sentence)
{
    struct minmea_sentence_gll frame;
    if (!minmea_parse_gll(&frame, sentence)) {
        return false;
    }

    if (minmeaTimeIsValid(frame.time)) {
        time.set(frame.time.hours, frame.time.minutes, frame.time.seconds);
    }

    if (frame.status == MINMEA_GLL_STATUS_DATA_VALID &&
            minmeaFloatIsValid(frame.latitude) &&
            minmeaFloatIsValid(frame.longitude)) {
        location.set(minmea_tocoord(&frame.latitude), minmea_tocoord(&frame.longitude));
        markFirstFix();
    }

    return true;
}

bool GPS::processGSV(const char *sentence)
{
    struct minmea_sentence_gsv frame;
    if (!minmea_parse_gsv(&frame, sentence)) {
        return false;
    }

    GNSSSatelliteSystem system = systemFromTalker(sentence);
    if (system == GNSS_SYSTEM_UNKNOWN) {
        return false;
    }

    uint8_t satelliteCount = gsvSatelliteCountInSentence(frame);
    for (uint8_t i = 0; i < satelliteCount; ++i) {
        if (frame.sats[i].nr <= 0) {
            continue;
        }

        GNSSSatelliteSystem satelliteSystem = satelliteSystemFromPrn(system, frame.sats[i].nr);
        GNSSSatelliteInfo *sat = upsertSatellite(satelliteSystem, frame.sats[i].nr);
        if (!sat) {
            continue;
        }
        sat->elevation = frame.sats[i].elevation;
        sat->azimuth = frame.sats[i].azimuth;
        sat->cn0 = frame.sats[i].snr;
        sat->has_cn0 = nmeaFieldHasValue(sentence, 7 + 4 * i);
        sat->updated_ms = millis();
    }
    return true;
}

bool GPS::processGSA(const char *sentence)
{
    struct minmea_sentence_gsa frame;
    if (!minmea_parse_gsa(&frame, sentence)) {
        return false;
    }

    GNSSSatelliteSystem system = systemFromGSA(sentence);
    if (system == GNSS_SYSTEM_UNKNOWN) {
        return false;
    }

    clearUsedForSystem(system);
    if (system == GNSS_SYSTEM_GPS) {
        clearUsedForSystem(GNSS_SYSTEM_QZSS);
    }
    for (uint8_t i = 0; i < 12; ++i) {
        if (frame.sats[i] > 0) {
            markSatelliteUsed(system, frame.sats[i]);
        }
    }

    if (minmeaFloatIsValid(frame.hdop)) {
        hdop.set(minmea_tofloat(&frame.hdop));
    }

    return true;
}

bool GPS::processVTG(const char *sentence)
{
    struct minmea_sentence_vtg frame;
    if (!minmea_parse_vtg(&frame, sentence)) {
        return false;
    }

    if (minmeaFloatIsValid(frame.speed_kph)) {
        speed.setKmph(minmea_tofloat(&frame.speed_kph));
    }

    return true;
}

bool GPS::processZDA(const char *sentence)
{
    struct minmea_sentence_zda frame;
    if (!minmea_parse_zda(&frame, sentence)) {
        return false;
    }

    if (minmeaTimeIsValid(frame.time)) {
        time.set(frame.time.hours, frame.time.minutes, frame.time.seconds);
    }

    if (minmeaDateIsValid(frame.date)) {
        date.set(minmeaFullYear(frame.date), frame.date.month, frame.date.day);
    }

    return true;
}

bool GPS::processTXT(const char *sentence)
{
    GNSSAntennaState state = GNSS_ANTENNA_UNKNOWN;
    if (nmeaContains(sentence, "ANTENNA OK")) {
        state = GNSS_ANTENNA_OK;
    } else if (nmeaContains(sentence, "ANTENNA OPEN")) {
        state = GNSS_ANTENNA_OPEN;
    } else if (nmeaContains(sentence, "ANTENNA SHORT")) {
        state = GNSS_ANTENNA_SHORT;
    }

    if (state != GNSS_ANTENNA_UNKNOWN) {
        _antennaState = state;
        _antennaLastUpdate = millis();
        return true;
    }
    return false;
}

void GPS::expireSatellites()
{
    uint32_t now = millis();
    for (size_t i = 0; i < _satelliteCount;) {
        if (now - _satellites[i].updated_ms > 5000) {
            if (i + 1 < _satelliteCount) {
                memmove(&_satellites[i], &_satellites[i + 1], (_satelliteCount - i - 1) * sizeof(_satellites[0]));
            }
            _satelliteCount--;
            continue;
        }
        i++;
    }
}

void GPS::clearUsedForSystem(GNSSSatelliteSystem system)
{
    for (size_t i = 0; i < _satelliteCount; ++i) {
        if (_satellites[i].system == system) {
            _satellites[i].used = false;
        }
    }
}

void GPS::markSatelliteUsed(GNSSSatelliteSystem system, uint16_t prn)
{
    system = satelliteSystemFromPrn(system, prn);
    GNSSSatelliteInfo *sat = upsertSatellite(system, prn);
    if (sat) {
        sat->used = true;
    }
}

GNSSSatelliteInfo *GPS::upsertSatellite(GNSSSatelliteSystem system, uint16_t prn)
{
    for (size_t i = 0; i < _satelliteCount; ++i) {
        if (_satellites[i].system == system && _satellites[i].prn == prn) {
            return &_satellites[i];
        }
    }

    GNSSSatelliteInfo *sat = NULL;
    if (_satelliteCount < GNSS_MAX_SATELLITES) {
        sat = &_satellites[_satelliteCount++];
    } else {
        size_t oldest = 0;
        for (size_t i = 1; i < _satelliteCount; ++i) {
            if (_satellites[i].updated_ms < _satellites[oldest].updated_ms) {
                oldest = i;
            }
        }
        sat = &_satellites[oldest];
    }

    memset(sat, 0, sizeof(*sat));
    sat->system = system;
    sat->prn = prn;
    sat->elevation = -1;
    sat->azimuth = -1;
    sat->cn0 = 0;
    sat->updated_ms = millis();
    return sat;
}

GNSSSatelliteSystem GPS::systemFromTalker(const char *sentence) const
{
    if (!sentence || sentence[0] != '$') {
        return GNSS_SYSTEM_UNKNOWN;
    }

    if (sentence[1] == 'G' && sentence[2] == 'P') {
        return GNSS_SYSTEM_GPS;
    }
    if (sentence[1] == 'G' && sentence[2] == 'L') {
        return GNSS_SYSTEM_GLONASS;
    }
    if ((sentence[1] == 'G' && sentence[2] == 'B') || (sentence[1] == 'B' && sentence[2] == 'D')) {
        return GNSS_SYSTEM_BEIDOU;
    }
    if (sentence[1] == 'G' && sentence[2] == 'A') {
        return GNSS_SYSTEM_GALILEO;
    }
    if (sentence[1] == 'G' && sentence[2] == 'Q') {
        return GNSS_SYSTEM_QZSS;
    }
    return GNSS_SYSTEM_UNKNOWN;
}

GNSSSatelliteSystem GPS::systemFromGSA(const char *sentence) const
{
    GNSSSatelliteSystem talker = systemFromTalker(sentence);
    if (talker != GNSS_SYSTEM_UNKNOWN) {
        return talker;
    }

    int systemId = 0;
    if (!nmeaFieldToInt(sentence, 18, &systemId)) {
        return GNSS_SYSTEM_UNKNOWN;
    }

    switch (systemId) {
    case 1:
        return GNSS_SYSTEM_GPS;
    case 2:
        return GNSS_SYSTEM_GLONASS;
    case 3:
        return GNSS_SYSTEM_GALILEO;
    case 4:
        return GNSS_SYSTEM_BEIDOU;
    case 5:
        return GNSS_SYSTEM_QZSS;
    default:
        return GNSS_SYSTEM_UNKNOWN;
    }
}

GNSSSatelliteSystem GPS::satelliteSystemFromPrn(GNSSSatelliteSystem system, uint16_t prn) const
{
    if (system == GNSS_SYSTEM_GPS && prn >= 193 && prn <= 202) {
        return GNSS_SYSTEM_QZSS;
    }
    return system;
}

size_t GPS::copySatellites(GNSSSatelliteInfo *buffer, size_t max_count) const
{
    if (!buffer || max_count == 0) {
        return 0;
    }

    size_t count = _satelliteCount < max_count ? _satelliteCount : max_count;
    memcpy(buffer, _satellites, count * sizeof(buffer[0]));

    for (size_t i = 1; i < count; ++i) {
        GNSSSatelliteInfo current = buffer[i];
        size_t j = i;
        while (j > 0 && (buffer[j - 1].system > current.system ||
                         (buffer[j - 1].system == current.system && buffer[j - 1].prn > current.prn))) {
            buffer[j] = buffer[j - 1];
            j--;
        }
        buffer[j] = current;
    }

    return count;
}

size_t GPS::getConstellationInfo(GNSSConstellationInfo *buffer, size_t max_count) const
{
    static const GNSSSatelliteSystem systems[] = {
        GNSS_SYSTEM_GPS,
        GNSS_SYSTEM_GLONASS,
        GNSS_SYSTEM_BEIDOU,
        GNSS_SYSTEM_GALILEO,
        GNSS_SYSTEM_QZSS
    };
    const size_t systemCount = sizeof(systems) / sizeof(systems[0]);
    size_t count = max_count < systemCount ? max_count : systemCount;
    if (!buffer || count == 0) {
        return 0;
    }

    int32_t cn0Sum[systemCount] = {0};
    for (size_t i = 0; i < count; ++i) {
        memset(&buffer[i], 0, sizeof(buffer[i]));
        buffer[i].system = systems[i];
    }

    for (size_t i = 0; i < _satelliteCount; ++i) {
        for (size_t s = 0; s < count; ++s) {
            if (_satellites[i].system != buffer[s].system) {
                continue;
            }
            buffer[s].visible++;
            if (_satellites[i].used) {
                buffer[s].used++;
            }
            if (_satellites[i].has_cn0) {
                buffer[s].tracking++;
                cn0Sum[s] += _satellites[i].cn0;
                if (_satellites[i].cn0 > buffer[s].max_cn0) {
                    buffer[s].max_cn0 = _satellites[i].cn0;
                }
            }
            break;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (buffer[i].tracking > 0) {
            buffer[i].avg_cn0 = cn0Sum[i] / buffer[i].tracking;
        }
    }

    return count;
}

uint32_t GPS::getAntennaAge() const
{
    if (_antennaLastUpdate == 0) {
        return UINT32_MAX;
    }
    return millis() - _antennaLastUpdate;
}

const char *GPS::systemName(GNSSSatelliteSystem system)
{
    switch (system) {
    case GNSS_SYSTEM_GPS:
        return "GPS";
    case GNSS_SYSTEM_GLONASS:
        return "GLONASS";
    case GNSS_SYSTEM_BEIDOU:
        return "BeiDou";
    case GNSS_SYSTEM_GALILEO:
        return "Galileo";
    case GNSS_SYSTEM_QZSS:
        return "QZSS";
    default:
        return "Unknown";
    }
}

bool GPS::factory()
{
    assert(_stream);

    if (_model == "LS550G") {
        // TODO:
        return true;
    }

    uint8_t buffer[256];
    // Revert module Clear, save and load configurations
    // B5 62 06 09 0D 00 FF FB 00 00 00 00 00 00  FF FF 00 00 17 2B 7E
    uint8_t _legacy_message_reset[] = { 0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0xFF, 0xFF, 0x00, 0x00, 0x17, 0x2B, 0x7E };
    _stream->write(_legacy_message_reset, sizeof(_legacy_message_reset));
    if (!getAck(*_stream, buffer, 256, 0x05, 0x01)) {
        return false;
    }
    delay(50);

    // UBX-CFG-RATE, Size 8, 'Navigation/measurement rate settings'
    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    _stream->write(cfg_rate, sizeof(cfg_rate));
    if (!getAck(*_stream, buffer, 256, 0x06, 0x08)) {
        return false;
    }
    LILYGO_LOG_D("GPS reset successes!");
    return true;
}


GPS::GPS() :
    _stream(NULL),
    _model("Unknown"),
    _probeMask(GPS_PROBE_ALL),
    _probeTask(NULL),
    _probeCallback(NULL),
    _probeCallbackUserData(NULL),
    _probeRunning(false),
    _probeDone(false),
    _probeSuccess(false),
    _charsProcessed(0),
    _nmeaToSerial(false),
    _satelliteCount(0),
    _antennaState(GNSS_ANTENNA_UNKNOWN),
    _antennaLastUpdate(0),
    _sentenceOffset(0),
    _collectingSentence(false),
    _sessionStartMillis(0),
    _ttffMillis(UINT32_MAX)
{
    memset(_satellites, 0, sizeof(_satellites));
    memset(_sentence, 0, sizeof(_sentence));
}

GPS::~GPS()
{
}

void GPS::markFirstFix()
{
    if (_ttffMillis == UINT32_MAX) {
        _ttffMillis = millis() - _sessionStartMillis;
    }
}

bool GPS::init(HardwareSerial *stream, GPSProbe probe)
{
    if (_probeRunning) {
        return false;
    }
    _sessionStartMillis = millis();
    _ttffMillis = UINT32_MAX;
    _probeDone = false;
    _probeSuccess = false;
    bool result = probeSync(stream, probe);
    _probeSuccess = result;
    _probeDone = true;
    return result;
}

bool GPS::beginAsyncProbe(HardwareSerial *stream, GPSProbe probe,
                          GPSProbeCallback callback, void *user_data)
{
    if (!stream) return false;
    if (_probeRunning) return true;

    _stream = stream;
    _sessionStartMillis = millis();
    _ttffMillis = UINT32_MAX;
    _model = "Unknown";
    _probeMask = probe;
    _probeCallback = callback;
    _probeCallbackUserData = user_data;
    _probeSuccess = false;
    _probeDone = false;
    _probeRunning = true;

    BaseType_t started = xTaskCreate(asyncProbeTask, "gps/probe", 6144, this, 1, &_probeTask);
    if (started != pdPASS) {
        _probeTask = NULL;
        _probeRunning = false;
        _probeDone = true;
        LILYGO_LOG_E("Failed to create GPS probe task");
        return false;
    }

    return true;
}

void GPS::asyncProbeTask(void *param)
{
    GPS *gps = (GPS *)param;
    if (!gps) {
        vTaskDelete(NULL);
        return;
    }

    bool result = gps->probeSync(gps->_stream, gps->_probeMask);
    gps->_probeSuccess = result;
    gps->_probeDone = true;
    gps->_probeRunning = false;

    if (gps->_probeCallback) {
        gps->_probeCallback(result, gps->_model.c_str(), gps->_probeCallbackUserData);
    }
    gps->_probeTask = NULL;
    vTaskDelete(NULL);
}

bool GPS::probeSync(HardwareSerial *stream, GPSProbe probe)
{
    _stream = stream;
    assert(_stream);
    uint32_t baudRate = _stream->baudRate();

    if (probe & GPS_PROBE_UBLOX) {
        LILYGO_LOG_D("Start probe Ublox GPS Module");
        // Get current baud rate
        // By default, probe starts from the initial baud rate.
        uint32_t nextBaudRate = baudRate == 38400 ? 9600 : 38400;
        for (int next = 0; next < 2; ++next) {
            LILYGO_LOG_D("Use baud: %d", _stream->baudRate());
            for (int retry = 0; retry < 2; ++retry) {
                if (checkUblox(*_stream, _model)) {
                    return true;
                }
                delay(10);
            }
            // Test the next possible baud rate
            _stream->updateBaudRate(nextBaudRate);
        }
    }

    if (probe & GPS_PROBE_QUECTEL_LS550G) {
        LILYGO_LOG_D("Start probe LS550G GPS Module");
        if (checkLS550G(*_stream, _model)) {
            return true;
        }
    }

    if(probe & GPS_PROBE_QUECTEL_L76K){
        LILYGO_LOG_D("Start probe L76K GPS Module");
        if (checkL76K(*_stream, _model)) {
            return true;
        }
    }
    // If the GPS Module is not found, revert to the original baud rate
    _stream->updateBaudRate(baudRate);
    return false;
}
