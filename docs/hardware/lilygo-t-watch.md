<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch (2019)🌟</h1>

## `1` Overview

* This page lists the hardware specifications for the original touch-screen `LilyGO T-Watch (2019)`.
* LilyGoLib covers the standard base backplate and the PN532 NFC backplate. LoRa, GNSS, cellular, game, and other legacy backplates are not covered here.
* The backplates are alternatives: a watch fitted with the NFC backplate does not also have the standard backplate's MicroSD socket.

### Supported Backplates

| Backplate     | Hardware exposed to LilyGoLib                                       |
| ------------- | ------------------------------------------------------------------- |
| Standard base | MicroSD socket; motor and speaker signals are also physically wired |
| PN532 NFC     | PN532 NFC reader; buzzer signal is physically wired; no MicroSD     |

> [!NOTE]
>
> LilyGoLib manages MicroSD and PN532 detection. The motor, speaker, and buzzer signals are listed for hardware reference but are not exposed through the current `LilyGoWatch` audio, haptic, or buzzer interfaces.

### ✨ Hardware Features

| Feature          | Specification                                        |
| ---------------- | ---------------------------------------------------- |
| SoC              | [Espressif ESP32-D0WDQ6][1], dual-core up to 240 MHz |
| Wireless         | 2.4 GHz Wi-Fi and Bluetooth                          |
| Flash            | 16 MB                                                |
| PSRAM            | 8 MB                                                 |
| Display          | 1.54-inch ST7789 IPS LCD, 240 x 240, SPI             |
| Capacitive Touch | FT6236/FT6336 family, separate I2C bus               |
| Accelerometer    | [Bosch BMA423][2] or [Bosch BMA456][5]               |
| Real-Time Clock  | [NXP PCF8563][3]                                     |
| Power Management | X-Powers AXP202                                      |
| User button      | GPIO36                                               |
| Storage          | MicroSD on the standard base backplate               |
| NFC              | [NXP PN532][4] on the optional NFC backplate         |

[1]: https://www.espressif.com/en/products/socs/esp32 "ESP32"
[2]: https://www.bosch-sensortec.com/products/motion-sensors/accelerometers/bma423/ "BMA423"
[3]: https://www.nxp.com/products/PCF8563 "PCF8563"
[4]: https://www.nxp.com/products/PN532_C1 "PN532"
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
| Touch controller | FT6236/FT6336 |
| Touch interface  | I2C           |

### 📍 [Pin Map](https://github.com/espressif/arduino-esp32/blob/3.3.10/variants/twatch/pins_arduino.h)

#### Main Watch

| Function          | GPIO          | Notes                                      |
| ----------------- | ------------- | ------------------------------------------ |
| I2C SDA           | 21            | Shared by PMU, RTC, BMA4xx, and backplate  |
| I2C SCL           | 22            | Shared by PMU, RTC, BMA4xx, and backplate  |
| Touch SDA         | 23            | Separate touch bus                         |
| Touch SCL         | 32            | Separate touch bus                         |
| Touch interrupt   | 38            | Active low                                 |
| Touch reset       | Not connected |                                            |
| RTC interrupt     | 37            |                                            |
| AXP202 interrupt  | 35            | Input-only GPIO                            |
| BMA4xx interrupt  | 39            | Input-only GPIO                            |
| User button       | 36            | Input-only GPIO                            |
| Display MOSI      | 19            |                                            |
| Display MISO      | Not connected | The display is write-only                  |
| Display SCK       | 18            |                                            |
| Display CS        | 5             | Active low                                 |
| Display DC        | 27            | Data/command selection                     |
| Display reset     | Not connected | Reset is handled by the power sequence     |
| Display backlight | 12            | Backlight power is supplied by AXP202 LDO2 |

#### Standard Base Backplate

| Function        | GPIO | Notes                         |
| --------------- | ---- | ----------------------------- |
| MicroSD CS      | 13   | Active low                    |
| MicroSD MOSI    | 15   | Dedicated HSPI bus            |
| MicroSD MISO    | 2    | ESP32 strapping pin           |
| MicroSD SCK     | 14   | Dedicated HSPI bus            |
| Vibration motor | 33   | Physical backplate connection |
| Speaker         | 25   | Physical backplate connection |

#### PN532 NFC Backplate

| Function    | GPIO | Notes                   |
| ----------- | ---- | ----------------------- |
| PN532 SDA   | 21   | Shared main I2C bus     |
| PN532 SCL   | 22   | Shared main I2C bus     |
| PN532 IRQ   | 34   | Input-only GPIO         |
| PN532 reset | 33   |                         |
| Buzzer      | 13   | Backplate buzzer signal |

> [!IMPORTANT]
>
> GPIO2 and GPIO15 are ESP32 strapping pins used by the standard backplate's MicroSD bus. Remove the MicroSD card before uploading firmware. LilyGoLib probes the PN532 before mounting MicroSD; when a PN532 responds, MicroSD remains unavailable until reboot.

### 🧑🏼‍🔧 I2C Device Addresses

| Device                          | 7-Bit Address | Bus                |
| ------------------------------- | ------------- | ------------------ |
| Touch Panel FT6236/FT6336       | 0x38          | Separate touch I2C |
| Accelerometer BMA423/BMA456     | 0x19          | Shared main I2C    |
| Power Manager AXP202            | 0x35          | Shared main I2C    |
| Real-Time Clock PCF8563         | 0x51          | Shared main I2C    |
| PN532 on optional NFC backplate | 0x24          | Shared main I2C    |

### ⚡ Power Management Channels

| Channel | Peripherals                         |
| ------- | ----------------------------------- |
| DCDC2   | Unused                              |
| DCDC3   | ESP32; must remain enabled          |
| LDO1    | Fixed internal rail                 |
| LDO2    | Display backlight                   |
| LDO3    | Backplate power supply              |
| LDO4    | Unused for the supported backplates |

* LilyGoLib configures the default battery charge current to 190 mA.
* The standard backplate MicroSD bus defaults to 4 MHz for compatibility.

### Resources

* [LilyGoLib T-Watch implementation](../../src/LilyGoWatch.h)
* [Legacy version repository](https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library)
