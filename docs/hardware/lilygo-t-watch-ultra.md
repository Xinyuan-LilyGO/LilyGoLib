<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-Ultra🌟</h1>

## `1` Overview

* This page lists the hardware specifications for the `LilyGO T-Watch-Ultra`.

```text

        /---------------------\
        |                     |
┌────── +                     -
|       |                   USB-C  ────────┐
|       |      410 x 502      -            |
|       |       (AMOLED)      +    ────┐   |
|       |                     -        |   └── Used for charging and programming;
|       |                SD SOCKET ─┐  |       cannot supply power to external devices
|  ┌─── +                     -     |  |  
|  |    |                     |     |  └────── Press to reset the device; this button is
|  |    |                     |     |          not programmable
|  |    \---------------------/     |  
|  |                                └───────── Supports SD cards up to 32 GB
|  └─────── GPIO0: user button and download-mode button
|                                  
└────────── PWR Button
    When off, press for one second to turn on
    When on, press for six seconds to shut down
    Programmable; software can monitor its state
```

### ✨ Hardware Features

| Feature                          | Specification                         |
| -------------------------------- | ------------------------------------- |
| SoC                              | [Espressif ESP32-S3][1]               |
| Flash                            | 16 MB (QSPI)                          |
| PSRAM                            | 8 MB (QSPI)                           |
| GNSS                             | [u-blox MIA-M10Q][2]                  |
| LoRa                             | [Semtech SX1262][3] or SX1280         |
| NFC                              | [ST25R3916][4]                        |
| Smart sensor                     | [Bosch BHI260AP][5]                   |
| Real-Time Clock                  | [NXP PCF85063A][6]                    |
| Power Management                 | [X-Powers AXP2101][7]                 |
| PDM Microphone                   | [TDK T3902][8]                        |
| GPIO Expander                    | [XINLUDA XL9555][9]                   |
| PCM Class D Amplifier            | [Analog MAX98357A (3.2 W Class D)][10] |
| Haptic driver                    | [Ti DRV2605][11]                      |
| Capacitive Touch                 | CST9217                               |
| SD Card Socket                   | ✅️ Up to 32 GB (FAT32)                 |
| External low-speed clock crystal | ✅️                                     |

> [!TIP]
> 
> * Format the SD card with a FAT-compatible filesystem.
> * The ST25R3916 NFC reader has no external capacitive card-presence detection circuit on this board. The reader must be powered to detect a card.
> * The ESP32-S3 uses external QSPI flash and PSRAM; neither is integrated into the SoC.


[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.u-blox.com/en/product/mia-m10-series "u-blox MIA-M10Q"
[3]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"
[4]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[5]: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ab "BHI260AP"
[6]: https://www.nxp.com/products/PCF85063A "PCF85063A"
[7]: http://www.x-powers.com/en.php/Info/product_detail/article_id/95 "AXP2101"
[8]: https://invensense.tdk.com/products/digital/t3902/ "T3902"
[9]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[10]: https://www.analog.com/en/products/max98357a.html "MAX98357A"
[11]: https://www.ti.com/product/DRV2605 "DRV2605"

### ✨ Display Specifications

| Feature               | Specification |
| --------------------- | ------------- |
| Resolution            | 410 x 502     |
| Display Size          | 2.06 inches   |
| Surface Luminance     | 600 nits      |
| Driver IC             | CO5300 (QSPI) |
| Contrast ratio        | 60000:1       |
| Display Colors        | 16.7M         |
| Viewing Angle         | All (AMOLED)  |
| Operating Temperature | -20 to 70 °C  |

### 📍 [Pin Map](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_twatch_ultra/pins_arduino.h)

| Name                                 | GPIO                    | Free |
| ------------------------------------ | --------------------------- | ---- |
| SDA                                  | 3                           | ❌    |
| SCL                                  | 2                           | ❌    |
| SPI MOSI                             | 34                          | ❌    |
| SPI MISO                             | 33                          | ❌    |
| SPI SCK                              | 35                          | ❌    |
| SD CS                                | 21                          | ❌    |
| SD MOSI                              | Shared with SPI bus          | ❌    |
| SD MISO                              | Shared with SPI bus          | ❌    |
| SD SCK                               | Shared with SPI bus          | ❌    |
| RTC(**PCF85063A**) SDA               | Shared with I2C bus          | ❌    |
| RTC(**PCF85063A**) SCL               | Shared with I2C bus          | ❌    |
| RTC(**PCF85063A**) Interrupt         | 1                           | ❌    |
| NFC(**ST25R3916**) CS                | 4                           | ❌    |
| NFC(**ST25R3916**) Interrupt         | 5                           | ❌    |
| NFC(**ST25R3916**) MOSI              | Shared with SPI bus          | ❌    |
| NFC(**ST25R3916**) MISO              | Shared with SPI bus          | ❌    |
| NFC(**ST25R3916**) SCK               | Shared with SPI bus          | ❌    |
| Sensor(**BHI260**) Interrupt         | 8                           | ❌    |
| Sensor(**BHI260**) SDA               | Shared with I2C bus          | ❌    |
| Sensor(**BHI260**) SCL               | Shared with I2C bus          | ❌    |
| PCM Amplifier(**MAX98357A**) BCLK    | 9                           | ❌    |
| PCM Amplifier(**MAX98357A**) WCLK    | 10                          | ❌    |
| PCM Amplifier(**MAX98357A**) DOUT    | 11                          | ❌    |
| GNSS(**MIA-M10Q**) TX                | 43                          | ❌    |
| GNSS(**MIA-M10Q**) RX                | 44                          | ❌    |
| GNSS(**MIA-M10Q**) PPS               | 13                          | ❌    |
| LoRa(**SX1262 or SX1280**) SCK       | Shared with SPI bus          | ❌    |
| LoRa(**SX1262 or SX1280**) MISO      | Shared with SPI bus          | ❌    |
| LoRa(**SX1262 or SX1280**) MOSI      | Shared with SPI bus          | ❌    |
| LoRa(**SX1262 or SX1280**) RESET     | 47                          | ❌    |
| LoRa(**SX1262 or SX1280**) BUSY      | 48                          | ❌    |
| LoRa(**SX1262 or SX1280**) CS        | 36                          | ❌    |
| LoRa(**SX1262 or SX1280**) Interrupt | 14                          | ❌    |
| Display CS                           | 41                          | ❌    |
| Display DATA0                        | 38                          | ❌    |
| Display DATA1                        | 39                          | ❌    |
| Display DATA2                        | 42                          | ❌    |
| Display DATA3                        | 45                          | ❌    |
| Display SCK                          | 40                          | ❌    |
| Display TE                           | 6                           | ❌    |
| Display RESET                        | 37                          | ❌    |
| Charger(**AXP2101**) SDA             | Shared with I2C bus          | ❌    |
| Charger(**AXP2101**) SCL             | Shared with I2C bus          | ❌    |
| Charger(**AXP2101**) Interrupt       | 7                           | ❌    |
| Haptic Driver(**DRV2605**) SDA       | Shared with I2C bus          | ❌    |
| Haptic Driver(**DRV2605**) SCL       | Shared with I2C bus          | ❌    |
| Expander(**XL9555**) SDA               | Shared with I2C bus          | ❌    |
| Expander(**XL9555**) SCL               | Shared with I2C bus          | ❌    |
| Expander(**XL9555**) GPIO6             | Haptic Driver Enable        | ❌    |
| Expander(**XL9555**) GPIO7             | Display Power Supply Enable | ❌    |
| Expander(**XL9555**) GPIO10            | Touchpad Reset              | ❌    |
| Expander(**XL9555**) GPIO12            | SD Insert Detect            | ❌    |

### 🧑🏼‍🔧 I2C Device Addresses

| Device                         | 7-Bit Address | Shared Bus |
| ------------------------------ | ------------- | --------- |
| Touch Panel CST9217            | 0x1A          | ✅️         |
| [GPIO Expander XL9555][9]      | 0x20          | ✅️         |
| [Smart sensor BHI260AP][5]     | 0x28          | ✅️         |
| [Power Manager AXP2101][7]     | 0x34          | ✅️         |
| [Real-Time Clock PCF85063A][6] | 0x51          | ✅️         |
| [Haptic driver DRV2605][11]    | 0x5A          | ✅️         |

### ⚡ Power Management Channels

| CHIP       | Peripherals              |
| ---------- | ------------------------ |
| DC1        | **ESP32-S3**             |
| DC2        | Unused                   |
| DC3        | Unused                   |
| DC4        | Unused                   |
| DC5        | Unused                   |
| LDO1(VRTC) | **GNSS Backup (always on)** |
| ALDO1      | **SD Card**                 |
| ALDO2      | **Display**              |
| ALDO3      | **LoRa**                 |
| ALDO4      | **Sensor**               |
| BLDO1      | **GNSS**                 |
| BLDO2      | **Speaker**              |
| DLDO1      | **NFC**                  |
| CPUSLDO    | Unused                   |
| VBACKUP    | **RTC Button Battery**   |

### ⚡ Electrical Specifications

| Feature              | Specification               |
| -------------------- | --------------------------- |
| 🔗USB-C Input Voltage | 3.9-6 V                    |
| ⚡Charge Current      | 0-1024 mA (programmable)  |
| 🔋Battery Voltage     | 3.7 V                      |
| 🔋Battery Capacity    | 1100 mAh (4.07 Wh)        |

> [!IMPORTANT]
> Use a charging current below 500 mA to prevent the PMU from overheating. Do not charge the battery at a rate greater than 0.5 C.

### ⚡ Power Consumption Reference

| Mode        | Wake Source                                  | Current |
| ----------- | -------------------------------------------- | ------- |
| Light Sleep | Power button + BOOT button + touch panel     | 4.6 mA  |
| Light Sleep | Power button + BOOT button                   | 2.1 mA  |
| Deep Sleep  | Power button + BOOT button (backup power on) | 1.1 mA  |
| Deep Sleep  | Power button + BOOT button (backup power off) | 840 uA |
| Deep Sleep  | Touch panel                                  | 3.34 mA |
| Deep Sleep  | Timer (backup power off)                     | 850 uA  |
| Deep Sleep  | Timer (backup power on)                      | 1.1 mA  |
| Power Off   | Backup power only                            | 77 uA   |

### Resources

* [Schematic](../../schematic/T-Watch%20Ultra%20V1.0%20SCH%2025-07-24.pdf)
