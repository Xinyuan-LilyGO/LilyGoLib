<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Watch-S3-Plus🌟</h1>

## `1` Overview

* This page introduces how to use the `LilyGO T-Watch-S3-Plus`
* As of 2025/04/28, platformio does not support the latest esp-arduino v3 and above. The current supported version is v2.0.17 (based on IDF v4.4.7) , If you need to use platformio for development, please jump [LilyGoLib-PlatformIO](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO)

## `2` Arduino IDE Quick Start

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Install [Arduino ESP32 **V3.3.0-alpha1** or later or latest](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
  * Tips : Arduino manager URL: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
3. [Download LilyGoLib Library](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip)
4. Open `Arduino IDE` -> `Sketch` -> `Include Library` -> `Add .ZIP Library` -> `Select the library compressed package downloaded in step 3`
5. [Install LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty)
    * Copy all directories in [LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty) to ArduinoIDE libraries directory, if there is no `libraries` directory, please create it.
    * Please note that instead of copying the `LilyGoLib-ThirdParty` directory, copy the folders in the `LilyGoLib-ThirdParty` directory to libraries
    * How to find the location of your own libraries on your computer, [please see here](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer)
        * Windows: `C:\Users\{username}\Documents\Arduino`
        * macOS: `/Users/{username}/Documents/Arduino`
        * Linux: `/home/{username}/Arduino`
  
> \[!IMPORTANT]
> Please note that the libraries in LilyGoLib-ThirdParty are not necessarily the latest versions. Please do not upgrade the versions of the dependent libraries before confirming that the hardware is running normally.
ArduinoIDE will prompt that there is a new version of the library to upgrade every time it is opened.
Please confirm that it is running normally before trying to update to the latest version. If you encounter problems, please roll back to the version of the dependent library that runs normally. The current list of dependent library versions can be viewed [here](./third_party.md#t-watch-s3-third-party)
>

6. `File` -> `Examples` -> `LilyGOLib` -> `helloworld`
7. `Tools` -> `Board` -> `esp32`,Please select from the table below

   | Arduino IDE Setting                  | Value                             |
   | ------------------------------------ | --------------------------------- |
   | Board                                | **LilyGo T-Watch-S3**             |
   | Port                                 | Your port                         |
   | USB CDC On Boot                      | Enabled                           |
   | CPU Frequency                        | 240MHZ(WiFi)                      |
   | Core Debug Level                     | None                              |
   | USB DFU On Boot                      | Disable                           |
   | Erase All Flash Before Sketch Upload | Disable                           |
   | Events Run On                        | Core 1                            |
   | JTAG Adapter                         | Disable                           |
   | Arduino Runs On                      | Core 1                            |
   | USB Firmware MSC On Boot             | Disable                           |
   | Partition Scheme                     | **16M Flash(3M APP/9.9MB FATFS)** |
   | Board Revision                       | **Radio-SX1262**                  |
   | Upload Mode                          | **UART0/Hardware CDC**            |
   | Upload Speed                         | 921600                            |
   | USB Mode                             | **CDC and JTAG**                  |

8. **Board Revision options**, please select according to the actual RF type purchased. The current options are:
    * Radio-SX1262(Sub 1G LoRa)
    * Radio-SX1280(2.4G LoRa)
    * Radio-CC1101(Sub 1G (G)MSK, 2(G)FSK, 4(G)FSK, ASK, OOK)
    * Radio-LR1121(Sub 1G + 2.4G LoRa)
    * Radio-SI4432(Sub 1G ISM)
9. Select `Port`
10. Click `upload` , Wait for compilation and writing to complete
11. If you cannot upload sketch or the USB device keeps popping up on the computer, please manually put the device into download mode. How to enter download mode, please see the [here](#t-watch-s3-plus-enter-download-mode)

> \[!TIP]
>
> * If there is no message output from the serial port, please check whether USB CDC ON Boot is set to Enabled.
> * Board Revision changes according to the actual RF module model. The current default version is SX1262
> * This library depends on the latest [arduino-esp32](https://github.com/espressif/arduino-esp32/releases/tag/3.3.0-alpha1) version. If it is lower than **V3.3.0-alpha1**, an error will be reported.

### T-Watch-S3-Plus Enter Download Mode

> \[!IMPORTANT]
>
> Download mode is only required when the program is not allowed to upload the sketch. This step is not required under normal circumstances.
>
> 🤖 T-Watch-S3-Plus the BOOT button and RST button have been reserved on the edge of the shell. Please follow the steps below to put the device into download mode.
>
> 1. Connect the board via the USB-C cable
> 2. Press and hold the **BOOT** button , While still pressing the **BOOT** button
> 3. Release the **RST** button
> 4. Release the **BOOT** button
> 5. The USB port should be fixed and will not flash again. You can click Upload.
> 6. Press the **RST** button to exit download mode
>
> If the new code is successfully written, but the device does not light up or has other problems, please use our factory test code to test whether the peripherals can work properly. Please jump here to download the firmware and write it for testing.
>