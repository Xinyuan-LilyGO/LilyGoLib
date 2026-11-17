#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x8150
#define USB_MANUFACTURER "LILYGO"
#define USB_PRODUCT "T-Embed"

#define DISP_WIDTH 170
#define DISP_HEIGHT 320
#define DISPLAY_WIDTH DISP_WIDTH
#define DISPLAY_HEIGHT DISP_HEIGHT

#define BOARD_POWERON 46
#define BAT_ADC 4

#define BOARD_I2C_SDA 18
#define BOARD_I2C_SCL 8
#define SDA BOARD_I2C_SDA
#define SCL BOARD_I2C_SCL

#define SI4735_RST 16
#define SI4735_AUDIO_MUTE 17

#define DISPLAY_BL 15
#define DISPLAY_DC 13
#define DISPLAY_CS 10
#define DISPLAY_SCLK 12
#define DISPLAY_MOSI 11
#define DISPLAY_MISO -1
#define DISPLAY_RST 9

#define ENCODER_INA 2
#define ENCODER_INB 1
#define ENCODER_KEY 0

#define APA102_NUM_LEDS 7
#define APA102_DATA_PIN 42
#define APA102_CLOCK_PIN 45

#define I2S_SCK 7
#define I2S_WS 5
#define I2S_SDOUT 6

#define MIC_I2S_SCK 47
#define MIC_I2S_WS 21
#define MIC_I2S_SDIN 14
#define MIC_I2S_MCLK 48

#define SD_CS 39
#define SD_SCK 40
#define SD_MOSI 41
#define SD_MISO 38
#define LILYGO_TEMBED_SD_SPI_FREQ 40000000U

static const uint8_t SS = SD_CS;
static const uint8_t SCK = SD_SCK;
static const uint8_t MOSI = SD_MOSI;
static const uint8_t MISO = SD_MISO;

#define HAS_SD_CARD_SOCKET
#define HAS_I2C_INTERFACE
#define USING_INPUT_DEV_ROTARY
#define USING_ES7210
#define USING_PCM_AMPLIFIER
#define USING_SI473X_RADIO

#endif
