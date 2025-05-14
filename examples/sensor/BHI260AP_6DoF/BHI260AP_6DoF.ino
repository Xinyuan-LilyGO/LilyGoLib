/**
 * @file      BHI260AP_6DoF.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-11
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef USING_BHI260_SENSOR

#include <bosch/BoschSensorDataHelper.hpp>


SensorXYZ accel(SensorBHI260AP::ACCEL_PASSTHROUGH, instance.sensor);
SensorXYZ gyro(SensorBHI260AP::GYRO_PASSTHROUGH, instance.sensor);

lv_obj_t *label1;

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

    if (!(instance.getDeviceProbe() & HW_SENSOR_ONLINE)) {
        lv_label_set_text(label1, "Sensor is not online");
        while (1) {
            lv_task_handler(); delay(5);
        }
    }

    // Output all sensors info to Serial
    BoschSensorInfo info = instance.sensor.getSensorInfo();
    info.printInfo(Serial);

    float sample_rate = 100.0;      /* Read out data measured at 100Hz */
    uint32_t report_latency_ms = 0; /* Report immediately */

    // Enable acceleration
    accel.enable(sample_rate, report_latency_ms);
    // Enable gyroscope
    gyro.enable(sample_rate, report_latency_ms);

}


void loop()
{

    instance.loop();

    if (accel.hasUpdated() && gyro.hasUpdated()) {
        uint32_t s;
        uint32_t ns;
        accel.getLastTime(s, ns);

        Serial.printf("[T: %" PRIu32 ".%09" PRIu32 "] AX:%+7.2f AY:%+7.2f AZ:%+7.2f GX:%+7.2f GY:%+7.2f GZ:%+7.2f \n",
                      s, ns, accel.getX(), accel.getY(), accel.getZ(),
                      gyro.getX(), gyro.getY(), gyro.getZ());

        lv_label_set_text_fmt(label1, "[T: %" PRIu32 ".%09" PRIu32 "]\nAX:%+7.2f AY:%+7.2f AZ:%+7.2f\nGX:%+7.2f GY:%+7.2f GZ:%+7.2f \n",
                              s, ns, accel.getX(), accel.getY(), accel.getZ(),
                              gyro.getX(), gyro.getY(), gyro.getZ());

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
    Serial.println("This example only supports examples with BHI260 sensors.");
    delay(1000);
}

#endif

