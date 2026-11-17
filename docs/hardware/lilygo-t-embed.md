<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Embed🌟</h1>

## `1` Overview

* This page lists the hardware specifications for the original
  `LILYGO T-Embed`.
* This board does not include CC1101 or PN532 hardware. Pin names for those
  devices in older reference examples describe obsolete or optional expansion
  work and are not part of the LilyGoLib capability definition.
* An optional SI473x receiver shield is supported and detected at runtime.

### Controls and Connectors

| Item                  | Description                                                     |
| --------------------- | --------------------------------------------------------------- |
| Rotary encoder        | Clockwise/counterclockwise navigation with a center push switch |
| Encoder center / BOOT | Application input on GPIO0 and ESP32-S3 download-mode switch    |
| Reset button          | Resets the ESP32-S3; it is not a programmable input             |
| USB-C                 | Power, charging, firmware upload, USB CDC, and native USB       |
| MicroSD slot          | Storage on a dedicated SPI controller                           |
| Speaker output        | MAX98357A I2S amplifier output                                  |
| Expansion interface   | Supports the optional SI473x receiver shield                    |

### Hardware Features

| Feature            | Specification                                         |
| ------------------ | ----------------------------------------------------- |
| SoC                | [Espressif ESP32-S3][1], dual-core LX7 up to 240 MHz  |
| Wireless           | 2.4 GHz Wi-Fi and Bluetooth LE                        |
| Flash              | 16 MB                                                 |
| PSRAM              | 8 MB OPI                                              |
| Display            | ST7789 LCD, 170 x 320 native resolution, SPI          |
| Storage            | MicroSD card socket, SPI                              |
| Input              | Rotary encoder with center switch                     |
| Addressable pixels | 7 x APA102-compatible RGB pixels                      |
| Microphone ADC     | ES7210, four inputs arranged as two microphone groups |
| Audio output       | [MAX98357A][2] I2S amplifier                          |
| Display backlight  | AW9364 16-level pulse-controlled backlight driver     |
| Battery charger    | TP4065B                                               |
| Battery monitoring | 2:1 resistor divider connected to ADC GPIO4           |
| Battery            | 900mA                                                 |
| Optional radio     | SI473x receiver shield, I2C                           |
| NFC                | Not fitted                                            |

[1]: https://www.espressif.com/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.analog.com/en/products/max98357a.html "MAX98357A"

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
[temporary T-Embed Arduino variant](../../arduino/temporary_t_embed/variants/lilygo_tembed/pins_arduino.h).

| Function                       | GPIO          | Notes                                                 |
| ------------------------------ | ------------- | ----------------------------------------------------- |
| Shared peripheral power enable | 46            | High enables ES7210, MAX98357A, APA102, and MicroSD   |
| Battery ADC                    | 4             | Battery voltage is measured through a 2:1 divider     |
| I2C SDA                        | 18            | Shared by ES7210 and the optional SI473x shield       |
| I2C SCL                        | 8             | Shared by ES7210 and the optional SI473x shield       |
| Display MOSI                   | 11            | Display SPI data                                      |
| Display MISO                   | Not connected | The display does not return SPI data                  |
| Display SCK                    | 12            | Display SPI clock                                     |
| Display CS                     | 10            | Active low                                            |
| Display DC                     | 13            | Data/command selection                                |
| Display reset                  | 9             | Active-low display reset                              |
| Display backlight              | 15            | AW9364 pulse input; 16 brightness levels              |
| Rotary encoder A               | 2             | Encoder phase A                                       |
| Rotary encoder B               | 1             | Encoder phase B                                       |
| Rotary encoder center / BOOT   | 0             | Active-low input and ESP32-S3 strapping pin           |
| APA102 data                    | 42            | Drives seven addressable pixels                       |
| APA102 clock                   | 45            | Addressable pixel clock                               |
| Speaker I2S BCLK               | 7             | ESP32-S3 to MAX98357A                                 |
| Speaker I2S LRCLK              | 5             | ESP32-S3 to MAX98357A                                 |
| Speaker I2S data out           | 6             | ESP32-S3 to MAX98357A                                 |
| ES7210 BCLK                    | 47            | Microphone bit clock                                  |
| ES7210 LRCLK                   | 21            | Microphone word select                                |
| ES7210 data in                 | 14            | ES7210 to ESP32-S3                                    |
| ES7210 MCLK                    | 48            | Microphone master clock                               |
| MicroSD CS                     | 39            | Active low                                            |
| MicroSD SCK                    | 40            | Dedicated MicroSD SPI clock                           |
| MicroSD MOSI                   | 41            | ESP32-S3 to MicroSD                                   |
| MicroSD MISO                   | 38            | MicroSD to ESP32-S3                                   |
| Optional SI473x reset          | 16            | Receiver reset on the optional shield                 |
| Optional SI473x audio mute     | 17            | Controls the optional shield's analog audio amplifier |
| USB D-                         | 19            | Native ESP32-S3 USB                                   |
| USB D+                         | 20            | Native ESP32-S3 USB                                   |

> [!IMPORTANT]
>
> GPIO46 controls one shared peripheral power rail. Driving it low affects the
> ES7210 codec, MAX98357A amplifier, seven APA102 pixels, and MicroSD card.
> LilyGoLib keeps this rail enabled during light sleep and only turns it off
> before deep sleep.

> [!IMPORTANT]
>
> The display and MicroSD card use separate ESP32-S3 SPI controllers. Their pin
> assignments are not a shared SPI bus and must not be combined into one bus
> definition.

> [!NOTE]
>
> GPIO0 is both the rotary encoder center switch and the ESP32-S3 download-mode
> strapping pin. Keep the encoder released during a normal reset.

### I2C Device Addresses

| Device                   | 7-bit address | Shared bus |
| ------------------------ | ------------- | ---------- |
| ES7210 microphone ADC    | 0x40          | Yes        |
| Optional SI473x receiver | 0x11 or 0x63  | Yes        |

The optional SI473x receiver is probed during board initialization. The Factory
example only exposes the T-Embed Radio app when the receiver responds. GPIO17
controls the shield's analog audio amplifier mute; this analog output is
separate from the onboard MAX98357A digital I2S output.

### Power Notes

* LilyGoLib drives GPIO46 high before it initializes the ES7210, MAX98357A,
  APA102 pixels, or MicroSD card.
* The board has no software-controlled PMIC or fuel gauge. Battery voltage and
  state of charge are estimated through the GPIO4 ADC divider.
* The display backlight is not PWM controlled. The AW9364 advances through 16
  brightness levels using pulses on GPIO15.
* Light sleep turns off the display backlight and places the ST7789 in sleep
  mode while GPIO46 remains high. Deep sleep places the display in sleep mode
  and then drives GPIO46 low.
* GPIO0 can wake the ESP32-S3 from light sleep or deep sleep. Timer wake-up is
  also supported for deep sleep.

### Electrical Specifications

| Feature                  | Specification  |
| ------------------------ | -------------- |
| USB-C input voltage      | 4.7-6 V        |
| Battery voltage          | 3.7 V          |
| Battery capacity         | 900mA          |
| Charge current           | 500 mA (fixed) |
| Charge temperature range | 0~60°C         |

### Resources

* [LILYGO T-Embed product page](https://www.lilygo.cc/products/t-embed)
* [LILYGO T-Embed reference repository](https://github.com/Xinyuan-LILYGO/T-Embed)
* [T-Embed schematic](https://github.com/Xinyuan-LILYGO/T-Embed/blob/master/schematic/schematic.pdf)
* [T-Embed SI473x shield schematic](https://github.com/Xinyuan-LILYGO/T-Embed/blob/master/schematic/T-Embed-SI4732.pdf)
* [LilyGoLib T-Embed board class](../../src/LilyGo_T_Embed.h)
