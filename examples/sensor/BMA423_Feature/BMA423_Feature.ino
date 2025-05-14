/**
 * @file      BMA423_Feature.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#ifdef ARDUINO_T_WATCH_S3

lv_obj_t *label1;
lv_obj_t *label2;
lv_obj_t *label3;


void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    //Default 4G ,200HZ
    instance.sensor.configAccelerometer();

    instance.sensor.enableAccelerometer();

    instance.sensor.enablePedometer();


    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_center(cont);

    label1 = lv_label_create(cont);
    lv_label_set_text(label1, "INT MASK:0x00");

    label2 = lv_label_create(cont);
    lv_label_set_text(label2, "STEP COUNTER:0");

    label3 = lv_label_create(cont);
    lv_label_set_text(label3, "Feature: null");

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    instance.onEvent(device_event_cb);
}

void device_event_cb(DeviceEvent_t event, void*user_data)
{
    uint32_t stepCounter;
    switch (event) {
    case PMU_EVENT_KEY_CLICKED:
        instance.sensor.resetPedometer();
        lv_label_set_text_fmt(label2, "[%lu]STEP COUNTER:%u", millis() / 1000, 0);
        break;
    case SENSOR_STEPS_UPDATED:
        stepCounter = instance.sensor.getPedometerCounter();
        Serial.printf("Step count interrupt,step Counter:%u\n", stepCounter);
        lv_label_set_text_fmt(label2, "[%lu]STEP COUNTER:%u", millis() / 1000, stepCounter);
        break;
    case SENSOR_ACTIVITY_DETECTED:
        Serial.println("Activity event");
        lv_label_set_text_fmt(label3, "[%lu]Feature: Activity", millis() / 1000);
        break;
    case SENSOR_TILT_DETECTED:
        Serial.println("Tilt event");
        lv_label_set_text_fmt(label3, "[%lu]Feature: Tilt", millis() / 1000);
        break;
    case SENSOR_DOUBLE_TAP_DETECTED:
        Serial.println("DoubleTap event");
        lv_label_set_text_fmt(label3, "[%lu]Feature: DoubleTap", millis() / 1000);
        break;
    case SENSOR_ANY_MOTION_DETECTED:
        Serial.println("Any motion / no motion event");
        lv_label_set_text_fmt(label3, "[%lu]Feature: Any motion/NoMotion", millis() / 1000);
        break;
    default:
        break;
    }
}

void loop()
{
    instance.loop();
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
