<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo 固件🌟</h1>

# [English](./README.md)

> \[!IMPORTANT]
>
> - 如何向 LilyGo 证明开发板出现故障？
> 1. 请拍摄视频，记录以下操作及故障现象，以便证明开发板出现故障.
> 2. 按照本页下方的步骤，让开发板进入下载模式.
> 3. 选择烧录工具，下载对应的固件，并使用烧录工具将固件写入开发板.
> 4. 烧录完成后，按 RST 键，或断开 USB 连接后重新连接.如果开发板装有电池，必须关机后重新启动，才能退出下载模式.
> 5. 连接 USB，打开串口，并录下串口监视器显示的内容.
> 6. 请在视频中完整记录上述操作，以便说明问题.
>

## 在哪里可以找到出厂固件？

* [采用白色背景的旧版固件](https://github.com/Xinyuan-LilyGO/LilyGoLib/releases/tag/V0.2.1)
* 当前页面提供采用黑色界面的新版固件.

## 1️⃣ 支持的产品

> [!TIP]
>
> T-Vibe-Pager 出厂时预装 T-LoRa-Pager 固件，因此要恢复出厂状态，同样是烧录 T-LoRa-Pager 固件.
> 
> T-Vibe-Dev 出厂时预装 T-Deck 固件，因此要恢复出厂状态，同样是烧录 T-Deck 固件.
>

| 产品 |
| ---- |
| [T-LoRa-Pager 或 T-Vibe-Pager][1] |
| [T-Watch-Ultra][2] |
| [T-Watch-S3][3] |
| [T-Watch-S3-Plus][4] |
| [T-Deck 或 T-Vibe-Dev][5] |
| [T-Deck-Plus][6] |

[1]: https://lilygo.cc/products/t-lora-pager
[2]: https://lilygo.cc/products/t-watch-ultra
[3]: https://lilygo.cc/products/t-watch-s3
[4]: https://lilygo.cc/products/t-watch-s3-plus
[5]: https://lilygo.cc/products/t-deck
[6]: https://lilygo.cc/products/t-deck-plus

## 2️⃣ 如何烧录固件？

> \[!IMPORTANT]
>
> 🤖 USB 端口是否反复出现又消失？
>
> ⚠️ 烧录固件前，请先让设备进入下载模式，以便正常写入固件.
>
> * [让 T-Deck 或 T-Vibe-Dev 进入下载模式](../docs/lilygo-t-deck.md#entering-download-mode-on-the-t-deck)
> * [让 T-Watch-S3 进入下载模式](../docs/lilygo-t-watch-s3.md#entering-download-mode-on-the-t-watch-s3)
> * [让 T-Watch-S3-Plus 进入下载模式](../docs/lilygo-t-watch-s3-plus.md#entering-download-mode-on-the-t-watch-s3-plus)
> * [让 T-Watch-Ultra 进入下载模式](../docs/lilygo-t-watch-ultra.md#entering-download-mode-on-the-t-watch-ultra)
> * [让 T-LoRa-Pager 或 T-Vibe-Pager 进入下载模式](../docs/lilygo-t-lora-pager.md#entering-download-mode-on-the-t-lora-pager)
>
> ⚠️ 烧录完成后，必须按一次复位按钮，否则设备会一直停留在下载模式.
>
> ⚠️ 烧录地址必须是 0，不能使用默认的 0x1000.
>
> ⚠️ 请选择以下一种适合自己的烧录方式.
>

## LILYGO Spark

- [LILYGO Spark](https://lilygo.cc/pages/lilygo-spark)

![](../docs/images/LILYGO%20Spark.gif)

## ESP-Launchpad 在线烧录

- [ESP-Launchpad](https://espressif.github.io/esp-launchpad/)

![ESP-Launchpad](../docs/images/web_flasher2.gif)

### 使用 ESP Download Tool

- 下载 [Flash_download_tool](https://dl.espressif.com/public/flash_download_tool.zip)

![烧录工具](../docs/images/esp_downloader.gif)

* 烧录完成后，请按 RST 按钮复位设备.
* T-Watch-S3 没有复位按钮，必须关机后重新启动，否则设备会一直停留在下载模式.

### 使用网页烧录工具

- [ESP Web Flasher 在线烧录工具](https://espressif.github.io/esptool-js/)

![网页烧录工具](../docs/images/web_flasher.gif)

* 烧录完成后，请按 RST 按钮复位设备.
* T-Watch-S3 没有复位按钮，必须关机后重新启动，否则设备会一直停留在下载模式.

### 使用命令行

如果系统提示安装开发者工具，请按提示安装.

```bash
python3 -m pip install --upgrade pip
python3 -m pip install esptool
```

可直接运行以下命令启动 esptool.py：

```bash
python3 -m esptool
```

对于 ESP32-S3，使用以下命令烧录固件：

```bash
esptool --chip esp32s3  --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m 0x0 firmware.bin
```

# 如何查看设备的串口启动日志

![](../docs/images/serial.gif)
