<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align="center">PlatformIO Quick Start</h1>

[中文](./platformio_CN.md)

LilyGoLib includes a ready-to-use `platformio.ini`. The recommended setup is
Visual Studio Code with the **pioarduino IDE** extension. Open the repository
directly; do not create another PlatformIO project around it.

## 1. Install the development tools

### Visual Studio Code

Download and install the current stable release of
[Visual Studio Code](https://code.visualstudio.com/). The official Microsoft
build is recommended because it can install extensions from the Visual Studio
Marketplace directly.

### Git

Install [Git](https://git-scm.com/downloads), then make sure this command works
in a system terminal:

```bash
git --version
```

Git is required to clone LilyGoLib and may also be used when PlatformIO resolves
libraries or development packages.

### Python

The VS Code extension normally creates and manages its own isolated Python
environment. Installing [Python 3](https://www.python.org/downloads/) is still
recommended because it makes recovery and command-line use much easier if the
automatic Core installation fails.

- On Windows, enable **Add Python to PATH** in the installer. Check it with
  `py -3 --version`.
- On macOS, check it with `python3 --version`.
- On Debian or Ubuntu, install Python and virtual-environment support with
  `sudo apt install python3 python3-venv`, then check `python3 --version`.

## 2. Install pioarduino IDE

1. Open VS Code and select **Extensions** from the Activity Bar, or press
   `Ctrl+Shift+X` (`Cmd+Shift+X` on macOS).
2. Search for
   **[pioarduino IDE](https://marketplace.visualstudio.com/items?itemName=pioarduino.pioarduino-ide)**.
3. Install the extension published by **pioarduino**. Its extension identifier
   is `pioarduino.pioarduino-ide`.
4. Install [Microsoft C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
   for code completion and source navigation. pioarduino IDE no longer installs
   a C/C++ language extension automatically.
5. Restart VS Code after installation.

This guide does not use the official **PlatformIO IDE** extension. If it is
already installed, disable it for this workspace before enabling pioarduino IDE
so that only one extension manages the PlatformIO commands and sidebar.

## 3. Open LilyGoLib

1. Clone or download LilyGoLib.
2. In VS Code, select **File > Open Folder**.
3. Select the LilyGoLib repository root, the directory that contains
   `platformio.ini`, `library.json`, `src`, and `examples`.
4. Allow the first initialization to finish. pioarduino downloads the selected
   ESP32 platform, toolchain, framework, and library dependencies on the first
   build, so this step can take several minutes.

Do not open an individual example folder as the workspace. The paths and build
script in `platformio.ini` are relative to the LilyGoLib repository root.

## 4. Select the example and board

The `[platformio]` section controls the default example and board:

```ini
[platformio]
default_envs = t_embed_plus
src_dir = examples/factory
```

`src_dir` selects one example directory. `default_envs` selects the board used
by the VS Code Build and Upload buttons and by `pio run` without `-e`.

The repository currently provides these environments:

| Board | Environment |
| --- | --- |
| T-Embed-Plus | `t_embed_plus` |
| T-Deck with SX1262 | `tdeck_sx1262` |
| T-Vibe Dev (T-Deck without LoRa or GNSS) | `tvibe_dev` |
| T-Watch-S3 with SX1262 | `twatchs3_sx1262` |
| T-Watch-S3 with SX1280 | `twatchs3_sx1280` |
| T-Watch-S3 with CC1101 | `twatchs3_cc1101` |
| T-Watch-Ultra with SX1262 | `twatch_ultra_sx1262` |
| T-Watch-Ultra with SX1280 | `twatch_ultra_sx1280` |
| T-LoRa-Pager with SX1262 | `tlora_pager_sx1262` |
| T-LoRa-Pager with LR1121 | `tlora_pager_lr1121` |
| T-Vibe Pager (T-LoRa-Pager without LoRa) | `tvibe_pager` |

The T-Vibe environments do not define a radio module and compile with
`EXCLUDE_LORA`. Their board capability tables report no radio, so the default
`instance.begin()` path does not probe or initialize LoRa hardware. T-Vibe Dev
also has no GNSS module and compiles with `EXCLUDE_GPS`; its default
initialization skips GNSS probing, and the factory example omits the GNSS apps.

To build another example, change only `src_dir`. For example:

```ini
[platformio]
default_envs = t_embed_plus
src_dir = examples/peripheral/DisplayRotation
```

Select an environment that matches the connected board and its radio. Choosing
the wrong radio environment can compile successfully but produce incorrect pin
or peripheral behavior on the device.

## 5. Build, upload, and monitor

Connect the board with a USB data cable. In the pioarduino sidebar, expand
**Project Tasks**, expand the required environment, and use:

- **Build** to compile the current example.
- **Upload** to compile and flash it.
- **Monitor** to open the serial monitor at 115200 baud.
- **Clean** before rebuilding if generated files from a previous example or
  environment cause unexpected errors.

The check mark, right arrow, and plug icons in the VS Code status bar perform
the same actions for `default_envs`.

The corresponding terminal commands are:

```bash
pio run -e t_embed_plus
pio run -e t_embed_plus -t upload
pio device monitor -b 115200
pio run -e t_embed_plus -t clean
```

Open a **pioarduino IDE Terminal** from VS Code when using these commands. A
separate global PlatformIO Core installation is not required for the VS Code
workflow.

## Why this project uses pioarduino

The `platform` entry is an exact pioarduino release URL rather than the
`espressif32` package from the PlatformIO registry:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
```

This pins the ESP32 Arduino framework and toolchain used to validate LilyGoLib,
instead of allowing a registry update to change the build environment. Do not
replace this line merely because the IDE offers a newer platform.

### Can the official PlatformIO IDE extension build this project?

Yes, technically. The pioarduino ESP32 platform is compatible with PlatformIO,
and the official extension can resolve a platform from the full HTTPS URL in
this repository. That configuration still uses the **pioarduino ESP32 platform
package**; installing the official extension does not turn it into the official
PlatformIO `espressif32` package.

The supported LilyGoLib workflow remains **pioarduino IDE**. It keeps project
creation, ESP32 platform selection, and pioarduino-specific tooling in the same
environment and avoids depending on the behavior of two independently updated
extensions.

## Troubleshooting

### pioarduino does not appear after installation

Restart VS Code and verify that `pioarduino.pioarduino-ide` is enabled for the
current workspace. If the official PlatformIO IDE extension is enabled, disable
it for this workspace and reload the window.

### The first build cannot download packages

Check Internet access, proxy settings, and that `git --version` works in a
system terminal. Then run **Clean** and build again. Do not manually copy
libraries into the project unless a dependency error identifies a local library
as the cause.

### The editor reports missing headers but the build succeeds

Install or enable Microsoft C/C++, then run **Project Tasks > General > Rebuild
IntelliSense Index**. pioarduino also supports clangd, but Microsoft C/C++ is
the simplest default setup.

### Upload or serial monitor cannot open the port

Close other serial-monitor programs and reconnect the board with a known USB
data cable. On Linux, install the required serial-port udev rules or add the
current user to the distribution's serial-port group, then log in again.
