/**
 * @file      GaugeExample.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 * 
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef USING_BQ_GAUGE

lv_obj_t *label1;

void setup()
{

    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label1,"Gauge example,all message output to serial");
    lv_obj_center(label1);

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    uint16_t newDesignCapacity = 800;
    uint16_t newFullChargeCapacity = 800;
    instance.gauge.setNewCapacity(newDesignCapacity, newFullChargeCapacity);

    instance.gauge.refresh();

    OperationConfig config = instance.gauge.getOperationConfig();
    //* OperationConfig A *//
    Serial.print("OperationConfigA Values:0x");
    Serial.println(config.getConfigA(), HEX);

    Serial.print("External Thermistor Selected: ");
    Serial.println(config.isTempsSet() ? "YES" : "NO");

    Serial.print("BATT_GD Pin Polarity High: ");
    Serial.println(config.isBatgPolHigh() ? "YES" : "NO");

    Serial.print("BATT_GD Function Enabled: ");
    Serial.println(config.isBatgEnEnabled() ? "YES" : "NO");

    Serial.print("Can Enter SLEEP State: ");
    Serial.println(config.canEnterSleep() ? "YES" : "NO");

    Serial.print("slpwakechg Function Enabled: ");
    Serial.println(config.isSlpwakechgEnabled() ? "YES" : "NO");

    Serial.print("Write Temperature Function Enabled: ");
    Serial.println(config.isWrtempEnabled() ? "YES" : "NO");

    Serial.print("Battery Insertion Detection Enabled: ");
    Serial.println(config.isBienableEnabled() ? "YES" : "NO");

    Serial.print("Battery Insertion Pin Pull - Up Enabled: ");
    Serial.println(config.isBlPupEnEnabled() ? "YES" : "NO");

    Serial.print("Pin Function Code (PFC) Mode: ");
    Serial.println(config.getPfcCfg());

    Serial.print("Wake - Up Function Enabled: ");
    Serial.println(config.isWakeEnEnabled() ? "YES" : "NO");

    Serial.print("Wake - Up Threshold 1: ");
    Serial.println(config.getWkTh1());

    Serial.print("Wake - Up Threshold 0: ");
    Serial.println(config.getWkTh0());

    //* OperationConfig B *//
    Serial.print("\nOperationConfigB Values:0x");
    Serial.println(config.getConfigB(), HEX);

    Serial.print("Default Seal Option Enabled: ");
    Serial.println(config.isDefaultSealEnabled() ? "YES" : "NO");

    Serial.print("Non - Removable Option Set: ");
    Serial.println(config.isNonRemovableSet() ? "YES" : "NO");

    Serial.print("INT_BREM Function Enabled: ");
    Serial.println(config.isIntBremEnabled() ? "YES" : "NO");

    Serial.print("INT_BATL Function Enabled: ");
    Serial.println(config.isIntBatLEnabled() ? "YES" : "NO");

    Serial.print("INT_STATE Function Enabled: ");
    Serial.println(config.isIntStateEnabled() ? "YES" : "NO");

    Serial.print("INT_OCV Function Enabled: ");
    Serial.println(config.isIntOcvEnabled() ? "YES" : "NO");

    Serial.print("INT_OT Function Enabled: ");
    Serial.println(config.isIntOtEnabled() ? "YES" : "NO");

    Serial.print("INT_POL Function Enabled (High - Level Polarity): ");
    Serial.println(config.isIntPolHigh() ? "YES" : "NO");

    Serial.print("INT_FOCV Function Enabled: ");
    Serial.println(config.isIntFocvEnabled() ? "YES" : "NO");

    delay(10000);
}


void loop()
{
    uint32_t startMeasTime = millis();

    lv_timer_handler();

    if (instance.gauge.refresh()) {

        uint32_t endMesTime = millis();

        Serial.print("Polling time: "); Serial.print(endMesTime - startMeasTime); Serial.println(" ms");

        Serial.println("\nStandard query:");
        Serial.print("\t- AtRate:"); Serial.print(instance.gauge.getAtRate()); Serial.println(" mA");
        Serial.print("\t- AtRateTimeToEmpty:"); Serial.print(instance.gauge.getAtRateTimeToEmpty()); Serial.println(" minutes");
        Serial.print("\t- Temperature:"); Serial.print(instance.gauge.getTemperature() ); Serial.println(" ℃");
        Serial.print("\t- BatteryVoltage:"); Serial.print(instance.gauge.getVoltage()); Serial.println(" mV");
        Serial.print("\t- InstantaneousCurrent:"); Serial.print(instance.gauge.getCurrent()); Serial.println(" mAh");
        Serial.print("\t- RemainingCapacity:"); Serial.print(instance.gauge.getRemainingCapacity()); Serial.println(" mAh");
        Serial.print("\t- FullChargeCapacity:"); Serial.print(instance.gauge.getFullChargeCapacity()); Serial.println(" mAh");
        Serial.print("\t- DesignCapacity:"); Serial.print(instance.gauge.getDesignCapacity()); Serial.println(" mAh");
        Serial.print("\t- TimeToEmpty:"); Serial.print(instance.gauge.getTimeToEmpty()); Serial.println(" minutes");
        Serial.print("\t- TimeToFull:"); Serial.print(instance.gauge.getTimeToFull()); Serial.println(" minutes");
        Serial.print("\t- StandbyCurrent:"); Serial.print(instance.gauge.getStandbyCurrent()); Serial.println(" mA");
        Serial.print("\t- StandbyTimeToEmpty:"); Serial.print(instance.gauge.getStandbyTimeToEmpty()); Serial.println(" minutes");
        Serial.print("\t- MaxLoadCurrent:"); Serial.print(instance.gauge.getMaxLoadCurrent()); Serial.println(" mA");
        Serial.print("\t- MaxLoadTimeToEmpty:"); Serial.print(instance.gauge.getMaxLoadTimeToEmpty()); Serial.println(" minute");
        Serial.print("\t- RawCoulombCount:"); Serial.print(instance.gauge.getRawCoulombCount()); Serial.println(" mAh");
        Serial.print("\t- AveragePower:"); Serial.print(instance.gauge.getAveragePower()); Serial.println(" mW");
        Serial.print("\t- InternalTemperature:"); Serial.print(instance.gauge.getInternalTemperature()); Serial.println(" ℃");
        Serial.print("\t- CycleCount:"); Serial.println(instance.gauge.getCycleCount());
        Serial.print("\t- StateOfCharge:"); Serial.print(instance.gauge.getStateOfCharge()); Serial.println(" %");
        Serial.print("\t- StateOfHealth:"); Serial.print(instance.gauge.getStateOfHealth()); Serial.println(" %");
        Serial.print("\t- RequestChargingVoltage:"); Serial.print(instance.gauge.getRequestChargingVoltage()); Serial.println(" mV");
        Serial.print("\t- RequestChargingCurrent:"); Serial.print(instance.gauge.getRequestChargingCurrent()); Serial.println(" mA");
        Serial.print("\t- BTPDischargeSet:"); Serial.print(instance.gauge.getBTPDischargeSet()); Serial.println(" mAh");
        Serial.print("\t- BTPChargeSet:"); Serial.print(instance.gauge.getBTPChargeSet()); Serial.println(" mAh");
        FuelGaugeOperationStatus status = instance.gauge.getOperationStatus();
        BatteryStatus batteryStatus = instance.gauge.getBatteryStatus();

        Serial.println("\nOperation Status:");
        Serial.print("\t- getIsConfigUpdateMode:"); Serial.println(status.getIsConfigUpdateMode() ? "YES" : "NO");
        Serial.print("\t- getIsBtpThresholdExceeded:"); Serial.println(status.getIsBtpThresholdExceeded() ? "YES" : "NO");
        Serial.print("\t- getIsCapacityAccumulationThrottled:"); Serial.println(status.getIsCapacityAccumulationThrottled() ? "YES" : "NO");
        Serial.print("\t- getIsInitializationComplete:"); Serial.println(status.getIsInitializationComplete() ? "YES" : "NO");
        Serial.print("\t- getIsDischargeCycleCompliant:"); Serial.println(status.getIsDischargeCycleCompliant() ? "YES" : "NO");
        Serial.print("\t- getIsBatteryVoltageBelowEdv2:"); Serial.println(status.getIsBatteryVoltageBelowEdv2() ? "YES" : "NO");
        Serial.print("\t- getSecurityAccessLevel:"); Serial.println(status.getSecurityAccessLevel());
        Serial.print("\t- getIsCalibrationModeEnabled:"); Serial.println(status.getIsCalibrationModeEnabled() ? "YES" : "NO");

        Serial.println("\nBattery Status:");
        if (batteryStatus.isFullDischargeDetected()) {
            Serial.println("\t- Full discharge detected.");
        }
        if (batteryStatus.isOcvMeasurementUpdateComplete()) {
            Serial.println("\t- OCV measurement update is complete.");
        }
        if (batteryStatus.isOcvReadFailedDueToCurrent()) {
            Serial.println("\t- Status bit indicating that an OCV read failed due to current.");
            Serial.println("\tThis bit can only be set if a battery is present after receiving an OCV_CMD().");
        }
        if (batteryStatus.isInSleepMode()) {
            Serial.println("\t- The device operates in SLEEP mode");
        }
        if (batteryStatus.isOverTemperatureDuringCharging()) {
            Serial.println("\t- Over-temperature is detected during charging.");
        }
        if (batteryStatus.isOverTemperatureDuringDischarge()) {
            Serial.println("\t- Over-temperature detected during discharge condition.");
        }
        if (batteryStatus.isFullChargeDetected()) {
            Serial.println("\t- Full charge detected.");
        }
        if (batteryStatus.isChargeInhibited()) {
            Serial.println("\t- Charge Inhibit: If set, indicates that charging should not begin because the Temperature() is outside the range");
            Serial.println("\t[Charge Inhibit Temp Low, Charge Inhibit Temp High]. ");
        }
        if (batteryStatus.isChargingTerminationAlarm()) {
            Serial.println("\t- Termination of charging alarm. This flag is set and cleared based on the selected SOC Flag Config A option.");
        }
        if (batteryStatus.isGoodOcvMeasurement()) {
            Serial.println("\t- A good OCV measurement was made.");
        }
        if (batteryStatus.isBatteryInserted()) {
            Serial.println("\t- Detects inserted battery.");
        }
        if (batteryStatus.isBatteryPresent()) {
            Serial.println("\t- Battery presence detected.");
        }
        if (batteryStatus.isDischargeTerminationAlarm()) {
            Serial.println("\t- Termination discharge alarm. This flag is set and cleared according to the selected SOC Flag Config A option.");
        }
        if (batteryStatus.isSystemShutdownRequired()) {
            Serial.println("\t- System shutdown bit indicating that the system should be shut down. True when set. If set, the SOC_INT pin toggles once.");
        }
        if (batteryStatus.isInDischargeMode()) {
            Serial.println("\t- When set, the device is in DISCHARGE mode; when cleared, the device is in CHARGING or RELAXATION mode.");
        }
        Serial.println("===============================================");

    }
    delay(3000);
}

#else


void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support  T-LoRa-Pager"); delay(1000);
}

#endif

