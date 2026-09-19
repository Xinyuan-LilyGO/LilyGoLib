/**
 * @file      PMU_Interrupt.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-28
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) ||defined(ARDUINO_T_WATCH_S3_ULTRA)

lv_obj_t *label1;

void device_event_cb(const DeviceEvent &event, void *user_data)
{
    if (event.type != POWER_EVENT) {
        return;
    }
    switch (instance.getPMUEventType(event)) {
    case PMU_EVENT_BATTERY_LOW_TEMP:
        Serial.println("Battery temperature is low");
        break;
    case PMU_EVENT_BATTERY_HIGH_TEMP:
        Serial.println("Battery temperature is very high");
        break;
    case PMU_EVENT_CHARGE_LOW_TEMP:
        Serial.println("Charger temperature is low");
        break;
    case PMU_EVENT_CHARGE_HIGH_TEMP:
        Serial.println("Charger temperature is high");
        break;
    case PMU_EVENT_KEY_CLICKED:
        Serial.println("Power button is clicked");
        break;
    case PMU_EVENT_KEY_LONG_PRESSED:
        Serial.println("Power button is long-pressed");
        break;
    case PMU_EVENT_BATTERY_REMOVE:
        Serial.println("Battery is removed");
        break;
    case PMU_EVENT_BATTERY_INSERT:
        Serial.println("Battery is inserted");
        break;
    case PMU_EVENT_USBC_REMOVE:
        Serial.println("Power adapter removed");
        break;
    case PMU_EVENT_USBC_INSERT:
        Serial.println("Power adapter plugged in");
        break;
    case PMU_EVENT_CHARGE_STARTED:
        Serial.println("Battery charging starts");
        break;
    case PMU_EVENT_CHARGE_FINISH:
        Serial.println("Battery charging finish");
        break;
    default:
        break;
    }
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    instance.enablePowerEvent(PowerEvent::IRQ_BAT_CHG_DONE |
                              PowerEvent::IRQ_BAT_CHG_START |
                              PowerEvent::IRQ_PEKEY_CLICKED |
                              PowerEvent::IRQ_PEKEY_LONG_PRESSED |
                              PowerEvent::IRQ_BAT_REMOVE |
                              PowerEvent::IRQ_BAT_INSERT |
                              PowerEvent::IRQ_BAT_TEMP_HIGH |
                              PowerEvent::IRQ_BAT_TEMP_LOW, true);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Register power event
    instance.onEvent(POWER_EVENT, device_event_cb);

}

void loop()
{
    instance.loop();
    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-Watch-S3 or T-Watch-Ultra"); delay(1000);
}

#endif
