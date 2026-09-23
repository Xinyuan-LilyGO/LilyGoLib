<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Deck🌟</h1>

## `1` Overview

* This page explains how to use the `LilyGO T-Deck` with LilyGoLib.
* See the [T-Deck hardware reference](./hardware/lilygo-t-deck.md) for hardware specifications, interfaces, GPIO assignments, and I2C addresses.

## `2` Arduino IDE Quick Start

1. Install the [latest Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install [Arduino-ESP32 **3.3.0** or later](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) through the Boards Manager.
   * Espressif stable Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Check whether `LILYGO T-Deck` is available under `Tools` > `Board`.
   * If it is available, select it and continue with step 6.
   * If it is not available, add the following URL under `File` > `Preferences` > `Additional Boards Manager URLs`:
     * `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_tdeck/package_lilygo_tdeck_index.json`
4. Open Boards Manager, search for `LILYGO T-Deck Temporary Support`, and install it.
5. Select `LILYGO T-Deck (Temporary)` under `Tools` > `Board`.
   * The temporary board package supports the original T-Deck only, not T-Deck V2.
6. [Download the LilyGoLib library](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip).
7. In the Arduino IDE, select `Sketch` > `Include Library` > `Add .ZIP Library`, then select the ZIP file downloaded in step 6.
8. [Download LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty), then copy each library folder inside it to your Arduino sketchbook's `libraries` directory. Do not copy the enclosing `LilyGoLib-ThirdParty` folder itself.
   * See [Find sketches, libraries, board cores, and other files on your computer](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer) if you need to locate your sketchbook.
   * The default library locations are:
     * Windows: `C:\Users\{username}\Documents\Arduino\libraries`
     * macOS: `/Users/{username}/Documents/Arduino/libraries`
     * Linux: `/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty contains tested dependency versions, which may not be the latest available. Confirm that the hardware works before updating them. If an update causes a problem, restore the tested version listed under [T-Deck third-party libraries](./third_party.md#t-deck-third-party).

9. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
10. Configure the options under `Tools` as shown below.

   | Arduino IDE Setting                  | Value                                              |
   | ------------------------------------ | -------------------------------------------------- |
   | Board                                | **LILYGO T-Deck** or **LILYGO T-Deck (Temporary)** |
   | Port                                 | Your port                                          |
   | Core Debug Level                     | None                                               |
   | Erase All Flash Before Sketch Upload | Disabled                                           |
   | Upload Speed                         | 921600                                             |

11. Select the device under `Tools` > `Port`.
12. Click **Upload** and wait for compilation and flashing to finish.
13. If the upload fails or the USB serial port repeatedly connects and disconnects, [manually enter download mode](#t-deck-enter-download-mode) and try again.

> [!TIP]
>
> * If the serial monitor shows no output, verify that a T-Deck board is selected.
> * LilyGoLib requires Arduino-ESP32 3.3.0 or later. Earlier versions will produce compilation errors.
> * When the official `LILYGO T-Deck` option becomes available, uninstall `LILYGO T-Deck Temporary Support` and select the official board.

### Migrating to the Official Board Definition

After T-Deck support is included in a stable Arduino-ESP32 release:

1. Open Boards Manager and uninstall `LILYGO T-Deck Temporary Support`.
2. Update `esp32 by Espressif Systems` to the stable version containing T-Deck
   support.
3. Select the official `LILYGO T-Deck` board under `Tools` > `Board`.
4. Recompile the sketch.

<a id="t-deck-enter-download-mode"></a>

### Entering Download Mode on the T-Deck

> [!IMPORTANT]
>
> Use these steps if the USB port repeatedly connects and disconnects or if a sketch cannot be uploaded normally. This may be necessary after installing third-party firmware such as Meshtastic.
>
> Follow these steps to put the device into download mode:
>
>  1. Slide the power switch to the OFF position (toward the SD card slot).
>  2. Connect the USB-C cable to the device.
>  3. Slide the power switch to the ON position (toward the USB-C port).
>  4. Press and hold the trackball's center button.
>  5. While holding the trackball button, press and release the reset button on the left side of the device.
>  6. Release the trackball button.
>  7. Upload the firmware.
>
> If flashing succeeds but the device does not start or a peripheral does not work, flash the [factory test firmware](../firmware/README.md) to verify the hardware.
