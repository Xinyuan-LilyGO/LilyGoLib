#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x83F5
#define USB_MANUFACTURER "LILYGO"
#define USB_PRODUCT "T-Embed-Plus"

static const uint8_t TX = 43;
static const uint8_t RX = 44;

#define DISP_WIDTH 170
#define DISP_HEIGHT 320

#define DISPLAY_WIDTH 170
#define DISPLAY_HEIGHT 320
#define DISPLAY_BL 21
#define DISPLAY_CS 41
#define DISPLAY_MISO 10
#define DISPLAY_MOSI 9
#define DISPLAY_SCLK 11
#define DISPLAY_DC 16
#define DISPLAY_RST -1

#define BOARD_USER_KEY 6
// HIGH enables and LOW disables the shared CC1101, WS2812 and MAX98357 power rail.
#define BOARD_PERIPHERAL_POWER_EN 15

#define WS2812_NUM_LEDS 8
#define WS2812_DATA_PIN 14

#define BOARD_IR_EN 2
#define BOARD_IR_TX BOARD_IR_EN
#define BOARD_IR_RX 1
#define IR_SEND BOARD_IR_EN
#define IR_RECEIVE_PIN BOARD_IR_RX

#define BOARD_MIC_DATA 42
#define BOARD_MIC_CLK 39
#define BOARD_VOICE_BCLK 46
#define BOARD_VOICE_LRCLK 40
#define BOARD_VOICE_DIN 7

#define ENCODER_INA 4
#define ENCODER_INB 5
#define ENCODER_KEY 0

#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 18
static const uint8_t SDA = BOARD_I2C_SDA;
static const uint8_t SCL = BOARD_I2C_SCL;

#define BOARD_SPI_SCK 11
#define BOARD_SPI_MOSI 9
#define BOARD_SPI_MISO 10
#define BOARD_SD_CS 13
#define BOARD_LORA_CS 12
#define BOARD_LORA_IO0 3
#define BOARD_LORA_IO2 38
#define BOARD_LORA_SW1 47
#define BOARD_LORA_SW0 48
#define BOARD_NRF24_CS 44
#define BOARD_NRF24_CE 43
#define BOARD_NRF24_IRQ -1

#define LORA_CS BOARD_LORA_CS
#define LORA_IRQ BOARD_LORA_IO0
#define LORA_BUSY BOARD_LORA_IO2
#define LORA_RST -1
#define LORA_SCK BOARD_SPI_SCK
#define LORA_MOSI BOARD_SPI_MOSI
#define LORA_MISO BOARD_SPI_MISO
#define SD_CS BOARD_SD_CS
static const uint8_t SS = BOARD_SD_CS;
static const uint8_t SCK = BOARD_SPI_SCK;
static const uint8_t MOSI = BOARD_SPI_MOSI;
static const uint8_t MISO = BOARD_SPI_MISO;

#define BOARD_PN532_SCL BOARD_I2C_SCL
#define BOARD_PN532_SDA BOARD_I2C_SDA
#define BOARD_PN532_RF_REST 45
#define BOARD_PN532_IRQ 17

#define HAS_SD_CARD_SOCKET
#define HAS_I2C_INTERFACE
#define HAS_SPI_INTERFACE
#define USING_PPM_MANAGE
#define GAUGE_CHIP_BQ27220
#define USING_INPUT_DEV_ROTARY
#define USING_PDM_MICROPHONE
#define USING_PCM_AMPLIFIER
#define USING_IR_REMOTE
#define USING_IR_RECEIVER

#endif /* Pins_Arduino_h */
