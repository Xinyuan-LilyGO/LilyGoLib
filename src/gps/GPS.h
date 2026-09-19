/**
 * @file      GPS.h
 * @brief     Declares GNSS probing, NMEA parsing, and TinyGPS-compatible accessors.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-07-07
 *
 */
#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Maximum number of satellites tracked in the GNSS cache.
 */
#ifndef GNSS_MAX_SATELLITES
#define GNSS_MAX_SATELLITES 96
#endif

/**
 * @brief GNSS module probe mask bits.
 */
enum GPSProbe {
    GPS_PROBE_UBLOX = _BV(1),          /**< Probe u-blox GNSS modules. */
    GPS_PROBE_QUECTEL_LS550G = _BV(2), /**< Probe Quectel LS550G modules. */
    GPS_PROBE_QUECTEL_L76K = _BV(3),   /**< Probe Quectel L76K modules. */
    GPS_PROBE_ALL = GPS_PROBE_UBLOX | GPS_PROBE_QUECTEL_LS550G | GPS_PROBE_QUECTEL_L76K /**< Probe every supported module. */
};

/**
 * @brief Callback invoked when asynchronous GNSS probing completes.
 * @param success true when a supported module was detected.
 * @param model Detected model string, or an implementation-defined fallback.
 * @param user_data User pointer supplied to beginAsyncProbe().
 */
typedef void (*GPSProbeCallback)(bool success, const char *model, void *user_data);

/**
 * @brief GNSS constellation identifiers.
 */
enum GNSSSatelliteSystem {
    GNSS_SYSTEM_UNKNOWN = 0, /**< Unknown or unsupported constellation. */
    GNSS_SYSTEM_GPS,         /**< GPS constellation. */
    GNSS_SYSTEM_GLONASS,     /**< GLONASS constellation. */
    GNSS_SYSTEM_BEIDOU,      /**< BeiDou constellation. */
    GNSS_SYSTEM_GALILEO,     /**< Galileo constellation. */
    GNSS_SYSTEM_QZSS         /**< QZSS constellation. */
};

/**
 * @brief External GNSS antenna state.
 */
enum GNSSAntennaState {
    GNSS_ANTENNA_UNKNOWN = 0, /**< Antenna state has not been reported. */
    GNSS_ANTENNA_OK,          /**< Antenna is present and operating normally. */
    GNSS_ANTENNA_OPEN,        /**< Antenna open-circuit state reported. */
    GNSS_ANTENNA_SHORT        /**< Antenna short-circuit state reported. */
};

/**
 * @brief Per-satellite tracking information parsed from NMEA data.
 */
struct GNSSSatelliteInfo {
    GNSSSatelliteSystem system; /**< Satellite constellation. */
    uint16_t prn;               /**< Satellite PRN or SVID. */
    int16_t elevation;          /**< Elevation angle in degrees. */
    int16_t azimuth;            /**< Azimuth angle in degrees. */
    int16_t cn0;                /**< Carrier-to-noise density in dB-Hz. */
    bool has_cn0;               /**< true when cn0 contains a valid value. */
    bool used;                  /**< true when the satellite is used in the current fix. */
    uint32_t updated_ms;        /**< millis() timestamp of the last update. */
};

/**
 * @brief Aggregated constellation tracking information.
 */
struct GNSSConstellationInfo {
    GNSSSatelliteSystem system; /**< Satellite constellation. */
    uint16_t visible;           /**< Number of visible satellites. */
    uint16_t tracking;          /**< Number of satellites with signal data. */
    uint16_t used;              /**< Number of satellites used in the fix. */
    int16_t max_cn0;            /**< Maximum C/N0 value in dB-Hz. */
    int16_t avg_cn0;            /**< Average C/N0 value in dB-Hz. */
};

/**
 * @brief Base class for TinyGPS-compatible fields with validity and age tracking.
 */
class TinyGPSField
{
public:
    /**
     * @brief Construct an invalid GPS field.
     */
    TinyGPSField();

    /**
     * @brief Check whether the field has received a valid value.
     * @return true if the field is valid.
     */
    bool isValid() const
    {
        return _valid;
    }

    /**
     * @brief Get the age of the last committed value.
     * @return Age in milliseconds, or UINT32_MAX if invalid.
     */
    uint32_t age() const;

protected:
    /**
     * @brief Mark the field valid and update its timestamp.
     */
    void commit();

    bool _valid;
    uint32_t _lastCommitTime;
};

/**
 * @brief TinyGPS-compatible latitude/longitude field.
 */
class TinyGPSLocation : public TinyGPSField
{
public:
    /**
     * @brief Construct a location field.
     */
    TinyGPSLocation();

    /**
     * @brief Get latitude.
     * @return Latitude in degrees.
     */
    double lat() const
    {
        return _lat;
    }

    /**
     * @brief Get longitude.
     * @return Longitude in degrees.
     */
    double lng() const
    {
        return _lng;
    }

    /**
     * @brief Update latitude and longitude.
     * @param lat Latitude in degrees.
     * @param lng Longitude in degrees.
     */
    void set(double lat, double lng);

private:
    double _lat;
    double _lng;
};

/**
 * @brief TinyGPS-compatible UTC date field.
 */
class TinyGPSDate : public TinyGPSField
{
public:
    /**
     * @brief Construct a date field.
     */
    TinyGPSDate();

    /**
     * @brief Get the UTC year.
     * @return Four-digit year.
     */
    uint16_t year() const
    {
        return _year;
    }

    /**
     * @brief Get the UTC month.
     * @return Month in the range 1-12.
     */
    uint8_t month() const
    {
        return _month;
    }

    /**
     * @brief Get the UTC day of month.
     * @return Day in the range 1-31.
     */
    uint8_t day() const
    {
        return _day;
    }

    /**
     * @brief Update the UTC date.
     * @param year Four-digit year.
     * @param month Month in the range 1-12.
     * @param day Day in the range 1-31.
     */
    void set(uint16_t year, uint8_t month, uint8_t day);

private:
    uint16_t _year;
    uint8_t _month;
    uint8_t _day;
};

/**
 * @brief TinyGPS-compatible UTC time field.
 */
class TinyGPSTime : public TinyGPSField
{
public:
    /**
     * @brief Construct a time field.
     */
    TinyGPSTime();

    /**
     * @brief Get the UTC hour.
     * @return Hour in the range 0-23.
     */
    uint8_t hour() const
    {
        return _hour;
    }

    /**
     * @brief Get the UTC minute.
     * @return Minute in the range 0-59.
     */
    uint8_t minute() const
    {
        return _minute;
    }

    /**
     * @brief Get the UTC second.
     * @return Second in the range 0-60.
     */
    uint8_t second() const
    {
        return _second;
    }

    /**
     * @brief Update the UTC time.
     * @param hour Hour in the range 0-23.
     * @param minute Minute in the range 0-59.
     * @param second Second in the range 0-60.
     */
    void set(uint8_t hour, uint8_t minute, uint8_t second);

private:
    uint8_t _hour;
    uint8_t _minute;
    uint8_t _second;
};

/**
 * @brief TinyGPS-compatible unsigned integer field.
 */
class TinyGPSInteger : public TinyGPSField
{
public:
    /**
     * @brief Construct an integer field.
     */
    TinyGPSInteger();

    /**
     * @brief Get the current integer value.
     * @return Current value.
     */
    uint32_t value() const
    {
        return _value;
    }

    /**
     * @brief Update the integer value.
     * @param value New value.
     */
    void set(uint32_t value);

private:
    uint32_t _value;
};

/**
 * @brief TinyGPS-compatible HDOP field.
 */
class TinyGPSHDOP : public TinyGPSField
{
public:
    /**
     * @brief Construct an HDOP field.
     */
    TinyGPSHDOP();

    /**
     * @brief Get horizontal dilution of precision.
     * @return HDOP value.
     */
    double hdop() const
    {
        return _hdop;
    }

    /**
     * @brief Update horizontal dilution of precision.
     * @param hdop HDOP value.
     */
    void set(double hdop);

private:
    double _hdop;
};

/**
 * @brief TinyGPS-compatible altitude field.
 */
class TinyGPSAltitude : public TinyGPSField
{
public:
    /**
     * @brief Construct an altitude field.
     */
    TinyGPSAltitude();

    /**
     * @brief Get altitude.
     * @return Altitude in meters.
     */
    double meters() const
    {
        return _meters;
    }

    /**
     * @brief Update altitude.
     * @param meters Altitude in meters.
     */
    void setMeters(double meters);

private:
    double _meters;
};

/**
 * @brief TinyGPS-compatible speed field.
 */
class TinyGPSSpeed : public TinyGPSField
{
public:
    /**
     * @brief Construct a speed field.
     */
    TinyGPSSpeed();

    /**
     * @brief Get speed over ground.
     * @return Speed in kilometers per hour.
     */
    double kmph() const
    {
        return _kmph;
    }

    /**
     * @brief Update speed over ground.
     * @param kmph Speed in kilometers per hour.
     */
    void setKmph(double kmph);

private:
    double _kmph;
};

/**
 * @brief Minimal TinyGPSPlus compatibility facade.
 */
class TinyGPSPlus
{
public:
    /**
     * @brief Return the parser compatibility version string.
     * @return Static version string.
     */
    static const char *libraryVersion()
    {
        return "minmea-compat";
    }
};

/**
 * @brief GNSS driver wrapper with module probing and NMEA parsing.
 */
class GPS : public TinyGPSPlus
{
public:
    /**
     * @brief Construct a GPS parser and probe wrapper.
     */
    GPS();

    /**
     * @brief Destroy the GPS wrapper.
     */
    ~GPS();

    /**
     * @brief Probe and initialize a GNSS module synchronously.
     * @param stream Hardware serial port connected to the module.
     * @param probe Probe mask selecting module families to test.
     * @return true if a supported module was detected.
     */
    bool init(HardwareSerial *stream, GPSProbe probe = GPS_PROBE_ALL);

    /**
     * @brief Start asynchronous GNSS module probing.
     * @param stream Hardware serial port connected to the module.
     * @param probe Probe mask selecting module families to test.
     * @param callback Optional completion callback.
     * @param user_data User pointer passed to the completion callback.
     * @return true if the async probe task was started.
     */
    bool beginAsyncProbe(HardwareSerial *stream, GPSProbe probe = GPS_PROBE_ALL,
                         GPSProbeCallback callback = nullptr, void *user_data = nullptr);

    /**
     * @brief Check whether asynchronous probing is still running.
     * @return true while the probe task is active.
     */
    bool probeInProgress() const
    {
        return _probeRunning;
    }

    /**
     * @brief Check whether asynchronous probing has completed.
     * @return true after the probe task finishes.
     */
    bool probeDone() const
    {
        return _probeDone;
    }

    /**
     * @brief Check whether the last asynchronous probe succeeded.
     * @return true if a supported module was detected.
     */
    bool probeSuccess() const
    {
        return _probeSuccess;
    }

    /**
     * @brief Run factory GNSS configuration.
     * @return true if factory configuration completed.
     */
    bool factory();

    /**
     * @brief Feed one NMEA character to the parser.
     * @param c Character read from the GNSS serial stream.
     * @return true when a complete sentence was processed.
     */
    bool encode(char c);

    /**
     * @brief Enable or disable mirroring all parsed NMEA bytes to Serial.
     * @param enable true to mirror bytes received from the GNSS stream.
     */
    void setNMEASerialOutput(bool enable)
    {
        _nmeaToSerial = enable;
    }

    /**
     * @brief Drain pending serial data and feed it to the parser.
     * @param nmea_to_serial Mirror raw NMEA data to Serial and forward bytes
     *                       received from Serial to the GNSS stream.
     * @return Number of characters processed during this call.
     */
    uint32_t loop(bool nmea_to_serial = false);

    /**
     * @brief Get the total number of processed serial characters.
     * @return Character count since construction or reset.
     */
    uint32_t charsProcessed() const
    {
        return _charsProcessed;
    }

    /**
     * @brief Get the detected GNSS module model.
     * @return Model string.
     */
    String getModel()
    {
        return _model;
    }

    /**
     * @brief Get the number of cached satellite entries.
     * @return Satellite entry count.
     */
    size_t getSatelliteCount() const
    {
        return _satelliteCount;
    }

    /**
     * @brief Get the internal satellite cache.
     * @return Pointer to the satellite array.
     */
    const GNSSSatelliteInfo *getSatellites() const
    {
        return _satellites;
    }

    /**
     * @brief Copy cached satellite entries into a caller-provided buffer.
     * @param buffer Destination array.
     * @param max_count Maximum entries that can be written to buffer.
     * @return Number of entries copied.
     */
    size_t copySatellites(GNSSSatelliteInfo *buffer, size_t max_count) const;

    /**
     * @brief Build per-constellation summary information.
     * @param buffer Destination array.
     * @param max_count Maximum constellation entries that can be written.
     * @return Number of entries copied.
     */
    size_t getConstellationInfo(GNSSConstellationInfo *buffer, size_t max_count) const;

    /**
     * @brief Get the latest GNSS antenna state.
     * @return Antenna state reported by the module.
     */
    GNSSAntennaState getAntennaState() const
    {
        return _antennaState;
    }

    /**
     * @brief Get the age of the last antenna-state update.
     * @return Age in milliseconds, or UINT32_MAX if never updated.
     */
    uint32_t getAntennaAge() const;

    /**
     * @brief Get time to the first valid position fix after GPS initialization.
     * @return Elapsed milliseconds, or UINT32_MAX until a fix is received.
     */
    uint32_t timeToFirstFix() const
    {
        return _ttffMillis;
    }

    /**
     * @brief Get a display name for a GNSS constellation.
     * @param system Constellation identifier.
     * @return Static constellation name string.
     */
    static const char *systemName(GNSSSatelliteSystem system);

    /** Parsed GNSS location field. */
    TinyGPSLocation location;
    /** Parsed UTC date field. */
    TinyGPSDate date;
    /** Parsed UTC time field. */
    TinyGPSTime time;
    /** Parsed number of satellites used in the fix. */
    TinyGPSInteger satellites;
    /** Parsed HDOP field. */
    TinyGPSHDOP hdop;
    /** Parsed altitude field. */
    TinyGPSAltitude altitude;
    /** Parsed speed-over-ground field. */
    TinyGPSSpeed speed;

private:
    /**
     * @brief Size of the internal NMEA sentence buffer.
     */
    enum {
        NMEA_BUFFER_SIZE = 128
    };

    /**
     * @brief Process one character through the NMEA sentence assembler.
     * @param c Character read from the GNSS serial stream.
     * @return true when a complete sentence was processed.
     */
    bool processNMEAChar(char c);

    /**
     * @brief Dispatch a complete NMEA sentence to a sentence-specific parser.
     * @param sentence Null-terminated NMEA sentence.
     * @return true if the sentence was recognized and processed.
     */
    bool processNMEASentence(const char *sentence);

    /** @brief Parse an RMC sentence. */
    bool processRMC(const char *sentence);

    /** @brief Parse a GGA sentence. */
    bool processGGA(const char *sentence);

    /** @brief Parse a GLL sentence. */
    bool processGLL(const char *sentence);

    /** @brief Parse a GSA sentence. */
    bool processGSA(const char *sentence);

    /** @brief Parse a GSV sentence. */
    bool processGSV(const char *sentence);

    /** @brief Parse a VTG sentence. */
    bool processVTG(const char *sentence);

    /** @brief Parse a ZDA sentence. */
    bool processZDA(const char *sentence);

    /** @brief Parse a TXT sentence. */
    bool processTXT(const char *sentence);

    /**
     * @brief Remove stale satellite entries from the cache.
     */
    void expireSatellites();

    /**
     * @brief Clear fix-use flags for one constellation.
     * @param system Constellation to update.
     */
    void clearUsedForSystem(GNSSSatelliteSystem system);

    /**
     * @brief Mark one satellite as used in the current fix.
     * @param system Satellite constellation.
     * @param prn Satellite PRN or SVID.
     */
    void markSatelliteUsed(GNSSSatelliteSystem system, uint16_t prn);

    /**
     * @brief Find or create a satellite cache entry.
     * @param system Satellite constellation.
     * @param prn Satellite PRN or SVID.
     * @return Pointer to the cache entry, or nullptr if the cache is full.
     */
    GNSSSatelliteInfo *upsertSatellite(GNSSSatelliteSystem system, uint16_t prn);

    /**
     * @brief Infer constellation from an NMEA talker ID.
     * @param sentence NMEA sentence.
     * @return Inferred constellation.
     */
    GNSSSatelliteSystem systemFromTalker(const char *sentence) const;

    /**
     * @brief Infer constellation from a GSA sentence.
     * @param sentence NMEA GSA sentence.
     * @return Inferred constellation.
     */
    GNSSSatelliteSystem systemFromGSA(const char *sentence) const;

    /**
     * @brief Normalize PRN numbering for mixed-constellation reports.
     * @param system Base constellation inferred from the sentence.
     * @param prn Reported PRN or SVID.
     * @return Satellite constellation for the PRN.
     */
    GNSSSatelliteSystem satelliteSystemFromPrn(GNSSSatelliteSystem system, uint16_t prn) const;

    /**
     * @brief Probe GNSS modules synchronously.
     * @param stream Hardware serial port connected to the module.
     * @param probe Probe mask selecting module families to test.
     * @return true if a supported module was detected.
     */
    bool probeSync(HardwareSerial *stream, GPSProbe probe);

    /**
     * @brief FreeRTOS task entry point for asynchronous probing.
     * @param param GPS instance pointer.
     */
    static void asyncProbeTask(void *param);

    /** Hardware serial stream connected to the GNSS module. */
    HardwareSerial *_stream;
    /** Detected GNSS model string. */
    String _model;
    /** Active probe mask. */
    GPSProbe _probeMask;
    /** Asynchronous probe task handle. */
    TaskHandle_t _probeTask;
    /** Asynchronous probe completion callback. */
    GPSProbeCallback _probeCallback;
    /** User pointer passed to the async probe callback. */
    void *_probeCallbackUserData;
    /** true while asynchronous probing is running. */
    volatile bool _probeRunning;
    /** true after asynchronous probing finishes. */
    volatile bool _probeDone;
    /** true when the last probe detected a supported module. */
    volatile bool _probeSuccess;
    /** Total number of processed serial characters. */
    uint32_t _charsProcessed;
    /** true when every incoming NMEA byte should also be written to Serial. */
    bool _nmeaToSerial;
    /** Cached satellite entries parsed from NMEA data. */
    GNSSSatelliteInfo _satellites[GNSS_MAX_SATELLITES];
    /** Number of valid entries in _satellites. */
    size_t _satelliteCount;
    /** Last reported external antenna state. */
    GNSSAntennaState _antennaState;
    /** millis() timestamp of the last antenna-state update. */
    uint32_t _antennaLastUpdate;
    /** NMEA sentence assembly buffer. */
    char _sentence[NMEA_BUFFER_SIZE];
    /** Current write offset in _sentence. */
    uint8_t _sentenceOffset;
    /** true while characters are being collected for one NMEA sentence. */
    bool _collectingSentence;
    /** millis() timestamp when the current GPS session started. */
    uint32_t _sessionStartMillis;
    /** Elapsed time to first valid position fix, or UINT32_MAX if not fixed. */
    volatile uint32_t _ttffMillis;

    void markFirstFix();
};
