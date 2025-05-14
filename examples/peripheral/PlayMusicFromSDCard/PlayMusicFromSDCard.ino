/**
 * @file      PlayMusicFromSDCard.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xinyuan Electronic Technology Co., Ltd
 * @date      2023-04-30
 *
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourceFunction.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <vector>


#if defined(ARDUINO_T_LORA_PAGER)
class EspAudioOutput : public AudioOutput
{
public:
    EspAudioOutput(EspCodec & codec)
    {
        _codec = &codec;
    }
    bool begin()
    {
        Serial.printf("bps:%d channels:%u hertz:%u\n", bps, channels, hertz);
        if (hertz != 16000 || hertz != 32000 || hertz != 24000 || hertz != 41000 || hertz == 0) {
            hertz = 16000;
        }
        channels = 2;
        alreadyBeing = _codec->open(bps, channels, hertz ) != -1;
        return alreadyBeing;
    };

    bool SetBitsPerSample(int bits)
    {
        Serial.printf("Set bps : %u\n", bps);
        bps = bits;
        return true;
    }
    bool SetChannels(int chan)
    {
        Serial.printf("Set channels : %u\n", channels);
        channels = chan;
        return true;
    }
    bool SetRate(int hz)
    {
        hertz = hz;
        Serial.printf("Set Rate : %u\n", hertz);
        if (alreadyBeing) {
            _codec->close();
        }
        alreadyBeing = _codec->open(bps, channels, hertz ) != -1;
        return true;
    }

    bool ConsumeSample(int16_t sample[2])
    {
        return _codec->write((uint8_t*)sample, 4) != -1;
    }

    uint16_t ConsumeSamples(int16_t *samples, uint16_t count)
    {
        Serial.println("ConsumeSamples....");
        _codec->write((uint8_t*)samples, count);
        return count;
    }
    bool stop()
    {
        Serial.println("Closed\n\n\n");
        alreadyBeing = false;
        _codec->close();
        return true;
    }
private:
    bool alreadyBeing = false;
    EspCodec *_codec;
};

EspAudioOutput          *out = NULL;

#else /*ARDUINO_T_LORA_PAGER*/

#include <AudioOutputI2S.h>

// It is recommended to use I2S channel 1.
// If the PDM microphone is turned on,
// the decoder must use channel 1 because PDM can only use channel 0
uint8_t i2sPort = 1;

AudioOutputI2S          *out = NULL;

#endif /*ARDUINO_T_LORA_PAGER*/

AudioGeneratorWAV       *wav = NULL;
AudioFileSourceSD       *file = NULL;
AudioGeneratorMP3       *mp3 = NULL;
AudioFileSourceID3      *id3 = NULL;
std::vector<String>     file_list;


void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() && levels) {
            Serial.print("  DIR : ");
            Serial.print(file.name());
            Serial.println();

            listDir(fs, file.path(), levels - 1);
        } else {
            String filename = String(file.name());
            if (filename.endsWith(".wav") || filename.endsWith(".mp3")) {
                Serial.print("  FILE: ");
                Serial.print(file.name());
                Serial.print("  SIZE: ");
                Serial.println(file.size());

                // Save full path
                file_list.push_back("/" + filename);
            }
        }
        file = root.openNextFile();
    }
}

void setup(void)
{
    Serial.begin(115200);

    // When initializing the instance,
    // FFat has already been initialized internally
    instance.begin();

    beginLvglHelper(instance);

    lv_obj_t *label;
    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Play Music From SDcard");
    lv_obj_center(label);


    file = new AudioFileSourceSD();

    wav = new AudioGeneratorWAV();

    mp3 = new AudioGeneratorMP3();

#if defined(ARDUINO_T_LORA_PAGER)

    // Set up the use of an external decoder
    out = new EspAudioOutput(instance.codec);

    // Set volume
    instance.codec.setVolume(50);

#else /*ARDUINO_T_LORA_PAGER*/

    // Set up the use of an external decoder
    out = new AudioOutputI2S(i2sPort, AudioOutputI2S::EXTERNAL_I2S);

    // Set up hardware connection
    out->SetPinout(I2S_BCLK, I2S_WCLK, I2S_DOUT);

    //Adjust to appropriate gain
    out->SetGain(3.8);

#endif /*ARDUINO_T_LORA_PAGER*/


    // Traverse files in SD card
    listDir(SD, "/", 2); // Change the root directory as needed

    Serial.print("\n\n\nTotal music file found: ");
    Serial.println(file_list.size());

    for (size_t i = 0; i < file_list.size(); i++) {
        Serial.print("File: ");
        Serial.print(i + 1);
        Serial.print(" path: ");
        Serial.println(file_list[i]);
    }

    // Set brightness to MAX
    // T-LoRa-Pager brightness level is 0 ~ 16
    // T-Watch-S3 , T-Watch-S3-Plus , T-Watch-Ultra brightness level is 0 ~ 255
    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_task_handler();

    // Turn on the audio power, the default is off
    instance.powerControl(POWER_SPEAK, true);

    for (size_t i = 0; i < file_list.size(); i++) {
        Serial.print("File: ");
        Serial.print(i + 1);
        Serial.print(" path: ");
        Serial.println(file_list[i]);


        if (file_list[i].endsWith(".mp3")) {
            file->open(file_list[i].c_str());
            id3 = new AudioFileSourceID3(file);
            mp3->begin(id3, out);
        } else if (file_list[i].endsWith(".wav")) {
            file->open(file_list[i].c_str());
            wav->begin(file, out);
        }

        while (mp3->isRunning()) {
            if (!mp3->loop()) {
                mp3->stop();
                delete id3;
            }
        }

        while (wav->isRunning()) {
            if (!wav->loop()) {
                wav->stop();
            }
        }
    }

    Serial.println("Done.....");


}


void loop()
{
}

