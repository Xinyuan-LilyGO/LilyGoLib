/**
 * @file      BMA4XX_SensorModel.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026 Shenzhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-09-23
 *
 * This example prints the BMA4xx accelerometer model detected by LilyGoLib.
 */

#include <LilyGoLib.h>

#if defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V3) || defined(ARDUINO_T_WATCH_S3)

static const char *getDetectedModelName()
{
    switch (instance.detectedModel) {
    case DetectedModel::BMA423:
        return "BMA423";
    case DetectedModel::BMA456H:
        return "BMA456H (BMA456 replacement)";
    default:
        return "Not detected";
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    instance.begin();

    Serial.println();
    Serial.println("BMA4xx sensor identification");
    Serial.print("Detected model: ");
    Serial.println(getDetectedModelName());

    if (instance.sensor) {
        Serial.print("Driver model: ");
        Serial.println(instance.sensor->getModelName());
    } else {
        Serial.println("Check the selected board revision and the sensor connection.");
    }
}

void loop()
{
    instance.loop();
    delay(10);
}

#else

void setup()
{
    Serial.begin(115200);
    Serial.println("This example requires a LilyGoLib board with a BMA423 or BMA456 sensor.");
}

void loop()
{
    delay(1000);
}

#endif
