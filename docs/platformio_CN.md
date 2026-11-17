<div align="center" markdown="1">
  <img src="../.github/LilyGo_logo.png" alt="LilyGo logo" width="100"/>
</div>

<h1 align="center">PlatformIO 快速入门</h1>

[English](./platformio.md)

LilyGoLib 已经提供可直接使用的 `platformio.ini`。推荐的开发环境是
Visual Studio Code 加 **pioarduino IDE** 扩展。打开本仓库即可使用，不要在
仓库外层再创建一个 PlatformIO 工程。

## 1. 安装开发工具

### Visual Studio Code

下载并安装最新稳定版
[Visual Studio Code](https://code.visualstudio.com/)。建议使用 Microsoft 官方
发行版，这样可以直接从 Visual Studio Marketplace 安装扩展。

### Git

安装 [Git](https://git-scm.com/downloads)，然后在系统终端确认下面的命令可以
正常执行：

```bash
git --version
```

Git 不仅用于获取 LilyGoLib，PlatformIO 下载部分依赖或开发版本的软件包时也
可能会使用它。

### Python

VS Code 扩展通常会自行创建并管理隔离的 Python 环境。仍然建议安装
[Python 3](https://www.python.org/downloads/)，这样在自动安装 Core 失败或需要
使用命令行时更容易排查和恢复。

- Windows 安装时勾选 **Add Python to PATH**，安装后运行
  `py -3 --version` 检查。
- macOS 安装后运行 `python3 --version` 检查。
- Debian 或 Ubuntu 可以运行 `sudo apt install python3 python3-venv`，然后用
  `python3 --version` 检查。

## 2. 安装 pioarduino IDE

1. 打开 VS Code，点击左侧活动栏中的“扩展”，也可以按 `Ctrl+Shift+X`
   （macOS 使用 `Cmd+Shift+X`）。
2. 搜索
   **[pioarduino IDE](https://marketplace.visualstudio.com/items?itemName=pioarduino.pioarduino-ide)**。
3. 安装发布者为 **pioarduino** 的扩展，其扩展标识为
   `pioarduino.pioarduino-ide`。
4. 安装 [Microsoft C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
   扩展，用于代码补全和源码跳转。pioarduino IDE 现在不会自动安装 C/C++
   语言扩展。
5. 安装完成后重新启动 VS Code。

本文档不使用官方的 **PlatformIO IDE** 扩展。如果已经安装了官方扩展，请在
当前工作区将其禁用后再启用 pioarduino IDE，避免两个扩展同时管理 PlatformIO
命令和侧边栏。

## 3. 打开 LilyGoLib

1. 克隆或下载 LilyGoLib。
2. 在 VS Code 中选择 **文件 > 打开文件夹**。
3. 选择 LilyGoLib 仓库根目录，也就是同时包含 `platformio.ini`、
   `library.json`、`src` 和 `examples` 的目录。
4. 等待第一次初始化完成。首次编译时 pioarduino 会下载所选 ESP32 平台、
   工具链、框架和库依赖，因此可能需要几分钟。

不要单独把某个示例目录作为工作区打开。`platformio.ini` 中的路径和编译脚本
都以 LilyGoLib 仓库根目录为基准。

## 4. 选择示例和开发板

`[platformio]` 段决定默认编译的示例和开发板：

```ini
[platformio]
default_envs = t_embed_plus
src_dir = examples/factory
```

`src_dir` 用于选择一个示例目录；`default_envs` 决定 VS Code 编译、上传按钮以及
不带 `-e` 参数的 `pio run` 使用哪块开发板。

仓库当前提供以下环境：

| 开发板 | 环境名称 |
| --- | --- |
| T-Embed-Plus | `t_embed_plus` |
| 搭载 SX1262 的 T-Deck | `tdeck_sx1262` |
| T-Vibe Dev（无 LoRa、无 GNSS 的 T-Deck） | `tvibe_dev` |
| 搭载 SX1262 的 T-Watch-S3 | `twatchs3_sx1262` |
| 搭载 SX1280 的 T-Watch-S3 | `twatchs3_sx1280` |
| 搭载 CC1101 的 T-Watch-S3 | `twatchs3_cc1101` |
| 搭载 SX1262 的 T-Watch-Ultra | `twatch_ultra_sx1262` |
| 搭载 SX1280 的 T-Watch-Ultra | `twatch_ultra_sx1280` |
| 搭载 SX1262 的 T-LoRa-Pager | `tlora_pager_sx1262` |
| 搭载 LR1121 的 T-LoRa-Pager | `tlora_pager_lr1121` |
| T-Vibe Pager（无 LoRa 的 T-LoRa-Pager） | `tvibe_pager` |

两个 T-Vibe 环境都不会定义无线模块，并在编译时设置 `EXCLUDE_LORA`。其板级能力表
也会声明无无线模块，因此默认的 `instance.begin()` 不会探测或初始化 LoRa 硬件。
T-Vibe Dev 同时没有 GNSS 模块，并设置 `EXCLUDE_GPS`；默认初始化会跳过 GNSS 探测，
Factory 示例也不会显示 GNSS 相关应用。

需要编译其他示例时，只修改 `src_dir`。例如：

```ini
[platformio]
default_envs = t_embed_plus
src_dir = examples/peripheral/DisplayRotation
```

环境名称必须和实际连接的开发板及无线模块一致。选择错误的无线模块环境可能仍然
能够完成编译，但烧录后会使用错误的引脚或外设配置。

## 5. 编译、烧录和串口监视

使用支持数据传输的 USB 线连接开发板。在 pioarduino 侧边栏中展开
**Project Tasks**，再展开需要的环境：

- **Build**：编译当前示例。
- **Upload**：编译并烧录固件。
- **Monitor**：以 115200 波特率打开串口监视器。
- **Clean**：切换过示例或环境后出现异常编译错误时，先清理旧的生成文件。

VS Code 状态栏中的对勾、向右箭头和插头图标也可以完成相同操作，它们默认使用
`default_envs` 指定的环境。

对应的终端命令如下：

```bash
pio run -e t_embed_plus
pio run -e t_embed_plus -t upload
pio device monitor -b 115200
pio run -e t_embed_plus -t clean
```

使用这些命令时，建议从 VS Code 中打开 **pioarduino IDE Terminal**。VS Code
工作流不需要另外安装一份全局 PlatformIO Core。

## 为什么使用 pioarduino

本仓库的 `platform` 使用固定版本的 pioarduino 下载地址，而不是 PlatformIO
注册表中的 `espressif32` 软件包：

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
```

这样可以固定 LilyGoLib 已验证过的 ESP32 Arduino 框架和工具链，避免注册表更新
后编译环境在用户不知情的情况下发生变化。不要仅仅因为 IDE 提示存在更新，就替换
这一行配置。

### 官方 PlatformIO IDE 扩展能否编译本项目？

技术上可以。pioarduino 的 ESP32 平台兼容 PlatformIO，官方扩展也能解析本仓库
`platform` 中的完整 HTTPS 地址。此时使用的仍然是 **pioarduino ESP32 平台包**；
安装官方扩展并不会把它变成 PlatformIO 官方的 `espressif32` 平台包。

LilyGoLib 推荐并支持的工作流仍然是 **pioarduino IDE**。它可以让工程创建、ESP32
平台选择以及 pioarduino 的专用工具保持在同一套环境中，也避免依赖两个独立更新的
扩展之间是否始终兼容。

## 常见问题

### 安装后没有出现 pioarduino

重新启动 VS Code，并确认当前工作区已经启用
`pioarduino.pioarduino-ide`。如果官方 PlatformIO IDE 扩展处于启用状态，请先在
当前工作区禁用它，再重新加载窗口。

### 第一次编译无法下载软件包

检查网络和代理设置，并在系统终端确认 `git --version` 可以正常执行。然后执行
**Clean** 再重新编译。除非错误信息明确指出本地库有问题，否则不要手动把依赖库
复制到工程中。

### 编译成功，但编辑器提示找不到头文件

安装或启用 Microsoft C/C++，然后执行 **Project Tasks > General > Rebuild
IntelliSense Index**。pioarduino 也支持 clangd，但 Microsoft C/C++ 是更简单的
默认选择。

### 无法打开烧录或串口端口

关闭其他串口监视软件，换用确认支持数据传输的 USB 线并重新连接开发板。Linux
用户还需要安装串口 udev 规则，或把当前用户加入系统的串口用户组，重新登录后生效。
