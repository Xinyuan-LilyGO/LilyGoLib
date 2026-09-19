/**
 * @file      LilyGoEventManage.h
 * @brief     Declares the shared device event types and dispatcher.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-18
 *
 */
#pragma once

#include "LilyGoLog.h"
#include <Arduino.h>
#include <vector>

/**
 * @brief Normalized button event types.
 */
typedef enum ButtonEvent {
    BUTTON_EVENT_NONE,         /**< No button event. */
    BUTTON_EVENT_RELEASED,     /**< Button released. */
    BUTTON_EVENT_PRESSED,      /**< Button pressed. */
    BUTTON_EVENT_CLICK,        /**< Short click completed. */
    BUTTON_EVENT_LONG_PRESSED, /**< Long press detected. */
    BUTTON_EVENT_DOUBLE_CLICK, /**< Double click detected. */
} ButtonEvent_t;

/**
 * @brief Button event payload with a button identifier.
 */
typedef struct ButtonEventParam {
    uint8_t id;    /**< Board-specific button identifier. */
    uint8_t event; /**< ButtonEvent_t value stored as an 8-bit field. */
} ButtonEventParam_t;

/**
 * @brief Trackball direction event types.
 */
typedef enum TrackballDir {
    TRACKBALL_DIR_NONE,  /**< No trackball direction. */
    TRACKBALL_DIR_UP,    /**< Trackball moved up. */
    TRACKBALL_DIR_DOWN,  /**< Trackball moved down. */
    TRACKBALL_DIR_LEFT,  /**< Trackball moved left. */
    TRACKBALL_DIR_RIGHT  /**< Trackball moved right. */
} TrackballDir_t;

/**
 * @brief SD card insertion and removal events.
 */
typedef enum SDEvent {
    SDCARD_EVENT_NONE,   /**< No SD card event. */
    SDCARD_EVENT_REMOVE, /**< SD card was removed. */
    SDCARD_EVENT_INSERT  /**< SD card was inserted. */
} SDEvent_t;

/**
 * @brief Trackball motion sample validity state.
 */
enum class MotionStatus : uint8_t {
    NO_MOTION = 0,       /**< No motion detected */
    MOTION_DETECTED = 1   /**< Motion occurred, data ready for reading */
};

/**
 * @brief Trackball two-axis motion payload.
 */
typedef struct TrackballXY {
    MotionStatus status; /**< Whether the delta values contain a motion sample. */
    int8_t delta_x;      /**< Horizontal delta. */
    int8_t delta_y;      /**< Vertical delta. */
} TrackballXY_t;

/**
 * @brief Power-management event types normalized across PMICs.
 */
typedef enum PMUEventType {
    PMU_EVENT_NONE,             /**< No PMU event. */
    PMU_EVENT_BATTERY_LOW_TEMP, /**< Battery temperature is below the safe range. */
    PMU_EVENT_BATTERY_HIGH_TEMP, /**< Battery temperature is above the safe range. */
    PMU_EVENT_CHARGE_LOW_TEMP,  /**< Charger temperature is below the safe range. */
    PMU_EVENT_CHARGE_HIGH_TEMP, /**< Charger temperature is above the safe range. */
    PMU_EVENT_KEY_CLICKED,      /**< PMU key short click detected. */
    PMU_EVENT_KEY_LONG_PRESSED, /**< PMU key long press detected. */
    PMU_EVENT_BATTERY_REMOVE,   /**< Battery was removed. */
    PMU_EVENT_BATTERY_INSERT,   /**< Battery was inserted. */
    PMU_EVENT_USBC_REMOVE,      /**< USB-C input was removed. */
    PMU_EVENT_USBC_INSERT,      /**< USB-C input was inserted. */
    PMU_EVENT_CHARGE_STARTED,   /**< Charging started. */
    PMU_EVENT_CHARGE_FINISH,    /**< Charging completed. */
} PMUEventType_t;

/**
 * @brief Sensor event types normalized across motion sensors.
 */
typedef enum SensorEventType {
    SENSOR_EVENT_NONE,          /**< No sensor event. */
    SENSOR_EVENT_INTERRUPT,     /**< Generic sensor interrupt. */
    SENSOR_STEPS_UPDATED,       /**< Step counter value changed. */
    SENSOR_STEP_DETECTED,       /**< Step detected. */
    SENSOR_ACTIVITY_DETECTED,   /**< Activity classification changed. */
    SENSOR_TILT_DETECTED,       /**< Tilt gesture detected. */
    SENSOR_DOUBLE_TAP_DETECTED, /**< Double tap detected. */
    SENSOR_ANY_MOTION_DETECTED, /**< Any-motion interrupt detected. */
    SENSOR_SINGLE_TAP_DETECTED, /**< Single tap detected. */
    SENSOR_DATA_READY,          /**< Sensor data-ready interrupt detected. */
} SensorEventType_t;

/**
 * @brief Top-level device event categories.
 */
typedef enum {
    NONE_EVENT,          /**< No event. */
    POWER_EVENT,         /**< Power-management event. */
    RTC_EVENT_INTERRUPT, /**< RTC interrupt event. */
    SENSOR_EVENT,        /**< Sensor event. */
    BUTTON_EVENT,        /**< Button event. */
    TRACKBALL_EVENT,     /**< Trackball event. */
    SDCARD_EVENT,        /**< SD card event. */
    ALL_EVENT_MAX,       /**< Wildcard used to register for every event. */
} DeviceEvent_t;

/**
 * @brief Type tag describing which union field is valid.
 */
typedef enum DeviceEventPayloadType {
    DEVICE_EVENT_PAYLOAD_NONE,          /**< Event carries no payload. */
    DEVICE_EVENT_PAYLOAD_PMU,           /**< payload.pmu is valid. */
    DEVICE_EVENT_PAYLOAD_SENSOR,        /**< payload.sensor is valid. */
    DEVICE_EVENT_PAYLOAD_BUTTON,        /**< payload.button is valid. */
    DEVICE_EVENT_PAYLOAD_TRACKBALL_DIR, /**< payload.trackball_dir is valid. */
    DEVICE_EVENT_PAYLOAD_TRACKBALL_XY,  /**< payload.trackball_xy is valid. */
    DEVICE_EVENT_PAYLOAD_SDCARD,        /**< payload.sdcard is valid. */
} DeviceEventPayloadType_t;

/**
 * @brief Union containing the payload for a typed device event.
 */
typedef union DeviceEventPayload {
    PMUEventType_t pmu;             /**< Power-management payload. */
    SensorEventType_t sensor;       /**< Sensor payload. */
    ButtonEventParam_t button;      /**< Button payload. */
    TrackballDir_t trackball_dir;   /**< Trackball direction payload. */
    TrackballXY_t trackball_xy;     /**< Trackball XY delta payload. */
    SDEvent_t sdcard;               /**< SD card payload. */
} DeviceEventPayload_t;

/**
 * @brief Complete normalized device event.
 */
typedef struct DeviceEvent {
    DeviceEvent_t type;                        /**< Top-level event category. */
    DeviceEventPayloadType_t payload_type;     /**< Payload type tag. */
    DeviceEventPayload_t payload;              /**< Event payload data. */

    /**
     * @brief Construct an empty event.
     */
    DeviceEvent() : type(NONE_EVENT), payload_type(DEVICE_EVENT_PAYLOAD_NONE)
    {
    }

    /**
     * @brief Construct an event with no payload.
     * @param event Top-level event category.
     */
    explicit DeviceEvent(DeviceEvent_t event) : type(event), payload_type(DEVICE_EVENT_PAYLOAD_NONE)
    {
    }

    /**
     * @brief Create a power-management event.
     * @param value Power event payload.
     * @return Device event with a PMU payload.
     */
    static DeviceEvent power(PMUEventType_t value)
    {
        DeviceEvent event(POWER_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_PMU;
        event.payload.pmu = value;
        return event;
    }

    /**
     * @brief Create a sensor event.
     * @param value Sensor event payload.
     * @return Device event with a sensor payload.
     */
    static DeviceEvent sensor(SensorEventType_t value)
    {
        DeviceEvent event(SENSOR_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_SENSOR;
        event.payload.sensor = value;
        return event;
    }

    /**
     * @brief Create a button event from an existing payload.
     * @param value Button event payload.
     * @return Device event with a button payload.
     */
    static DeviceEvent button(const ButtonEventParam_t &value)
    {
        DeviceEvent event(BUTTON_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_BUTTON;
        event.payload.button = value;
        return event;
    }

    /**
     * @brief Create a button event from an identifier and event type.
     * @param id Board-specific button identifier.
     * @param value Button event type.
     * @return Device event with a button payload.
     */
    static DeviceEvent button(uint8_t id, ButtonEvent_t value)
    {
        ButtonEventParam_t params = {id, static_cast < uint8_t > (value)};
        return button(params);
    }

    /**
     * @brief Create a trackball direction event.
     * @param value Direction payload.
     * @return Device event with a trackball direction payload.
     */
    static DeviceEvent trackball(TrackballDir_t value)
    {
        DeviceEvent event(TRACKBALL_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_TRACKBALL_DIR;
        event.payload.trackball_dir = value;
        return event;
    }

    /**
     * @brief Create a trackball XY delta event.
     * @param value XY delta payload.
     * @return Device event with a trackball XY payload.
     */
    static DeviceEvent trackball(const TrackballXY_t &value)
    {
        DeviceEvent event(TRACKBALL_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_TRACKBALL_XY;
        event.payload.trackball_xy = value;
        return event;
    }

    /**
     * @brief Create an SD card event.
     * @param value SD card payload.
     * @return Device event with an SD card payload.
     */
    static DeviceEvent sdcard(SDEvent_t value)
    {
        DeviceEvent event(SDCARD_EVENT);
        event.payload_type = DEVICE_EVENT_PAYLOAD_SDCARD;
        event.payload.sdcard = value;
        return event;
    }
} DeviceEvent;

/**
 * @brief Device event handler callback.
 * @param event Event delivered to the callback.
 * @param user_data User pointer supplied during registration.
 */
using DeviceEventHandler_t = void (*)(const DeviceEvent &event, void * user_data);

/**
 * @brief Registered event handler entry.
 */
typedef struct DeviceEventHandlerList {
    DeviceEventHandler_t cb; /**< Callback function. */
    DeviceEvent_t event;     /**< Event category handled by this entry. */
    void *user_data;         /**< User pointer passed to the callback. */

    /**
     * @brief Construct an empty handler entry.
     */
    DeviceEventHandlerList() :  cb(NULL), event(NONE_EVENT), user_data(NULL) {}
} DeviceEventHandlerList_t;

/**
 * @brief Lightweight dispatcher for board-level device events.
 */
class LilyGoEventManage
{
private:
    /** Registered event handlers. */
    std::vector < DeviceEventHandlerList_t > eventHandlerList;

    /**
     * @brief Check whether an exact handler entry is still registered.
     * @param target Handler entry to match.
     * @return true if the entry is present in eventHandlerList.
     */
    bool isEventRegistered(const DeviceEventHandlerList_t &target) const
    {
        for (uint32_t i = 0; i < eventHandlerList.size(); i++) {
            const DeviceEventHandlerList_t &entry = eventHandlerList[i];
            if (entry.cb == target.cb &&
                    entry.event == target.event &&
                    entry.user_data == target.user_data) {
                return true;
            }
        }
        return false;
    }

public:
    /**
     * @brief Construct an empty event manager.
     */
    LilyGoEventManage()
    {
    }

    /**
     * @brief Destroy the event manager.
     */
    ~LilyGoEventManage()
    {
    }

    /**
     * @brief Find a registered handler for an event category.
     * @param event Event category to search for.
     * @param cbEvent Callback pointer to match.
     * @return Index of the handler, or eventHandlerList.size() when not found.
     */
    uint32_t findEvent(DeviceEvent_t event, DeviceEventHandler_t cbEvent) const
    {
        uint32_t i;

        if (!cbEvent) {
            return eventHandlerList.size();
        }
        for (i = 0; i < eventHandlerList.size(); i++) {
            const DeviceEventHandlerList_t &entry = eventHandlerList[i];
            if (entry.cb == cbEvent && entry.event == event) {
                break;
            }
        }
        return i;
    }

    /**
     * @brief Register a handler for every event category.
     * @param cbEvent Callback invoked when events are sent.
     * @param user_data User pointer passed to the callback.
     * @return true if the handler was registered.
     */
    bool onEvent(DeviceEventHandler_t cbEvent, void *user_data = NULL)
    {
        return onEvent(ALL_EVENT_MAX, cbEvent, user_data);
    }

    /**
     * @brief Register a handler for a specific event category.
     * @param event Event category to observe.
     * @param cbEvent Callback invoked when matching events are sent.
     * @param user_data User pointer passed to the callback.
     * @return true if the handler was registered.
     */
    bool onEvent(DeviceEvent_t event, DeviceEventHandler_t cbEvent, void *user_data = NULL)
    {
        if (!cbEvent) {
            return false;
        }
        if (findEvent(event, cbEvent) < eventHandlerList.size()) {
            LILYGO_LOG_E("Attempt to add duplicate event handler!");
            return false;
        }
        DeviceEventHandlerList_t newEventHandler;
        newEventHandler.cb = cbEvent;
        newEventHandler.user_data = user_data;
        newEventHandler.event = event;
        eventHandlerList.push_back(newEventHandler);
        return true;
    }

    /**
     * @brief Remove a registered event handler.
     * @param event Event category used during registration.
     * @param cbEvent Callback pointer used during registration.
     * @return true if a matching handler was removed.
     */
    bool removeEvent(DeviceEvent_t event, DeviceEventHandler_t cbEvent)
    {
        uint32_t i;
        if (!cbEvent) {
            return false;
        }
        i = findEvent(event, cbEvent);
        if (i >= eventHandlerList.size()) {
            return false;
        }
        eventHandlerList.erase(eventHandlerList.begin() + i);
        return true;
    }

    /**
     * @brief Send an event to all matching registered handlers.
     * @param event Event object to deliver.
     */
    void sendEvent(const DeviceEvent &event)
    {
        std::vector < DeviceEventHandlerList_t > handlerList = eventHandlerList;
        for (uint32_t i = 0; i < handlerList.size(); i++) {
            DeviceEventHandlerList_t entry = handlerList[i];
            if (!isEventRegistered(entry)) {
                continue;
            }
            if (entry.cb && (entry.event == event.type || entry.event == ALL_EVENT_MAX)) {
                entry.cb(event, entry.user_data);
            }
        }
    }

    /**
     * @brief Send an event category without a payload.
     * @param event Event category to deliver.
     */
    void sendEvent(DeviceEvent_t event)
    {
        sendEvent(DeviceEvent(event));
    }

    /**
     * @brief Extract a PMU event payload.
     * @param event Source event.
     * @return PMU payload, or PMU_EVENT_NONE if the payload type does not match.
     */
    PMUEventType_t getPMUEventType(const DeviceEvent &event) const
    {
        return event.payload_type == DEVICE_EVENT_PAYLOAD_PMU ? event.payload.pmu : PMU_EVENT_NONE;
    }

    /**
     * @brief Extract a sensor event payload.
     * @param event Source event.
     * @return Sensor payload, or SENSOR_EVENT_NONE if the payload type does not match.
     */
    SensorEventType_t getSensorEventType(const DeviceEvent &event) const
    {
        return event.payload_type == DEVICE_EVENT_PAYLOAD_SENSOR ? event.payload.sensor : SENSOR_EVENT_NONE;
    }

    /**
     * @brief Extract an SD card event payload.
     * @param event Source event.
     * @return SD card payload, or SDCARD_EVENT_NONE if the payload type does not match.
     */
    SDEvent_t getSDEventType(const DeviceEvent &event) const
    {
        return event.payload_type == DEVICE_EVENT_PAYLOAD_SDCARD ? event.payload.sdcard : SDCARD_EVENT_NONE;
    }

    /**
     * @brief Extract the full button event payload.
     * @param event Source event.
     * @return Pointer to the button payload, or NULL if the payload type does not match.
     */
    const ButtonEventParam_t *getButtonEventParam(const DeviceEvent &event) const
    {
        if (event.payload_type == DEVICE_EVENT_PAYLOAD_BUTTON) {
            return &event.payload.button;
        }
        return NULL;
    }

    /**
     * @brief Extract the button identifier from a button event.
     * @param event Source event.
     * @return Button identifier, or 0 if the payload type does not match.
     */
    uint8_t getButtonEventId(const DeviceEvent &event) const
    {
        const ButtonEventParam_t *params = getButtonEventParam(event);
        if (!params) {
            return 0;
        }
        return params->id;
    }

    /**
     * @brief Extract the button event type from a button event.
     * @param event Source event.
     * @return Button event type, or BUTTON_EVENT_NONE if unavailable.
     */
    ButtonEvent_t getButtonEventType(const DeviceEvent &event) const
    {
        const ButtonEventParam_t *params = getButtonEventParam(event);
        if (!params) {
            return BUTTON_EVENT_NONE;
        }
        if (params->event >= BUTTON_EVENT_NONE && params->event <= BUTTON_EVENT_DOUBLE_CLICK) {
            return static_cast < ButtonEvent_t > (params->event);
        }
        return BUTTON_EVENT_NONE;
    }

    /**
     * @brief Extract a trackball direction payload.
     * @param event Source event.
     * @return Trackball direction, or TRACKBALL_DIR_NONE if unavailable.
     */
    TrackballDir_t getTrackballDirType(const DeviceEvent &event) const
    {
        if (event.payload_type == DEVICE_EVENT_PAYLOAD_TRACKBALL_DIR) {
            return event.payload.trackball_dir;
        }
        return TRACKBALL_DIR_NONE;
    }

    /**
     * @brief Extract a trackball XY delta payload.
     * @param event Source event.
     * @return Pointer to the XY payload, or NULL if the payload type does not match.
     */
    const TrackballXY_t *getTrackballXY(const DeviceEvent &event) const
    {
        if (event.payload_type == DEVICE_EVENT_PAYLOAD_TRACKBALL_XY) {
            return &event.payload.trackball_xy;
        }
        return NULL;
    }
};
