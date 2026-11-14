# Temporary Arduino board package for LILYGO T-Embed-CC1101

This package provides the T-Embed-CC1101 board definition until it is available
in a stable Arduino-ESP32 release. It contains only `boards.txt` and the
T-Embed-CC1101 variant, and reuses the installed `esp32:esp32` core and tools.

## Install in Arduino IDE

1. Install the latest Arduino IDE 2.x.
2. Install `esp32 by Espressif Systems` version 3.3.11 or later from Boards
   Manager.
3. Add this URL under **File > Preferences > Additional Boards Manager URLs**:

   `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_embed_cc1101/package_lilygo_t_embed_cc1101_index.json`

4. Open Boards Manager, search for
   `LILYGO T-Embed-CC1101 Temporary Support`, and install it.
5. Select **LilyGo T-Embed-CC1101** under **Tools > Board**.

The board definition supplies the settings required by T-Embed-CC1101: 16 MB
flash, 8 MB OPI PSRAM, QIO flash mode, hardware USB CDC, a 3 MB application
with 9.9 MB FATFS, and the CC1101 radio definition.

Uninstall this package and select the official T-Embed-CC1101 board after
support is included in a stable Arduino-ESP32 release.
