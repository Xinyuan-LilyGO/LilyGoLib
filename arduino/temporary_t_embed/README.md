# Temporary Arduino board package for LILYGO T-Embed

This package provides the original T-Embed board definition until it is
available in a stable Arduino-ESP32 release. It reuses the installed
`esp32:esp32` core and tools.

## Install in Arduino IDE

1. Install Arduino IDE 2.x.
2. Install `esp32 by Espressif Systems` 3.3.11 or later.
3. Add this URL under **File > Preferences > Additional Boards Manager URLs**:

   `https://raw.githubusercontent.com/Xinyuan-LilyGO/LilyGoLib/master/arduino/temporary_t_embed/package_lilygo_tembed_index.json`

4. Install `LILYGO T-Embed Temporary Support` from Boards Manager.
5. Select **LilyGo T-Embed** under **Tools > Board**.

The definition supplies 16 MB flash, 8 MB OPI PSRAM, hardware USB CDC, a
3 MB application with 9.9 MB FATFS, and the original T-Embed pin map. It does
not enable CC1101 or PN532 hardware.

When using LilyGoLib, also install `PU2CLR SI4735` 2.1.8 or later. The original
T-Embed can use its optional SI473x receiver shield; the library probes the
shield at runtime, and the board works normally when it is absent.
