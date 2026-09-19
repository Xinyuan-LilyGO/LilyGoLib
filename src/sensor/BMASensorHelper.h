/**
 * @file      BMASensorHelper.h
 * @brief     Declares BMA4xx motion sensor detection, setup, and event helpers.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-03
 *
 */
#pragma once

#include <AccelerometerDrv.hpp>
#include "../core/LilyGoEventManage.h"

/**
 * @brief Runtime model detected on the BMA4xx I2C address.
 */
enum class DetectedModel {
    NONE,   /**< No supported BMA sensor was detected. */
    BMA423, /**< Bosch BMA423 accelerometer. */
    BMA456H /**< Bosch BMA456H accelerometer. */
};

/**
 * @brief Internal runtime events tracked by the BMA helper.
 */
enum class BMASensorRuntimeEvent {
    NONE,       /**< No runtime event. */
    STEP,       /**< Step detected. */
    SINGLE_TAP, /**< Single tap detected. */
    DOUBLE_TAP, /**< Double tap detected. */
    TRIPLE_TAP, /**< Triple tap detected. */
    ACTIVITY,   /**< Activity classification changed. */
    TILT,       /**< Tilt gesture detected. */
    ANY_MOTION, /**< Any-motion interrupt detected. */
    NO_MOTION,  /**< No-motion interrupt detected. */
    DATA_READY  /**< New accelerometer data is ready. */
};

/**
 * @brief Snapshot of BMA sensor data and accumulated event counters.
 */
typedef struct {
    bool dataValid;                       /**< true when accel contains valid data. */
    AccelerometerData accel;              /**< Last accelerometer sample. */
    float magnitude;                      /**< Magnitude of the last acceleration sample. */
    float peakMagnitude;                  /**< Highest magnitude observed since resetRuntimeStatus(). */
    uint32_t stepCount;                   /**< Current step counter value reported by the sensor. */
    uint32_t stepEvents;                  /**< Number of step events observed. */
    uint32_t singleTaps;                  /**< Number of single-tap events observed. */
    uint32_t doubleTaps;                  /**< Number of double-tap events observed. */
    uint32_t tripleTaps;                  /**< Number of triple-tap events observed. */
    uint32_t activityEvents;              /**< Number of activity events observed. */
    uint32_t tiltEvents;                  /**< Number of tilt events observed. */
    uint32_t motionEvents;                /**< Number of any-motion events observed. */
    uint32_t noMotionEvents;              /**< Number of no-motion events observed. */
    ActivityType activity;                /**< Last activity classification reported by the sensor. */
    BMASensorRuntimeEvent lastEvent;      /**< Last runtime event recorded by the helper. */
} BMASensorRuntimeStatus_t;

/**
 * @brief Helper for probing, configuring, and polling supported BMA sensors.
 */
class BMASensorHelper
{
public:
    /** Active sensor interface after beginSensor() succeeds. */
    SensorBMA4XX *sensor;
    /** BMA423 concrete driver instance. */
    SensorBMA423 bma423;
    /** BMA456H concrete driver instance. */
    SensorBMA456H bma456h;
    /** Detected BMA sensor model. */
    DetectedModel detectedModel = DetectedModel::NONE;

    /**
     * @brief Construct a BMA helper.
     * @param eventManage Optional event dispatcher used for sensor events.
     */
    BMASensorHelper(LilyGoEventManage *eventManage);

    /**
     * @brief Probe and initialize a supported BMA sensor.
     * @param wire I2C bus used by the sensor.
     * @param address I2C address to probe.
     * @param remap Axis remap configuration for board orientation.
     * @return true if a supported sensor was found and configured.
     */
    bool beginSensor(TwoWire &wire, uint8_t address, SensorRemap remap);

    /**
     * @brief Read the last accelerometer sample.
     * @param accelData Receives the latest valid accelerometer sample.
     * @return true if valid data was copied.
     */
    bool readAccelerometerData(AccelerometerData &accelData);

    /**
     * @brief Enable and configure accelerometer data-ready reporting.
     * @param enableDRY true to enable data-ready interrupt reporting.
     * @param data_rate_hz Accelerometer output data rate in Hz.
     * @param range Accelerometer full-scale range.
     * @param bandwidth Accelerometer bandwidth setting.
     * @param perf_mode Accelerometer performance mode.
     * @return true if the sensor was configured successfully.
     */
    bool enableSensor(bool enableDRY,
                      float data_rate_hz = 100.0f,
                      AccelFullScaleRange range = AccelFullScaleRange::FS_2G,
                      AccelBandwidth bandwidth = AccelBandwidth::OSR2_AVG2,
                      AccelPerfMode perf_mode = AccelPerfMode::CIC_AVG_MODE);

    /**
     * @brief Put the accelerometer into suspend mode.
     */
    void disableSensor();

    /**
     * @brief Poll the sensor driver and dispatch pending callbacks.
     */
    void loopSensor();

    /**
     * @brief Reset the step counter in local state and in the sensor when supported.
     */
    void resetStepCounter();

    /**
     * @brief Clear runtime counters while preserving the latest accelerometer sample.
     */
    void resetRuntimeStatus();

    /**
     * @brief Get the current runtime status snapshot.
     * @return Copy of the runtime status structure.
     */
    BMASensorRuntimeStatus_t getRuntimeStatus() const
    {
        return runtimeStatus;
    }

    /**
     * @brief Get the current step counter value.
     * @return Step count reported by the sensor helper.
     */
    uint32_t getStepCounter()
    {
        return runtimeStatus.stepCount;
    }

private:
    /**
     * @brief Register tap detection callbacks for the detected model.
     */
    void setupTapCallback();

    /**
     * @brief Register step detection and step counter callbacks.
     */
    void setupStepCallbacks();

    /**
     * @brief Register activity classification callbacks.
     */
    void setupActivityCallback();

    /**
     * @brief Register tilt detection callbacks.
     */
    void setupTiltCallback();

    /**
     * @brief Register any-motion and no-motion callbacks.
     */
    void setupMotionCallbacks();

    /**
     * @brief Register accelerometer data-ready callbacks.
     */
    void setupDataReadyCallback();

    /**
     * @brief Update cached accelerometer data and magnitude values.
     * @param data Latest accelerometer sample.
     */
    void updateAccelStatus(const AccelerometerData &data);

    /**
     * @brief Record an internal runtime event and update counters.
     * @param event Runtime event to record.
     */
    void recordRuntimeEvent(BMASensorRuntimeEvent event);

    /** Optional event manager used to emit normalized sensor events. */
    LilyGoEventManage *eventManage;
    /** true when a data-ready callback has fired. */
    volatile  bool dataReady = false;
    /** Cached sensor data and event counters. */
    BMASensorRuntimeStatus_t runtimeStatus;
};
