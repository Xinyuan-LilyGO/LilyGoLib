<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-Ultra🌟</h1>

## `1` Overview

* This page introduces the hardware parameters related to `LilyGO T-Watch-Ultra`

```bash

        /---------------------\
        |                     |
┌────── +                     -
|       |                   USB-C  ────────┐
|       |      410 x 502      -            |
|       |       (AMOLED)      +    ────┐   |
|       |                     -        |   └── Used for charging and programming
|       |                SD SOCKET ─┐  |       No external power supply function
|  ┌─── +                     -     |  |  
|  |    |                     |     |  └──────Click to reset the device, 
|  |    |                     |     |    it cannot be programmed or controlled by the program
|  |    \---------------------/     |  
|  |                                └───────── Supports up to 
|  |                                        32 GB SD memory card   
|  └─────── (GPIO0) Custom Button or                
|         Enter download Mode      
|                                  
└────────── PWR  Button            
    In shutdown state, press 1 second to turn on
    In the power-on state, press for 6 seconds to shut down
    Programmable, can monitor key status in program
```

### ✨ Hardware-Features

| Features                         | Params                                |
| -------------------------------- | ------------------------------------- |
| SOC                              | [Espressif ESP32-S3][1]               |
| Flash                            | 16MB(QSPI)                            |
| PSRAM                            | 8MB (QSPI)                            |
| GNSS                             | [UBlox MIA-M10Q][2]                   |
| LoRa                             | [Semtech SX1262][3]                   |
| NFC                              | [ST25R3916][4]                        |
| Smart sensor                     | [Bosch BHI260AP][5]                   |
| Real-Time Clock                  | [NXP PCF85063A][6]                    |
| Power Manage                     | [X-Powers AXP2101][7]                 |
| PDM Microphone                   | [TDK T3902][8]                        |
| GPIO Expand                      | [XINLUDA XL9555][9]                   |
| PCM Class D Amplifier            | [Analog MAX98357A (3.2W Class D)][10] |
| Haptic driver                    | [Ti DRV2605][11]                      |
| Capacitive Touch                 | CST9217                               |
| SD Card Socket                   | ✅️ Maximum 32GB (FAT32 format)         |
| External low speed clock crystal | ✅️                                     |

> \[!TIP]
> 
> * SD card only supports FAT format, please pay attention to the selection of SD format
> * ST25R3916 (NFC) does not have an integrated capacitive sensor, which means that to read a card, the reader must be turned on, and the presence of a card cannot be detected by turning on the capacitive sensor.
> * ESP32-S3 uses an external QSPI Flash and PSRAM solution, not a built-in PSRAM or Flash solution


[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.u-blox.com/en/product/mia-m10-series "UBlox MIA-M10Q"
[3]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"
[4]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[5]: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ab "BHI260AP"
[6]: https://www.nxp.com/products/PCF85063A "PCF85063A"
[7]: http://www.x-powers.com/en.php/Info/product_detail/article_id/95 "AXP2101"
[8]: https://invensense.tdk.com/products/digital/t3902/ "T3902"
[9]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[10]: https://www.analog.com/en/products/max98357a.html "MAX98357A"
[11]: https://www.ti.com/product/DRV2605 "DRV2605"

### ✨ Display-Features

| Features              | Params        |
| --------------------- | ------------- |
| Resolution            | 410 x 502     |
| Display Size          | 2.06 Inch     |
| Luminance on surface  | 600 nit       |
| Driver IC             | CO5300 (QSPI) |
| Contrast ratio        | 60000:1       |
| Display Colors        | 16.7M         |
| View Direction        | All(AMOLED)   |
| Operating Temperature | -20～70°C     |

### 📍 [Pins Map](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_twatch_ultra/pins_arduino.h)

| Name                                 | GPIO NUM                    | Free |
| ------------------------------------ | --------------------------- | ---- |
| SDA                                  | 3                           | ❌    |
| SCL                                  | 2                           | ❌    |
| SPI MOSI                             | 34                          | ❌    |
| SPI MISO                             | 33                          | ❌    |
| SPI SCK                              | 35                          | ❌    |
| SD CS                                | 21                          | ❌    |
| SD MOSI                              | Share with SPI bus          | ❌    |
| SD MISO                              | Share with SPI bus          | ❌    |
| SD SCK                               | Share with SPI bus          | ❌    |
| RTC(**PCF85063A**) SDA               | Share with I2C bus          | ❌    |
| RTC(**PCF85063A**) SCL               | Share with I2C bus          | ❌    |
| RTC(**PCF85063A**) Interrupt         | 1                           | ❌    |
| NFC(**ST25R3916**) CS                | 4                           | ❌    |
| NFC(**ST25R3916**) Interrupt         | 5                           | ❌    |
| NFC(**ST25R3916**) MOSI              | Share with SPI bus          | ❌    |
| NFC(**ST25R3916**) MISO              | Share with SPI bus          | ❌    |
| NFC(**ST25R3916**) SCK               | Share with SPI bus          | ❌    |
| Sensor(**BHI260**) Interrupt         | 8                           | ❌    |
| Sensor(**BHI260**) SDA               | Share with I2C bus          | ❌    |
| Sensor(**BHI260**) SCL               | Share with I2C bus          | ❌    |
| PCM Amplifier(**MAX98357A**) BCLK    | 9                           | ❌    |
| PCM Amplifier(**MAX98357A**) WCLK    | 10                          | ❌    |
| PCM Amplifier(**MAX98357A**) DOUT    | 11                          | ❌    |
| GNSS(**MIA-M10Q**) TX                | 43                          | ❌    |
| GNSS(**MIA-M10Q**) RX                | 44                          | ❌    |
| GNSS(**MIA-M10Q**) PPS               | 13                          | ❌    |
| LoRa(**SX1262 or SX1280**) SCK       | Share with SPI bus          | ❌    |
| LoRa(**SX1262 or SX1280**) MISO      | Share with SPI bus          | ❌    |
| LoRa(**SX1262 or SX1280**) MOSI      | Share with SPI bus          | ❌    |
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
| Charger(**AXP2101**) SDA             | Share with I2C bus          | ❌    |
| Charger(**AXP2101**) SCL             | Share with I2C bus          | ❌    |
| Charger(**AXP2101**) Interrupt       | 7                           | ❌    |
| Haptic Driver(**DRV2605**) SDA       | Share with I2C bus          | ❌    |
| Haptic Driver(**DRV2605**) SCL       | Share with I2C bus          | ❌    |
| Expand(**XL9555**) SDA               | Share with I2C bus          | ❌    |
| Expand(**XL9555**) SCL               | Share with I2C bus          | ❌    |
| Expand(**XL9555**) GPIO6             | Haptic Driver Enable        | ❌    |
| Expand(**XL9555**) GPIO7             | Display Power supply enable | ❌    |
| Expand(**XL9555**) GPIO10            | Touchpad Reset              | ❌    |
| Expand(**XL9555**) GPIO12            | SD Insert Detect            | ❌    |

### 🧑🏼‍🔧 I2C Devices Address

| Devices                        | 7-Bit Address | Share Bus |
| ------------------------------ | ------------- | --------- |
| Touch Panel CST9217            | 0x1A          | ✅️         |
| [Expands IO XL9555][9]         | 0x20          | ✅️         |
| [Smart sensor BHI260AP][5]     | 0x28          | ✅️         |
| [Power Manager AXP2101][7]     | 0x34          | ✅️         |
| [Real-Time Clock PCF85063A][6] | 0x51          | ✅️         |
| [Haptic driver DRV2605][11]    | 0x5A          | ✅️         |

### ⚡ PowerManage Channel

| CHIP       | Peripherals          |
| ---------- | -------------------- |
| DC1        | **ESP32-S3**         |
| DC2        | Unused               |
| DC3        | Unused               |
| DC4        | Unused               |
| DC5        | Unused               |
| LDO1(VRTC) | **RTC & GPS Backup** |
| ALDO1      | **Display**          |
| ALDO2      | **SDCard**           |
| ALDO3      | **LoRa**             |
| ALDO4      | **Sensor**           |
| BLDO1      | **GNSS**             |
| BLDO2      | **Speaker**          |
| DLDO1      | **NFC**              |
| CPUSLDO    | Unused               |
| VBACKUP    | Unused               |

### ⚡ Electrical parameters

| Features             | Details                     |
| -------------------- | --------------------------- |
| 🔗USB-C Input Voltage | 3.9V-6V                     |
| ⚡Charge Current      | 0-1024mA (\(Programmable\)) |
| 🔋Battery Voltage     | 3.7V                        |
| 🔋Battery capacity    | 1100mA (\(4.07Wh\))         |

### ⚡ Power consumption reference

| Mode        | Wake-Up Mode                                | Current |
| ----------- | ------------------------------------------- | ------- |
| Light-Sleep | PowerButton + BootButton + TouchPanel       | 4.6mA   |
| Light-Sleep | PowerButton + BootButton                    | 2.1mA   |
| DeepSleep   | PowerButton + BootButton (Backup power on)  | 1.1mA   |
| DeepSleep   | PowerButton + BootButton (Backup power off) | 840uA   |
| DeepSleep   | TouchPanel                                  | 3.34mA  |
| DeepSleep   | Timer (Backup power on)                     | 850uA   |
| DeepSleep   | Timer (Backup power off)                    | 1.1mA   |
| Power OFF   | Only keep the backup power                  | 77uA    |

### Resource

* [Schematic]()
