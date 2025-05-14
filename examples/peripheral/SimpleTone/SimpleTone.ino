/**
 * @file      SimpleTone.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-19
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>

#define SAMPLE_RATE 44100

// Generate a simple beep signal
const int beep_freq = 1000;
const int beep_duration = 100;      // Beep duration (ms)
const int beep_interval = 300;      // Beep Interval (milliseconds)
const int pause_duration = 1000;    // The time between two beeps (milliseconds)
float volume = 0.5; // Volume control variable, range 0.0 - 1.0


const int beep_samples = beep_duration * SAMPLE_RATE / 1000;
int16_t beep_buffer[beep_samples];
int state = 0; // 0: first beep, 1: second beep, 2: pause
uint32_t last_action_time = 0;

void write(uint8_t * buffer, size_t size)
{
#ifdef ARDUINO_T_LORA_PAGER
    // T-LoRa-Pager uses Codec
    instance.codec.write(buffer, size);
#else
    // T-Watch-S3 / T-Watch-S3-Ultar Use Player
    instance.player.write(buffer, size);
#endif
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label1 = lv_label_create(lv_screen_active());
    lv_label_set_text(label1, "SimapleTone");
    lv_obj_center(label1);

    // Turn on the audio power, the default is off
    instance.powerControl(POWER_SPEAK, true);

    // Producing BB Audio
    for (int i = 0; i < beep_samples; i++) {
        beep_buffer[i] = (int16_t)(32767 * sin(2 * PI * beep_freq * i / SAMPLE_RATE) * volume);
    }

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);


#ifdef ARDUINO_T_LORA_PAGER
    // T-LoRa-Pager Open the audio device before writing
    instance.codec.setVolume(80);
    if (instance.codec.open(16, 1, SAMPLE_RATE) < 0) {
        Serial.println("Open audio device failed!");
        return;
    }
    // If you don't need to play, you need to turn off the audio device.
    // instance.codec.close();
#endif
}

void loop()
{
    size_t bytes_written;
    uint32_t current_time = millis();
    switch (state) {
    case 0:
        if (current_time - last_action_time >= beep_interval) {
            last_action_time = current_time;
            write((uint8_t*)beep_buffer, sizeof(beep_buffer));
            state = 1;
        }
        break;
    case 1:
        if (current_time - last_action_time >= beep_interval) {
            last_action_time = current_time;
            write((uint8_t*)beep_buffer, sizeof(beep_buffer));
            state = 2;
        }
        break;
    case 2:
        if (current_time - last_action_time >= pause_duration) {
            last_action_time = current_time;
            state = 0;
        }
        break;
    }
    lv_task_handler();
}