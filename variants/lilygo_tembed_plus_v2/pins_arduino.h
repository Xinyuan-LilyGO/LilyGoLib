#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID          0x303A
#define USB_PID          0x83F5
#define USB_MANUFACTURER "LILYGO"
#define USB_PRODUCT      "T-Embed-Plus-V2"

// ST7789 display and shared SPI bus.
#define DISP_WIDTH  (170)
#define DISP_HEIGHT (320)
static const uint8_t TX = 43;
static const uint8_t RX = 44;
// GPIO47/GPIO48 are shared by the nRF24 CE/CS signals and the ESP32-C5 UART.
// XL9555 P06 selects which device is connected to these two S3 pins.
#define C5_UART_TX (47)
#define C5_UART_RX (48)
static const uint8_t SDA = 3;
static const uint8_t SCL = 2;
static const uint8_t SD_CS = 13;
static const uint8_t SS = SD_CS;
static const uint8_t MOSI = 9;
static const uint8_t MISO = 10;
static const uint8_t SCK = 11;

// Rotary encoder and side button.
#define ROTARY_A (4)
#define ROTARY_B (5)
#define ROTARY_C (0)
#define USER_KEY (6)

// Audio.
#define MIC_I2S_SDIN (42)
#define MIC_I2S_SCK  (39)
#define I2S_BCLK    (46)
#define I2S_LRCLK   (40)
#define I2S_DIN     (7)

// Infrared and RGB LED.
#define IR_SEND         (15)
#define IR_RECEIVE_PIN  (1)
#define RGB_DATA        (14)
#define WS2812_NUM_LEDS (8)

// CC1101 and LoRa-compatible radio connector.
#define LORA_SCK  (SCK)
#define LORA_MISO (MISO)
#define LORA_MOSI (MOSI)
#define LORA_CS   (12)
#define LORA_RST  (-1)
#define LORA_BUSY (38)
#define LORA_IRQ  (8)

// ST25R3916 and display share the SPI bus.
#define NFC_CS  (45)
#define NFC_INT (17)
#define DISP_MOSI (MOSI)
#define DISP_MISO (MISO)
#define DISP_SCK  (SCK)
#define DISP_RST  (-1)
#define DISP_CS   (41)
#define DISP_DC   (16)
#define DISP_BL   (21)

// NRF24 expansion connector.
#define NRF24_CS  (47)
#define NRF24_CE  (48)
#define NRF24_IRQ (-1)

// XL9555 expansion pins and I2C addresses.
#define EXPANDS_ADDR       (0x22)
#define BQ27220_ADDR       (0x55)
#define BQ25896_ADDR       (0x6B)
#define SY6970_ADDR        (0x6A)
#define EXPANDS_CC_SW1     (0)
#define EXPANDS_CC_SW0     (1)
#define EXPANDS_LCD_RST    (2)
#define EXPANDS_LOW_PWR_EN (3)
#define EXPANDS_AMP_EN     (4)
#define EXPANDS_C5_RST     (5)
#define EXPANDS_MOD_SEL    (6)
#define EXPANDS_EX_PWR_EN1 (7)
#define EXPANDS_EX_PWR_EN2 (8)

#define HAS_SD_CARD_SOCKET
#define HAS_I2C_INTERFACE
#define HAS_SPI_INTERFACE
#define USING_XL9555_EXPANDS
#define USING_PPM_MANAGE
#define GAUGE_CHIP_BQ27220
#define USING_INPUT_DEV_ROTARY
#define USING_PDM_MICROPHONE
#define USING_PCM_AMPLIFIER
#define USING_IR_REMOTE
#define USING_IR_RECEIVER
#define USING_ST25R3916
#define EXCLUDE_PN532

#endif /* Pins_Arduino_h */
