# ESP32-C5 ESP-AT test firmware

This directory contains the official Espressif GitHub Actions test artifact
used by the T-Embed-Plus V2 `WiFi C5` Factory app.

- Source workflow: <https://github.com/espressif/esp-at/actions/workflows/main.yml>
- Source run: <https://github.com/espressif/esp-at/actions/runs/43203125436>
- Direct Actions artifact download: <https://nightly.link/espressif/esp-at/workflows/main.yml/master/esp32c5-4MB-at.zip>
- Artifact: `esp32c5-4MB-at`, commit `b2921f8fdbfa6bacab17e44b7711267b3083b103`
- Downloaded archive: `esp32c5-4MB-at-github-actions.zip`
- SHA-256: `79d1bb80b8c2d85639b53c3a64078034ab493ab75087fb70080159e60b22ea70`

This is a temporary test build. It does not provide Espressif official OTA
support and is not a production or mass-production release. For production,
request the released ESP32-C5-4MB AT v5.0.1.0 firmware through Espressif's
[AT firmware application](https://www.espressif.com/en/contact-us/at-firmware-application).

The archive contains a factory image and individual images. The factory image
is the simplest option for a blank C5 flash:

```text
build/factory/factory_ESP32C5-4MB.bin -> 0x000000
```

For individual images, use the offsets from `build/flasher_args.json` in the
archive (`0x2000`, `0xc000`, `0xd000`, `0x30000`, `0x31000`, `0xa0000`).

The C5 firmware is a separate image from the ESP32-S3 application. It must be
flashed to the C5 separately; uploading the LilyGoLib application does not
program or update the C5. The official default AT command port is UART1 at
115200 baud. On T-Embed-Plus V2, wire the crossed link as C5 GPIO23 (TX) to S3
GPIO44 (RX), and C5 GPIO24 (RX) to S3 GPIO43 (TX).
