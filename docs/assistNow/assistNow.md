<div align="center" markdown="1">
  <img src="https://www.u-blox.com/logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align = "center">🌟u-blox AssistNow Usage Guide🌟</h1>

[中文](./assistNow_CN.md)

## Supported Devices

| Device               | Supported |
| -------------------- | --- |
| [T-Deck Plus][1]     | ✅   |
| [T-Deck Pro][2]      | ✅   |
| [T-LoRa-Pager][3]    | ✅   |
| [T-Watch-S3-Plus][4] | ✅   |
| [T-Watch-Ultra][5]   | ✅   |
| [T-Beam-Supreme][6]  | ✅   |

[1]: https://lilygo.cc/products/t-deck-plus-1
[2]: https://lilygo.cc/products/t-deck-pro
[3]: https://lilygo.cc/products/t-lora-pager
[4]: https://lilygo.cc/products/t-watch-s3-plus
[5]: https://lilygo.cc/products/t-watch-ultra
[6]: https://lilygo.cc/products/t-beam-supreme

> [!IMPORTANT]
>
> * This guide applies only to the supported devices listed above that use a **u-blox M10-series** GNSS receiver.
> * Do not use this procedure with devices that are not listed.

## Step 1: Enable GNSS Passthrough

Flash the appropriate GPS loopback firmware to the device. On a supported device running the LilyGoLib factory firmware, open the GPS screen and set **NMEA to Serial** to **Enabled** instead.

![app1](./images/app1.jpg)

![app2](./images/app2.jpg)

## Step 2: Register a Thingstream Account

1. Open [u-blox Thingstream](https://portal.thingstream.io/) and create an account.

![ThingstreamRegister](./images/ThingstreamRegister.jpg)

2. Request an AssistNow token.

![AssistNowToken1](./images/AssistNowToken1.jpg)

3. Create a profile.

![AssistNowToken2](./images/AssistNowToken2.jpg)

![AssistNowToken3](./images/AssistNowToken3.jpg)

4. Copy the token for use in the following steps.

![AssistNowToken4](./images/AssistNowToken4.jpg)

## Step 3: Transfer Assistance Data with u-center 2

1. Download and install [u-center 2 version 25.06.18 or later](https://www.u-blox.com/en/product/u-center).

2. Create a u-center 2 account and sign in.

![ucenter2login](./images/ucenter2login.jpg)

3. Select the device's serial port and baud rate.

![start1](./images/ucetner2-start1.jpg)

4. Confirm that u-center 2 is receiving GNSS data from the device.

![start2](./images/ucetner2-start2.jpg)

5. Open AssistNow as shown below and enter the token obtained from Thingstream.

![start3](./images/ucetner2-start3.jpg)

6. If the token is valid, the status icon turns green.

![start4](./images/ucetner2-start4.jpg)

7. Click **Download** to retrieve the GNSS assistance data. Keep the default settings and set the validity period to no more than one day.

![start5](./images/ucetner2-start5.jpg)

8. Click **Transfer** to send the assistance data to the device.

![start6](./images/ucetner2-start6.jpg)

9. Wait for the transfer to finish. If it fails, retry the transfer.

![start7](./images/ucetner2-start7.jpg)

10. The message shown below confirms a successful transfer.

![start8](./images/ucetner2-start8.jpg)

## Step 4: Test Positioning Performance

1. Set **NMEA to Serial** to **Disabled**, restore the device's normal firmware if you used loopback firmware, and place the device outdoors. AssistNow should reduce the time required to obtain a position fix.
2. The transferred assistance data remains valid for up to one day while the GNSS receiver remains powered. If the receiver loses power or the data expires, repeat the transfer procedure.
