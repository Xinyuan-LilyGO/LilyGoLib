<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-2020-V3🌟</h1>

## `1` Overview

* This page lists the hardware specifications for the `LilyGO T-Watch-2020-V3`.
* T-Watch-2020-V3 uses the original ESP32 and integrates its speaker amplifier, microphone, vibration motor, and infrared transmitter on the watch hardware.
* The replaceable 2019 T-Watch backplates are not supported by this model.

### ✨ Hardware Features

| Feature               | Specification                                        |
| --------------------- | ---------------------------------------------------- |
| SoC                   | [Espressif ESP32-D0WDQ6][1], dual-core up to 240 MHz |
| Wireless              | 2.4 GHz Wi-Fi and Bluetooth                          |
| Flash                 | 16 MB                                                |
| PSRAM                 | 8 MB                                                 |
| Display               | 1.54-inch ST7789 IPS LCD, 240 x 240, SPI             |
| Capacitive Touch      | FT6336                                               |
| Accelerometer         | [Bosch BMA423][2] or [Bosch BMA456][5]               |
| Real-Time Clock       | [NXP PCF8563][3]                                     |
| Power Management      | X-Powers AXP202                                      |
| PDM Microphone        | SPM1423HM4H-B                                        |
| PCM Class D Amplifier | [Analog Devices MAX98357A][4]                        |
| Haptic Feedback       | GPIO-driven vibration motor                          |
| Infrared              | Infrared transmitter                                 |
| Storage               | Internal flash filesystem; no MicroSD socket         |

[1]: https://www.espressif.com/en/products/socs/esp32 "ESP32"
[2]: https://www.bosch-sensortec.com/products/motion-sensors/accelerometers/bma423/ "BMA423"
[3]: https://www.nxp.com/products/PCF8563 "PCF8563"
[4]: https://www.analog.com/en/products/max98357a.html "MAX98357A"
[5]: https://www.bosch-sensortec.com/products/motion-sensors/accelerometers/bma456/ "BMA456"

> [!TIP]
>
> Bosch BMA423 is being discontinued. Depending on the production batch, your watch may use a BMA456/BMA456H replacement, and later batches will transition to that sensor. LilyGoLib probes both models at runtime. To identify the installed sensor, upload the [BMA4XX Sensor Model example](../../examples/sensor/BMA4XX_SensorModel/BMA4XX_SensorModel.ino), open the serial monitor at 115200 baud, and check whether `Detected model` reports `BMA423` or `BMA456H (BMA456 replacement)`.

### ✨ Display Specifications

| Feature          | Specification |
| ---------------- | ------------- |
| Resolution       | 240 x 240     |
| Display size     | 1.54 inches   |
| Display type     | IPS LCD       |
| Driver IC        | ST7789        |
| Interface        | SPI           |
| Touch controller | FT6336        |
| Touch interface  | I2C           |

### 📍 Pin Map

| Function             | GPIO          | Notes                                      |
| -------------------- | ------------- | ------------------------------------------ |
| I2C SDA              | 21            | Shared by PMU, RTC, and sensor             |
| I2C SCL              | 22            | Shared by PMU, RTC, and sensor             |
| Touch SDA            | 23            | Separate touch bus                         |
| Touch SCL            | 32            | Separate touch bus                         |
| Touch interrupt      | 38            | Active low                                 |
| Touch reset          | 14            |                                            |
| RTC interrupt        | 37            |                                            |
| AXP202 interrupt     | 35            | Input-only GPIO                            |
| BMA4xx interrupt     | 39            | Input-only GPIO                            |
| Display MOSI         | 19            |                                            |
| Display MISO         | Not connected | The display is write-only                  |
| Display SCK          | 18            |                                            |
| Display CS           | 5             | Active low                                 |
| Display DC           | 27            | Data/command selection                     |
| Display reset        | Not connected | Reset is handled by the power sequence     |
| Display backlight    | 15            | Backlight power is supplied by AXP202 LDO2 |
| PDM microphone clock | 0             | ESP32 strapping pin                        |
| PDM microphone data  | 2             | ESP32 strapping pin                        |
| MAX98357A BCLK       | 26            | I2S bit clock                              |
| MAX98357A WCLK       | 25            | I2S word select                            |
| MAX98357A data out   | 33            | ESP32 to amplifier                         |
| Vibration motor      | 4             | Active-high motor control                  |
| Infrared transmitter | 13            |                                            |

> [!IMPORTANT]
>
> GPIO0 and GPIO2 are ESP32 strapping pins. Do not drive the microphone signals externally while the watch is resetting or entering download mode.

### 🧑🏼‍🔧 I2C Device Addresses

| Device                        | 7-Bit Address | Bus                |
| ----------------------------- | ------------- | ------------------ |
| Touch Panel FT6336            | 0x38          | Separate touch I2C |
| Accelerometer BMA423/BMA456   | 0x19          | Shared main I2C    |
| Power Manager AXP202          | 0x35          | Shared main I2C    |
| Real-Time Clock PCF8563       | 0x51          | Shared main I2C    |

### ⚡ Power Management Channels

| Channel | Peripherals                |
| ------- | -------------------------- |
| DCDC2   | Unused                     |
| DCDC3   | ESP32; must remain enabled |
| LDO1    | Fixed internal rail        |
| LDO2    | Display backlight          |
| LDO3    | Unused                     |
| LDO4    | MAX98357A audio amplifier  |
| LDOIO   | Unused                     |

* LilyGoLib configures the default battery charge current to 125 mA.

### Resources

* [T-Watch-2020-V3 schematic](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library/blob/master/Schematic/T_WATCH-2020V03.pdf)
* [Legacy version repository](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library)
