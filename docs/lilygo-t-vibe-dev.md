<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo 标志" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Vibe-Dev🌟</h1>


## `1` 概述

* 本文介绍如何通过 LilyGoLib 使用 `LilyGO T-Vibe-Dev`
* 有关硬件规格、接口、GPIO 引脚分配和 I2C 地址，请参阅 [T-Vibe-Dev 硬件参考文档](./hardware/lilygo-t-vibe-dev.md)

> [!TIP]
> 
> **T-Vibe-Dev** 是 **T-Deck** 的 **无LoRa** 版本,所以下方的设置是按照 **T-Deck** 来设置的,所以这不是文档错误,请知悉.💕
>

## `2` Arduino IDE 快速入门

1. 安装[最新版 Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. 通过开发板管理器安装 [Arduino-ESP32 **3.3.0** 或更高版本](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
   * Espressif 稳定版开发板管理器地址：`https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. 检查 `Tools` > `Board` 中是否有 `LilyGo T-Deck` (这不是文档错误,请看上方Tips)
   * 如果有，选择该开发板，然后跳到第 6 步
   * 如果没有，在 `File` > `Preferences` > `Additional Boards Manager URLs` 中添加以下地址：
     * `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_tdeck/package_lilygo_tdeck_index.json`
4. 打开开发板管理器，搜索 `LILYGO T-Deck Temporary Support` 并安装
5. 在 `Tools` > `Board` 中选择 `LilyGo T-Deck`
6. [下载 LilyGoLib 库](https://github.com/Xinyuan-LilyGO/LilyGoLib/archive/refs/heads/master.zip)
7. 在 Arduino IDE 中选择 `Sketch` > `Include Library` > `Add .ZIP Library`，然后选择第 6 步下载的 ZIP 文件。
8. [下载 LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty)，将其中的各个库文件夹分别复制到 Arduino 项目文件夹的 `libraries` 目录。不要复制最外层的 `LilyGoLib-ThirdParty` 文件夹。
   * 如果找不到 Arduino 项目文件夹，请参阅[在计算机上查找草图、库、开发板核心及其他文件](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer)
   * 默认的库目录如下：
     * Windows：`C:\Users\{username}\Documents\Arduino\libraries`
     * macOS：`/Users/{username}/Documents/Arduino/libraries`
     * Linux：`/home/{username}/Arduino/libraries`

> [!IMPORTANT]
>
> LilyGoLib-ThirdParty 包含经过测试的依赖版本，这些版本可能不是最新版本。更新前，请先确认硬件工作正常。如果更新后出现问题，请恢复为 [T-Deck 第三方库](./third_party.md#t-deck-third-party)中列出的已测试版本。

9. 打开 `File` > `Examples` > `LilyGoLib` > `helloworld`
10. 按下表配置 `Tools` 菜单中的选项。

   | Arduino IDE 设置 | 值 |
   | ---------------- | -- |
   | Board | **LilyGo T-Deck** |
   | Port | 设备对应的端口 |
   | Core Debug Level | None |
   | Erase All Flash Before Sketch Upload | Disabled |
   | Upload Speed | 921600 |

11. 在 `Tools` > `Port` 中选择设备对应的端口。
12. 点击 **Upload**，等待编译和烧录完成。
13. 如果上传失败，或 USB 串口反复连接和断开，请[手动进入下载模式](#t-deck-enter-download-mode)后重试。

> [!TIP]
>
> * 如果串口监视器没有输出，请确认已选择 T-Deck 开发板。
> * LilyGoLib 需要 Arduino-ESP32 3.3.0 或更高版本。使用更早的版本会导致编译错误。
> * 官方 `LilyGo T-Deck` 开发板选项可用后，请卸载 `LILYGO T-Deck Temporary Support`，并选择官方开发板。

### 迁移到官方开发板定义

稳定版 Arduino-ESP32 加入 T-Deck 支持后：

1. 打开开发板管理器，卸载 `LILYGO T-Deck Temporary Support`
2. 将 `esp32 by Espressif Systems` 更新到包含 T-Deck 支持的稳定版本。
3. 在 `Tools` > `Board` 中选择官方的 `LilyGo T-Deck` 开发板。
4. 重新编译示例程序。

<a id="t-deck-enter-download-mode"></a>

### 让 T-Vibe-Dev 进入下载模式

> [!IMPORTANT]
>
> 如果 USB 端口反复连接和断开，或无法正常上传程序，请按以下步骤操作。安装 Meshtastic 等第三方固件后，可能需要手动进入下载模式。
>
> 1. 将电源开关拨到 OFF 位置（朝向 SD 卡槽）。
> 2. 将 USB-C 数据线连接到设备。
> 3. 将电源开关拨到 ON 位置（朝向 USB-C 接口）。
> 4. 按住轨迹球的中央按钮。
> 5. 按住轨迹球按钮的同时，按下并松开设备左侧的复位按钮。
> 6. 松开轨迹球按钮。
> 7. 上传固件。
>
> 如果烧录成功后设备仍无法启动，或某个外设无法工作，请烧录[出厂测试固件](../firmware/README.md)以检查硬件。
