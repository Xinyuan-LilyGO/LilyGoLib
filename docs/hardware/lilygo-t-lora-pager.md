<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-LoRa-Pager🌟</h1>


## `1` Overview

* This page lists the hardware specifications for the `LilyGO T-LoRa-Pager`.

```text

/---------------------------------------------------\
| ┌───────────────────────────────────────────┐ |-| |
| |                                           | |/| |
| |               480 x 222 IPS               | |/| |  
| |                                           | |/| |
| └───────────────────────────────────────────┘ |-| |
|                                                   |
|                                                   |
|                                                   |
|                                                   |
\---|RST|--|BOOT|--|POWER|--|SD SOCKET|--|USB-C|----/
      ^       ^       ^          ^           ^
      |       |       |          |           |
      |       |       |          |           └─── Used for charging and programming; the
      |       |       |          |                USB-C port can also be configured to power
      |       |       |          |                external devices
      |       |       |          |      
      |       |       |          └────── Supports SD cards up to 32 GB
      |       |       |               
      |       |       └───────────── Press for one second to turn on the device; this button
      |       |                     is not programmable
      |       |
      |       └───────────────────── GPIO0: user button and download-mode button
      |
      └───────────────────────────── Press to reset the device; this button is not
                                     programmable






```

### Extension interface

```text

>----------Place the screen facing up---------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

```

* CE is XL9555 GPIO9
* TX is ESP32-S3 GPIO43
* RX is ESP32-S3 GPIO44
* MISO is ESP32-S3 GPIO33
* MOSI is ESP32-S3 GPIO34
* SCK is ESP32-S3 GPIO35
* SDA is ESP32-S3 GPIO3
* SCL is ESP32-S3 GPIO2

### nRF24L01 PA Shield interface

```text
>----------Place the screen facing up---------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

```

* CE is XL9555 GPIO9 and controls the shield's TX/RX mode (`LOW`: RX; `HIGH`: TX)
* TX is ESP32-S3 GPIO43 and connects to the shield's CE pin
* RX is ESP32-S3 GPIO44 and connects to the shield's CS pin
* MISO is ESP32-S3 GPIO33 and connects to the shield's MISO pin
* MOSI is ESP32-S3 GPIO34 and connects to the shield's MOSI pin
* SCK is ESP32-S3 GPIO35 and connects to the shield's SCK pin
* SDA (ESP32-S3 GPIO3) is not connected on the shield
* SCL (ESP32-S3 GPIO2) is not connected on the shield

### ✨ Hardware Features

| Feature                          | Specification                    |
| -------------------------------- | -------------------------------- |
| SoC                              | [Espressif ESP32-S3][1]          |
| Flash                            | 16 MB (QSPI)                     |
| PSRAM                            | 8 MB (QSPI)                      |
| GNSS                             | [u-blox MIA-M10Q][2]             |
| LoRa                             | [Semtech SX1262][3]              |
| NFC                              | [ST25R3916][4]                   |
| Smart sensor                     | [Bosch BHI260AP][5]              |
| Real-Time Clock                  | [NXP PCF85063A][6]               |
| Battery Charger                  | [Ti BQ25896][7]                  |
| Battery Gauge                    | [Ti BQ27220][8]                  |
| Haptic driver                    | [Ti DRV2605][9]                  |
| Audio Codec                      | [Everest-semi ES8311][10]        |
| GPIO Expander                    | [XINLUDA XL9555][11]             |
| I2C Keyboard                     | [Ti TCA8418][12]                 |
| Audio Power Amplifier            | [Nsiway NS4150B (3 W Class D)][13] |
| Display Backlight Driver         | [AW9364 16-Level Led Driver][14] |
| SD Card Socket                   | ✅️ Up to 32 GB (FAT32)            |
| External low-speed clock crystal | ✅️                                |

> [!TIP]
> 
> * Format the SD card with a FAT-compatible filesystem.
> * The device can shut down completely only when USB power is disconnected.
> * The PWR button can only be used to wake up the device by pressing it for one second when the device is turned off. It cannot be used for programming.
> * The ST25R3916 NFC reader has no external capacitive card-presence detection circuit on this board. The reader must be powered to detect a card.
> * The ESP32-S3 uses external QSPI flash and PSRAM; neither is integrated into the SoC.

[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.u-blox.com/en/product/mia-m10-series "u-blox MIA-M10Q"
[3]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"
[4]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[5]: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ab "BHI260AP"
[6]: https://www.nxp.com/products/PCF85063A "PCF85063A"
[7]: https://www.ti.com/product/BQ25896 "BQ25896"
[8]: https://www.ti.com/product/BQ27220 "BQ27220"
[9]: https://www.ti.com/product/DRV2605 "DRV2605"
[10]: http://www.everest-semi.com/pdf/ES8311%20PB.pdf "ES8311"
[11]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[12]: https://www.ti.com/product/TCA8418 "TCA8418"
[13]: http://www.nsiway.com.cn/product/58.html "NS4150B"
[14]: https://item.szlcsc.com/datasheet/AW9364DNR/385721.html "AW9364"

### ✨ Display Specifications

| Feature               | Specification |
| --------------------- | ------------- |
| Resolution            | 480 x 222     |
| Display Size          | 2.33 inches   |
| Surface Luminance     | 450 cd/m²     |
| Driver IC             | ST7796U (SPI) |
| Contrast ratio        | 1000:1        |
| Color gamut           | 70%           |
| PPI                   | 221           |
| Display Colors        | 262K          |
| Viewing Angle         | All (IPS)     |
| Operating Temperature | -20 to 70 °C  |

### 📍 [Pin Map](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_tlora_pager/pins_arduino.h)

| Name                                 | GPIO                       | Free |
| ------------------------------------ | ------------------------------ | ---- |
| Custom Pin                           | GPIO9 (External 12-Pin socket) | ✅️    |
| Uart1 TX                             | 43(External 12-Pin socket)     | ✅️    |
| Uart1 RX                             | 44(External 12-Pin socket)     | ✅️    |
| SDA                                  | 3                              | ❌    |
| SCL                                  | 2                              | ❌    |
| SPI MOSI                             | 34                             | ❌    |
| SPI MISO                             | 33                             | ❌    |
| SPI SCK                              | 35                             | ❌    |
| SD CS                                | 21                             | ❌    |
| SD MOSI                              | Shared with SPI bus             | ❌    |
| SD MISO                              | Shared with SPI bus             | ❌    |
| SD SCK                               | Shared with SPI bus             | ❌    |
| Keyboard(**TCA8418**) SDA            | Shared with I2C bus             | ❌    |
| Keyboard(**TCA8418**) SCL            | Shared with I2C bus             | ❌    |
| Keyboard(**TCA8418**) Interrupt      | 6                              | ❌    |
| Keyboard Backlight                   | 46                             | ❌    |
| Rotary Encoder A                     | 40                             | ❌    |
| Rotary Encoder B                     | 41                             | ❌    |
| Rotary Encoder Center                | 7                              | ❌    |
| RTC(**PCF85063A**) SDA               | Shared with I2C bus             | ❌    |
| RTC(**PCF85063A**) SCL               | Shared with I2C bus             | ❌    |
| RTC(**PCF85063A**) Interrupt         | 1                              | ❌    |
| NFC(**ST25R3916**) CS                | 39                             | ❌    |
| NFC(**ST25R3916**) Interrupt         | 5                              | ❌    |
| NFC(**ST25R3916**) MOSI              | Shared with SPI bus             | ❌    |
| NFC(**ST25R3916**) MISO              | Shared with SPI bus             | ❌    |
| NFC(**ST25R3916**) SCK               | Shared with SPI bus             | ❌    |
| Sensor(**BHI260**) Interrupt         | 8                              | ❌    |
| Sensor(**BHI260**) SDA               | Shared with I2C bus             | ❌    |
| Sensor(**BHI260**) SCL               | Shared with I2C bus             | ❌    |
| Audio Codec(**ES8311**) WS           | 18                             | ❌    |
| Audio Codec(**ES8311**) SCK          | 11                             | ❌    |
| Audio Codec(**ES8311**) MCLK         | 10                             | ❌    |
| Audio Codec(**ES8311**) data out     | 45                             | ❌    |
| Audio Codec(**ES8311**) data in      | 17                             | ❌    |
| Audio Codec(**ES8311**) SDA          | Shared with I2C bus             | ❌    |
| Audio Codec(**ES8311**) SCL          | Shared with I2C bus             | ❌    |
| GNSS(**MIA-M10Q**) TX                | 12                             | ❌    |
| GNSS(**MIA-M10Q**) RX                | 4                              | ❌    |
| GNSS(**MIA-M10Q**) PPS               | 13                             | ❌    |
| LoRa(**SX1262 or SX1280**) SCK       | Shared with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) MISO      | Shared with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) MOSI      | Shared with SPI bus             | ❌    |
| LoRa(**SX1262 or SX1280**) RESET     | 47                             | ❌    |
| LoRa(**SX1262 or SX1280**) BUSY      | 48                             | ❌    |
| LoRa(**SX1262 or SX1280**) CS        | 36                             | ❌    |
| LoRa(**SX1262 or SX1280**) Interrupt | 14                             | ❌    |
| Display CS                           | 38                             | ❌    |
| Display MOSI                         | Shared with SPI bus             | ❌    |
| Display MISO                         | Shared with SPI bus             | ❌    |
| Display SCK                          | Shared with SPI bus             | ❌    |
| Display DC                           | 37                             | ❌    |
| Display RESET                        | Not Connected                  | ❌    |
| Display Backlight(16 Level)          | 42                             | ❌    |
| Gauge(**BQ27220**) SDA               | Shared with I2C bus             | ❌    |
| Gauge(**BQ27220**) SCL               | Shared with I2C bus             | ❌    |
| Charger(**BQ25896**) SDA             | Shared with I2C bus             | ❌    |
| Charger(**BQ25896**) SCL             | Shared with I2C bus             | ❌    |
| Haptic Driver(**DRV2605**) SDA       | Shared with I2C bus             | ❌    |
| Haptic Driver(**DRV2605**) SCL       | Shared with I2C bus             | ❌    |
| Expander(**XL9555**) SDA               | Shared with I2C bus             | ❌    |
| Expander(**XL9555**) SCL               | Shared with I2C bus             | ❌    |
| Expander(**XL9555**) GPIO0             | Haptic Driver Enable           | ❌    |
| Expander(**XL9555**) GPIO1             | Audio Power Amplifier Enable   | ❌    |
| Expander(**XL9555**) GPIO2             | Keyboard RESET                 | ❌    |
| Expander(**XL9555**) GPIO3             | LoRa Power Supply Enable       | ❌    |
| Expander(**XL9555**) GPIO4             | GNSS Power Supply Enable       | ❌    |
| Expander(**XL9555**) GPIO5             | NFC Power Supply Enable        | ❌    |
| Expander(**XL9555**) GPIO6             | ~~Display RESET~~ (No connect) | ❌    |
| Expander(**XL9555**) GPIO7             | GNSS RESET                     | ❌    |
| Expander(**XL9555**) GPIO10            | Keyboard Power Supply Enable   | ❌    |
| Expander(**XL9555**) GPIO11            | External 12-Pin socket         | ✅️    |
| Expander(**XL9555**) GPIO12            | SD Insert Detect               | ❌    |
| Expander(**XL9555**) GPIO14            | SD Power Supply Enable         | ❌    |
<!-- | Expander(**XL9555**) GPIO13            | SD PullUp Enable               | ❌    | -->

### 🧑🏼‍🔧 I2C Device Addresses

| Device                         | 7-Bit Address | Shared Bus |
| ------------------------------ | ------------- | --------- |
| [Codec ES8311][10]             | 0x18          | ✅️         |
| [GPIO Expander XL9555][11]     | 0x20          | ✅️         |
| [Smart sensor BHI260AP][5]     | 0x28          | ✅️         |
| [Real-Time Clock PCF85063A][6] | 0x51          | ✅️         |
| [Battery Charger BQ25896][7]   | 0x6B          | ✅️         |
| [Gauge BQ27220][8]             | 0x55          | ✅️         |
| [Keyboard TCA8418][12]         | 0x34          | ✅️         |
| [Haptic driver DRV2605][9]     | 0x5A          | ✅️         |

### ⚡ Power Management Channels

| Channel                  | Peripherals        |
| ------------------------ | ------------------ |
| Expander(**XL9555**) GPIO0 | **DRV2605 Enable** |
| Expander(**XL9555**) GPIO1 | **Speaker**        |
| Expander(**XL9555**) GPIO3 | **LoRa**           |
| Expander(**XL9555**) GPIO4 | **GNSS**           |
| Expander(**XL9555**) GPIO5 | **NFC**            |
| Expander(**XL9555**) GPIO10 | **Keyboard**      |
| Expander(**XL9555**) GPIO14 | **SD Card**       |

### ⚡ Electrical Specifications

| Feature                    | Specification              |
| -------------------------- | -------------------------- |
| 🔗USB-C Input Voltage       | 3.9-6 V                    |
| 🔗USB-C Output Voltage      | 4.55-5.55 V                |
| ⚡USB-C Output Current      | 0.5-1 A                    |
| ⚡Charge Current            | 0-3008 mA (programmable)   |
| 🔋Battery Voltage           | 3.7 V                      |
| 🔋Battery Capacity          | 1500 mAh (5.55 Wh)         |
| 🔋Charge Temperature Range  | 0-60 °C                    |

> [!IMPORTANT]
> Use a charging current below 750 mA. Do not charge the battery at a rate greater than 0.5 C.

### ⚡ Power Consumption Reference

| Mode        | Wake Source  | Current |
| ----------- | ------------ | ------- |
| Deep Sleep  | BOOT button  | 530 uA  |
| Deep Sleep  | Timer        | 530 uA  |
| Light Sleep | BOOT button  | ~2.26 mA |
| Power Off   | Power button | 26 uA   |

### Resources

* [Radio-SX1262(Sub 1G LoRa and FSK )](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262)
* [Radio-SX1280(2.4G LoRa,FLRC,(G)FSK)](https://www.semtech.cn/products/wireless-rf/lora-connect/sx1280)
* [Radio-CC1101(Sub 1G (G)MSK, 2(G)FSK, 4(G)FSK, ASK, OOK)](https://www.ti.com/product/CC1101)
* [Radio-LR1121(Sub 1G + 2.4G LoRa)](https://www.semtech.com/products/wireless-rf/lora-connect/lr1121)
* [Radio-SI4432(Sub 1G ISM)](https://www.silabs.com/wireless/proprietary/ezradiopro-sub-ghz-ics/device.si4432?tab=specs)
* [Schematic](../../schematic/T-Lora%20Pager%20V1.0%20SCH%2025-06-13.pdf)
