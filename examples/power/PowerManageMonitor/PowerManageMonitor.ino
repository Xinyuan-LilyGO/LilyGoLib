/**
 * @file      PMU_ADC.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) ||defined(ARDUINO_T_WATCH_S3_ULTRA)

lv_obj_t *label1;
uint32_t interval;

const char *chg_status[] = {
    "No charge",
    "Pre-charge",
    "Fast charging",
    "Charging terminated",
    "Unknown",
};

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

void loop()
{
    if (interval < millis()) {
        PmicChargerBase::ChargingStatus charge_status = instance.getChgStatus();
        lv_label_set_text_fmt(label1, "Charging:%s\nDischarge:%s\nUSB PlugIn:%s\nCHG state:%s\nBattery Voltage:%u mV\n USB Voltage:%u mV\n SYS Voltage:%u mV\n Battery Percent:%d%%",
                              instance.isCharging() ? "YES" : "NO",
                              instance.isDischarge() ? "YES" : "NO",
                              instance.isUsbIn() ? "YES" : "NO",
                              chg_status[static_cast<uint8_t>(charge_status)],
                              instance.getBattVoltage(),
                              instance.getVbusVoltage(),
                              instance.getSysVoltage(),
                              instance.getBatteryPercent()
                             );
        interval = millis() + 1000;
    }
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
