<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Embed🌟</h1>

## `1` Overview

* This page explains how to use the original `LILYGO T-Embed` with LilyGoLib.
* The original T-Embed is distinct from T-Embed-Plus and T-Embed CC1101.
* See the [T-Embed hardware reference](./hardware/lilygo-t-embed.md) for
  hardware specifications, interfaces, GPIO assignments, and I2C addresses.

## `2` Arduino IDE Quick Start

1. Install the [latest Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install [Arduino-ESP32 **3.3.11** or later](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
   through Boards Manager.
   * Espressif stable Boards Manager URL:
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Check whether `LilyGo T-Embed` is available under `Tools` > `Board`.
   * If it is available, select it and continue with step 6.
   * If it is not available, add the following URL under `File` > `Preferences`
     > `Additional Boards Manager URLs`:
     * `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_embed/package_lilygo_tembed_index.json`
4. Open Boards Manager, search for
   `LILYGO T-Embed Temporary Support`, and install it.
5. Select `LilyGo T-Embed` under `Tools` > `Board`.
6. [Download the LilyGoLib library](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip).
7. In Arduino IDE, select `Sketch` > `Include Library` > `Add .ZIP Library`,
   then select the LilyGoLib ZIP file downloaded in step 6.
8. [Download LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty),
   then copy each library folder inside it to the Arduino sketchbook's
   `libraries` directory. Do not copy the enclosing `LilyGoLib-ThirdParty`
   folder itself.
   * See [Find sketches, libraries, board cores, and other files on your computer](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer)
     if you need to locate the sketchbook.
   * The default library locations are:
     * Windows: `C:\Users\{username}\Documents\Arduino\libraries`
     * macOS: `/Users/{username}/Documents/Arduino/libraries`
     * Linux: `/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty contains tested dependency versions, which may not be the
> latest available. Confirm that the hardware works before updating them. If an
> update causes a problem, restore the tested version listed under


9. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
10. Configure the options under `Tools` as shown below.

    | Arduino IDE Setting                  | Value                           |
    | ------------------------------------ | ------------------------------- |
    | Board                                | **LilyGo T-Embed**              |
    | Port                                 | Your port                       |
    | USB CDC On Boot                      | Enabled                         |
    | CPU Frequency                        | 240 MHz                         |
    | Core Debug Level                     | None                            |
    | USB DFU On Boot                      | Disabled                        |
    | Erase All Flash Before Sketch Upload | Disabled                        |
    | Events Run On                        | Core 1                          |
    | Arduino Runs On                      | Core 1                          |
    | USB Firmware MSC On Boot             | Disabled                        |
    | Partition Scheme                     | 16M Flash (3MB APP/9.9MB FATFS) |
    | Upload Mode                          | UART0 / Hardware CDC            |
    | Upload Speed                         | 921600                          |
    | USB Mode                             | Hardware CDC and JTAG           |

11. Select the device under `Tools` > `Port`.
12. Click **Upload** and wait for compilation and flashing to finish.
13. If the upload fails or the USB serial port repeatedly connects and
    disconnects, [manually enter download mode](#t-embed-enter-download-mode)
    and try again.

> [!TIP]
>
> * LilyGoLib requires Arduino-ESP32 3.3.11 or later. Earlier versions will
>   produce compilation errors.
> * The temporary board package supplies the board macros, pin definition,
>   16 MB flash setting, and 8 MB OPI PSRAM setting required by LilyGoLib.
> * The optional SI473x receiver shield is detected at runtime. The Factory
>   example only displays the T-Embed Radio app when the receiver responds.
> * When an official `LilyGo T-Embed` board becomes available, uninstall the
>   temporary package and select the official board.

### Migrating to the Official Board Definition

After T-Embed support is included in a stable Arduino-ESP32 release:

1. Open Boards Manager and uninstall `LILYGO T-Embed Temporary Support`.
2. Update `esp32 by Espressif Systems` to the stable version containing
   T-Embed support.
3. Select the official `LilyGo T-Embed` board under `Tools` > `Board`.
4. Recompile the sketch.

<a id="t-embed-enter-download-mode"></a>

## `3` Entering Download Mode on the T-Embed

Use these steps if the board cannot be uploaded normally:

1. Connect the board to the computer through USB-C.
2. Press and hold the center of the rotary encoder. This is the BOOT switch on
   GPIO0.
3. While holding BOOT, press and release the reset button.
4. Release the rotary encoder.
5. Start the upload again.

> [!NOTE]
>
> The rotary encoder center switch is also the normal application input on
> GPIO0. Keep it released during a normal reset.
>

## Restore Factory Firmware Online

Open [LilyGo Flash Lab](https://xinyuan-lilygo.github.io/LilyGoLib/web-flasher/) in desktop Chrome or Edge and select the factory firmware matching your board and radio variant. See the [firmware guide](../firmware/README.md#lilygo-flash-lab) for download-mode, flashing, and restart instructions.
