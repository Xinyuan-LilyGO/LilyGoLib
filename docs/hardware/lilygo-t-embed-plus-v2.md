<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Embed-Plus V2🌟</h1>


## `1` Overview

* This page lists the hardware specifications for the `LILYGO T-Embed-Plus V2`.
* The V2 board is a separate hardware design from the T-Embed-Plus V1.
* The built-in Sub-GHz transceiver is a CC1101. Some source-level pin names
  retain the common `LORA` prefix for compatibility.

### V2 Hardware Characteristics

* The nRF24 radio is an on-board device. It uses the shared SPI bus and does
  not represent an external SPI interface.
* The display, MicroSD, CC1101, ST25R3916, and nRF24 all share the same SPI
  controller. V2 raises the inactive chip-select signals before display
  initialization.
* The XL9555 at I2C address `0x22` controls the CC1101 RF path, display reset,
  shared peripheral power, audio amplifier shutdown, ESP32-C5 reset, and module
  selection.
* The ESP32-C5 is a separate AT co-processor. Its firmware is flashed
  independently of the ESP32-S3 application. The board is shipped with AT
  firmware pre-flashed; see the [ESP32-C5 AT firmware notes](../../firmware/esp-at/README.md).

###  Hardware Features

| Feature                  | Specification                                         |
| ------------------------ | ----------------------------------------------------- |
| SoC                      | [ESP32-S3-WROOM-1][1], dual-core LX7 up to 240 MHz    |
| Wireless                 | 2.4 GHz Wi-Fi (802.11b/g/n) and Bluetooth 5 module    |
| Flash                    | 16 MB                                                 |
| PSRAM                    | 8 MB OPI                                              |
| Sub-GHz radio            | [TI CC1101][2]                                        |
| Supported radio bands    | 300-348 MHz, 387-464 MHz, and 779-928 MHz             |
| 2.4 GHz radio            | [nRF24L01][9]                                         |
| NFC                      | [ST25R3916][3],13.56 MHz,SPI                          |
| Battery Charger          | [TI BQ25896][4]                                       |
| Battery Gauge            | [TI BQ27220][5]                                       |
| GPIO Expander            | [XINLUDA XL9555][6]                                   |
| Input                    | Rotary encoder with center switch and one user button |
| Microphone               | PDM digital microphone                                |
| Microphone model         | [SPM1423HM4H-B][7]                                    |
| Audio Power Amplifier    | MAX98357A I2S amplifier and two-pin speaker connector |
| Display Backlight Driver | AW9364 16-level pulse-controlled backlight driver     |
| Storage                  | MicroSD card socket, SPI                              |
| Addressable pixels       | 8 x WS2812-compatible RGB pixels                      |
| Infrared                 | Transmitter and receiver                              |
| ESP32-C5 co-processor    | [ESP32-C5-MINI-1 (4MB,NoPSRAM)][8]                    |
| Battery                  | 3.7 V, 1300 mAh , NTC                                 |

[1]: https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf "ESP32-S3-WROOM-1U"
[2]: https://www.ti.com/product/CC1101 "TI CC1101"
[3]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[4]: https://www.ti.com/product/BQ25896 "BQ25896"
[5]: https://www.ti.com/product/BQ27220 "BQ27220"
[6]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[7]: https://media.digikey.com/pdf/Data%20Sheets/Knowles%20Acoustics%20PDFs/SPM1423HM4H-B.pdf "SPM1423HM4H"
[8]: https://documentation.espressif.com/esp32-c5-mini-1_mini-1u_datasheet_en.html
[9]: https://datasheet4u.com/datasheets/Nordic/NRF24L01/829184 "nRF24L01"

### ✨ Display Specifications

| Feature             | Specification                      |
| ------------------- | ---------------------------------- |
| Default orientation | Landscape                          |
| Default resolution  | 320 x 170                          |
| Native resolution   | 170 x 320                          |
| Display size        | 1.9 inches                         |
| Display type        | LCD                                |
| Driver IC           | ST7789                             |
| Interface           | SPI                                |
| Touch controller    | Not fitted                         |
| Backlight driver    | AW9364                             |
| Backlight range     | 0 to 16; pulse controlled, not PWM |

### [Pin Map](../../variants/lilygo_tembed_plus_v2/pins_arduino.h)

The LilyGoLib board definition is available in the
[T-Embed-Plus V2 Arduino variant](../../arduino/temporary_t_embed_plus/variants/lilygo_tembed_plus_v2/pins_arduino.h).

| Name                         | GPIO                | Free |
| ---------------------------- | ------------------- | ---- |
| UART TX / C5 link            | 43                  | ❌    |
| UART RX / C5 link            | 44                  | ❌    |
| SDA                          | 3                   | ❌    |
| SCL                          | 2                   | ❌    |
| SPI MOSI                     | 9                   | ❌    |
| SPI MISO                     | 10                  | ❌    |
| SPI SCK                      | 11                  | ❌    |
| SD CS                        | 13                  | ❌    |
| SD MOSI                      | Shared with SPI bus | ❌    |
| SD MISO                      | Shared with SPI bus | ❌    |
| SD SCK                       | Shared with SPI bus | ❌    |
| Rotary Encoder A             | 4                   | ❌    |
| Rotary Encoder B             | 5                   | ❌    |
| Rotary Encoder Center / BOOT | 0                   | ❌    |
| User Button                  | 6                   | ❌    |
| NFC(**ST25R3916**) CS        | 45                  | ❌    |
| NFC(**ST25R3916**) Interrupt | 17                  | ❌    |
| NFC(**ST25R3916**) MOSI      | Shared with SPI bus | ❌    |
| NFC(**ST25R3916**) MISO      | Shared with SPI bus | ❌    |
| NFC(**ST25R3916**) SCK       | Shared with SPI bus | ❌    |
| CC1101 CS                    | 12                  | ❌    |
| CC1101 GDO0 / Interrupt      | 8                   | ❌    |
| CC1101 GDO2                  | 38                  | ❌    |
| CC1101 RF switch SW1         | XL9555 P00          | ❌    |
| CC1101 RF switch SW0         | XL9555 P01          | ❌    |
| Display CS                   | 41                  | ❌    |
| Display MOSI                 | Shared with SPI bus | ❌    |
| Display MISO                 | Shared with SPI bus | ❌    |
| Display SCK                  | Shared with SPI bus | ❌    |
| Display DC                   | 16                  | ❌    |
| Display RESET                | XL9555 P02          | ❌    |
| Display Backlight            | 21                  | ❌    |
| MicroSD CS                   | 13                  | ❌    |
| WS2812 data                  | 14                  | ❌    |
| Infrared TX                  | 15                  | ❌    |
| Infrared RX                  | 1                   | ❌    |
| PDM microphone data          | 42                  | ❌    |
| PDM microphone clock         | 39                  | ❌    |
| Speaker I2S BCLK             | 46                  | ❌    |
| Speaker I2S LRCLK            | 40                  | ❌    |
| Speaker I2S data             | 7                   | ❌    |
| nRF24 CS                     | 48                  | ❌    |
| nRF24 CE                     | 47                  | ❌    |
| nRF24 Interrupt              | Not connected       | ❌    |
| USB D-                       | 19                  | ❌    |
| USB D+                       | 20                  | ❌    |
| XL9555 I2C address           | 0x22                | ❌    |

> [!IMPORTANT]
>
> The display, MicroSD, CC1101, ST25R3916, and nRF24 interface share
> the SPI bus. Keep every inactive device's chip-select signal high and
> serialize SPI transactions.

### CC1101 RF Path Selection

| Frequency path | SW1 (XL9555 P00) | SW0 (XL9555 P01) |
| -------------- | ---------------- | ---------------- |
| 315 MHz        | High             | Low              |
| 434 MHz        | High             | High             |
| 868 / 915 MHz  | Low              | High             |

LilyGoLib selects the RF path when the CC1101 frequency is changed.

### ESP32-C5 AT Link

The ESP32-C5 module is a separate co-processor and is not programmed when the
ESP32-S3 application is uploaded. The board is shipped with AT firmware
pre-flashed. The V2 board routes S3 GPIO43 (TX)
and GPIO44 (RX) to the C5 AT command link. Espressif's default ESP32-C5-4MB
ESP-AT firmware uses C5 UART1 GPIO23 (TX) and GPIO24 (RX) at 115200 baud. The
crossed connection is C5 GPIO23 (TX) to S3 GPIO44 (RX), and C5 GPIO24 (RX) to
S3 GPIO43 (TX). C5 UART0 GPIO11 (TX) and GPIO12 (RX) are reserved for
download/log output.

### I2C Device Addresses

| Device                  | 7-Bit Address | Shared Bus |
| ----------------------- | ------------- | ---------- |
| GPIO Expander XL9555    | 0x22          | ✅️          |
| Battery Charger BQ25896 | 0x6B          | ✅️          |
| Battery Gauge BQ27220   | 0x55          | ✅️          |

### XL9555 Outputs

| XL9555 pin         | Function                             |
| ------------------ | ------------------------------------ |
| XL9555 GPIO0 (P00) | CC1101 RF switch SW1                 |
| XL9555 GPIO1 (P01) | CC1101 RF switch SW0                 |
| XL9555 GPIO2 (P02) | Display reset                        |
| XL9555 GPIO3 (P03) | `LOW_PWR_3V3` shared peripheral rail |
| XL9555 GPIO4 (P04) | Speaker amplifier shutdown           |
| XL9555 GPIO5 (P05) | ESP32-C5 reset                       |
| XL9555 GPIO6 (P06) | Module select                        |
| XL9555 GPIO7 (P07) | External power enable 1              |
| XL9555 GPIO8 (P10) | External power enable 2              |

### Power Notes

* `LOW_PWR_3V3` supplies several peripherals, including the CC1101, NFC,
  RGB pixels, infrared, microphone, and amplifier.
* LilyGoLib keeps the shared rail enabled during normal operation and light
  sleep. The amplifier has a separate XL9555 shutdown control.

###  Electrical Specifications

| Feature                  | Specification            |
| ------------------------ | ------------------------ |
| USB-C Input Voltage      | 4.7-6V                   |
| USB-C Output Voltage     | 4.55-5.55 V              |
| USB-C Output Current     | 0.5-1 A                  |
| Charge Current           | 0-3008 mA (programmable) |
| Battery Voltage          | 3.7 V                    |
| Battery Capacity         | 1300 mAh                 |
| Charge Temperature Range | 0-60 °C                  |

> [!IMPORTANT]
>
> LilyGoLib configures the fast-charge current for the BQ25896 to 640 mA. According to the battery datasheet, the charging current should not exceed half of the battery's capacity. If you are not using LilyGoLib, please ensure that the power management IC is initialized with a charging current of 640 mA or less. If it is not initialized, the device will default to a 2A charging current, which will reduce the battery's lifespan.
>

### Resources

* [ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3)
* [CC1101](https://www.ti.com/product/CC1101)
* [ST25R3916](https://www.st.com/en/nfc/st25r3916.html)
* [BQ25896](https://www.ti.com/product/BQ25896)
* [BQ27220](https://www.ti.com/product/BQ27220)
* [XL9555](https://www.xinluda.com/en/I2C-to-GPIO-extension/)
