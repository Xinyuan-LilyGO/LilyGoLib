<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-S3🌟</h1>


## `1` Overview

* This page lists the hardware specifications for the `LilyGO T-Watch-S3`.

```text

  /---------------------\
  |                     |
  |                     -
  |                    PWR Button ────┐
  |      240 x 240      -             |
  |       (IPS)         |             |   Programmable; software can monitor its state
  |                     -             └── When off, press for two seconds to turn on
  |               Micro-USB ─┐        When on, press for six seconds to shut down
  |                     |    |
  |                     |    |
  |                     |    |
  \---------------------/    └───────── Used for charging and programming;
                                        cannot supply power to external devices
```

### ✨ Hardware Features

| Feature               | Specification                        |
| --------------------- | ------------------------------------ |
| SoC                   | [Espressif ESP32-S3][1]              |
| Flash                 | 16 MB (QSPI)                         |
| PSRAM                 | 8 MB (OPI)                           |
| LoRa                  | [Semtech SX1262][3] or SX1280        |
| Accelerometer sensor  | [Bosch BMA423][4]                    |
| Real-Time Clock       | [NXP PCF8563][5]                     |
| Power Management      | [X-Powers AXP2101][6]                |
| Haptic driver         | [Ti DRV2605][7]                      |
| PDM Microphone        | [SPM1423HM4H-B][8]                   |
| PCM Class D Amplifier | [Analog MAX98357A (3.2 W Class D)][9] |
| Capacitive Touch      | [FT6336U][10]                        |
| Infrared transmitter  | [IR12-21C][11]                       |

[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[3]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"
[4]: https://www.mouser.com/datasheet/2/783/BSCH_S_A0010021471_1-2525113.pdf "BMA423"
[5]: https://www.nxp.com/products/PCF8563 "PCF8563"
[6]: http://www.x-powers.com/en.php/Info/product_detail/article_id/95 "AXP2101"
[7]: https://www.ti.com/product/DRV2605 "DRV2605"
[8]: https://media.digikey.com/pdf/Data%20Sheets/Knowles%20Acoustics%20PDFs/SPM1423HM4H-B.pdf "SPM1423HM4H-B"
[9]: https://www.analog.com/en/products/max98357a.html "MAX98357A"
[10]: https://buydisplay.com/download/ic/FT6236-FT6336-FT6436L-FT6436_Datasheet.pdf "FT6336U"
[11]: https://www.everlight-led.cn/zh/datasheet-download/item/ir12-21c-tr8-datasheet "IR12-21C"

### ✨ Display Specifications

| Feature               | Specification  |
| --------------------- | -------------- |
| Resolution            | 240 x 240      |
| Display Size          | 1.54 inches    |
| Surface Luminance     | 450 cd/m²      |
| Driver IC             | ST7789V3 (SPI) |
| Contrast ratio        | 800:1          |
| Display Colors        | 262K           |
| Viewing Angle         | All (IPS)      |
| Operating Temperature | -20 to 70 °C   |

### 📍 [Pin Map](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_twatch_s3/pins_arduino.h)

| Name                                 | GPIO           | Free |
| ------------------------------------ | ------------------ | ---- |
| SDA                                  | 10                 | ❌    |
| SCL                                  | 11                 | ❌    |
| Touchpad(**FT6336U**) SDA            | 39                 | ❌    |
| Touchpad(**FT6336U**) SCL            | 40                 | ❌    |
| Touchpad(**FT6336U**) Interrupt      | 16                 | ❌    |
| Touchpad(**FT6336U**) RESET          | Not Connected      | ❌    |
| RTC(**PCF8563**) SDA                 | Shared with I2C bus | ❌    |
| RTC(**PCF8563**) SCL                 | Shared with I2C bus | ❌    |
| RTC(**PCF8563**) Interrupt           | 17                 | ❌    |
| Sensor(**BMA423**) Interrupt         | 14                 | ❌    |
| Sensor(**BMA423**) SDA               | Shared with I2C bus | ❌    |
| Sensor(**BMA423**) SCL               | Shared with I2C bus | ❌    |
| PCM Amplifier(**MAX98357A**) BCLK    | 48                 | ❌    |
| PCM Amplifier(**MAX98357A**) WCLK    | 15                 | ❌    |
| PCM Amplifier(**MAX98357A**) DOUT    | 46                 | ❌    |
| LoRa(**SX1262 or SX1280**) SCK       | 3                  | ❌    |
| LoRa(**SX1262 or SX1280**) MISO      | 4                  | ❌    |
| LoRa(**SX1262 or SX1280**) MOSI      | 1                  | ❌    |
| LoRa(**SX1262 or SX1280**) RESET     | 8                  | ❌    |
| LoRa(**SX1262 or SX1280**) BUSY      | 7                  | ❌    |
| LoRa(**SX1262 or SX1280**) CS        | 5                  | ❌    |
| LoRa(**SX1262 or SX1280**) Interrupt | 9                  | ❌    |
| Display CS                           | 12                 | ❌    |
| Display MOSI                         | 13                 | ❌    |
| Display MISO                         | Not Connected      | ❌    |
| Display SCK                          | 18                 | ❌    |
| Display DC                           | 38                 | ❌    |
| Display RESET                        | Not Connected      | ❌    |
| Display Backlight                    | 45                 | ❌    |
| Charger(**AXP2101**) SDA             | Shared with I2C bus | ❌    |
| Charger(**AXP2101**) SCL             | Shared with I2C bus | ❌    |
| Charger(**AXP2101**) Interrupt       | 21                 | ❌    |
| Haptic Driver(**DRV2605**) SDA       | Shared with I2C bus | ❌    |
| Haptic Driver(**DRV2605**) SCL       | Shared with I2C bus | ❌    |
| PDM Microphone(**SPM1423HM4H**) SCK  | 44                 | ❌    |
| PDM Microphone(**SPM1423HM4H**) DATA | 47                 | ❌    |
| Infrared transmitter                 | 2                  | ❌    |

### 🧑🏼‍🔧 I2C Device Addresses

| Device                           | 7-Bit Address | Shared Bus  |
| -------------------------------- | ------------- | ----------- |
| [Touch Panel FT6336U][10]        | 0x38          | ❌ Use Wire1 |
| [Accelerometer sensor BMA423][4] | 0x19          | ✅️           |
| [Power Manager AXP2101][6]       | 0x34          | ✅️           |
| [Real-Time Clock PCF8563][5]     | 0x51          | ✅️           |
| [Haptic driver DRV2605][7]       | 0x5A          | ✅️           |

### ⚡ Power Management Channels

| Channel    | Peripherals            |
| ---------- | ---------------------- |
| DC1        | **ESP32-S3**           |
| DC2        | Unused                 |
| DC3        | Unused                 |
| DC4        | Unused                 |
| DC5        | Unused                 |
| LDO1(VRTC) | Unused                 |
| ALDO1      | Unused                 |
| ALDO2      | **Display Backlight**  |
| ALDO3      | **Display and Touch**  |
| ALDO4      | **LoRa**               |
| BLDO1      | Unused                 |
| BLDO2      | **DRV2605 Enable**     |
| DLDO1      | Unused                 |
| CPUSLDO    | Unused                 |
| VBACKUP    | **RTC Button Battery** |

### ⚡ Electrical Specifications

| Feature              | Specification               |
| -------------------- | --------------------------- |
| 🔗Micro-USB Input Voltage | 3.9-6 V                  |
| ⚡Charge Current          | 0-1024 mA (programmable) |
| 🔋Battery Voltage         | 3.8 V                    |
| 🔋Battery Capacity        | 470 mAh                  |

> [!IMPORTANT]
> Use a charging current below 130 mA. Excessive charging current can damage the battery.
> If not in use for an extended period, turn the battery switch to OFF.
>

### ⚡ Power Consumption Reference

| Mode        | Wake Source                                  | Current |
| ----------- | -------------------------------------------- | ------- |
| Light Sleep | Power button + BOOT button + touch panel     | 2.38 mA |
| Light Sleep | Power button + BOOT button                   | N/A     |
| Deep Sleep  | Power button + BOOT button (backup power on) | 530 uA  |
| Deep Sleep  | Power button + BOOT button (backup power off) | 460 uA |
| Deep Sleep  | Touch panel                                  | 1.08 mA |
| Deep Sleep  | Timer (backup power on)                      | 510 uA  |
| Deep Sleep  | Timer (backup power off)                     | 460 uA  |
| Power Off   | Backup power only                            | 50 uA   |

* The T-Watch-S3 touch controller has no reset pin connected. If the controller is put to sleep, touch input cannot be restored without restarting the device.

### Resources

* [Motherboard schematic](../../schematic/T_WATCH-S3%2025-03-24.pdf)
