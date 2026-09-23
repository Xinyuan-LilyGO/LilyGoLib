<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch (2019)🌟</h1>

## `1` Overview

* This page explains how to use the original `LilyGO T-Watch (2019)` with LilyGoLib.
* LilyGoLib supports the touch-screen model with either the standard base backplate or the PN532 NFC backplate. Other legacy backplates are outside the scope of this guide.
* See the [T-Watch (2019) hardware reference](./hardware/lilygo-t-watch.md) for hardware specifications, backplate differences, GPIO assignments, and I2C addresses.

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
> LilyGoLib-ThirdParty contains tested dependency versions, which may not be the latest available. Confirm that the hardware works before updating them. If an update causes a problem, restore the tested version listed under [T-Watch third-party libraries](./third_party.md#t-watch-third-party-libraries).

6. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
7. Configure the options under `Tools` as shown below.

   | Arduino IDE Setting                  | Value                                      |
   | ------------------------------------ | ------------------------------------------ |
   | Board                                | **TTGO T-Watch**                           |
   | Port                                 | Your port                                  |
   | Board Revision                       | **T-Watch Base**                           |
   | PSRAM                                | **Enabled**                                |
   | Partition Scheme                     | **Default (2 x 6.5 MB app, 3.6 MB SPIFFS)** |
   | Core Debug Level                     | None                                       |
   | Erase All Flash Before Sketch Upload | Disabled                                   |
   | Upload Speed                         | 921600                                     |

8. Select the device under `Tools` > `Port`.
9. If the standard base backplate contains a MicroSD card, remove the card before uploading.
10. Click **Upload** and wait for compilation and flashing to finish.

> [!TIP]
>
> * Select `T-Watch Base`, not one of the T-Watch 2020 revisions.
> * The NFC backplate is detected at runtime. When it is detected, LilyGoLib disables MicroSD access because the standard and NFC backplates are mutually exclusive.

## `3` Upload Troubleshooting

* Hold the side power key for about two seconds and confirm that the watch is powered on before uploading.
* Remove the MicroSD card from the standard base backplate during upload.
* Confirm that the selected serial port belongs to the watch and that the USB cable supports data.
* If uploading at 921600 fails, retry with an upload speed of 115200.
* On systems where no serial port appears, install the driver required by the onboard USB-to-serial bridge or try another USB port.
