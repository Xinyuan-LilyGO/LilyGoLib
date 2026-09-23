<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-S3🌟</h1>

## `1` Overview

* This page explains how to use the `LilyGO T-Watch-S3` with LilyGoLib.
* See the [T-Watch-S3 hardware reference](./hardware/lilygo-t-watch-s3.md) for hardware specifications, interfaces, GPIO assignments, and I2C addresses.
* For PlatformIO development, use the separate [LilyGoLib-PlatformIO](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO) project.

## `2` Arduino IDE Quick Start

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Install [Arduino-ESP32 **3.3.0-alpha1** or later](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) through the Boards Manager.
   * Additional Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json`
3. [Download the LilyGoLib library](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip).
4. In the Arduino IDE, select `Sketch` > `Include Library` > `Add .ZIP Library`, then select the ZIP file downloaded in step 3.
5. [Download LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty), then copy each library folder inside it to your Arduino sketchbook's `libraries` directory. Do not copy the enclosing `LilyGoLib-ThirdParty` folder itself.
   * See [Find sketches, libraries, board cores, and other files on your computer](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer) if you need to locate your sketchbook.
   * The default library locations are:
     * Windows: `C:\Users\{username}\Documents\Arduino\libraries`
     * macOS: `/Users/{username}/Documents/Arduino/libraries`
     * Linux: `/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty contains tested dependency versions, which may not be the latest available. Confirm that the hardware works before updating them. If an update causes a problem, restore the tested version listed under [the board's third-party libraries](./third_party.md#t-watch-s3-third-party).

6. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
7. Configure the options under `Tools` as shown below.

   | Arduino IDE Setting                  | Value                             |
   | ------------------------------------ | --------------------------------- |
   | Board                                | **LilyGo T-Watch-S3**             |
   | Port                                 | Your port                         |
   | USB CDC On Boot                      | Enabled                           |
   | CPU Frequency                        | 240MHz (WiFi)                     |
   | Core Debug Level                     | None                              |
   | USB DFU On Boot                      | Disabled                          |
   | Erase All Flash Before Sketch Upload | Disabled                          |
   | Events Run On                        | Core 1                            |
   | JTAG Adapter                         | Disabled                          |
   | Arduino Runs On                      | Core 1                            |
   | USB Firmware MSC On Boot             | Disabled                          |
   | Partition Scheme                     | **16M Flash (3MB APP/9.9MB FATFS)** |
   | Board Revision                       | **Radio-SX1262**                  |
   | Upload Mode                          | **UART0 / Hardware CDC**          |
   | Upload Speed                         | 921600                            |
   | USB Mode                             | **Hardware CDC and JTAG**         |

8. Set `Board Revision` to match the radio module fitted to your device:
    * `Radio-SX1262` (sub-GHz LoRa)
    * `Radio-SX1280` (2.4 GHz LoRa)
    * `Radio-CC1101` (sub-GHz (G)MSK, 2(G)FSK, 4(G)FSK, ASK, and OOK)
    * `Radio-LR1121` (sub-GHz and 2.4 GHz LoRa)
    * `Radio-SI4432` (sub-GHz ISM)
9. Select the device under `Tools` > `Port`.
10. Click **Upload** and wait for compilation and flashing to finish.
11. If the upload fails or the USB serial port repeatedly connects and disconnects, [manually enter download mode](#t-watch-s3-enter-download-mode) and try again.

> [!TIP]
>
> * If the serial monitor shows no output, make sure `USB CDC On Boot` is set to `Enabled`.
> * `Radio-SX1262` is the default `Board Revision`. Change it when your device uses a different radio module.
> * LilyGoLib requires Arduino-ESP32 3.3.0-alpha1 or later. Earlier versions will produce compilation errors.

<a id="t-watch-s3-enter-download-mode"></a>

### Entering Download Mode on the T-Watch-S3

> [!IMPORTANT]
>
> Use these steps if the USB port repeatedly connects and disconnects or if a sketch cannot be uploaded normally. This may be necessary after installing third-party firmware such as Meshtastic.
>
> 1. Remove the back cover and carefully lift the battery out of the enclosure. Do not pull on its soldered wires.
> 2. Connect the T-Watch-S3 to the computer with a Micro-USB cable.
> 3. Open Windows Device Manager and expand **Ports (COM & LPT)**.
> 4. Press and hold the crown until the serial port disappears.
> 5. Press and hold the **BOOT** button shown below.
> 6. While holding **BOOT**, press the crown for one second. The serial port should reappear in Device Manager.
> 7. Release the **BOOT** button.
> 8. Select the serial port and upload the firmware.
> 9. After the upload finishes, press and hold the crown to turn the watch off, then turn it on again to exit download mode.
>
> If flashing succeeds but the device does not start or a peripheral does not work, flash the [factory test firmware](../firmware/README.md) to verify the hardware.

#### Boot Button

![twatchs3-bootbutton](./images/twatchs3-bootbutton.jpg)
