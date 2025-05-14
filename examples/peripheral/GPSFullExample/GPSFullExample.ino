/**
 * @file      GPSFullExample.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-10
 *
 */


#include <LilyGoLib.h>
#include <LV_Helper.h>

#define SerialGPS  Serial1

lv_obj_t *label;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    // Enable debug message output
    // Serial.setDebugOutput(true);

    beginLvglHelper(instance);

    Serial.println(F("FullExample.ino"));
    Serial.println(F("An extensive example of many interesting TinyGPSPlus features"));
    Serial.print(F("Testing TinyGPSPlus library v. ")); Serial.println(TinyGPSPlus::libraryVersion());
    Serial.println(F("by Mikal Hart"));
    Serial.println();
    Serial.println(F("Sats HDOP  Latitude   Longitude   Fix  Date       Time     Date Alt    Course Speed Card  Distance Course Card  Chars Sentences Checksum"));
    Serial.println(F("           (deg)      (deg)       Age                      Age  (m)    --- from GPS ----  ---- to London  ----  RX    RX        Fail"));
    Serial.println(F("----------------------------------------------------------------------------------------------------------------------------------------"));

    label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);


    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

}

void loop()
{
    uint32_t  satellites = instance.gps.satellites.isValid() ? instance.gps.satellites.value() : 0;
    double hdop = instance.gps.hdop.isValid() ? instance.gps.hdop.hdop() : 0;
    double lat = instance.gps.location.isValid() ? instance.gps.location.lat() : 0;
    double lng = instance.gps.location.isValid() ? instance.gps.location.lng() : 0;
    uint32_t age = instance.gps.location.isValid() ? instance.gps.location.age() : 0;
    uint16_t year = instance.gps.date.isValid() ? instance.gps.date.year() : 0;
    uint8_t  month = instance.gps.date.isValid() ? instance.gps.date.month() : 0;
    uint8_t  day = instance.gps.date.isValid() ? instance.gps.date.day() : 0;
    uint8_t  hour = instance.gps.time.isValid() ? instance.gps.time.hour() : 0;
    uint8_t  minute = instance.gps.time.isValid() ? instance.gps.time.minute() : 0;
    uint8_t  second = instance.gps.time.isValid() ? instance.gps.time.second() : 0;
    double  meters = instance.gps.altitude.isValid() ? instance.gps.altitude.meters() : 0;
    double  kmph = instance.gps.speed.isValid() ? instance.gps.speed.kmph() : 0;
    lv_label_set_text_fmt(label, "Fix:%u\nSats:%u\nHDOP:%.1f\nLat:%.5f\nLon:%.5f \nDate:%d/%d/%d \nTime:%d/%d/%d\nAlt:%.2f m \nSpeed:%.2f\nRX:%u",
                          age, satellites, hdop, lat, lng,  year, month, day, hour, minute, second, meters, kmph, instance.gps.charsProcessed());
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 5, 20);

    Serial.printf(
        "Fix:%u  Sats:%u  HDOP:%.1f  Lat:%.5f  Lon:%.5f   Date:%d/%d/%d   Time:%d/%d/%d  Alt:%.2f m   Speed:%.2f  RX:%u\n",
        age, satellites, hdop, lat, lng,  year, month, day, hour, minute, second, meters, kmph, instance.gps.charsProcessed());

    smartDelay(1000);

    lv_task_handler();
}

// This custom version of delay() ensures that the gps object
// is being "fed".
static void smartDelay(unsigned long ms)
{
    unsigned long start = millis();
    do {
        // read message from GPSSerial
        while (SerialGPS.available()) {
            int r = SerialGPS.read();
            instance.gps.encode(r);
            Serial.write(r);
        }
    } while (millis() - start < ms);
}

static void printFloat(float val, bool valid, int len, int prec)
{
    if (!valid) {
        while (len-- > 1)
            Serial.print('*');
        Serial.print(' ');
    } else {
        Serial.print(val, prec);
        int vi = abs((int)val);
        int flen = prec + (val < 0.0 ? 2 : 1); // . and -
        flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
        for (int i = flen; i < len; ++i)
            Serial.print(' ');
    }
    smartDelay(0);
}

static void printInt(unsigned long val, bool valid, int len)
{
    char sz[32] = "*****************";
    if (valid)
        sprintf(sz, "%ld", val);
    sz[len] = 0;
    for (int i = strlen(sz); i < len; ++i)
        sz[i] = ' ';
    if (len > 0)
        sz[len - 1] = ' ';
    Serial.print(sz);
    smartDelay(0);
}

static void printDateTime(TinyGPSDate &d, TinyGPSTime &t)
{
    if (!d.isValid()) {
        Serial.print(F("********** "));
    } else {
        char sz[32];
        sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
        Serial.print(sz);
    }

    if (!t.isValid()) {
        Serial.print(F("******** "));
    } else {
        char sz[32];
        sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
        Serial.print(sz);
    }

    printInt(d.age(), d.isValid(), 5);
    smartDelay(0);
}

static void printStr(const char *str, int len)
{
    int slen = strlen(str);
    for (int i = 0; i < len; ++i)
        Serial.print(i < slen ? str[i] : ' ');
    smartDelay(0);
}
