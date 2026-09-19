/**
 * @file      hw_si4735.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-09-09
 * @note      Si4735 example from https://github.com/pu2clr/SI4735 by Ricardo Lima Caratti, pu2clr@gmail.com
 */

#include <LilyGoLog.h>
#ifdef ARDUINO
#include <LilyGoLib.h>
#include <Preferences.h>
#else
#include <stdlib.h>
#endif


#include "hal_interface.h"

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

/**
 *  Band data structure
 */
typedef struct {
    const char *bandName; // Band description
    uint8_t bandType;     // Band type (FM, MW or SW)
    uint16_t minimumFreq; // Minimum frequency of the band
    uint16_t maximumFreq; // maximum frequency of the band
    uint16_t currentFreq; // Default frequency or current frequency
    uint16_t currentStep; // Default step (increment and decrement)
} Band;

/*
   Band table
*/
Band band[] = {
    {"VHF", FM_BAND_TYPE, 6400, 10800,  10570, 1},
    {"MW1", MW_BAND_TYPE,   150,  1720,   810, 10},
    {"MW2", MW_BAND_TYPE,  1700,  3500,  2500, 5},
    {"80M", MW_BAND_TYPE,  3500,  4000,  3700, 1},
    {"SW1", SW_BAND_TYPE,  4000,  5500,  4885, 5},
    {"SW2", SW_BAND_TYPE,  5500,  6500,  6000, 5},
    {"40M", SW_BAND_TYPE,  6500,  7300,  7100, 1},
    {"SW3", SW_BAND_TYPE,  7200,  8000,  7200, 5},
    {"SW4", SW_BAND_TYPE,  9000, 11000,  9500, 5},
    {"SW5", SW_BAND_TYPE, 11100, 13000, 11900, 5},
    {"SW6", SW_BAND_TYPE, 13000, 14000, 13500, 5},
    {"20M", SW_BAND_TYPE, 14000, 15000, 14200, 1},
    {"SW7", SW_BAND_TYPE, 15000, 17000, 15300, 5},
    {"SW8", SW_BAND_TYPE, 17000, 18000, 17500, 5},
    {"15M", SW_BAND_TYPE, 20000, 21400, 21100, 1},
    {"SW9", SW_BAND_TYPE, 21400, 22800, 21500, 5},
    {"CB ", SW_BAND_TYPE, 26000, 28000, 27500, 1},
    {"10M", SW_BAND_TYPE, 28000, 30000, 28400, 1},
    {"ALL", SW_BAND_TYPE, 150, 30000, 15000, 1}    // All band. LW, MW and SW (from 150kHz to 30MHz)
}; // Super band: 150kHz to 30MHz

static const int lastBand = (sizeof band / sizeof(Band)) - 1;
static int bandIdx = 0;
// Some variables to check the SI4735 status
static uint16_t currentFrequency = 0;
static uint16_t currentStep = 1;
static uint8_t currentBFOStep = 25;
static uint8_t bwIdxSSB = 2;
static const char *bandwidthSSB[] = {"1.2", "2.2", "3.0", "4.0", "0.5", "1.0"};
static const char *bandwidthAM[] = {"6", "4", "3", "2", "1", "1.8", "2.5"};
static const char *bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
static uint8_t bwIdxAM = 1;
static uint8_t currentMode = FM;
static bool bfoOn = false;
static bool disableAgc = true;
static bool ssbLoaded = false;
static bool fmStereo = true;
static int currentBFO = 0;

#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
// Test it with patch_init.h or patch_full.h. Do not try load both.
#include <patch_init.h> // SSB patch for whole SSBRX initialization string

static const uint16_t size_content = sizeof ssb_patch_content; // see ssb_patch_content in patch_full.h or patch_init.h
#endif


static void useBand();
static void loadSSB();
static void loadFmTunedState();
static void saveFmTunedState();

#if defined(ARDUINO) && defined(USING_SI473X_RADIO)
static constexpr const char *SI4735_PREF_NAMESPACE = "si4735";
static constexpr const char *SI4735_PREF_STATE_KEY = "fm_state";
static constexpr uint32_t SI4735_PREF_MAGIC = 0x53493531; // SI51
static constexpr uint16_t SI4735_PREF_VERSION = 1;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t frequency;
    uint16_t step;
} Si4735FmState;

static bool si4735_state_loaded = false;

static bool isValidFmFrequency(uint16_t frequency)
{
    return frequency >= band[0].minimumFreq && frequency <= band[0].maximumFreq;
}

static uint16_t normalizeFmStep(uint16_t step)
{
    switch (step) {
    case 1:
    case 5:
    case 10:
    case 50:
        return step;
    default:
        return band[0].currentStep;
    }
}

static void loadFmTunedState()
{
    if (si4735_state_loaded) {
        return;
    }
    si4735_state_loaded = true;

    Preferences store;
    if (!store.begin(SI4735_PREF_NAMESPACE, true)) {
        return;
    }

    Si4735FmState state = {};
    bool valid = store.getBytes(SI4735_PREF_STATE_KEY, &state, sizeof(state)) == sizeof(state) &&
                 state.magic == SI4735_PREF_MAGIC &&
                 state.version == SI4735_PREF_VERSION &&
                 state.size == sizeof(state) &&
                 isValidFmFrequency(state.frequency);
    store.end();

    if (!valid) {
        return;
    }

    bandIdx = 0;
    currentMode = FM;
    bfoOn = false;
    ssbLoaded = false;
    currentFrequency = state.frequency;
    currentStep = normalizeFmStep(state.step);
    band[0].currentFreq = currentFrequency;
    band[0].currentStep = currentStep;
}

static void saveFmTunedState()
{
    if (currentMode != FM || !isValidFmFrequency(currentFrequency)) {
        return;
    }

    Si4735FmState state = {
        .magic = SI4735_PREF_MAGIC,
        .version = SI4735_PREF_VERSION,
        .size = (uint16_t)sizeof(Si4735FmState),
        .frequency = currentFrequency,
        .step = currentStep,
    };

    Preferences store;
    if (!store.begin(SI4735_PREF_NAMESPACE, false)) {
        return;
    }
    store.putBytes(SI4735_PREF_STATE_KEY, &state, sizeof(state));
    store.end();
}
#else
static void loadFmTunedState() {}
static void saveFmTunedState() {}
#endif

void hw_si4735_set_power(bool powerOn)
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    AudioOutputIf *audioOutput = instance.getAudioOutput();
    if (!audioOutput) {
        LILYGO_LOG_PRINTLN("Audio output not initialized");
        return;
    }

    if (powerOn) {
        loadFmTunedState();
        instance.powerControl(POWER_SI4735_RADIO, true);
        if (!instance.si4735Enabled()) {
            LILYGO_LOG_PRINTLN("Si4735 power on failed");
            return;
        }
        band[bandIdx].bandType = FM_BAND_TYPE;
        useBand();
        delay(100);
        instance.si4735.setFrequency(currentFrequency);
        instance.si4735.setFrequencyStep(currentStep);
        LILYGO_LOG_PRINTF("Power on , set freq = %u step : %u\n", currentFrequency, currentStep);

        // Setting SI473X Sample rate to 48K.
        instance.si4735.digitalOutputSampleRate(48000);

        audioOutput->open(16, 2, 48000);
        audioOutput->setVolume(43);

        instance.si4735.digitalOutputFormat(0 /* OSIZE */, 0 /* OMONO */, 0 /* OMODE */, 0 /* OFALL*/);

        instance.si4735.setVolume(63);

    } else {
        saveFmTunedState();
        audioOutput->close();
        instance.powerControl(POWER_SI4735_RADIO, false);
    }
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

void hw_si4735_set_volume(uint8_t vol)
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    // instance.si4735.setVolume(vol);
    AudioOutputIf *audioOutput = instance.getAudioOutput();
    if (!audioOutput) {
        LILYGO_LOG_PRINTLN("Audio output not initialized");
        return;
    }
    audioOutput->setVolume(vol);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

uint8_t hw_si4735_get_volume(void)
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    // return  instance.si4735.getVolume();
    AudioOutputIf *audioOutput = instance.getAudioOutput();
    if (!audioOutput) {
        LILYGO_LOG_PRINTLN("Audio output not initialized");
        return 0;
    }
    return audioOutput->getVolume();
#else
    return 10;
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

uint8_t hw_si4735_get_rssi()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (!instance.si4735Enabled()) {
        return 0;
    }
    instance.si4735.getStatus();
    instance.si4735.getCurrentReceivedSignalQuality();
    // uint16_t currentFrequency = instance.si4735.getFrequency();
    instance.si4735.getCurrentReceivedSignalQuality();
    uint8_t rssi = instance.si4735.getCurrentRSSI();

    // const char *modeStr[] = {"FM", "LSB", "USB", "AM"};
    // Serial.printf("Mode: %s Frequency %u Step:%u | SNR: %d db | RSSI: %d dBuV\n", modeStr[currentMode], currentFrequency, currentStep, snr, rssi );
    return  rssi;
#else
    return rand() % 100;
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

uint8_t hw_si4735_get_snr()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (!instance.si4735Enabled()) {
        return 0;
    }
    instance.si4735.getCurrentReceivedSignalQuality();
    return instance.si4735.getCurrentSNR();
#else
    return rand() % 40;
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

uint16_t  hw_si4735_get_freq()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    return currentFrequency;
#else
    return currentFrequency;
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

bool hw_si4735_is_fm()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (!instance.si4735Enabled()) {
        return currentMode == FM;
    }
    return instance.si4735.isCurrentTuneFM();
#else
    return currentMode == FM;
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

Si4735Mode hw_si4735_get_mode()
{
    return (Si4735Mode)currentMode;
}

const char *hw_si4735_get_band_name()
{
    return band[bandIdx].bandName;
}

uint16_t hw_si4735_get_current_step()
{
    return currentStep;
}

void hw_si4735_set_mode(Si4735Mode bandType)
{
    switch (bandType) {
    case FM:
        currentMode = FM;
        break;
    case LSB:
        loadSSB();
        currentMode = LSB;
        break;
    case USB:
        currentMode = USB;
        break;
    case AM:
        currentMode = AM;
        bfoOn = ssbLoaded = false;
        break;
    default:
        break;
    }
    if (currentMode != FM) {
        // Nothing to do if you are in FM mode
        band[bandIdx].currentFreq = currentFrequency;
        band[bandIdx].currentStep = currentStep;
        useBand();
    }
}


void hw_si4735_set_bfo(bool on)
{
    if (currentMode == LSB || currentMode == USB) {
        bfoOn = !on;
        if (bfoOn) {
        } else {
        }
    }
}

void hw_si4735_set_agc(bool on)
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    disableAgc = !on;
    // switch on/off ACG; AGC Index = 0. It means Minimum attenuation (max gain)
    instance.si4735.setAutomaticGainControl(disableAgc, 1);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

void hw_si4735_set_freq_up()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (bfoOn) {
        currentBFO = (currentBFO + currentBFOStep);
        instance.si4735.setSSBBfo(currentBFO);
    } else {
        instance.si4735.frequencyUp();
        currentFrequency = instance.si4735.getFrequency();
        band[bandIdx].currentFreq = currentFrequency;
        band[bandIdx].currentStep = currentStep;
        saveFmTunedState();
    }
#else
    if (bfoOn) {
        currentBFO = (currentBFO + currentBFOStep);
    } else {
        if (currentFrequency + currentStep <= band[bandIdx].maximumFreq) {
            currentFrequency += currentStep;
        } else {
            currentFrequency = band[bandIdx].minimumFreq;
        }
    }
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

void hw_si4735_set_freq_down()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (bfoOn) {
        currentBFO = (currentBFO - currentBFOStep);
        instance.si4735.setSSBBfo(currentBFO);
    } else {
        instance.si4735.frequencyDown();
        currentFrequency = instance.si4735.getFrequency();
        band[bandIdx].currentFreq = currentFrequency;
        band[bandIdx].currentStep = currentStep;
        saveFmTunedState();
    }
#else
    if (bfoOn) {
        currentBFO = (currentBFO - currentBFOStep);
    } else {
        if (currentFrequency - currentStep >= band[bandIdx].minimumFreq) {
            currentFrequency -= currentStep;
        } else {
            currentFrequency = band[bandIdx].minimumFreq;
        }
    }
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

void hw_si4735_set_freq(uint16_t freq)
{
    currentFrequency = freq;
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    instance.si4735.setFrequency(freq);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
    saveFmTunedState();
}

void hw_si4735_band_up()
{
    // save the current frequency for the band
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
    bandIdx = (bandIdx < lastBand) ? (bandIdx + 1) : 0;
    useBand();
}

void hw_si4735_band_down()
{
    // save the current frequency for the band
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
    bandIdx = (bandIdx > 0) ? (bandIdx - 1) : lastBand;
    useBand();
}


void hw_si4735_update_band()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    if (currentMode == LSB || currentMode == USB) {
        bwIdxSSB++;
        if (bwIdxSSB > 5)
            bwIdxSSB = 0;
        instance.si4735.setSSBAudioBandwidth(bwIdxSSB);
        // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
        if (bwIdxSSB == 0 || bwIdxSSB == 4 || bwIdxSSB == 5) {
            instance.si4735.setSSBSidebandCutoffFilter(0);
        } else {
            instance.si4735.setSSBSidebandCutoffFilter(1);
        }
    } else if (currentMode == AM) {
        bwIdxAM++;
        if (bwIdxAM > 6)
            bwIdxAM = 0;
        instance.si4735.setBandwidth(bwIdxAM, 1);
    }
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}


uint16_t hw_si4735_update_steps()
{
    // This command should work only for SSB mode
    if (bfoOn && (currentMode == LSB || currentMode == USB)) {
        currentBFOStep = (currentBFOStep == 25) ? 10 : 25;
        // showBFO();
    } else {
        if (currentStep == 1)
            currentStep = 5;
        else if (currentStep == 5)
            currentStep = 10;
        else if (currentStep == 10)
            currentStep = 50;
        else if ( currentStep == 50 &&  bandIdx == lastBand)  // If band index is All, you can use 500kHz Step.
            currentStep = 500;
        else
            currentStep = 1;

#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
        instance.si4735.setFrequencyStep(currentStep);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/

        band[bandIdx].currentStep = currentStep;
        saveFmTunedState();
    }
    LILYGO_LOG_PRINTF("Step changed to %u\n", currentStep);
    return currentStep;
}

void hw_si4735_seek_up()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    instance.si4735.seekNextStation();
    delay(100);
    currentFrequency = instance.si4735.getFrequency();
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
    LILYGO_LOG_PRINTF("Seek up: %u\n", currentFrequency);
    instance.si4735.setFrequency(currentFrequency);
    saveFmTunedState();
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

void hw_si4735_seek_down()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    instance.si4735.seekPreviousStation();
    delay(100);
    currentFrequency = instance.si4735.getFrequency();
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
    LILYGO_LOG_PRINTF("Seek down: %u\n", currentFrequency);
    instance.si4735.setFrequency(currentFrequency);
    saveFmTunedState();
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
}

/*
   Switch the radio to current band
*/
static void useBand()
{
    if (band[bandIdx].bandType == FM_BAND_TYPE) {
        currentMode = FM;
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
        instance.si4735.setTuneFrequencyAntennaCapacitor(0);
        instance.si4735.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/

        bfoOn = ssbLoaded = false;
    } else {
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
        // set the tuning capacitor for SW or MW/LW
        instance.si4735.setTuneFrequencyAntennaCapacitor( (band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) ? 0 : 1);
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/

        if (ssbLoaded) {
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
            instance.si4735.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep, currentMode);
            instance.si4735.setSSBAutomaticVolumeControl(1);
            instance.si4735.setSsbSoftMuteMaxAttenuation(0); // Disable Soft Mute for SSB
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/

        } else {
            currentMode = AM;
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
            instance.si4735.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
            instance.si4735.setAutomaticGainControl(1, 0);
            instance.si4735.setAmSoftMuteMaxAttenuation(0); // // Disable Soft Mute for AM
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/
            bfoOn = false;
        }
    }
    // delay(100);
    if (band[bandIdx].currentFreq < band[bandIdx].minimumFreq || band[bandIdx].currentFreq > band[bandIdx].maximumFreq) {
        band[bandIdx].currentFreq = band[bandIdx].minimumFreq;
        band[bandIdx].currentStep = 1;
    }
    currentFrequency = band[bandIdx].currentFreq;
    currentStep = band[bandIdx].currentStep;
    LILYGO_LOG_PRINTF("[si4735] bandIdx:%d bandName:%s minBand:%u maxBand:%u currentFreq:%u bandStep:%d \n", bandIdx,
           band[bandIdx].bandName,
           band[bandIdx].minimumFreq,
           band[bandIdx].maximumFreq,
           band[bandIdx].currentFreq,
           band[bandIdx].currentStep);
}


/*
   This function loads the contents of the ssb_patch_content array into the CI (Si4735) and starts the radio on
   SSB mode.
*/
void loadSSB()
{
#if  defined(ARDUINO) && defined(USING_SI473X_RADIO)
    instance.si4735.reset();
    instance.si4735.queryLibraryId(); // Is it really necessary here? I will check it.
    instance.si4735.patchPowerUp();
    delay(50);
    instance.si4735.setI2CFastMode(); // Recommended
    instance.si4735.downloadPatch(ssb_patch_content, size_content);
    instance.si4735.setI2CStandardMode(); // goes back to default (100kHz)

    // Parameters
    // AUDIOBW - SSB Audio bandwidth; 0 = 1.2kHz (default); 1=2.2kHz; 2=3kHz; 3=4kHz; 4=500Hz; 5=1kHz;
    // SBCUTFLT SSB - side band cutoff filter for band passand low pass filter ( 0 or 1)
    // AVC_DIVIDER  - set 0 for SSB mode; set 3 for SYNC mode.
    // AVCEN - SSB Automatic Volume Control (AVC) enable; 0=disable; 1=enable (default).
    // SMUTESEL - SSB Soft-mute Based on RSSI or SNR (0 or 1).
    // DSP_AFCDIS - DSP AFC Disable or enable; 0=SYNC MODE, AFC enable; 1=SSB MODE, AFC disable.
    instance.si4735.setSSBConfig(bwIdxSSB, 1, 0, 0, 0, 1);
    delay(25);
    ssbLoaded = true;
    LILYGO_LOG_PRINTF("SSB patch loaded\n");
#endif /*defined(ARDUINO) && defined(USING_SI473X_RADIO)*/

}
