<div align="center" markdown="1">
  <img src="https://www.u-blox.com/logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟u-blox AssistNow Usage Guide🌟</h1>

[English](./assistNow.md)

## 支持的设备

| 设备 | 是否支持 |
| -------------------- | --- |
| [T-Deck Plus][1] | ✅ |
| [T-Deck Pro][2] | ✅ |
| [T-LoRa-Pager][3] | ✅ |
| [T-Watch-S3-Plus][4] | ✅ |
| [T-Watch-Ultra][5] | ✅ |
| [T-Beam-Supreme][6] | ✅ |

[1]: https://lilygo.cc/products/t-deck-plus-1
[2]: https://lilygo.cc/products/t-deck-pro
[3]: https://lilygo.cc/products/t-lora-pager
[4]: https://lilygo.cc/products/t-watch-s3-plus
[5]: https://lilygo.cc/products/t-watch-ultra
[6]: https://lilygo.cc/products/t-beam-supreme

> [!IMPORTANT]
>
> * 本指南仅适用于以上列出的、使用 **u-blox M10 系列** GNSS 接收器的设备。
> * 请勿在未列出的设备上使用此流程。

## 步骤 1：启用 GNSS 数据透传

为设备刷入对应的 GPS 回环固件。如果受支持的设备正在运行 LilyGoLib 出厂固件，也可以直接打开 GPS 界面，并将 **NMEA to Serial** 设置为 **Enabled**。

![app1](./images/app1.jpg)

![app2](./images/app2.jpg)


## 步骤 2：注册 Thingstream 账号

1. 打开 [u-blox Thingstream](https://portal.thingstream.io/) 并注册账号。

![ThingstreamRegister](./images/ThingstreamRegister.jpg)

2. 申请 AssistNow token。

![AssistNowToken1](./images/AssistNowToken1.jpg)

3. 创建一个 profile。

![AssistNowToken2](./images/AssistNowToken2.jpg)

![AssistNowToken3](./images/AssistNowToken3.jpg)

4. 复制 token，后续步骤需要使用。

![AssistNowToken4](./images/AssistNowToken4.jpg)


## 步骤 3：使用 u-center 2 传输辅助数据

1. 下载并安装 [u-center 2 25.06.18 或更高版本](https://www.u-blox.com/en/product/u-center)。

2. 注册 u-center 2 账号并登录。

![ucenter2login](./images/ucenter2login.jpg)

3. 选择设备的串口和波特率。

![start1](./images/ucetner2-start1.jpg)

4. 确认 u-center 2 能正常接收设备的 GNSS 数据。

![start2](./images/ucetner2-start2.jpg)

5. 按下图打开 AssistNow，并填入从 Thingstream 获取的 token。

![start3](./images/ucetner2-start3.jpg)

6. 如果 token 有效，状态图标会变成绿色。

![start4](./images/ucetner2-start4.jpg)

7. 点击 **Download** 下载 GNSS 辅助数据。保持默认设置，并将有效期设置为不超过一天。

![start5](./images/ucetner2-start5.jpg)

8. 点击 **Transfer**，将辅助数据发送到设备。

![start6](./images/ucetner2-start6.jpg)

9. 等待传输完成。如果传输失败，请重试。

![start7](./images/ucetner2-start7.jpg)

10. 出现下图所示的消息即表示传输成功。

![start8](./images/ucetner2-start8.jpg)


## 步骤 4：测试定位效果

1. 将 **NMEA to Serial** 设置为 **Disabled**；如果之前刷入了回环固件，请恢复设备的正常固件。然后将设备放到户外，AssistNow 应能缩短首次定位所需的时间。
2. 在 GNSS 接收器持续供电的情况下，传输的辅助数据最多可保持一天有效。如果接收器断电或数据过期，请重新执行传输流程。
