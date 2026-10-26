<div align="center" markdown="1">
  <img src="../../.github/LilyGo_logo.png" alt="LilyGo 标志" width="100"/>
</div>

<h1 align = "center">🌟LilyGo T-Vibe-Pager🌟</h1>

## `1` 概述

* 本文列出 `LilyGO T-Vibe-Pager` 的硬件规格。

```text

/---------------------------------------------------\
| ┌───────────────────────────────────────────┐ |-| |
| |                                           | |/| |
| |               480 x 222 IPS               | |/| |
| |                                           | |/| |
| └───────────────────────────────────────────┘ |-| |
|                                                   |
|                                                   |
|                                                   |
|                                                   |
\---|RST|--|BOOT|--|POWER|--|SD 卡槽|--|USB-C|----/
      ^       ^       ^          ^           ^
      |       |       |          |           |
      |       |       |          |           └─── 用于充电和编程；USB-C 接口也可配置为
      |       |       |          |                外部设备供电
      |       |       |          |
      |       |       |          └────── 支持最大 32 GB 的 SD 卡
      |       |       |
      |       |       └───────────── 按住一秒可开机；该按钮只能作为开机按钮,此按钮不可编程;
      |       |
      |       └───────────────────── GPIO0：用户按钮和下载模式按钮
      |
      └───────────────────────────── 按下可复位设备；此按钮不可编程






```

### 扩展接口

```text

>----------屏幕朝上放置-----------------------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

```

* CE 为 XL9555 GPIO9
* TX 为 ESP32-S3 GPIO43
* RX 为 ESP32-S3 GPIO44
* MISO 为 ESP32-S3 GPIO33
* MOSI 为 ESP32-S3 GPIO34
* SCK 为 ESP32-S3 GPIO35
* SDA 为 ESP32-S3 GPIO3
* SCL 为 ESP32-S3 GPIO2

### nRF24L01 PA Shield 接口

```text
>----------屏幕朝上放置-----------------------------<
|---------------------------------------------------|
|     | SCL | SDA | MISO  | SCK  | TX | GND  |      |
|     | 5V  | CE  | GPIO9 | MOSI | RX | 3.3V |      |
|---------------------------------------------------|

```

* CE 为 XL9555 GPIO9，用于控制扩展板的 TX/RX 模式（`LOW`：RX；`HIGH`：TX）
* TX 为 ESP32-S3 GPIO43，连接到扩展板的 CE 引脚
* RX 为 ESP32-S3 GPIO44，连接到扩展板的 CS 引脚
* MISO 为 ESP32-S3 GPIO33，连接到扩展板的 MISO 引脚
* MOSI 为 ESP32-S3 GPIO34，连接到扩展板的 MOSI 引脚
* SCK 为 ESP32-S3 GPIO35，连接到扩展板的 SCK 引脚
* 扩展板未连接 SDA（ESP32-S3 GPIO3）
* 扩展板未连接 SCL（ESP32-S3 GPIO2）

### ✨ 硬件特性

| 特性             | 规格                               |
| ---------------- | ---------------------------------- |
| SoC              | [Espressif ESP32-S3][1]            |
| Flash            | 16 MB (QSPI)                       |
| PSRAM            | 8 MB (QSPI)                        |
| 全球卫星导航     | [u-blox MIA-M10Q][2]               |
| 像素灯           | [WS2812C][3] * 12                   |
| NFC              | [ST25R3916][4]                     |
| 智能传感器       | [Bosch BHI260AP][5]                |
| 实时时钟         | [NXP PCF85063A][6]                 |
| 电池充电器       | [Ti BQ25896][7]                    |
| 电量计           | [Ti BQ27220][8]                    |
| 触觉驱动器       | [Ti DRV2605][9]                    |
| 音频编解码器     | [Everest-semi ES8311][10]          |
| GPIO 扩展器      | [XINLUDA XL9555][11]               |
| I2C 键盘         | [Ti TCA8418][12]                   |
| 音频功率放大器   | [Nsiway NS4150B (3 W Class D)][13] |
| 显示屏背光驱动器 | [AW9364 16-Level Led Driver][14]   |
| SD 卡槽          | ✅️ 支持最大 32 GB（FAT32）          |
| 外部低速时钟晶体 | ✅️                                  |

> [!TIP]
>
> * 请将 SD 卡格式化为兼容 FAT 的文件系统。
> * 只有断开 USB 电源后，设备才能完全关机。
> * 设备关机时，按住 PWR 按钮一秒只能唤醒设备；该按钮不能用于编程。
> * 此开发板上的 ST25R3916 NFC 读卡器没有外部电容式卡片存在检测电路。必须为读卡器供电才能检测卡片。
> * ESP32-S3 使用外置 QSPI Flash 和 PSRAM；两者均未集成在 SoC 内。

[1]: https://www.espressif.com.cn/en/products/socs/esp32-s3 "ESP32-S3"
[2]: https://www.u-blox.com/en/product/mia-m10-series "u-blox MIA-M10Q"
[3]: https://item.szlcsc.com/datasheet/WS2812C-2020/1051116.html "ws2812c"
[4]: https://www.st.com/en/nfc/st25r3916.html "ST25R3916"
[5]: https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ab "BHI260AP"
[6]: https://www.nxp.com/products/PCF85063A "PCF85063A"
[7]: https://www.ti.com/product/BQ25896 "BQ25896"
[8]: https://www.ti.com/product/BQ27220 "BQ27220"
[9]: https://www.ti.com/product/DRV2605 "DRV2605"
[10]: http://www.everest-semi.com/pdf/ES8311%20PB.pdf "ES8311"
[11]: https://www.xinluda.com/en/I2C-to-GPIO-extension/ "XL9555"
[12]: https://www.ti.com/product/TCA8418 "TCA8418"
[13]: http://www.nsiway.com.cn/product/58.html "NS4150B"
[14]: https://item.szlcsc.com/datasheet/AW9364DNR/385721.html "AW9364"

### ✨ 显示屏规格

| 特性     | 规格          |
| -------- | ------------- |
| 分辨率   | 480 x 222     |
| 屏幕尺寸 | 2.33 英寸     |
| 表面亮度 | 450 cd/m²     |
| 驱动 IC  | ST7796U (SPI) |
| 对比度   | 1000:1        |
| 色域     | 70%           |
| 像素密度 | 221 PPI       |
| 显示色彩 | 262K          |
| 可视角度 | 全视角（IPS） |
| 工作温度 | -20 至 70 °C  |

### 📍 [引脚映射](https://github.com/espressif/arduino-esp32/blob/master/variants/lilygo_tlora_pager/pins_arduino.h)

| 名称                               | GPIO                       | 可用 |
| ---------------------------------- | -------------------------- | ---- |
| 自定义引脚                         | GPIO9（外部 12 针接口）    | ✅️    |
| UART1 TX                           | 43（外部 12 针接口）       | ✅️    |
| UART1 RX                           | 44（外部 12 针接口）       | ✅️    |
| SDA                                | 3                          | ❌    |
| SCL                                | 2                          | ❌    |
| SPI MOSI                           | 34                         | ❌    |
| SPI MISO                           | 33                         | ❌    |
| SPI SCK                            | 35                         | ❌    |
| SD 卡 CS                           | 21                         | ❌    |
| SD 卡 MOSI                         | 与 SPI 总线共用            | ❌    |
| SD 卡 MISO                         | 与 SPI 总线共用            | ❌    |
| SD 卡 SCK                          | 与 SPI 总线共用            | ❌    |
| 键盘（**TCA8418**）SDA             | 与 I2C 总线共用            | ❌    |
| 键盘（**TCA8418**）SCL             | 与 I2C 总线共用            | ❌    |
| 键盘（**TCA8418**）中断            | 6                          | ❌    |
| 键盘背光                           | 46                         | ❌    |
| 旋转编码器 A                       | 40                         | ❌    |
| 旋转编码器 B                       | 41                         | ❌    |
| 旋转编码器中央按钮                 | 7                          | ❌    |
| RTC（**PCF85063A**）SDA            | 与 I2C 总线共用            | ❌    |
| RTC（**PCF85063A**）SCL            | 与 I2C 总线共用            | ❌    |
| RTC（**PCF85063A**）中断           | 1                          | ❌    |
| NFC（**ST25R3916**）CS             | 39                         | ❌    |
| NFC（**ST25R3916**）中断           | 5                          | ❌    |
| NFC（**ST25R3916**）MOSI           | 与 SPI 总线共用            | ❌    |
| NFC（**ST25R3916**）MISO           | 与 SPI 总线共用            | ❌    |
| NFC（**ST25R3916**）SCK            | 与 SPI 总线共用            | ❌    |
| 传感器（**BHI260**）中断           | 8                          | ❌    |
| 传感器（**BHI260**）SDA            | 与 I2C 总线共用            | ❌    |
| 传感器（**BHI260**）SCL            | 与 I2C 总线共用            | ❌    |
| 音频编解码器（**ES8311**）WS       | 18                         | ❌    |
| 音频编解码器（**ES8311**）SCK      | 11                         | ❌    |
| 音频编解码器（**ES8311**）MCLK     | 10                         | ❌    |
| 音频编解码器（**ES8311**）数据输出 | 45                         | ❌    |
| 音频编解码器（**ES8311**）数据输入 | 17                         | ❌    |
| 音频编解码器（**ES8311**）SDA      | 与 I2C 总线共用            | ❌    |
| 音频编解码器（**ES8311**）SCL      | 与 I2C 总线共用            | ❌    |
| GNSS（**MIA-M10Q**）TX             | 12                         | ❌    |
| GNSS（**MIA-M10Q**）RX             | 4                          | ❌    |
| GNSS（**MIA-M10Q**）PPS            | 13                         | ❌    |
| WS2812B 左边 * 6                      | 48                         | ❌    |
| WS2812B 右边 * 6                      | 47                         | ❌    |
| 显示屏 CS                          | 38                         | ❌    |
| 显示屏 MOSI                        | 与 SPI 总线共用            | ❌    |
| 显示屏 MISO                        | 与 SPI 总线共用            | ❌    |
| 显示屏 SCK                         | 与 SPI 总线共用            | ❌    |
| 显示屏 DC                          | 37                         | ❌    |
| 显示屏 RESET                       | 未连接                     | ❌    |
| 显示屏背光（16 级）                | 42                         | ❌    |
| 电量计（**BQ27220**）SDA           | 与 I2C 总线共用            | ❌    |
| 电量计（**BQ27220**）SCL           | 与 I2C 总线共用            | ❌    |
| 充电器（**BQ25896**）SDA           | 与 I2C 总线共用            | ❌    |
| 充电器（**BQ25896**）SCL           | 与 I2C 总线共用            | ❌    |
| 触觉驱动器（**DRV2605**）SDA       | 与 I2C 总线共用            | ❌    |
| 触觉驱动器（**DRV2605**）SCL       | 与 I2C 总线共用            | ❌    |
| 扩展器（**XL9555**）SDA            | 与 I2C 总线共用            | ❌    |
| 扩展器（**XL9555**）SCL            | 与 I2C 总线共用            | ❌    |
| 扩展器（**XL9555**）GPIO0          | 触觉驱动器使能             | ❌    |
| 扩展器（**XL9555**）GPIO1          | 音频功率放大器使能         | ❌    |
| 扩展器（**XL9555**）GPIO2          | 键盘 RESET                 | ❌    |
| 扩展器（**XL9555**）GPIO3          | LoRa 电源使能              | ❌    |
| 扩展器（**XL9555**）GPIO4          | GNSS 电源使能              | ❌    |
| 扩展器（**XL9555**）GPIO5          | NFC 电源使能               | ❌    |
| 扩展器（**XL9555**）GPIO6          | ~~显示屏 RESET~~（未连接） | ❌    |
| 扩展器（**XL9555**）GPIO7          | GNSS RESET                 | ❌    |
| 扩展器（**XL9555**）GPIO10         | 键盘电源使能               | ❌    |
| 扩展器（**XL9555**）GPIO11         | 外部 12 针接口             | ✅️    |
| 扩展器（**XL9555**）GPIO12         | SD 卡插入检测              | ❌    |
| 扩展器（**XL9555**）GPIO14         | SD 卡电源使能              | ❌    |

### 🧑🏼‍🔧 I2C 设备地址

| 设备                     | 7 位地址 | 共用总线 |
| ------------------------ | -------- | -------- |
| [编解码器 ES8311][10]    | 0x18     | ✅️        |
| [扩展器 XL9555][11]      | 0x20     | ✅️        |
| [智能传感器 BHI260AP][5] | 0x28     | ✅️        |
| [实时时钟 PCF85063A][6]  | 0x51     | ✅️        |
| [电池充电器 BQ25896][7]  | 0x6B     | ✅️        |
| [电量计 BQ27220][8]      | 0x55     | ✅️        |
| [键盘 TCA8418][12]       | 0x34     | ✅️        |
| [触觉驱动器 DRV2605][9]  | 0x5A     | ✅️        |

### ⚡ 电源管理通道

| 通道                       | 外设             |
| -------------------------- | ---------------- |
| 扩展器（**XL9555**）GPIO0  | **DRV2605 使能** |
| 扩展器（**XL9555**）GPIO1  | **扬声器**       |
| 扩展器（**XL9555**）GPIO3  | **WS2812 LED**   |
| 扩展器（**XL9555**）GPIO4  | **GNSS**         |
| 扩展器（**XL9555**）GPIO5  | **NFC**          |
| 扩展器（**XL9555**）GPIO10 | **键盘**         |
| 扩展器（**XL9555**）GPIO14 | **SD 卡**        |

### ⚡ 电气规格

| 特性            | 规格                |
| --------------- | ------------------- |
| 🔗USB-C 输入电压 | 3.9-6 V             |
| 🔗USB-C 输出电压 | 4.55-5.55 V         |
| ⚡USB-C 输出电流 | 0.5-1 A             |
| ⚡充电电流       | 0-3008 mA（可编程） |
| 🔋电池电压       | 3.7 V               |
| 🔋电池容量       | 1500 mAh（5.55 Wh） |
| 🔋充电温度范围   | 0-60 °C             |

> [!IMPORTANT]
> 充电电流请设置为低于 750 mA。电池充电倍率不得超过 0.5 C。

### ⚡ 功耗参考

| 模式     | 唤醒源    | 电流     |
| -------- | --------- | -------- |
| 深度睡眠 | BOOT 按钮 | 530 uA   |
| 深度睡眠 | 定时器    | 530 uA   |
| 轻度睡眠 | BOOT 按钮 | ~2.26 mA |
| 关机     | 电源按钮  | 26 uA    |

### 资源


* [原理图](../../schematic/T-Lora%20Pager%20V1.0%20SCH%2025-06-13.pdf)

> [!TIP]
> 
> **T-Vibe-Pager** 是 **T-LoRa-Pager** 的 **无LoRa** 版本,原理图和**T-LoRa-Pager**一样,只是没有焊接LoRa功能芯片,请知悉.💕
>
