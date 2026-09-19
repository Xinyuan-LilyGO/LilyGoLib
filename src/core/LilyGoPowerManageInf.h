/**
 * @file      LilyGoPowerManageInf.h
 * @brief     Defines the common PMIC and battery-gauge power management interface.
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-06-01
 *
 */

#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <PmicTI.hpp>
#include <PmicXPowers.hpp>
#include "LilyGoTypedef.h"

/**
 * @brief Source that produced a power metric value.
 */
enum LilyGoPowerMetricSource {
    LILYGO_POWER_SRC_NONE = 0,       /**< Metric is not available. */
    LILYGO_POWER_SRC_PMU,            /**< Metric came from the PMIC ADC or charger. */
    LILYGO_POWER_SRC_EXTERNAL_GAUGE, /**< Metric came from an external fuel gauge. */
    LILYGO_POWER_SRC_INTERNAL_GAUGE, /**< Metric came from an internal PMIC gauge. */
    LILYGO_POWER_SRC_ESTIMATED,      /**< Metric was estimated by software. */
    LILYGO_POWER_SRC_ADC,            /**< Metric came from a board-level ADC input. */
};

/**
 * @brief One measured or estimated power metric.
 */
struct LilyGoPowerMetric {
    bool valid = false;                                      /**< true when value is usable. */
    float value = 0.0f;                                      /**< Metric value in the unit implied by the field name. */
    LilyGoPowerMetricSource source = LILYGO_POWER_SRC_NONE;  /**< Source of the metric value. */
};

/**
 * @brief Aggregated PMIC, charger, and fuel-gauge state.
 */
struct LilyGoPowerSnapshot {
    uint32_t onlineMask = 0;                       /**< Hardware online mask from getDeviceProbe(). */
    char pmicName[32] = {0};                       /**< PMIC display name. */
    char gaugeName[32] = {0};                      /**< Fuel-gauge display name. */
    bool pmuPresent = false;                       /**< true when a PMIC is online. */
    bool externalGaugePresent = false;             /**< true when an external fuel gauge is online. */
    bool fuelGaugePresent = false;                 /**< true when any battery percentage source is available. */
    bool batteryPresentValid = false;              /**< true when batteryPresent is reliable. */
    bool batteryPresent = false;                   /**< true when a battery is detected. */
    bool vbusPresentValid = false;                 /**< true when vbusPresent is reliable. */
    bool vbusPresent = false;                      /**< true when USB/VBUS input is present. */
    bool charging = false;                         /**< true when charging is active. */
    bool chargeDone = false;                       /**< true when charging is complete. */
    bool chargeFault = false;                      /**< true when the charger reports a fault. */
    bool chargeEnabledValid = false;               /**< true when chargeEnabled is reliable. */
    bool chargeEnabled = false;                    /**< true when charging is enabled. */
    bool otgSupported = false;                     /**< true when OTG output is supported. */
    bool otgEnabled = false;                       /**< true when OTG output is enabled. */
    char chargeState[32] = {0};                    /**< Human-readable charge state. */
    char ntcState[32] = {0};                       /**< Human-readable NTC / temperature state. */
    LilyGoPowerMetric vbusMv;                      /**< VBUS voltage in millivolts. */
    LilyGoPowerMetric vbusMa;                      /**< VBUS current in milliamperes. */
    LilyGoPowerMetric sysMv;                       /**< System voltage in millivolts. */
    LilyGoPowerMetric batteryMv;                   /**< Battery voltage in millivolts. */
    LilyGoPowerMetric batteryMa;                   /**< Battery current in milliamperes. */
    LilyGoPowerMetric batteryPercent;              /**< Battery state of charge in percent. */
    LilyGoPowerMetric temperatureC;                /**< PMIC die temperature in degrees Celsius. */
    LilyGoPowerMetric batteryTemperatureC;         /**< Battery temperature in degrees Celsius. */
    LilyGoPowerMetric instantaneousPowerW;         /**< Instantaneous battery power in watts. */
    LilyGoPowerMetric averagePowerMw;              /**< Average battery power in milliwatts. */
    LilyGoPowerMetric remainingCapacityMah;        /**< Remaining battery capacity in mAh. */
    LilyGoPowerMetric fullChargeCapacityMah;       /**< Full-charge battery capacity in mAh. */
    LilyGoPowerMetric designCapacityMah;           /**< Battery design capacity in mAh. */
    LilyGoPowerMetric standbyCurrentMa;            /**< Standby current in milliamperes. */
    LilyGoPowerMetric maxLoadCurrentMa;            /**< Maximum load current in milliamperes. */
    LilyGoPowerMetric timeToEmptyMin;              /**< Estimated time to empty in minutes. */
    LilyGoPowerMetric timeToFullMin;               /**< Estimated time to full in minutes. */
};

/**
 * @brief Normalized PMIC interrupt mask bits.
 */
enum PowerEvent {
    IRQ_VBUS_INSERT = _BV(0),       /**< USB/VBUS insertion interrupt. */
    IRQ_VBUS_REMOVE = _BV(1),       /**< USB/VBUS removal interrupt. */
    IRQ_BAT_CHG_START = _BV(2),     /**< Battery charge start interrupt. */
    IRQ_BAT_CHG_DONE = _BV(3),      /**< Battery charge complete interrupt. */
    IRQ_PEKEY_CLICKED = _BV(4),     /**< PMIC key short-click interrupt. */
    IRQ_PEKEY_LONG_PRESSED = _BV(5), /**< PMIC key long-press interrupt. */
    IRQ_BAT_REMOVE = _BV(6),        /**< Battery removal interrupt. */
    IRQ_BAT_INSERT = _BV(7),        /**< Battery insertion interrupt. */
    IRQ_BAT_TEMP_HIGH = _BV(8),     /**< Battery high-temperature interrupt. */
    IRQ_BAT_TEMP_LOW = _BV(9)       /**< Battery low-temperature interrupt. */
};

/**
 * @brief Maps normalized PMIC events to PMIC-specific interrupt bits.
 */
struct IrqMapEntry {
    uint64_t powerEvent; /**< Normalized PowerEvent bit. */
    uint64_t axp202Irq;  /**< AXP202 interrupt bit. */
    uint64_t axp2101Irq; /**< AXP2101 interrupt bit. */
};

/**
 * @brief Lookup table used to translate normalized PMIC IRQ bits.
 */
static constexpr IrqMapEntry irqMapTable[] = {
    { IRQ_VBUS_INSERT,       AXP202Irq::IRQ_VBUS_INSERT,       AXP2101Irq::IRQ_VBUS_INSERT },
    { IRQ_VBUS_REMOVE,       AXP202Irq::IRQ_VBUS_REMOVE,       AXP2101Irq::IRQ_VBUS_REMOVE },
    { IRQ_BAT_CHG_START,     AXP202Irq::IRQ_BAT_CHG_START,     AXP2101Irq::IRQ_BAT_CHG_START },
    { IRQ_BAT_CHG_DONE,      AXP202Irq::IRQ_BAT_CHG_DONE,      AXP2101Irq::IRQ_BAT_CHG_DONE },
    { IRQ_PEKEY_CLICKED,     AXP202Irq::IRQ_PEKEY_SHORT_PRESS, AXP2101Irq::IRQ_PEKEY_SHORT_PRESS },
    { IRQ_PEKEY_LONG_PRESSED,  AXP202Irq::IRQ_PEKEY_LONG_PRESS,  AXP2101Irq::IRQ_PEKEY_LONG_PRESS },
    { IRQ_BAT_REMOVE,        AXP202Irq::IRQ_BAT_REMOVE,        AXP2101Irq::IRQ_BAT_REMOVE },
    { IRQ_BAT_INSERT,        AXP202Irq::IRQ_BAT_INSERT,        AXP2101Irq::IRQ_BAT_INSERT },
    {IRQ_BAT_TEMP_HIGH,     AXP202Irq::IRQ_BAT_TEMP_HIGH,     AXP2101Irq::IRQ_BAT_OVER_TEMP_WORK },
    {IRQ_BAT_TEMP_LOW,      AXP202Irq::IRQ_BAT_TEMP_LOW,      AXP2101Irq::IRQ_BAT_UNDER_TEMP_WORK },
};

/**
 * @brief Power management interface class for LilyGo devices.
 *
 * This class provides a unified interface for power management functions including
 * OTG control, charging management, voltage monitoring, and battery status reporting.
 * It wraps the underlying PMIC (Power Management IC) hardware and provides virtual
 * methods that can be overridden by specific device implementations.
 */
class LilyGoPowerManageInf
{
public:
    /**
     * @brief Construct a new LilyGoPowerManageInf object.
     *
     * @param pmic Reference to the PMIC base object for hardware access.
     * @param type PMIC type represented by this interface.
     */
    LilyGoPowerManageInf(PmicBase &pmic, PmicType type) : pmic(pmic), type(type)
    {
    }

    /**
     * @brief Destroy the LilyGoPowerManageInf object.
     */
    ~LilyGoPowerManageInf() {}

    /**
     * @brief Check whether USB/VBUS input is present.
     * @return true when the charger reports VBUS present.
     */
    virtual bool isUsbIn()
    {
        return pmic.getCharger()->getStatus().vbusPresent;
    }


    /**
     * @brief Check if the device supports OTG (On-The-Go) functionality.
     *
     * @return bool True if OTG is supported, false otherwise.
     */
    virtual bool hasOTG()
    {
        return false;
    }

    /**
     * @brief Check if the device has a battery gauge IC.
     *
     * @return bool True if battery gauge is available, false otherwise.
     */
    virtual bool hasGauge()
    {
        return false;
    }

    /**
     * @brief Get the board hardware online mask.
     * @return Hardware presence mask made from HW_*_ONLINE bits.
     */
    virtual uint32_t getDeviceProbe()
    {
        return 0;
    }

    /**
     * @brief Check if OTG power output is currently enabled.
     *
     * @return bool True if OTG is enabled, false otherwise.
     */
    virtual bool isOTGEnabled()
    {
        return false;
    }

    /**
     * @brief Enable OTG power output.
     *
     * @return bool True if OTG was enabled successfully, false otherwise.
     */
    virtual bool enableOTG()
    {
        return false;
    }

    /**
     * @brief Disable OTG power output.
     *
     * @return bool True if OTG was disabled successfully, false otherwise.
     */
    virtual bool disableOTG()
    {
        return false;
    }


    /**
     * @brief Check if charging is currently enabled.
     *
     * @return bool True if charging is enabled, false otherwise.
     */
    bool isEnableCharge()
    {
        return true;
    }

    /**
     * @brief Enable battery charging.
     *
     * @return bool True if charging was enabled successfully, false otherwise.
     */
    bool enableCharge()
    {
        return pmic.getCharger()->enableCharging(true);
    }

    /**
     * @brief Disable battery charging.
     *
     * @return bool True if charging was disabled successfully, false otherwise.
     */
    bool disableCharge()
    {
        return pmic.getCharger()->enableCharging(false);
    }

    /**
     * @brief Get the current fast charge current setting.
     *
     * @return uint16_t The charge current in milliamperes.
     */
    uint16_t getChargeCurrent()
    {
        return pmic.getCharger()->getFastChargeCurrent();
    }

    /**
     * @brief Set the fast charge current.
     *
     * @param milliampere The desired charge current in milliamperes.
     */
    void setChargeCurrent(uint16_t milliampere)
    {
        pmic.getCharger()->setFastChargeCurrent(milliampere);
    }

    /**
     * @brief Get the charge current configuration parameters.
     *
     * @param[out] minMilliampere Minimum charge current in milliamperes.
     * @param[out] maxMilliampere Maximum charge current in milliamperes.
     * @param[out] step Current step size in milliamperes.
     * @param[out] steps Number of available steps.
     */
    void getChargeConfig(uint16_t &minMilliampere, uint16_t &maxMilliampere, uint16_t &step, uint16_t &steps)
    {
        minMilliampere = pmic.getConfig().chargeCurrentMin;
        maxMilliampere = pmic.getConfig().chargeCurrentMax;
        step = pmic.getConfig().chargeCurrentStep;
        if (step == 0) {
            step = pmic.getConfig().chargeCurrentMax / pmic.getConfig().chargeCurrentSteps;
        }
        steps = pmic.getConfig().chargeCurrentSteps - 1;
    }

    /**
     * @brief Check if the battery is currently charging.
     *
     * @return bool True if charging is in progress, false otherwise.
     */
    virtual bool isCharging()
    {
        return pmic.getCharger()->isCharging();
    }


    /**
     * @brief Check whether the battery is currently discharging.
     * @return true if discharging is detected.
     */
    virtual bool isDischarge()
    {
        return false;
    }

    /**
     * @brief Get the PMIC charger status enum.
     * @return Charging status reported by getChargeStatus().
     */
    virtual PmicChargerBase::ChargingStatus getChgStatus()
    {
        return getChargeStatus().chargingStatus;
    }

    /**
     * @brief Get the full PMIC charger status structure.
     * @return Charger status, or a default status if no charger interface exists.
     */
    virtual PmicChargerBase::Status getChargeStatus()
    {
        PmicChargerBase *charger = pmic.getCharger();
        return charger ? charger->getStatus() : PmicChargerBase::Status();
    }

    /**
     * @brief Get the PMIC type for this interface.
     * @return PMIC type enum.
     */
    PmicType getPmicType() const
    {
        return type;
    }

    /**
     * @brief Read one PMIC ADC channel.
     * @param channel ADC channel to read.
     * @param value Receives the ADC value.
     * @return true if the ADC read succeeded.
     */
    bool readPowerAdc(PmicAdcBase::Channel channel, float &value)
    {
        return pmic.getAdc().read(channel, value);
    }

    /**
     * @brief Read a complete power status snapshot.
     * @param snapshot Receives the current power status.
     * @return true if at least one power information source is available.
     */
    virtual bool readPowerSnapshot(LilyGoPowerSnapshot &snapshot)
    {
        snapshot = LilyGoPowerSnapshot();
        copyPowerLabel(snapshot.chargeState, sizeof(snapshot.chargeState), "Unavailable");
        copyPowerLabel(snapshot.ntcState, sizeof(snapshot.ntcState), "Unknown");

        snapshot.onlineMask = getDeviceProbe();
        snapshot.pmuPresent = snapshot.onlineMask & HW_PMU_ONLINE;
        snapshot.externalGaugePresent = hasGauge() && (snapshot.onlineMask & HW_GAUGE_ONLINE);
        copyPowerLabel(snapshot.pmicName, sizeof(snapshot.pmicName), getPowerPmicName(getPmicType()));

        if (snapshot.pmuPresent) {
            PmicChargerBase::Status chargeStatus = getChargeStatus();
            snapshot.vbusPresentValid = chargeStatus.online;
            snapshot.vbusPresent = chargeStatus.vbusPresent;
            snapshot.batteryPresentValid = chargeStatus.online && getPmicType() != PMIC_TYPE_BQ25896;
            snapshot.batteryPresent = chargeStatus.batteryPresent;
            snapshot.charging = chargeStatus.charging;
            snapshot.chargeDone = chargeStatus.chargeDone;
            snapshot.chargeFault = chargeStatus.fault;
            copyPowerLabel(snapshot.chargeState, sizeof(snapshot.chargeState), getPowerChargeStateName(chargeStatus));
            snapshot.otgSupported = hasOTG();
            snapshot.otgEnabled = snapshot.otgSupported && isOTGEnabled();

            readPowerAdcMetric(PmicAdcBase::Channel::VBUS_VOLTAGE, snapshot.vbusMv, LILYGO_POWER_SRC_PMU);
            readPowerAdcMetric(PmicAdcBase::Channel::VSYS_VOLTAGE, snapshot.sysMv, LILYGO_POWER_SRC_PMU);
            readPowerAdcMetric(PmicAdcBase::Channel::BAT_VOLTAGE, snapshot.batteryMv, LILYGO_POWER_SRC_PMU);
            readPowerAdcMetric(PmicAdcBase::Channel::BAT_PERCENTAGE, snapshot.batteryPercent, LILYGO_POWER_SRC_INTERNAL_GAUGE);
            if (snapshot.batteryPercent.value > 100 || snapshot.batteryPercent.value == -1) {
                snapshot.batteryPercent.value = 0;
            }

            if (getPmicType() == PMIC_TYPE_AXP202) {
                readPowerAdcMetric(PmicAdcBase::Channel::DIE_TEMPERATURE, snapshot.temperatureC, LILYGO_POWER_SRC_PMU);
                readPowerAdcMetric(PmicAdcBase::Channel::VBUS_CURRENT, snapshot.vbusMa, LILYGO_POWER_SRC_PMU);
                readPowerAdcMetric(PmicAdcBase::Channel::BAT_CURRENT, snapshot.batteryMa, LILYGO_POWER_SRC_PMU);
            } else if (getPmicType() == PMIC_TYPE_AXP2101) {
                readPowerAdcMetric(PmicAdcBase::Channel::DIE_TEMPERATURE, snapshot.temperatureC, LILYGO_POWER_SRC_PMU);
            }

            if (snapshot.batteryMv.valid && !snapshot.batteryPresentValid) {
                snapshot.batteryPresentValid = true;
                snapshot.batteryPresent = snapshot.batteryMv.value > 2500.0f;
            }
        }

        if (snapshot.externalGaugePresent) {
            copyPowerLabel(snapshot.gaugeName, sizeof(snapshot.gaugeName), "External gauge");
        } else {
            snapshot.fuelGaugePresent = snapshot.batteryPercent.valid;
        }

        if (snapshot.gaugeName[0] == '\0') {
            copyPowerLabel(snapshot.gaugeName, sizeof(snapshot.gaugeName),
                           snapshot.fuelGaugePresent ? "PMU internal" : "None");
        }

        return snapshot.pmuPresent || snapshot.externalGaugePresent || snapshot.fuelGaugePresent;
    }

    /**
     * @brief Get the VBUS voltage.
     *
     * @return float The VBUS voltage in volts.
     */
    virtual float getVbusVoltage()
    {
        float val = 0;
        pmic.getAdc().read(PmicAdcBase::Channel::VBUS_VOLTAGE, val);
        return val;
    }

    /**
     * @brief Get the system voltage.
     *
     * @return float The system voltage in volts.
     */
    virtual float getSysVoltage()
    {
        float val = 0;
        pmic.getAdc().read(PmicAdcBase::Channel::VSYS_VOLTAGE, val);
        return val;
    }

    /**
     * @brief Get the battery voltage.
     *
     * @return float The battery voltage in volts.
     */
    virtual float getBattVoltage()
    {
        float val = 0;
        pmic.getAdc().read(PmicAdcBase::Channel::BAT_VOLTAGE, val);
        return val;
    }

    /**
     * @brief Get the battery percentage.
     *
     * @return float The battery level as a percentage (0-100).
     */
    virtual float getBatteryPercent()
    {
        float val = 0;
        pmic.getAdc().read(PmicAdcBase::Channel::BAT_PERCENTAGE, val);
        return val;
    }

    /**
     * @brief Get the die temperature of the PMIC.
     *
     * @return float The temperature in degrees Celsius.
     */
    virtual float getTemperature()
    {
        float val = 0;
        pmic.getAdc().read(PmicAdcBase::Channel::DIE_TEMPERATURE, val);
        return val;
    }

    /**
     * @brief Check if the hardware adapter is connected.
     * @return True if the hardware adapter is connected, false otherwise.
     */
    virtual bool isAdapterConnected()
    {
        return pmic.getCharger()->getStatus().vbusPresent;
    }

    /**
     * @brief Shutdown the device.
     *
     * @return bool True if shutdown was successful, false otherwise.
     */
    virtual bool shutdown()
    {
        return false;
    }

    /**
     * @brief Convert charge level to charge current value.
     *
     * @param level The charge level to convert.
     * @return uint16_t The corresponding charge current in milliamperes.
     */
    virtual uint16_t getChargeLevelToCurrent(uint8_t level)
    {
        return getChargeLevelToCurrentImpl(level);
    }

    /**
     * @brief Convert current charge current to a level value.
     *
     * @return uint16_t The corresponding charge level.
     */
    virtual uint16_t getChargeCurrentToLevel()
    {
        return getChargeCurrentToLevelImpl();
    }

    /**
     * @brief Disable the PMIC's automatic shutdown on a long power-key press.
     * @return true if the PMIC supports the operation and it was applied successfully.
     */
    virtual bool disableLongPressShutdown()
    {
        switch (type) {
        case PMIC_TYPE_AXP202:
            static_cast<PmicAXP202 &>(pmic).power().disableLongPressShutdown();
            return true;
        case PMIC_TYPE_AXP2101:
            static_cast<PmicAXP2101 &>(pmic).power().disableLongPressShutdown();
            return true;
        default:
            return false;
        }
    }


    /**
     * @brief  Enable or disable a specific power event.
     * @note   This function allows you to enable or disable power events for the device.
     * @param  event The power event to enable or disable.
     * @param  enable true to enable the event, false to disable it.
     * @return true if the operation was successful, false otherwise.
     */
    virtual bool enablePowerEvent(uint64_t event, bool enable)
    {
        return enablePowerEvent(static_cast<PowerEvent>(event), enable);
    }

    /**
     * @brief Enable a specific power event for interrupt handling.
     *
     * @param event The power event to enable.
     * @param enable true to enable the event, false to disable it.
     * @return bool true if the event setting was applied successfully.
     */
    virtual bool enablePowerEvent(PowerEvent event, bool enable)
    {
        PmicIrqBase *irq = pmic.getIrq();
        if (!irq) {
            return false;
        }
        uint64_t mask = 0;
        for (const auto &entry : irqMapTable) {
            if (event & entry.powerEvent) {
                switch (type) {
                case PMIC_TYPE_AXP202:
                    mask |= entry.axp202Irq;
                    break;
                case PMIC_TYPE_AXP2101:
                    mask |= entry.axp2101Irq;
                    break;
                default:
                    break;
                }
            }
        }

        if (mask == 0) {
            return false;
        }

        return enable ? irq->enable(mask) : irq->disable(mask);
    }

protected:
    /**
     * @brief Copy a power-status label into a fixed-size destination buffer.
     * @param dst Destination buffer.
     * @param dstSize Destination buffer size in bytes.
     * @param src Source string, or nullptr for an empty label.
     */
    static void copyPowerLabel(char *dst, size_t dstSize, const char *src)
    {
        if (!dst || dstSize == 0) {
            return;
        }
        snprintf(dst, dstSize, "%s", src ? src : "");
    }

    /**
     * @brief Store a finite metric value and mark it valid.
     * @param metric Metric to update.
     * @param value Metric value.
     * @param source Source of the metric value.
     */
    static void setPowerMetric(LilyGoPowerMetric &metric,
                               float value,
                               LilyGoPowerMetricSource source)
    {
        if (isnan(value) || isinf(value)) {
            return;
        }
        metric.valid = true;
        metric.value = value;
        metric.source = source;
    }

    /**
     * @brief Read a PMIC ADC channel into a LilyGoPowerMetric.
     * @param channel ADC channel to read.
     * @param metric Metric to update.
     * @param source Source tag to store when the read succeeds.
     * @return true if the metric contains a valid value after reading.
     */
    bool readPowerAdcMetric(PmicAdcBase::Channel channel,
                            LilyGoPowerMetric &metric,
                            LilyGoPowerMetricSource source)
    {
        float value = NAN;
        if (!readPowerAdc(channel, value)) {
            return false;
        }
        setPowerMetric(metric, value, source);
        return metric.valid;
    }

private:
    /**
     * @brief Get a display name for a PMIC type.
     * @param type PMIC type.
     * @return Static PMIC name string.
     */
    static const char *getPowerPmicName(PmicType type)
    {
        switch (type) {
        case PMIC_TYPE_AXP2101:
            return "AXP2101";
        case PMIC_TYPE_AXP202:
            return "AXP202";
        case PMIC_TYPE_BQ25896:
            return "BQ25896";
        default:
            return "PMIC";
        }
    }

    /**
     * @brief Convert a charger status to a display label.
     * @param status Charger status structure.
     * @return Static status label.
     */
    static const char *getPowerChargeStateName(const PmicChargerBase::Status &status)
    {
        if (!status.online) {
            return "Unavailable";
        }
        if (status.fault) {
            return "Fault";
        }
        if (status.chargeDone) {
            return "Full";
        }
        switch (status.chargingStatus) {
        case PmicChargerBase::ChargingStatus::PRE_CHARGE:
            return "Pre-charge";
        case PmicChargerBase::ChargingStatus::FAST_CHARGE:
            return "Fast charging";
        case PmicChargerBase::ChargingStatus::TERMINATION:
            return "Termination";
        case PmicChargerBase::ChargingStatus::NO_CHARGING:
            return status.vbusPresent ? "USB idle" : "Discharging";
        default:
            return status.vbusPresent ? "USB present" : "Unknown";
        }
    }

private:

    /**
     * @brief Implementation of charge level to current conversion.
     *
     * @param level The charge level to convert.
     * @return uint16_t The corresponding charge current in milliamperes.
     */
    virtual uint16_t getChargeLevelToCurrentImpl(uint8_t level) = 0;

    /**
     * @brief Implementation of charge current to level conversion.
     *
     * @return uint16_t The corresponding charge level.
     */
    virtual uint16_t getChargeCurrentToLevelImpl() = 0;

    /** @brief Reference to the PMIC hardware interface. */
    PmicBase &pmic;
    /** @brief PMIC type used to map common operations to chip-specific behavior. */
    PmicType type;
};
