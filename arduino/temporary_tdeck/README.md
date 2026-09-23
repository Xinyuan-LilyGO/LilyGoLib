# Temporary Arduino board package for LILYGO T-Deck

This package provides the original T-Deck board definition until it is available
in a stable Arduino-ESP32 release. It contains only `boards.txt` and the T-Deck
variant, and reuses the installed `esp32:esp32` core and tools.

## Install in Arduino IDE

1. Install the latest Arduino IDE 2.x.
2. Install `esp32 by Espressif Systems` version 3.3.0 or later from Boards
   Manager.
3. Add this URL under **File > Preferences > Additional Boards Manager URLs**:

   `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_tdeck/package_lilygo_tdeck_index.json`

4. Open Boards Manager, search for `LILYGO T-Deck Temporary Support`, and
   install it.
5. Select **LILYGO T-Deck (Temporary)** under **Tools > Board**.

The board definition fixes the settings required by the original T-Deck: 16 MB
flash, 8 MB OPI PSRAM, hardware USB CDC, a 3 MB application with 9.9 MB FATFS,
and the SX1262 radio definition.

Uninstall this package and select the official T-Deck board after support is
included in a stable Arduino-ESP32 release.
