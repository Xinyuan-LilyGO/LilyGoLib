# T-Dongle-S3 Wireless Microphone Receiver

This example turns the original T-Dongle-S3 into a wireless USB microphone and
keyboard composite device. It is the receiver for the Wireless Mic application
included in the LilyGoLib Factory example.

## Hardware

- Original T-Dongle-S3, not the Plus or Dual variant.
- A supported LilyGo device running the Factory Wireless Mic sender.
- A computer with a USB port and USB Audio Class support.

For Arduino IDE board setup, install the
[temporary T-Dongle-S3 board package](https://github.com/Xinyuan-LilyGO/LilyGoLib/tree/master/arduino/temporary_t_dongle_s3).

## Technical parameters

| Item | Value |
| --- | --- |
| Wireless transport | ESP-NOW, automatic channel scan from 1 to 13 |
| Radio audio | G.722, 16 kHz, mono, 16-bit, 20 ms frames |
| USB audio | UAC microphone, 48 kHz, mono, 16-bit |
| USB control | HID keyboard |
| USB identity | VID `0x303A`, PID `0x82C2` |

The sender and receiver pair automatically and do not require Wi-Fi setup. The
radio link is unencrypted; use it only where microphone confidentiality is not
required.

## Use

1. Connect the programmed T-Dongle-S3 to the computer.
2. Select `LILYGO Wireless Microphone` as the computer microphone.
3. Open Wireless Mic on the sender. Wait until the receiver is connected.
4. Start or stop the microphone from the sender.

Starting the microphone sends Space and Right Alt together to start continuous
voice input. Stopping the microphone sends Escape to cancel continuous input.

## Status LED

- Blinking amber: searching for a sender.
- Solid green: sender connected and idle.
- Solid blue: audio stream active.
- Solid red: receiver initialization failed.
