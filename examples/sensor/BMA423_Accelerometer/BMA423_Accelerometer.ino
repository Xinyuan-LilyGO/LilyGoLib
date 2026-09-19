/**
 * @file      BMA423_Accelerometer.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_WATCH_S3

uint32_t interval;
lv_obj_t *label1;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    // Accelerometer configuration
    // The desired operation mode. Allowed values are SUSPEND, NORMAL.
    OperationMode operationMode = OperationMode::NORMAL;
    // The desired full-scale range. Allowed values are FS_2G, FS_4G, FS_8G, FS_16G.
    AccelFullScaleRange fullScaleRange = AccelFullScaleRange::FS_2G;
    // The desired bandwidth. Allowed values are OSR4_AVG1, OSR2_AVG2, NORMAL_AVG4, etc.
    AccelBandwidth bandwidth = AccelBandwidth::OSR2_AVG2;
    // The desired data rate in Hz. Allowed values are 0.78, 1.56, 3.12, 6.25, 12.5, 25, 50, 100, 200, 400, 800, 1600.
    float data_rate_hz = 100.0f;
    // The desired performance mode. Allowed values are CIC_AVG_MODE, CONTINUOUS_MODE
    AccelPerfMode perfMode = AccelPerfMode::CIC_AVG_MODE;

    if (!instance.sensor->configAccelerometer(operationMode, fullScaleRange, data_rate_hz, bandwidth, perfMode)) {
        Serial.println("Failed to configure accelerometer");
        while (1);
    }

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}


void loop()
{
    instance.loop();

    int16_t x, y, z;
    if (interval < millis()) {
        interval = millis() + 50;
        AccelerometerData data;
        instance.sensor->readData(data);
        Serial.print("X:");
        Serial.print(data.mps2.x); Serial.print(" ");
        Serial.print("Y:");
        Serial.print(data.mps2.y); Serial.print(" ");
        Serial.print("Z:");
        Serial.print(data.mps2.z);
        Serial.println();
        lv_label_set_text_fmt(label1, "X:%.2f \nY:%.2f \nZ:%.2f\nTemp:%.2f", data.mps2.x, data.mps2.y, data.mps2.z, data.temperature);
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
    Serial.println("The example only support  T-Watch-S3"); delay(1000);
}

#endif

