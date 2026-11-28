<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Embed-Plus V2🌟</h1>

## `1` Overview

* This page explains how to use the `LILYGO T-Embed-Plus V2` with LilyGoLib.
* V2 is a separate hardware design from T-Embed-Plus V1. Do not select the
  original T-Embed-Plus board definition for V2 hardware.
* See the [T-Embed-Plus V2 hardware reference](./hardware/lilygo-t-embed-plus-v2.md)
  for hardware specifications, GPIO assignments, shared SPI devices, and
  XL9555 power-control channels.

## `2` Arduino IDE Quick Start

1. Install the [latest Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Install [Arduino-ESP32 **3.3.11** or later](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
   through Boards Manager.
   * Espressif stable Boards Manager URL:
     `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Check whether `LilyGo T-Embed-Plus V2` is available under `Tools` > `Board`.
   * If it is available, select it and continue with step 6.
   * If it is not available, add the following URL under `File` > `Preferences`
     > `Additional Boards Manager URLs`:
     * `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_embed_plus/package_lilygo_tembed_plus_index.json`
4. Open Boards Manager, search for
   `LILYGO T-Embed-Plus Temporary Support`, and install it.
5. Select `LilyGo T-Embed-Plus V2` under `Tools` > `Board`.
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
> [T-Embed-Plus V2 third-party libraries](./third_party.md#t-embed-plus-v2-third-party).
>
> V2 uses the ST25R3916 NFC controller and does not use the T-Embed-Plus V1
> PN532 integration. Do not install or select the V1 PN532-specific setup for V2.

9. Open `File` > `Examples` > `LilyGoLib` > `helloworld`.
10. Configure the options under `Tools` as shown below.

    | Arduino IDE Setting                  | Value                                 |
    | ------------------------------------ | ------------------------------------- |
    | Board                                | **LilyGo T-Embed-Plus V2**            |
    | Port                                 | Your port                             |
    | USB CDC On Boot                      | Enabled                               |
    | CPU Frequency                        | 240 MHz (WiFi)                        |
    | Core Debug Level                     | None                                  |
    | USB DFU On Boot                      | Disabled                              |
    | Erase All Flash Before Sketch Upload | Disabled                              |
    | Events Run On                        | Core 1                                |
    | Arduino Runs On                      | Core 1                                |
    | USB Firmware MSC On Boot             | Disabled                              |
    | Partition Scheme                     | 16M Flash (3MB APP/9.9MB FATFS)       |
    | Upload Mode                          | UART0 / Hardware CDC                  |
    | Upload Speed                         | 921600                                |
    | USB Mode                             | Hardware CDC and JTAG                 |

11. Select the device under `Tools` > `Port`.
12. Click **Upload** and wait for compilation and flashing to finish.
13. If the upload fails or the USB serial port repeatedly connects and
    disconnects, [manually enter download mode](#t-embed-plus-v2-enter-download-mode)
    and try again.

> [!TIP]
>
> * LilyGoLib requires Arduino-ESP32 3.3.11 or later. Earlier versions may
>   produce compilation errors.
> * The temporary board package supplies the V2 board macro, V2 pin definition,
>   16 MB flash, 8 MB OPI PSRAM, hardware USB CDC, and the 3 MB application
>   partition.
> * V2 uses a separate board macro, `ARDUINO_T_EMBED_PLUS_V2`; it must not be
>   compiled together with `ARDUINO_T_EMBED_PLUS`.

### Migrating to the Official Board Definition

After T-Embed-Plus V2 support is included in a stable Arduino-ESP32 release:

1. Open Boards Manager and uninstall
   `LILYGO T-Embed-Plus Temporary Support`.
2. Update `esp32 by Espressif Systems` to the stable version containing
   T-Embed-Plus V2 support.
3. Select the official `LilyGo T-Embed-Plus V2` board under `Tools` > `Board`.
4. Recompile the sketch.

<a id="t-embed-plus-v2-enter-download-mode"></a>

## `3` Entering Download Mode on the T-Embed-Plus V2

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

## `4` V2 Hardware and ESP32-C5 AT Link

V2 has a separate hardware path from T-Embed-Plus V1:

* The on-board nRF24, display, MicroSD, CC1101, and ST25R3916 share one SPI
  bus. The V2 board initializes inactive chip-select signals high before the
  display starts.
* The ST25R3916 NFC controller is on SPI, not I2C.
* The XL9555 at `0x22` controls the CC1101 RF path, display reset, shared
  peripheral power, amplifier shutdown, ESP32-C5 reset, and module selection.
* The SY6970 charger and BQ27220 gauge are configured for the 1300 mAh
  battery. LilyGoLib uses a 640 mA charge-current setting.
* V2 has eight WS2812-compatible RGB pixels, infrared TX/RX, a PDM microphone,
  and a MAX98357A I2S audio output.

The ESP32-C5 is a separate AT co-processor. The board is shipped with the
ESP-AT firmware already programmed; the corresponding firmware is stored in
the repository under the [`firmware/esp-at/`](../firmware/esp-at/) directory.
Its firmware is not programmed by the ESP32-S3 upload process. Espressif's
default ESP32-C5-4MB ESP-AT firmware uses C5 UART1 GPIO23 (TX) and GPIO24 (RX)
at 115200 baud. The crossed V2 link is C5 GPIO23 (TX) to S3 GPIO48 (RX), and C5
GPIO24 (RX) to S3 GPIO47 (TX). C5 UART0 GPIO11 (TX) and GPIO12 (RX) remain the
download/log port.

GPIO47 and GPIO48 are shared with the on-board nRF24 CE and CS signals through
the SN74LVC1G3157DTBR analog switches. XL9555 P06 selects the route: HIGH
connects the pair to C5 UART1 and LOW connects it to nRF24. XL9555 P07 (`EX_PWR_EN1`) enables C5 power; P10 (`EX_PWR_EN2`)
enables nRF24L01 power. Both signals are active HIGH and control separate supplies. Applications must select the C5
route before opening UART1, pulse P05 low then high to reset the module, and
restore P06 LOW before using the nRF24 interface.

The C5 firmware must be flashed separately. Uploading `t_embed_plus_v2` only
programs the ESP32-S3. For a blank C5 flash, use the AT image from the
downloaded archive at address `0x000000`; the archive's
`build/flasher_args.json` lists the individual-image offsets when a split flash
is required.

The example applications can expose separate **C5 WiFi Scan** and **C5 BLE
Scan** pages. Their result lists use the same focused-row layout as regular WiFi
and BLE pages; C5 WiFi rows also label whether an access point is 2.4 GHz or
5 GHz. These pages select the C5 side of the shared GPIO47/48 switch before
opening UART1. Leaving either page closes UART1, turns off C5 power (P07 LOW),
and restores the nRF24 side and its supply (P10 HIGH). Scanning starts automatically
with asynchronous boot and AT command handling so the UI remains responsive.
Each completed scan is followed by a five-second interval before the next scan;
results stay visible and C5 remains powered until the page is closed.

References:

* [Espressif ESP-AT GitHub releases](https://github.com/espressif/esp-at/releases)
* [ESP32-C5 AT firmware list](https://docs.espressif.com/projects/esp-at/en/latest/esp32c5/AT_Binary_Lists/esp_at_binaries.html)
* [ESP32-C5 ESP-AT Wi-Fi scan commands](https://docs.espressif.com/projects/esp-at/en/latest/esp32c5/AT_Command_Set/Wi-Fi_AT_Commands.html#at-cwlap-list-available-aps)
* [ESP32-C5 ESP-AT BLE scan commands](https://docs.espressif.com/projects/esp-at/en/latest/esp32c5/AT_Command_Set/BLE_AT_Commands.html#at-blescan-enable-bluetooth-le-scanning)
* [ESP32-C5 AT port pins](https://docs.espressif.com/projects/esp-at/en/latest/esp32c5/Compile_and_Develop/How_to_set_AT_port_pin.html)
* [ESP32-C5 AT firmware archive](../firmware/esp-at/esp32c5-4MB-at-github-actions.zip)

## Restore Factory Firmware Online

Open [LilyGo Flash Lab](https://xinyuan-lilygo.github.io/LilyGoLib/web-flasher/) in desktop Chrome or Edge and select the factory firmware matching your board and radio variant. See the [firmware guide](../firmware/README.md#lilygo-flash-lab) for download-mode, flashing, and restart instructions.
