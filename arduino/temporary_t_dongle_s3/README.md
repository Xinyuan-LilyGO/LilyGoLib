# Temporary Arduino board package for LILYGO T-Dongle-S3

This package provides the original T-Dongle-S3 board definition until it is
available in a stable Arduino-ESP32 release. It reuses the installed
`esp32:esp32` core and tools.

## Install in Arduino IDE

1. Install Arduino IDE 2.x.
2. Install `esp32 by Espressif Systems` 3.3.11 or later.
3. Add this URL under **File > Preferences > Additional Boards Manager URLs**:

   `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_dongle_s3/package_lilygo_t_dongle_s3_index.json`

4. Install `LILYGO T-Dongle-S3 Temporary Support` from Boards Manager.
5. Select **LilyGo T-Dongle-S3** under **Tools > Board**.

The definition selects 16 MB QIO flash, no PSRAM, USB-OTG mode with USB CDC
disabled, and the original T-Dongle-S3 board macro. It is not intended for the
Plus or Dual variants.
