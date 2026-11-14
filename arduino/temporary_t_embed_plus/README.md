# Temporary Arduino board package for LILYGO T-Embed-Plus

This package provides the T-Embed-Plus board definition until it is available
in a stable Arduino-ESP32 release. It contains only `boards.txt` and the
T-Embed-Plus variant, and reuses the installed `esp32:esp32` core and tools.

## Install in Arduino IDE

1. Install the latest Arduino IDE 2.x.
2. Install `esp32 by Espressif Systems` version 3.3.11 or later from Boards
   Manager.
3. Add this URL under **File > Preferences > Additional Boards Manager URLs**:

   `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_embed_plus/package_lilygo_tembed_plus_index.json`

4. Open Boards Manager, search for
   `LILYGO T-Embed-Plus Temporary Support`, and install it.
5. Select **LilyGo T-Embed-Plus** under **Tools > Board**.

The board definition supplies the settings required by T-Embed-Plus: 16 MB
flash, 8 MB OPI PSRAM, QIO flash mode, hardware USB CDC, a 3 MB application
with 9.9 MB FATFS, and the CC1101 radio definition.

Uninstall this package and select the official T-Embed-Plus board after
support is included in a stable Arduino-ESP32 release.
