<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Embed-Plus🌟</h1>

## `1` Overview

* This page lists the hardware specifications for the `LILYGO T-Embed-Plus`.
* The built-in Sub-GHz transceiver is a CC1101. Some source-level pin names
  retain the common `LORA` prefix for compatibility, but the device is not a
  LoRa transceiver.

### Controls and Connectors

| Item                    | Description                                                           |
| ----------------------- | --------------------------------------------------------------------- |
| Rotary encoder          | Clockwise/counterclockwise navigation with a center push switch       |
| Encoder center / BOOT   | Application input on GPIO0 and ESP32-S3 download-mode switch          |
| User button             | Application input on GPIO6                                            |
| Reset button            | Resets the ESP32-S3; it is not a programmable input                   |
| USB-C                   | Power, charging, firmware upload, USB CDC, and native USB functions   |
| MicroSD slot            | SPI storage sharing the display and radio SPI bus                     |
| Speaker connector       | Two-pin output driven by the MAX98357A I2S amplifier                  |
| I2C expansion connector | Exposes GPIO8 (SDA) and GPIO18 (SCL)                                  |
| UART expansion pins     | GPIO43 (TX) and GPIO44 (RX); shared with the optional nRF24 interface |
| nRF24 interface         | Optional external nRF24 module using the shared SPI bus               |

### Hardware Features

| Feature               | Specification                                         |
| --------------------- | ----------------------------------------------------- |
| SoC                   | [ESP32-S3-WROOM-1][1], dual-core LX7 up to 240 MHz    |
| Wireless              | 2.4 GHz Wi-Fi (802.11b/g/n) and Bluetooth 5 module    |
| Flash                 | 16 MB                                                 |
| PSRAM                 | 8 MB OPI                                              |
| Display               | ST7789 LCD, 170 x 320 native resolution, SPI          |
| Sub-GHz radio         | [TI CC1101][2]                                        |
| Supported radio bands | 300-348 MHz, 387-464 MHz, and 779-928 MHz             |
| 2.4G radio            | [nRF24L01][6]                                         |
| NFC                   | PN532, 13.56 MHz, I2C                                 |
| Storage               | MicroSD card socket, SPI                              |
| Input                 | Rotary encoder with center switch and one user button |
| Addressable pixels    | 8 x WS2812-compatible RGB pixels                      |
| Infrared              | Transmitter and receiver                              |
| Microphone            | PDM digital microphone                                |
| Microphone model      | [SPM1423HM4H-B][5]                                    |
| Audio output          | MAX98357A I2S amplifier and two-pin speaker connector |
| Display backlight     | AW9364 16-level pulse-controlled backlight driver     |
| Battery charger       | [TI BQ25896][3]                                       |
| Battery gauge         | [TI BQ27220][4]                                       |
| Battery               | 3.7 V, 1300 mAh                                       |

[1]: https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf "ESP32-S3-WROOM-1U"
[2]: https://www.ti.com/product/CC1101 "TI CC1101"
[3]: https://www.ti.com/product/BQ25896 "TI BQ25896"
[4]: https://www.ti.com/product/BQ27220 "TI BQ27220"
[5]: https://media.digikey.com/pdf/Data%20Sheets/Knowles%20Acoustics%20PDFs/SPM1423HM4H-B.pdf "SPM1423HM4H"
[6]: https://datasheet4u.com/datasheets/Nordic/NRF24L01/829184 "nRF24L01"

### Display Specifications

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

### Pin Map

The LilyGoLib board definition is available in the
[temporary T-Embed-Plus Arduino variant](../../arduino/temporary_t_embed_plus/variants/lilygo_tembed_plus/pins_arduino.h).

| Function                       | GPIO          | Notes                                                         |
| ------------------------------ | ------------- | ------------------------------------------------------------- |
| Shared peripheral power enable | 15            | High enables CC1101, WS2812, and MAX98357A power              |
| I2C SDA                        | 8             | Shared by PN532, BQ25896, and BQ27220                         |
| I2C SCL                        | 18            | Shared by PN532, BQ25896, and BQ27220                         |
| SPI MOSI                       | 9             | Shared by display, MicroSD, CC1101, and optional nRF24        |
| SPI MISO                       | 10            | Shared by display, MicroSD, CC1101, and optional nRF24        |
| SPI SCK                        | 11            | Shared by display, MicroSD, CC1101, and optional nRF24        |
| Display CS                     | 41            | Active low                                                    |
| Display DC                     | 16            | Data/command selection                                        |
| Display reset                  | Not connected | No independent display reset is used by LilyGoLib             |
| Display backlight              | 21            | AW9364 pulse input; 16 brightness levels                      |
| MicroSD CS                     | 13            | Active low                                                    |
| Rotary encoder A               | 4             | Encoder phase A                                               |
| Rotary encoder B               | 5             | Encoder phase B                                               |
| Rotary encoder center / BOOT   | 0             | Active-low input and ESP32-S3 strapping pin                   |
| User button                    | 6             | Active-low application input                                  |
| WS2812 data                    | 14            | Drives eight addressable pixels                               |
| Infrared transmit              | 2             | Infrared transmitter enable/data                              |
| Infrared receive               | 1             | Infrared receiver input                                       |
| PDM microphone data            | 42            | Digital microphone input                                      |
| PDM microphone clock           | 39            | Digital microphone clock                                      |
| Speaker I2S BCLK               | 46            | ESP32-S3 to MAX98357A                                         |
| Speaker I2S LRCLK              | 40            | ESP32-S3 to MAX98357A                                         |
| Speaker I2S data out           | 7             | ESP32-S3 to MAX98357A                                         |
| PN532 reset / power-down       | 45            | `RSTPD_N`; named `RF_RST` in the schematic                    |
| PN532 interrupt                | 17            | Interrupt input                                               |
| CC1101 CS                      | 12            | Active low                                                    |
| CC1101 GDO0 / interrupt        | 3             | Connected interrupt signal used by LilyGoLib                  |
| CC1101 GDO2                    | 38            | Additional CC1101 digital signal                              |
| CC1101 RF switch SW1           | 47            | Selects the RF matching path together with SW0                |
| CC1101 RF switch SW0           | 48            | Selects the RF matching path together with SW1                |
| CC1101 reset                   | Not connected | CC1101 has no dedicated reset GPIO in the board definition    |
| Optional nRF24 CS / UART RX    | 44            | Cannot be used as UART RX while the nRF24 interface is active |
| Optional nRF24 CE / UART TX    | 43            | Cannot be used as UART TX while the nRF24 interface is active |
| Optional nRF24 interrupt       | Not connected | LilyGoLib uses polling for this optional module               |
| USB D-                         | 19            | Native ESP32-S3 USB                                           |
| USB D+                         | 20            | Native ESP32-S3 USB                                           |

> [!IMPORTANT]
>
> GPIO15 controls one shared peripheral power rail. Driving it high powers the
> CC1101, the eight WS2812 pixels, and the MAX98357A speaker amplifier. Driving
> it low turns all three off; it cannot switch only one of these peripherals.

> [!IMPORTANT]
>
> The display, MicroSD card, CC1101, and optional nRF24 module share one SPI
> bus. Keep every inactive device's chip-select signal high and serialize SPI
> transactions. LilyGoLib initializes these chip-select pins before starting
> the bus clients.

> [!NOTE]
>
> GPIO43 and GPIO44 are multiplexed between the external UART and the optional
> nRF24 module. The nRF24 module is an expansion module and is not populated on
> the main board.

### CC1101 RF Path Selection

| Frequency path | SW1 (GPIO47) | SW0 (GPIO48) |
| -------------- | ------------ | ------------ |
| 315 MHz        | High         | Low          |
| 434 MHz        | High         | High         |
| 868 / 915 MHz  | Low          | High         |

LilyGoLib selects the RF path when the CC1101 frequency is changed. The CC1101
GDO0 line is connected to GPIO3 and can be used for interrupt-driven receive
and transmit completion handling.

### I2C Device Addresses

| Device                  | 7-bit address | Shared bus |
| ----------------------- | ------------- | ---------- |
| PN532 NFC controller    | 0x24          | Yes        |
| BQ27220 battery gauge   | 0x55          | Yes        |
| BQ25896 battery charger | 0x6B          | Yes        |

### Power Notes

* LilyGoLib drives GPIO15 high before it probes the CC1101 or initializes the
  WS2812 pixels and MAX98357A amplifier.
* The BQ25896 manages battery charging and the BQ27220 reports battery state.
* The display backlight is not PWM controlled. The AW9364 advances through 16
  brightness levels using pulses on GPIO21.
* GPIO0 is both the encoder center switch and the ESP32-S3 download-mode
  strapping pin. Keep it released during a normal reset.

### Electrical Specifications

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

### Resources

* [LILYGO T-Embed-Plus product page](https://www.lilygo.cc/products/t-embed-cc1101-plus)
* [LILYGO T-Embed-Plus reference repository](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101)
* [T-Embed-Plus V1.0 schematic](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/master/hardware/T-Embed-CC1101%20V1.0%2024-07-29.pdf)
* [LilyGoLib T-Embed-Plus board class](../../src/LilyGo_T_Embed_Plus.h)
