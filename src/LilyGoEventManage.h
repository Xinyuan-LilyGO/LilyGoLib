/**
 * @file      LilyGoEventManage.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-18
 *
 */
#include <Arduino.h>
#include <vector>

typedef enum {
    PMU_EVENT_NONE,
    PMU_EVENT_BATTERY_LOW_TEMP,
    PMU_EVENT_BATTERY_HIGH_TEMP,
    PMU_EVENT_CHARGE_LOW_TEMP,
    PMU_EVENT_CHARGE_HIGH_TEMP,
    PMU_EVENT_LOW_VOLTAGE_LEVEL1,
    PMU_EVENT_LOW_VOLTAGE_LEVEL2,
    PMU_EVENT_KEY_CLICKED,
    PMU_EVENT_KEY_LONG_PRESSED,
    PMU_EVENT_BATTERY_REMOVE,
    PMU_EVENT_BATTERY_INSERT,
    PMU_EVENT_USBC_REMOVE,
    PMU_EVENT_USBC_INSERT,
    PMU_EVENT_BATTERY_OVER_VOLTAGE,
    PMU_EVENT_CHARGE_TIMEOUT,
    PMU_EVENT_CHARGE_STARTED,
    PMU_EVENT_CHARGE_FINISH,
    PMU_EVENT_BAT_FET_OVER_CURRENT,

    RTC_EVENT_INTERRUPT,

    SENSOR_EVENT_INTERRUPT,

    SENSOR_STEPS_UPDATED,
    SENSOR_ACTIVITY_DETECTED,
    SENSOR_TILT_DETECTED,
    SENSOR_DOUBLE_TAP_DETECTED,
    SENSOR_ANY_MOTION_DETECTED,

    ALL_EVENT_MAX,
} DeviceEvent_t;

using DeviceEventCb_t = void (*)(DeviceEvent_t event, void * user_data);

typedef struct DeviceEventCbList {
    DeviceEventCb_t cb;
    DeviceEvent_t event;
    void *user_data;
    DeviceEventCbList() :  cb(NULL), event(PMU_EVENT_NONE), user_data(NULL) {}
} DeviceEventCbList_t;

class LilyGoEventManage
{
private:
    std::vector<DeviceEventCbList_t> cbEventList;
public:
    LilyGoEventManage()
    {
    }
    ~LilyGoEventManage()
    {
    }

    uint32_t findEvent(DeviceEventCb_t cbEvent, DeviceEvent_t event)
    {
        uint32_t i;

        if (!cbEvent) {
            return cbEventList.size();
        }
        for (i = 0; i < cbEventList.size(); i++) {
            DeviceEventCbList_t entry = cbEventList[i];
            if (entry.cb == cbEvent && entry.event == event) {
                break;
            }
        }
        return i;
    }

    void onEvent(DeviceEventCb_t cbEvent, void*user_data = NULL, DeviceEvent_t event = ALL_EVENT_MAX)
    {
        onEvent(cbEvent, event, user_data);
    }

    void onEvent(DeviceEventCb_t cbEvent, DeviceEvent_t event, void*user_data)
    {
        if (!cbEvent) {
            return;
        }
        if (findEvent(cbEvent, event) < cbEventList.size()) {
            log_e("Attempt to add duplicate event handler!");
            return;
        }
        DeviceEventCbList_t newEventHandler;
        newEventHandler.cb = cbEvent;
        newEventHandler.user_data = user_data;
        newEventHandler.event = event;
        cbEventList.push_back(newEventHandler);
    }

    void removeEvent(DeviceEventCb_t cbEvent, DeviceEvent_t event)
    {
        uint32_t i;
        if (!cbEvent) {
            return;
        }
        i = findEvent(cbEvent, event);
        if (i >= cbEventList.size()) {
            return;
        }
        cbEventList.erase(cbEventList.begin() + i);
    }

    void sendEvent(DeviceEvent_t event)
    {
        for (uint32_t i = 0; i < cbEventList.size(); i++) {
            DeviceEventCbList_t entry = cbEventList[i];
            if (entry.cb && entry.event == event || entry.event == ALL_EVENT_MAX) {
                entry.cb(event, entry.user_data);
            }
        }
    }
};

