<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">LilyGo T-Deck</h1>

## `1` Overview

* This page lists the hardware specifications for the original `LILYGO T-Deck`.
* T-Deck Plus uses the same main GPIO mapping but adds an onboard GNSS module.
  This page identifies the differences where they affect the available interfaces.
* This page does not apply to T-Deck V2.

### Hardware Variants

* The SX1262 LoRa module is optional. Check the product variant before using the
  radio or connecting an antenna.
* The original T-Deck does not have onboard GNSS. An external GNSS module can
  use the Grove UART on GPIO43 and GPIO44.
* T-Deck Plus connects its onboard GNSS module to GPIO43 and GPIO44, so its
  Grove connector is not available as a general-purpose UART.

### Controls and Connectors

| Item                        | Description                                                                 |
| --------------------------- | --------------------------------------------------------------------------- |
| Power switch                | Controls device power; slide it toward the USB-C port to turn the device on |
| Reset button                | Resets the ESP32-S3; it is not a programmable input                         |
| Trackball                   | Four direction switches plus a center switch                                |
| Trackball center switch     | Also connected to GPIO0 and used as the download-mode button                |
| Keyboard                    | Mini keyboard managed by a dedicated ESP32-C3 over I2C                      |
| USB-C                       | Power, charging, firmware upload, USB CDC, and native USB functions         |
| MicroSD slot                | SPI storage sharing the display and LoRa SPI bus                            |
| Grove connector             | HY2.0-4P UART connector on the original T-Deck                              |
| Keyboard programming header | Six-pin header for programming the keyboard's ESP32-C3                      |

### Hardware Features

| Feature             | Specification                                                       |
| ------------------- | ------------------------------------------------------------------- |
| SoC                 | [Espressif ESP32-S3FN16R8][1], dual-core LX7 up to 240 MHz          |
| Wireless            | 2.4 GHz Wi-Fi and Bluetooth 5 LE                                    |
| Flash               | 16 MB                                                               |
| PSRAM               | 8 MB OPI                                                            |
| Display             | 2.8-inch ST7789 IPS LCD, 320 x 240, SPI                             |
| Touch controller    | GT911 capacitive touch, I2C                                         |
| Keyboard controller | ESP32-C3 I2C slave                                                  |
| Pointing device     | Five-switch trackball                                               |
| LoRa                | Optional [Semtech SX1262][2], 433/868/915 MHz product variants      |
| LoRa transmit power | Up to +22 dBm                                                       |
| Microphone ADC      | Everest Semiconductor ES7210; MIC1 and MIC3 are connected           |
| Speaker             | I2S output with onboard amplifier                                   |
| Display backlight   | 16-level pulse-controlled backlight driver                          |
| Storage             | MicroSD card socket, SPI                                            |
| Battery monitoring  | 2:1 resistor divider connected to ADC GPIO4                         |
| GNSS                | External module through the Grove UART; onboard only on T-Deck Plus |

[1]: https://www.espressif.com/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262 "Semtech SX1262"

### Display Specifications

| Feature           | Specification                                  |
| ----------------- | ---------------------------------------------- |
| Resolution        | 320 x 240 in the default landscape orientation |
| Native resolution | 240 x 320                                      |
| Display size      | 2.8 inches                                     |
| Display type      | IPS LCD                                        |
| Driver IC         | ST7789                                         |
| Interface         | SPI                                            |
| Touch controller  | GT911 capacitive touch                         |
| Touch interface   | I2C                                            |
| Backlight range   | 0 to 16                                        |

### Pin Map

The board definition is available in the
[T-Deck Arduino variant](../../arduino/temporary_tdeck/variants/lilygo_tdeck/pins_arduino.h).

| Function                | GPIO          | Notes                                              |
| ----------------------- | ------------- | -------------------------------------------------- |
| Peripheral power enable | 10            | Set high before accessing board peripherals        |
| Battery ADC             | 4             | Voltage is measured through a 2:1 divider          |
| I2C SDA                 | 18            | Shared by touch, keyboard, and ES7210              |
| I2C SCL                 | 8             | Shared by touch, keyboard, and ES7210              |
| SPI MOSI                | 41            | Shared by display, MicroSD, and LoRa               |
| SPI MISO                | 38            | Shared by display, MicroSD, and LoRa               |
| SPI SCK                 | 40            | Shared by display, MicroSD, and LoRa               |
| Display CS              | 12            | Active low                                         |
| Display DC              | 11            | Data/command selection                             |
| Display reset           | Not connected | Reset is handled by the board power sequence       |
| Display backlight       | 42            | 16-level pulse-controlled brightness               |
| Touch interrupt         | 16            | GT911 interrupt                                    |
| Touch reset             | Not connected | The controller is initialized without a reset GPIO |
| Keyboard interrupt      | 46            | Reserved by the keyboard interface                 |
| Trackball right         | 2             | Active-low switch                                  |
| Trackball up            | 3             | Active-low switch                                  |
| Trackball left          | 1             | Active-low switch                                  |
| Trackball down          | 15            | Active-low switch                                  |
| Trackball center / BOOT | 0             | ESP32-S3 strapping pin and download-mode button    |
| Speaker I2S WS          | 5             | Word select                                        |
| Speaker I2S BCLK        | 7             | Bit clock                                          |
| Speaker I2S data out    | 6             | ESP32-S3 to amplifier                              |
| ES7210 MCLK             | 48            | Microphone master clock                            |
| ES7210 WS               | 21            | Microphone word select                             |
| ES7210 BCLK             | 47            | Microphone bit clock                               |
| ES7210 data in          | 14            | ES7210 to ESP32-S3                                 |
| MicroSD CS              | 39            | Active low                                         |
| LoRa CS                 | 9             | Optional SX1262; active low                        |
| LoRa BUSY               | 13            | Optional SX1262                                    |
| LoRa RESET              | 17            | Optional SX1262                                    |
| LoRa DIO1 / interrupt   | 45            | Optional SX1262                                    |
| UART TX / GNSS TX       | 43            | Grove UART on the original T-Deck                  |
| UART RX / GNSS RX       | 44            | Grove UART on the original T-Deck                  |

> [!IMPORTANT]
>
> The display, MicroSD card, and SX1262 share one SPI bus. Only one chip-select
> signal may be low during an SPI transaction. LilyGoLib manages this when its
> board APIs are used.

> [!NOTE]
>
> GPIO0 is both the trackball center switch and the ESP32-S3 download-mode
> strapping pin. Keep the trackball released during reset for normal startup.

### I2C Device Addresses

| Device                       | 7-bit address | Shared bus |
| ---------------------------- | ------------- | ---------- |
| GT911 touch controller       | 0x14 or 0x5D  | Yes        |
| ES7210 microphone ADC        | 0x40          | Yes        |
| ESP32-C3 keyboard controller | 0x55          | Yes        |

### Power Notes

* GPIO10 enables the board peripheral power rail. LilyGoLib drives it high
  during board initialization.
* The original T-Deck has no software-controlled PMIC or fuel gauge. Battery
  voltage is estimated through GPIO4, so charge current, battery temperature,
  and precise state of charge are not available.
* The display backlight is not controlled by PWM. Its driver advances through
  16 brightness levels using pulses on GPIO42.

### Electrical Specifications

| Feature                  | Specification  |
| ------------------------ | -------------- |
| USB-C Input Voltage      | 4.7-6 V        |
| Charge Current           | 500 mA (fixed) |
| Battery Voltage          | 3.7 V          |
| Battery Capacity         | 2000 mAh       |
| Charge Temperature Range | 0-60 °C        |

### Resources

* [LILYGO T-Deck product page](https://lilygo.cc/products/t-deck)
* [LILYGO T-Deck source repository](https://github.com/Xinyuan-LilyGO/T-Deck)
* [T-Deck schematic](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/schematic.pdf)
* [T-Deck board pin definitions](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h)
