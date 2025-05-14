/**
 * @file      BHI260AP_Euler.ino
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

SensorQuaternion quaternion(instance.sensor);
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

    Serial.println("Initializing the sensor successfully!");

    // Output all sensors info to Serial
    BoschSensorInfo info = instance.sensor.getSensorInfo();
    info.printInfo(Serial);

    // Define the sample rate for data reading.
    // The sensor will read out data measured at a frequency of 100Hz.
    float sample_rate = 100.0;
    // Define the report latency in milliseconds.
    // A value of 0 means the sensor will report the measured data immediately.
    uint32_t report_latency_ms = 0;

    quaternion.enable(sample_rate, report_latency_ms);

}


void loop()
{
    instance.loop();

    if (quaternion.hasUpdated()) {
        // Convert rotation vector to Euler angles
        quaternion.toEuler();
        // Print the roll angle to the serial monitor.
        Serial.print(quaternion.getRoll());
        // Print a comma as a separator between the roll and pitch angles.
        Serial.print(",");
        // Print the pitch angle to the serial monitor.
        Serial.print(quaternion.getPitch());
        // Print a comma as a separator between the pitch and yaw angles.
        Serial.print(",");
        // Print the yaw angle to the serial monitor and start a new line.
        Serial.println(quaternion.getHeading());

        lv_label_set_text_fmt(label1, "Roll:%.2f\nPitch:%.2f\nHeading:%.2f",
                              quaternion.getRoll(),
                              quaternion.getPitch(),
                              quaternion.getHeading());
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

