/**
 * @file      BMASensorHelper.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-03
 *
 */

#include "LilyGoLog.h"
#include "BMASensorHelper.h"
#include <math.h>

static float calc_accel_magnitude(const AccelerometerData &data)
{
    return sqrtf(data.mps2.x * data.mps2.x +
                 data.mps2.y * data.mps2.y +
                 data.mps2.z * data.mps2.z);
}

static void setupFeatures(SensorBMA4XX * sensor)
{
    assert(sensor != nullptr);
    auto caps = sensor->getCapabilities();
    LILYGO_LOG_D("Chip :%s Capabilities:0x%X\n", sensor->getModelName(), static_cast<uint32_t>(caps));
    if (caps & BMA4XXCapability::Capability::SupportAnyMotion) {
        LILYGO_LOG_D("  + enableAnyMotion");
        sensor->enableAnyMotionDetection(
            SensorBMA4XX::MotionAxesConfig(1, 1, 1), true
        );
    }
    if (caps & BMA4XXCapability::Capability::SupportTap) {
        LILYGO_LOG_D("  + enableTapDetector");
        sensor->enableTapDetector(true, true);
    }
    if (caps & BMA4XXCapability::Capability::SupportStepDetector) {
        LILYGO_LOG_D("  + enableStepDetector");
        sensor->enableStepCounter(true, 1, true);
        sensor->enableStepDetector(true, true);
    }
    if (caps & BMA4XXCapability::Capability::SupportActivity) {
        LILYGO_LOG_D("  + enableActivityRecognition");
        sensor->enableActivityRecognition(true, true);
    }
    if (caps & BMA4XXCapability::Capability::SupportTilt) {
        LILYGO_LOG_D("  + enableTiltDetector");
        sensor->enableTiltDetector(true, true);
    }
}


BMASensorHelper::BMASensorHelper(LilyGoEventManage *eventManage) : sensor(nullptr),
    detectedModel(DetectedModel::NONE), eventManage(eventManage)
{
    runtimeStatus = BMASensorRuntimeStatus_t();
    runtimeStatus.activity = ActivityType::UNKNOWN;
    runtimeStatus.lastEvent = BMASensorRuntimeEvent::NONE;
}

bool BMASensorHelper::beginSensor(TwoWire &wire, uint8_t address, SensorRemap remap)
{
    if (sensor != nullptr) return false;

    if (bma423.begin(wire, address)) {
        sensor = &bma423;
        detectedModel = DetectedModel::BMA423;
        LILYGO_LOG_D("Detected BMA423");
    } else if (bma456h.begin(wire, address)) {
        sensor = &bma456h;
        detectedModel = DetectedModel::BMA456H;
        LILYGO_LOG_D("Detected BMA456H");
    }

    if (sensor == nullptr) {
        LILYGO_LOG_E("Failed to find BMA sensor!");
        return false;
    }

    sensor->setRemapAxes(remap);

    if (!sensor->configAccelerometer(
                OperationMode::NORMAL,
                AccelFullScaleRange::FS_2G,
                100.0f,
                AccelBandwidth::OSR2_AVG2,
                AccelPerfMode::CIC_AVG_MODE)) {
        LILYGO_LOG_E("Failed to configure accelerometer");
        return false;
    }

    sensor->setInterruptPinConfig(
        InterruptPinMap::PIN1,
        false,   // level trigger
        false,   // active high
        true,    // output enable
        false);  // input disable

    setupFeatures(sensor);
    setupTapCallback();
    setupStepCallbacks();
    setupActivityCallback();
    setupTiltCallback();
    setupMotionCallbacks();
    setupDataReadyCallback();

    return true;
}

bool BMASensorHelper::readAccelerometerData(AccelerometerData &accelData)
{
    if (!sensor) return false;
    if (!dataReady && !runtimeStatus.dataValid)
        return false;
    dataReady = false;
    accelData = runtimeStatus.accel;
    return runtimeStatus.dataValid;
}

bool BMASensorHelper::enableSensor(bool enableDRY,
                                   float data_rate_hz,
                                   AccelFullScaleRange range,
                                   AccelBandwidth bandwidth,
                                   AccelPerfMode perf_mode)
{
    if (!sensor) return false;
    if (!sensor->enableDataReady(enableDRY)) {
        LILYGO_LOG_E("Enable data ready failed");
        return false;
    }
    if (!sensor->configAccelerometer(OperationMode::NORMAL, range, data_rate_hz, bandwidth, perf_mode)) {
        LILYGO_LOG_E("Failed to configure accelerometer");
        return false;
    }
    return true;
}

void BMASensorHelper::disableSensor()
{
    if (!sensor) return;
    sensor->setOperationMode(OperationMode::SUSPEND);
}

void BMASensorHelper::loopSensor()
{
    if (!sensor) {
        return;
    }
    sensor->update();
}

void BMASensorHelper::resetStepCounter()
{
    runtimeStatus.stepCount = 0;
    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.resetStepCounter();
        break;
    case DetectedModel::BMA456H:
        bma456h.resetStepCounter();
        break;
    default:
        break;
    }
}

void BMASensorHelper::resetRuntimeStatus()
{
    AccelerometerData accel = runtimeStatus.accel;
    bool valid = runtimeStatus.dataValid;
    resetStepCounter();
    runtimeStatus = BMASensorRuntimeStatus_t();
    runtimeStatus.dataValid = valid;
    runtimeStatus.accel = accel;
    runtimeStatus.magnitude = valid ? calc_accel_magnitude(accel) : 0.0f;
    runtimeStatus.peakMagnitude = runtimeStatus.magnitude;
    runtimeStatus.activity = ActivityType::UNKNOWN;
    runtimeStatus.lastEvent = BMASensorRuntimeEvent::NONE;
}

void BMASensorHelper::updateAccelStatus(const AccelerometerData &data)
{
    runtimeStatus.accel = data;
    runtimeStatus.dataValid = true;
    runtimeStatus.magnitude = calc_accel_magnitude(data);
    if (runtimeStatus.magnitude > runtimeStatus.peakMagnitude) {
        runtimeStatus.peakMagnitude = runtimeStatus.magnitude;
    }
}

void BMASensorHelper::recordRuntimeEvent(BMASensorRuntimeEvent event)
{
    runtimeStatus.lastEvent = event;
    switch (event) {
    case BMASensorRuntimeEvent::STEP:
        runtimeStatus.stepEvents++;
        break;
    case BMASensorRuntimeEvent::SINGLE_TAP:
        runtimeStatus.singleTaps++;
        break;
    case BMASensorRuntimeEvent::DOUBLE_TAP:
        runtimeStatus.doubleTaps++;
        break;
    case BMASensorRuntimeEvent::TRIPLE_TAP:
        runtimeStatus.tripleTaps++;
        break;
    case BMASensorRuntimeEvent::ACTIVITY:
        runtimeStatus.activityEvents++;
        break;
    case BMASensorRuntimeEvent::TILT:
        runtimeStatus.tiltEvents++;
        break;
    case BMASensorRuntimeEvent::ANY_MOTION:
        runtimeStatus.motionEvents++;
        break;
    case BMASensorRuntimeEvent::NO_MOTION:
        runtimeStatus.noMotionEvents++;
        break;
    default:
        break;
    }
}

void BMASensorHelper::setupTapCallback()
{
    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.setOnTapCallback([this](TapType t) {
            SensorEventType_t event = SENSOR_EVENT_NONE;
            switch (t) {
            case TapType::SINGLE_TAP:
                LILYGO_LOG_D("Single tap detected");
                recordRuntimeEvent(BMASensorRuntimeEvent::SINGLE_TAP);
                event = SENSOR_SINGLE_TAP_DETECTED;
                break;
            case TapType::DOUBLE_TAP:
                LILYGO_LOG_D("Double tap detected");
                recordRuntimeEvent(BMASensorRuntimeEvent::DOUBLE_TAP);
                event = SENSOR_DOUBLE_TAP_DETECTED;
                break;
            default:
                return;
            }
            if (eventManage && event != SENSOR_EVENT_NONE) {
                eventManage->sendEvent(DeviceEvent::sensor(event));
            }
        });
        break;
    case DetectedModel::BMA456H:
        bma456h.setOnTapCallback([this](TapType t) {
            SensorEventType_t event = SENSOR_EVENT_NONE;
            switch (t) {
            case TapType::SINGLE_TAP:
                recordRuntimeEvent(BMASensorRuntimeEvent::SINGLE_TAP);
                event = SENSOR_SINGLE_TAP_DETECTED;
                break;
            case TapType::DOUBLE_TAP:
                recordRuntimeEvent(BMASensorRuntimeEvent::DOUBLE_TAP);
                event = SENSOR_DOUBLE_TAP_DETECTED;
                break;
            case TapType::TRIPLE_TAP:
                recordRuntimeEvent(BMASensorRuntimeEvent::TRIPLE_TAP);
                break;
            default:
                return;
            }
            if (eventManage && event != SENSOR_EVENT_NONE) {
                eventManage->sendEvent(DeviceEvent::sensor(event));
            }
        });
        break;
    default:
        break;
    }
}

void BMASensorHelper::setupStepCallbacks()
{
    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.setOnStepDetectedCallback([this]() {
            LILYGO_LOG_D("Step detected");
            recordRuntimeEvent(BMASensorRuntimeEvent::STEP);
            SensorEventType_t event  = SENSOR_STEP_DETECTED;
            if (eventManage) {
                eventManage->sendEvent(DeviceEvent::sensor(event));
            }
        });
        bma423.setOnStepCountCallback([this](uint32_t count) {
            LILYGO_LOG_D("Step count callback: %u", count);
            if (count != runtimeStatus.stepCount) {
                LILYGO_LOG_D("Step count updated: %u", count);
                runtimeStatus.stepCount = count;
                SensorEventType_t event  = SENSOR_STEPS_UPDATED;
                if (eventManage) {
                    eventManage->sendEvent(DeviceEvent::sensor(event));
                }
            }
        });
        break;
    case DetectedModel::BMA456H:
        bma456h.setOnStepDetectedCallback([this]() {
            LILYGO_LOG_D("Step detected");
            recordRuntimeEvent(BMASensorRuntimeEvent::STEP);
            SensorEventType_t event  = SENSOR_STEP_DETECTED;
            if (eventManage) {
                eventManage->sendEvent(DeviceEvent::sensor(event));
            }
        });
        bma456h.setOnStepCountCallback([this](uint32_t count) {
            if (count != runtimeStatus.stepCount) {
                runtimeStatus.stepCount = count;
                SensorEventType_t event  = SENSOR_STEPS_UPDATED;
                if (eventManage) {
                    eventManage->sendEvent(DeviceEvent::sensor(event));
                }
            }
        });
        break;
    default:
        break;
    }
}

void BMASensorHelper::setupActivityCallback()
{
    auto fn = [this](ActivityType a) {
        runtimeStatus.activity = a;
        recordRuntimeEvent(BMASensorRuntimeEvent::ACTIVITY);
        SensorEventType_t event  = SENSOR_ACTIVITY_DETECTED;
        LILYGO_LOG_D("Activity detected: %d", static_cast<int>(a));
        if (eventManage) {
            eventManage->sendEvent(DeviceEvent::sensor(event));
        }
    };
    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.setOnActivityCallback(fn);
        break;
    case DetectedModel::BMA456H:
        bma456h.setOnActivityCallback(fn);
        break;
    default:
        break;
    }
}

void BMASensorHelper::setupTiltCallback()
{
    if (detectedModel == DetectedModel::BMA423) {
        SensorBMA423::Platform platform = SensorBMA423::Platform::SMARTPHONE;
        bma423.selectPlatform(platform);
        bma423.setOnTiltDetectedCallback([this]() {
            LILYGO_LOG_D("Tilt detected");
            recordRuntimeEvent(BMASensorRuntimeEvent::TILT);
            SensorEventType_t event  = SENSOR_TILT_DETECTED;
            if (eventManage) {
                eventManage->sendEvent(DeviceEvent::sensor(event));
            }
        });
    }
}

void BMASensorHelper::setupMotionCallbacks()
{
    auto anyMotionFn = [this]() {
        recordRuntimeEvent(BMASensorRuntimeEvent::ANY_MOTION);
        if (eventManage) {
            eventManage->sendEvent(DeviceEvent::sensor(SENSOR_ANY_MOTION_DETECTED));
        }
    };
    auto noMotionFn = [this]() {
        recordRuntimeEvent(BMASensorRuntimeEvent::NO_MOTION);
    };

    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.setOnAnyMotionCallback(anyMotionFn);
        bma423.setOnNoMotionCallback(noMotionFn);
        break;
    case DetectedModel::BMA456H:
        bma456h.setOnAnyMotionCallback(anyMotionFn);
        bma456h.setOnNoMotionCallback(noMotionFn);
        break;
    default:
        break;
    }
}

void BMASensorHelper::setupDataReadyCallback()
{
    auto fn = [this](AccelerometerData data) {
        updateAccelStatus(data);
        dataReady = true;
    };
    switch (detectedModel) {
    case DetectedModel::BMA423:
        bma423.setOnDataReadyCallback(fn);
        break;
    case DetectedModel::BMA456H:
        bma456h.setOnDataReadyCallback(fn);
        break;
    default:
        break;
    }
}
