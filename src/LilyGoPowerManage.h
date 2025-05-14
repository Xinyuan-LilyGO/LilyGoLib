/**
 * @file      LilyGoPowerManage.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-03-19
 *
 */
#pragma once
#include <Arduino.h>

#ifdef USING_PMU_MANAGE

#include <XPowersLib.h>

class LilyGoPowerManage
{
public:
    LilyGoPowerManage(XPowersAXP2101 * pmu);

    /**
     * @brief Disable the charging function.
     *
     * This function disables the charging functionality of the device.
     */
    void disableCharge();

    /**
     * @brief Enable the charging function.
     *
     * This function enables the charging functionality of the device. The 'milliampere' parameter specifies the
     * charging current in milliamperes (default: 300mA).
     *
     * @param milliampere The charging current in milliamperes (default: 300).
     */
    void enableCharge(uint16_t milliampere = 300);

    /**
     * @brief Check if the charging function is enabled.
     *
     * This function checks whether the charging function is currently enabled. It returns 'true' if enabled,
     * and 'false' otherwise.
     *
     * @return bool True if the charging function is enabled, false otherwise.
     */
    bool isEnableCharge();

    /**
     * @brief Get the current charging current.
     *
     * This function retrieves the current charging current in milliamperes.
     *
     * @return uint16_t The current charging current in milliamperes.
     */
    uint16_t getChargeCurrent();

    /**
     * @brief Set the charging current.
     *
     * This function sets the charging current to the specified value in milliamperes.
     *
     * @param milliampere The charging current in milliamperes to set.
     */
    void setChargeCurrent(uint16_t milliampere);

    /**
     * @brief Disable power manager ADC measurement.
     */
    void disablePowerMeasure();

    /**
    * @brief Enable power manager ADC measurement.
    */
    void enablePowerMeasure();

private:
    XPowersAXP2101 *power;
};

#endif