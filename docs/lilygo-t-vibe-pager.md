<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo 标志" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Vibe-Pager🌟</h1>

> [!TIP]
> 
> **T-Vibe-Pager** 是 **T-LoRa-Pager** 的 **无LoRa** 版本,所以下方的设置是按照 **T-LoRa-Pager** 来设置的,所以这不是文档错误,请知悉.💕
>

## `1` 概述

* 本文介绍如何通过 LilyGoLib 使用 `LilyGO T-Vibe-Pager`。
* 如需使用 PlatformIO 开发，请参阅独立的 [LilyGoLib-PlatformIO](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO) 项目。
* 有关硬件规格、接口、GPIO 引脚分配和 I2C 地址，请参阅 [T-Vibe-Pager 硬件参考文档](./hardware/lilygo-t-vibe-pager.md)。

## `2` Arduino IDE 快速入门

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)。
2. 通过开发板管理器安装 [Arduino-ESP32 **3.3.0-alpha1** 或更高版本](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)。
   * 开发板管理器附加地址：`https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json`
3. [下载 LilyGoLib 库](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip)。
4. 在 Arduino IDE 中选择 `Sketch` > `Include Library` > `Add .ZIP Library`，然后选择第 3 步下载的 ZIP 文件。
5. [下载 LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty)，将其中的各个库文件夹分别复制到 Arduino 项目文件夹的 `libraries` 目录。不要复制最外层的 `LilyGoLib-ThirdParty` 文件夹。
   * 如果找不到 Arduino 项目文件夹，请参阅[在计算机上查找草图、库、开发板核心及其他文件](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer)。
   * 默认的库目录如下：
     * Windows：`C:\Users\{username}\Documents\Arduino\libraries`
     * macOS：`/Users/{username}/Documents/Arduino/libraries`
     * Linux：`/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty 包含经过测试的依赖版本，这些版本可能不是最新版本。更新前，请先确认硬件工作正常。如果更新后出现问题，请恢复为 [T-Vibe-Pager 第三方库](./third_party.md#t-lora-pager-third-party)中列出的已测试版本。

6. 打开 `File` > `Examples` > `LilyGoLib` > `helloworld`。
7. 按下表配置 `Tools` 菜单中的选项。

   | Arduino IDE 设置                     | 值                                      |
   | ------------------------------------ | --------------------------------------- |
   | Board                                | **LilyGo-T-LoRa-Pager**                 |
   | Port                                 | 设备对应的端口                          |
   | USB CDC On Boot                      | Enabled                                 |
   | CPU Frequency                        | 240MHz (WiFi)                           |
   | Core Debug Level                     | None                                    |
   | USB DFU On Boot                      | Disabled                                |
   | Erase All Flash Before Sketch Upload | Disabled                                |
   | Events Run On                        | Core 1                                  |
   | JTAG Adapter                         | Disabled                                |
   | Arduino Runs On                      | Core 1                                  |
   | USB Firmware MSC On Boot             | Disabled                                |
   | Partition Scheme                     | **16M Flash (3MB APP/9.9MB FATFS)**     |
   | Board Revision                       | **Radio-SX1262**(保持默认,因为没有LoRa) |
   | Upload Mode                          | **UART0 / Hardware CDC**                |
   | Upload Speed                         | 921600                                  |
   | USB Mode                             | **Hardware CDC and JTAG**               |

8. 在 `Tools` > `Port` 中选择设备对应的端口。
9. 点击 **Upload**，等待编译和烧录完成。
10. 如果上传失败，或 USB 串口反复连接和断开，请[手动进入下载模式](#t-lora-pager-enter-download-mode)后重试。

> [!TIP]
>
> * 如果串口监视器没有输出，请确认 `USB CDC On Boot` 已设置为 `Enabled`。
> * LilyGoLib 需要 Arduino-ESP32 3.3.0-alpha1 或更高版本。使用更早的版本会导致编译错误。

### 键盘功能设置和输入方式

| 功能           | 操作                |
| -------------- | ------------------- |
| 输入数字和符号 | 空格键 + 对应按键   |
| 切换字母大小写 | CAP 键 + 对应按键   |
| 控制背光       | 左侧橙色按钮 + B 键 |

* 使用出厂固件时，仅能在输入文字期间控制键盘背光。

<a id="t-lora-pager-enter-download-mode"></a>

### 让 T-Vibe-Pager 进入下载模式

> [!IMPORTANT]
>
> 如果 USB 端口反复连接和断开，或无法正常上传程序，请按以下步骤操作。安装 Meshtastic 等第三方固件后，可能需要手动进入下载模式。
>
> 1. 用 USB-C 数据线连接开发板。
> 2. 按住 **BOOT** 按钮。
> 3. 按住 **BOOT** 的同时，按下并松开 **RST** 按钮。
> 4. 松开 **BOOT** 按钮。
> 5. 选择此时已稳定连接的串口，并上传固件。
> 6. 上传完成后，按 **RST** 退出下载模式。
>
> 如果烧录成功后设备仍无法启动，或某个外设无法工作，请烧录[出厂测试固件](../firmware/README.md)以检查硬件。
