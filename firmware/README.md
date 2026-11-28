<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟LilyGo Firmware🌟</h1>

# [中文](README_CN.MD) 


> \[!IMPORTANT]
>
> - How do I prove to LilyGo that the board is broken?
> 1. You need to shoot a video to prove that the board is broken. Please follow the steps below to shoot a video to prove that the board is broken.
> 2. Follow the steps at the bottom of this page to put the board into download mode.
> 3. Select the burning tool, download the corresponding firmware, and upload the firmware to the board through the burning tool.
> 4. After flashing, check that the board restarts. If it does not restart automatically, press RST or power it off and on again. T-Watch-S3 has no reset button and needs a power cycle if it stays in download mode.
> 5. Plug in the USB, open the serial port, and record the contents of the serial monitor.
> 6. Any of the above steps must be included in the video content to facilitate explanation.
>

## Where can I find the factory firmware?

* [Older firmware version with a white background](https://github.com/Xinyuan-LilyGO/LilyGoLib/releases/tag/V0.2.1)
* New black UI on the current page.

## 1️⃣Support Product


> [!TIP]
>
> Select T-Vibe-Pager in the online flasher to use its dedicated firmware listed below.
>
> To restore T-Vibe-Dev to its factory state, flash the dedicated T-Vibe-Dev firmware.
>

| Product                            | Factory firmware                         |
| ---------------------------------- | ---------------------------------------- |
| [T-LoRa-Pager SX1262 version][1]   | tlora-pager-sx1262-factory-xxxx-0x0.bin  |
| [T-LoRa-Pager LR1121 version][1]   | tlora-pager-lr1121-factory-xxxx-0x0.bin  |
| [T-Vibe-Pager][1]                  | tvibe-pager-factory-xxxx-0x0.bin         |
| [T-Watch-Ultra SX1280  version][2] | twatch-ultra-sx1280-factory-xxxx-0x0.bin |
| [T-Watch-Ultra SX1262 version][2]  | twatch-ultra-sx1262-factory-xxxx-0x0.bin |
| [T-Watch-S3 SX1262 version][3]     | twatchs3-sx1262-factory-xxxx-0x0.bin     |
| [T-Watch-S3 SX1280 version][3]     | twatchs3-sx1280-factory-xxxx-0x0.bin     |
| [T-Watch-S3 CC1101][3]             | twatchs3-cc1101-factory-xxxx-0x0.bin     |
| [T-Watch-S3-Plus][4]               | Same T-Watch-S3 firmware                 |
| [T-Deck][5]                        | tdeck-sx1262-factory-xxxx-0x0.bin        |
| [T-Deck-Plus][6]                   | tdeck-sx1262-factory-xxxx-0x0.bin        |
| [T-Vibe-Dev][5]                    | tvibe-dev-factory-xxxx-0x0.bin           |
| [T-Watch-2020-V3][7]               | q307-twatch2020v3-factory-xxxx-0x0.bin   |
| [T-Watch-2019][8]                  | twatch-2019-factory-xxxx-0x0.bin         |
| [T-Embed-Plus][9]                  | tembed-plus-factory-xxxx-0x0.bin         |
| [T-Embed-Plus V2](../docs/lilygo-t-embed-plus-v2.md) | tembed-plus-v2-factory-xxxx-0x0.bin |
| [T-Embed][10]                      | tembed-factory-xxxx-0x0.bin              |

[1]: https://lilygo.cc/products/t-lora-pager
[2]: https://lilygo.cc/products/t-watch-ultra
[3]: https://lilygo.cc/products/t-watch-s3
[4]: https://lilygo.cc/products/t-watch-s3-plus
[5]: https://lilygo.cc/products/t-deck
[6]: https://lilygo.cc/products/t-deck-plus
[7]: https://lilygo.cc/products/t-watch-2020-v3
[8]: https://lilygo.cc/products
[9]: https://lilygo.cc/products/t-embed-cc1101-plus
[10]: https://lilygo.cc/products/t-embed-si4732

## 2️⃣How to Flash ?

> \[!IMPORTANT]
>
> 🤖 USB port keeps appearing intermittently?
>
> ⚠️ When writing firmware, please put the device into download mode first in order to download the firmware normally.
>
> * [How to put T-Deck or T-Vibe-Dev into download mode](../docs/lilygo-t-deck.md#entering-download-mode-on-the-t-deck)
> * [How to put T-Watch-S3 into download mode](../docs/lilygo-t-watch-s3.md#entering-download-mode-on-the-t-watch-s3)
> * [How to put T-Watch-S3-Plus into download mode](../docs/lilygo-t-watch-s3-plus.md#entering-download-mode-on-the-t-watch-s3-plus)
> * [How to put T-Watch-Ultra into download mode](../docs/lilygo-t-watch-ultra.md#entering-download-mode-on-the-t-watch-ultra)
> * [How to put T-LoRa-Pager or T-Vibe-Pager into download mode](../docs/lilygo-t-lora-pager.md#entering-download-mode-on-the-t-lora-pager)
> * [How to put T-Embed-Plus into download mode](../docs/lilygo-t-embed-plus.md#3-entering-download-mode-on-the-t-embed-plus)
> * [How to put T-Embed-Plus V2 into download mode](../docs/lilygo-t-embed-plus-v2.md#3-entering-download-mode-on-the-t-embed-plus-v2)
> * [How to put T-Embed into download mode](../docs/lilygo-t-embed.md#3-entering-download-mode-on-the-t-embed)
>
> After flashing, check that the device restarts. If it stays in download mode, press RST; power T-Watch-S3 off and on again.
>
> ⚠️ The write address must be 0, not the default 0x1000.
>
> ⚠️Choose one of the following methods that suits you.
>

## LilyGo Flash Lab

**[Open the online flasher](https://xinyuan-lilygo.github.io/LilyGoLib/web-flasher/)**

Use desktop Chrome or Edge and keep an Internet connection. The HTTPS page supports English, Chinese, and Japanese. It offers the published factory firmware for each device and radio variant without a separate flashing application or Python installation. Phone and tablet browsers are not supported for USB flashing by this page.

1. Connect the board to the computer running the browser with a USB data cable.
2. Select the matching device and radio variant. T-Embed-Plus and T-Embed-Plus V2 require different firmware.
3. Follow the device-specific download-mode link on the page, then click **Connect device** and select its serial port in the browser prompt.
4. Check the selected firmware and click **Flash firmware**. The page uses address `0x0` automatically; progress stays visible at the top while writing. Keep the USB cable connected until writing finishes.
5. The page attempts an automatic reset, closes the serial connection, and opens a completion dialog. If the device has not restarted, press RST/RESET; power T-Watch-S3 off and on again. Click **OK** to close the dialog.

Flashing overwrites the image's flash regions, which may include settings and filesystem data. Back up anything you need before writing. To use your own `.bin`, select a merged image for the chosen board that includes its bootloader and partition table; a standalone application `.bin` cannot be written with this page.

If no port appears, check the USB data cable, enter download mode again, and check whether your board's USB-to-serial driver is installed. Connection or write failures are recorded in the page's activity log.

## LILYGO Spark

- [LILYGO Spark](https://lilygo.cc/pages/lilygo-spark)

![](../docs/images/LILYGO%20Spark.gif)


## ESP-Launchpad Online

- [Esp-launchpad](https://espressif.github.io/esp-launchpad/)

![ESP-Launchpad](../docs/images/web_flasher2.gif)

### Use ESP Download Tool

- Download [Flash_download_tool](https://dl.espressif.com/public/flash_download_tool.zip)

![web_flasher](../docs/images/esp_downloader.gif)

* Note that after writing is completed, you need to press RST to reset.
* T-Watch-S3 does not have a reset button, you must power off and restart to start, otherwise it will always be in download mode

### Use Web Flasher

- [ESP Web Flasher Online](https://espressif.github.io/esptool-js/)

![web_flasher](../docs/images/web_flasher.gif)

* Note that after writing is completed, you need to press RST to reset.
* T-Watch-S3 does not have a reset button, you must power off and restart to start, otherwise it will always be in download mode

### Use command line


If system asks about install Developer Tools, do it.

```bash
python3 -m pip install --upgrade pip
python3 -m pip install esptool
```

In order to launch esptool.py, exec directly with this:

```bash
python3 -m esptool
```

Use the following command to write

```bash
esptool --port /dev/tty.usbmodexxxx --baud 115200 write-flash 0x0 firmware.bin

```

# How to view the device serial boot log

![](../docs/images/serial.gif)
