<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-2020-V3🌟</h1>

## `1` Overview

* This page explains how to use the `LilyGO T-Watch-2020-V3` with LilyGoLib.
* See the [T-Watch-2020-V3 hardware reference](./hardware/lilygo-t-watch-2020-v3.md) for hardware specifications, GPIO assignments, power channels, and I2C addresses.
* T-Watch-2020-V3 has integrated audio, microphone, vibration, and infrared hardware. It does not use the replaceable backplates made for the original 2019 T-Watch.

## `2` Arduino IDE Quick Start

1. Install the [latest Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install [Arduino-ESP32 **3.3.10** or later](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) through the Boards Manager.
   * Espressif stable Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. [Download the LilyGoLib library](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip).
4. In the Arduino IDE, select `Sketch` > `Include Library` > `Add .ZIP Library`, then select the ZIP file downloaded in step 3.
5. [Download LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty), then copy each library folder inside it to your Arduino sketchbook's `libraries` directory. Do not copy the enclosing `LilyGoLib-ThirdParty` folder itself.
   * See [Find sketches, libraries, board cores, and other files on your computer](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer) if you need to locate your sketchbook.
   * The default library locations are:
     * Windows: `C:\\Users\\{username}\\Documents\\Arduino\\libraries`
     * macOS: `/Users/{username}/Documents/Arduino/libraries`
     * Linux: `/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty contains tested dependency versions, which may not be the latest available. Confirm that the hardware works before updating them. If an update causes a problem, restore the tested version listed under [T-Watch 2020 V3 third-party libraries](./third_party.md#t-watch-2020-v3-third-party-libraries).

6. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
7. Configure the options under `Tools` as shown below.

   | Arduino IDE Setting | Value                                       |
   | ------------------- | ------------------------------------------- |
   | Board               | **TTGO T-Watch**                            |
   | Port                | Your port                                   |
   | Board Revision      | **T-Watch-2020-V3**                         |
   | PSRAM               | **Enabled**                                 |
   | Partition Scheme    | **Default (2 x 6.5 MB app, 3.6 MB SPIFFS)** |
   | Core Debug Level    | None                                        |
   | Upload Speed        | 921600                                      |

8. Select the device under `Tools` > `Port`.
9. Click **Upload** and wait for compilation and flashing to finish.

> [!TIP]
>
> * `Board Revision` is required. Selecting `T-Watch Base` uses the 2019 pin map and will configure several peripherals incorrectly.
> * T-Watch-2020-V3 has no MicroSD socket. Use the internal flash filesystem when an example needs storage.

## `3` Upload Troubleshooting

* Hold the side power key for about two seconds and confirm that the watch is powered on before uploading.
* Confirm that the selected serial port belongs to the watch and that the USB cable supports data.
* If uploading at 921600 fails, retry with an upload speed of 115200.
* On systems where no serial port appears, install the driver required by the onboard USB-to-serial bridge or try another USB port.
