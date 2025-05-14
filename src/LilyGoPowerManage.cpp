/**
 * @file      LilyGoPowerManage.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-02
 *
 */

#include "LilyGoPowerManage.h"

#ifdef USING_PMU_MANAGE

static const uint16_t table[17] = {
    0, 0, 0, 0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000
};

LilyGoPowerManage::LilyGoPowerManage(XPowersAXP2101 * pmu) : power(pmu)
{
}

void LilyGoPowerManage::disableCharge()
{
    power->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_0MA);
}

void LilyGoPowerManage::enableCharge(uint16_t milliampere)
{
    setChargeCurrent(milliampere);
}

bool LilyGoPowerManage::isEnableCharge()
{
    return power->getChargerConstantCurr() != XPOWERS_AXP2101_CHG_CUR_0MA;
}

uint16_t LilyGoPowerManage::getChargeCurrent()
{
    uint8_t val = power->getChargerConstantCurr();
    if (val > (sizeof(table) / sizeof(table[0]))) {
        return 0;
    }
    return  (table[val]);
}

void LilyGoPowerManage::setChargeCurrent(uint16_t milliampere)
{
    uint8_t val = 0;
    size_t tableSize = sizeof(table) / sizeof(table[0]);

    if (milliampere < 1000) {
        for (size_t i = 0; i < tableSize - 1; ++i) {
            if (milliampere >= table[i] && milliampere < table[i + 1]) {
                val = static_cast<uint8_t>(i);
                break;
            }
        }
        if (milliampere >= table[tableSize - 2]) {
            val = static_cast<uint8_t>(tableSize - 2);
        }
        power->setChargerConstantCurr(val);
    } else {
        power->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_1000MA);
    }
}

void LilyGoPowerManage::disablePowerMeasure()
{
    power->disableBattDetection();
    power->disableVbusVoltageMeasure();
    power->disableBattVoltageMeasure();
    power->disableSystemVoltageMeasure();
    power->disableTemperatureMeasure();
}

void LilyGoPowerManage::enablePowerMeasure()
{
    power->enableBattDetection();
    power->enableVbusVoltageMeasure();
    power->enableBattVoltageMeasure();
    power->enableSystemVoltageMeasure();
    power->enableTemperatureMeasure();
}

#endif /*#ifdef USING_PMU_MANAGE*/