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


void device_event_cb(DeviceEvent_t event, void*user_data)
{
    switch (event) {
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
    case PMU_EVENT_LOW_VOLTAGE_LEVEL1:
        Serial.println("Low battery low voltage warning level 1");
        break;
    case PMU_EVENT_LOW_VOLTAGE_LEVEL2:
        Serial.println("Low battery low voltage warning level 2");
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
    case PMU_EVENT_BATTERY_OVER_VOLTAGE:
        Serial.println("Battery over-voltage protection warning");
        break;
    case PMU_EVENT_CHARGE_TIMEOUT:
        Serial.println("Battery charging timeout");
        break;
    case PMU_EVENT_CHARGE_STARTED:
        Serial.println("Battery charging starts");
        break;
    case PMU_EVENT_CHARGE_FINISH:
        Serial.println("Battery charging finish");
        break;
    case PMU_EVENT_BAT_FET_OVER_CURRENT:
        Serial.println("Battery FET overcurrent detected");
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

    // Clear all interrupt status
    instance.pmu.clearIrqStatus();

    // Enable the required interrupt function
    instance.pmu.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ    |
        XPOWERS_AXP2101_BAT_REMOVE_IRQ    |   // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ   |
        XPOWERS_AXP2101_VBUS_REMOVE_IRQ   |   // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ    |
        XPOWERS_AXP2101_PKEY_LONG_IRQ     |   // POWER KEY
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ  |
        XPOWERS_AXP2101_BAT_CHG_START_IRQ     // CHARGE
    );

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Register device event
    instance.onEvent(device_event_cb);

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
