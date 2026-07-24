# VibeKeyboard

`VibeKeyboard` 是一个面向 **LILYGO T-Lora Pager（T-LoRa-Pager）** 的多屏 LVGL 示例。它把设备作为随身 AI 会话控制器：通过 BLE 接收主机端的会话、通知、权限请求、工具状态、时间、声音和 YOLO 配置，并把会话切换、权限响应和配置操作回传给主机端。

> 这个示例不是独立的通用 BLE 键盘固件。完整体验需要配套的主机端 daemon 或调试程序来实现 `vk_protocol` 中定义的 BLE 协议。

## 功能

- Vibe Keyboard 主屏：显示和切换最多 32 个 AI 会话，包括名称、模型、状态、上下文占用、费用、token 和最后消息。
- Select Session 弹窗：选择当前会话，并向主机端发送会话切换事件。
- Notify / Notify Detail 弹窗：最多保留 32 条通知；新的权限请求可自动打开通知页，并发送 Allow / Deny 响应。
- Setup 菜单：进入 AI Agent、YOLO、Sound 和 About 页面。
- AI Agent 页面：查询 Claude Code、Codex、iTerm2 和 terminal-notifier 状态，配置 daemon 端口，并通过 BLE 或串口桥发送安装请求。
- YOLO 页面：同步和更新启用状态、自动允许通知开关以及 allow/deny 规则；每个规则文本框最多输入 192 个字符，协议最多编码 16 条非空规则。
- Sound 页面：调节音量、事件音效映射和即时试听；全局静音状态可由 BLE 下行设置。
- Keys / About 页面：展示功能键映射和设备信息；Keys 页面当前已创建，但 Setup 菜单尚未提供入口。
- Sound 页面：固件内置 `permission_alert`、`session_complete`、`error`、`click`
  四个 22.05 kHz、16-bit、单声道 WAV，通过 ES8311 后台播放。
- 键盘按下、旋钮转动和旋钮中心键按下会触发当前 `Button Click` 映射。
- BLE 下行队列：使用数组深度为 8 的环形队列缓存 Command 写入，其中一个槽位用于区分队列满/空，因此最多挂起 7 包；单包最大 2048 字节，再在主循环中解码并刷新 UI。
- 时钟同步：BLE 连接后向主机请求 Unix 时间和 UTC 偏移；未同步时每分钟重试，成功后每 6 小时刷新，离线时可回退到板载 RTC。
- 中文显示：会话名、提示内容和通知相关标签使用 Alibaba PuHuiTi 字体，其他 UI 文本和图标使用扩展 Montserrat 字体。

## 硬件与环境

- 目标硬件：LILYGO T-LoRa-Pager
- 开发框架：Arduino
- 推荐环境：Arduino IDE + Arduino-ESP32 3.3.0-alpha1 或更高版本
- UI：LVGL 9.x
- BLE：NimBLE-Arduino

当前固件中的主要容量限制：

| 项目 | 限制 |
| --- | ---: |
| 会话 | 32 |
| 通知 | 32 |
| Setup 工具状态 | 4 |
| YOLO allow 规则 | 16；下行单条本地保存 95 字节 |
| YOLO deny 规则 | 16；下行单条本地保存 95 字节 |
| BLE Command 待处理包 | 7 |
| BLE Command 单包 | 2048 字节 |

### 示例输入适配层

VibeKeyboard 的 `CAP`、`Fn`、Space 按下/松开以及旋钮反馈语义由
`vibe_input.h/.cpp` 实现。该适配层只调用 LilyGoLib 原有的公开接口，并替换本示例使用的 LVGL
键盘和旋钮输入设备；`src/` 保持上游库实现不变。后续调整 VibeKeyboard 的输入行为时，应继续在
该适配层内完成，不要向通用库接口加入示例专属按键。

## 完整运行步骤

完整体验由两部分组成：运行在 T-LoRa-Pager 上的 `VibeKeyboard` 固件，以及运行在电脑上的
`vk-daemon`。daemon 读取本机 Claude Code/Codex 会话，通过 BLE 连接设备，并在本机提供
loopback HTTP API。只烧录固件时可以浏览 UI，但不会显示真实会话，也不能执行远程操作。

以下路径和命令均以 `LilyGoLib` 仓库根目录为起点。完整环境需要：

- Arduino IDE，以及 Arduino-ESP32 3.3.0-alpha1 或更高版本
- [LilyGoLib-ThirdParty](https://github.com/Xinyuan-LilyGO/LilyGoLib-ThirdParty) 中的依赖库
- Python 3.12 或更高版本
- [`uv`](https://docs.astral.sh/uv/)
- 已开启的 Bluetooth，以及至少一个已安装的 Claude Code 或 Codex

macOS 是 `vk-daemon` 的主要完整支持平台。Linux 可以运行 daemon 核心和 BLE，但不提供窗口
聚焦、按键和本机通知等桌面适配；Windows 尚未完成项目级验证。需要连接实体设备时，daemon
必须运行在拥有 Bluetooth 权限的本机，远程或云端 agent 不能代替本机 BLE 访问。

### macOS 宿主权限

`vk-daemon` 使用的是 macOS 透明度、许可与控制（TCC）授权，不是 Unix root 权限；不要使用
`sudo` 启动 daemon。权限归属于启动 `vk-daemon` 的宿主应用，Terminal、iTerm2 和 Visual
Studio Code 必须分别授权。

| 权限 | 系统设置位置 | 用途 |
| --- | --- | --- |
| 蓝牙 | 隐私与安全性 > 蓝牙 | 扫描并连接 VibeKeyboard |
| 辅助功能 | 隐私与安全性 > 辅助功能 | 聚焦会话窗口、发送按键和触发“双击 Fn”听写 |
| 麦克风 | 隐私与安全性 > 麦克风 | Voice/听写音频输入 |

`vk-daemon serve` 会在启动时请求麦克风和辅助功能权限，并在 BLE 扫描时触发蓝牙权限申请。
如果用户此前拒绝过，macOS 可能不会再次弹窗，需要在系统设置中手动开启。修改权限后必须
完全退出并重新打开对应的 Terminal、iTerm2 或 Visual Studio Code，再启动 daemon。

### 1. 准备 Arduino IDE

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)。
2. 在 Arduino IDE 的开发板管理器中安装 Arduino-ESP32 3.3.0-alpha1 或更高版本。开发板管理器
   URL 为：

```text
https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
```

3. 将当前 `LilyGoLib` 仓库安装到 Arduino libraries 目录，或者下载仓库 ZIP 后通过
   **项目 > 加载库 > 添加 .ZIP 库** 安装。
4. 下载 LilyGoLib-ThirdParty，将其内部的所有库目录复制到 Arduino libraries 目录。不要复制
   `LilyGoLib-ThirdParty` 外层目录，也不要在确认硬件正常前升级这些配套依赖。

完整的 Arduino IDE 安装说明见
[`docs/lilygo-t-lora-pager.md`](../../docs/lilygo-t-lora-pager.md)。PlatformIO 使用独立的
[`LilyGoLib-PlatformIO`](https://github.com/Xinyuan-LilyGO/LilyGoLib-PlatformIO) 仓库，不适用
本 README 中的固件路径。

### 2. 编译并烧录固件

在 Arduino IDE 中打开 **文件 > 示例 > LilyGoLib > VibeKeyboard**，或者直接打开
`examples/VibeKeyboard/VibeKeyboard.ino`。连接 T-LoRa-Pager 后，至少确认以下设置：

| Arduino IDE 设置 | 值 |
| --- | --- |
| Board | `LilyGo-T-LoRa-Pager` |
| USB CDC On Boot | `Enabled` |
| Partition Scheme | `16M Flash (3M APP/9.9MB FATFS)` |
| Board Revision | 与实际射频模块一致，例如 `Radio-SX1262` |
| Upload Mode | `UART0/Hardware CDC` |
| Upload Speed | `921600` |
| USB Mode | `CDC and JTAG` |

选择正确串口，点击 **Upload** 完成编译和烧录。需要查看设备日志时，打开 Arduino IDE Serial
Monitor 并设置为 `115200` baud。

如果设备无法进入烧录状态，按住 **BOOT**，短按并释放 **RST**，再释放 **BOOT**，然后重新上传。
烧录完成后按 **RST** 退出下载模式。固件启动后会显示 LVGL UI，并广播名为
`VibeKeyboard` 的 BLE peripheral。

### 3. 创建 daemon 环境

进入随示例提供的 daemon 目录，并根据锁文件创建本地虚拟环境：

```bash
cd examples/VibeKeyboard/tools/vk-daemon
uv sync --frozen --extra ble
```

没有 Bluetooth 的开发环境可以只执行 `uv sync`，并在下一步使用 `--no-ble`。

### 4. 安装 AI 工具集成

只为本机实际使用的工具安装 hook：

```bash
# 使用 Codex 时执行
uv run vk-daemon setup install codex

# 使用 Claude Code 时执行
uv run vk-daemon setup install claude-code
```

这些命令会修改当前用户的 AI 工具配置：Codex 使用 `~/.codex/config.toml` 和
`~/.codex/hooks.json`，Claude Code 使用 `~/.claude/settings.json`。在受限的 AI agent 或
sandbox 中运行时，需要授予对应用户目录的写权限。移动或删除 daemon 目录后，应重新安装
hook，确保其中记录的 Python 路径仍然有效。

### 5. 开启 Bluetooth 并启动 vk-daemon

1. 按下设备 **RST**，或重新上电。进入 VibeKeyboard 主界面后，固件会自动广播名为
   `VibeKeyboard` 的 BLE peripheral。
2. 在 macOS 打开 **系统设置 > 蓝牙**，确认 Bluetooth 已开启。无需在系统蓝牙设备列表中
   手动配对；`vk-daemon` 会作为 BLE central 按设备名或 Service UUID 自动扫描并连接。
3. 首次运行时，如果 macOS 请求 Bluetooth、辅助功能或麦克风权限，请选择允许。也可以按照
   [macOS 宿主权限](#macos-宿主权限)中的路径，确认运行 daemon 的终端或 AI agent 宿主程序
   已有权限。
4. 打开一个终端，显式启用 daemon 的 BLE transport，然后启动服务并保持终端运行：

```bash
cd examples/VibeKeyboard/tools/vk-daemon
uv run vk-daemon config set ble.enabled true
uv run vk-daemon serve
```

daemon 默认监听 `http://127.0.0.1:19280`，并循环扫描、连接及重连 `VibeKeyboard`。成功连接后，
日志会出现 `device_loop.connected label=BLE`，随后 `/device/state` 中的 `ble_connected` 会变为
`true`。设备晚于 daemon 启动也没有关系；扫描失败时 daemon 会继续重试。

Linux 主机需要先启动 BlueZ 服务并打开 Bluetooth adapter，例如在 `bluetoothctl` 中执行
`power on`，同时确保当前用户具有访问 BlueZ 的权限。

Voice/听写还要求在 **系统设置 > 键盘 > 听写 > 快捷键** 中选择 **按下 Fn 键两次**。
`vk-daemon serve` 以前台方式运行，按 `Ctrl-C` 可安全停止。

如果只需要验证 HTTP、hook 或会话发现，不连接实体设备，可以执行：

```bash
uv run vk-daemon serve --no-ble
```

### 6. 验证完整链路

保留 daemon 终端，在第二个终端进入同一目录并执行：

```bash
curl -sS http://127.0.0.1:19280/health
curl -sS http://127.0.0.1:19280/device/state
curl -sS http://127.0.0.1:19280/sessions
uv run vk-daemon setup status
```

验证时应确认：

- `/health` 可以返回响应，`setup status` 中 daemon 可访问。
- `/device/state` 中 `ble_connected` 为 `true`。
- `setup status` 中 `system.accessibility` 为 `true`，`system.microphone_authorization` 为
  `authorized`；启动日志同时包含 `voice.microphone status=authorized` 和
  `voice.accessibility status=authorized`。
- 启动 Claude Code 或 Codex 会话后，`/sessions` 返回对应会话，设备主屏同步显示。
- 设备进入 AI Agent 或 YOLO 页面时，可以收到工具状态或配置；权限请求可以从设备端 Allow/Deny。

如果 HTTP 正常但 `ble_connected` 一直为 `false`，确认设备已启动并正在广播、电脑 Bluetooth
已开启、运行 daemon 的终端具有 Bluetooth 权限。日志位于
`~/.config/vk-daemon/daemon.log`；反复出现 `no 'VibeKeyboard' BLE device found` 时，daemon
仍会继续重试，不需要重新启动。

如果 Voice 在 Visual Studio Code Terminal 中正常、在 macOS Terminal 中失败，通常表示两个
宿主应用的 TCC 权限不同。请为 Terminal 同时开启麦克风和辅助功能权限，完全退出并重新打开
Terminal，再启动 daemon。日志中的以下错误明确表示缺少辅助功能权限，而不是麦克风权限：

```text
macOS Accessibility permission is required to control Dictation
```

daemon 的完整配置、CLI 和平台差异见
[`tools/vk-daemon/README.zh-CN.md`](tools/vk-daemon/README.zh-CN.md)，HTTP 契约见
[`tools/vk-daemon/docs/http_api.zh-CN.md`](tools/vk-daemon/docs/http_api.zh-CN.md)。

### 运行流程

1. 固件启动后，设备显示 LVGL UI 并开始 BLE 广播。
2. `vk-daemon` 作为 BLE central 扫描并连接设备，订阅 Event characteristic，再通过 Command
   characteristic 下发会话、通知、权限请求等状态。
3. 连接后设备主动查询时间；进入 AI Agent 或 YOLO 页面时，还会分别查询工具状态或 YOLO 配置。
4. 设备通过 Event characteristic 回传会话切换、权限响应、setup action 和配置请求。
5. 没有 daemon 连接时，UI 仍可浏览，但真实会话数据和远程操作不会生效。

## 按键导航

| 输入 | 页面 | 作用 |
| --- | --- | --- |
| 旋钮旋转 / 上下键 | 通用 | 移动选择、滚动列表；Sound 音量编辑状态下调整音量 |
| 旋钮中心键 / Enter | 主屏 | 打开 Select Session；旋转切换焦点时会更新当前会话并发送 Session Switch |
| 旋钮中心键 / Enter | Setup、AI Agent、Sound、YOLO、Notify | 确认当前选项或打开详情 |
| 左 / Backspace / Esc | 非主屏 | 返回上一层；离开 YOLO 时会提交当前配置 |
| Backspace / Delete | 主屏 | 发送 Delete 按键事件 |
| 左 / Esc | 主屏 | 发送 Cancel 按键事件 |
| `Fn` | 主屏 | 发送 Session 按键事件并打开 Notify |
| Space | 主屏 | 按下时发送 Voice Press，松开时发送 Voice Release |
| `CAP` | 主屏 | 进入 Setup |
| `CAP` | Select Session | 关闭会话选择弹窗 |
| `A` / `D` | Notify Detail | 分别发送 Allow / Deny 权限响应 |

## 屏幕结构

| 屏幕 | 文件 | 说明 |
| --- | --- | --- |
| Vibe Keyboard | `ui_vibe_keyboard.*` | 主会话面板 |
| Select Session | `ui_select_session.*` | 会话选择弹窗 |
| Notify | `ui_notify_screen.*` | 权限/通知列表 |
| Notify Detail | `ui_notify_detail_screen.*` | 权限请求详情 |
| Setup | `ui_setup_screen.*` | 功能菜单 |
| AI Agent | `ui_agent_screen.*` | 工具和 hook 操作 |
| Sound | `ui_sound_screen.*` | 声音设置界面 |
| YOLO | `ui_yolo_screen.*` | YOLO 设置界面 |
| Keys | `ui_keys_screen.*` | 功能键映射界面；当前 Setup 无入口 |
| About | `ui_about_screen.*` | 设备信息界面 |

主屏、Setup、AI Agent、Sound、YOLO、Notify 和 Select Session 都使用独立的 LVGL group。切换屏幕或弹窗时，固件会同步切换默认输入 group，避免旋钮焦点留在不可见页面。

## BLE 协议

设备名：

```text
VibeKeyboard
```

服务与特征值：

| UUID | 方向 | 说明 |
| --- | --- | --- |
| `5a5f5b5e-1234-5678-abcd-000000000001` | Service | VibeKeyboard BLE service |
| `5a5f5b5e-1234-5678-abcd-000000000002` | 主机 -> 设备 | Command，`WRITE` / `WRITE_NR` |
| `5a5f5b5e-1234-5678-abcd-000000000003` | 设备 -> 主机 | Event，`NOTIFY` |

上行事件（设备 -> 主机）：

| Tag | 编码函数 | 说明 |
| --- | --- | --- |
| `0x01` | `vk_encode_button_press` | 按键按下 |
| `0x02` | `vk_encode_button_release` | 按键释放 |
| `0x03` | `vk_encode_knob_rotate` | 旋钮旋转 |
| `0x04` | `vk_encode_knob_press` | 旋钮按下 |
| `0x05` | `vk_encode_knob_release` | 旋钮释放 |
| `0x06` | `vk_encode_permission_response` | 权限响应 |
| `0x07` | `vk_encode_session_switch` | 会话切换 |
| `0x08` | `vk_encode_setup_action_request` | Setup/Agent 操作请求 |
| `0x09` | `vk_encode_setup_status_request` | 查询工具与 Hook 安装状态 |
| `0x0A` | `vk_encode_time_sync_request` | 查询主机时间 |
| `0x0B` | `vk_encode_yolo_config_request` | 查询 YOLO 配置 |
| `0x0C` | `vk_encode_yolo_config_set` | 更新 YOLO 开关、规则和自动允许通知开关 |

当前页面逻辑主要发送 `0x01`、Voice 松开使用的 `0x02`，以及 `0x06` 到 `0x0C`。`0x03` 到 `0x05` 的编码函数仍保留在协议层，但当前示例没有直接调用。按键 ID 为 Delete `0`、Cancel `1`、Mode `2`、Session `3`、Send `4`、Voice `5`；权限响应值为 Allow `0`、Deny `1`、Always `2`，当前 UI 只提供 Allow 和 Deny。

下行事件（主机 -> 设备）：

| Tag | 解码结果 | 说明 |
| --- | --- | --- |
| `0x81` | `VK_DOWNLINK_SESSION_LIST` | 更新会话列表 |
| `0x82` | `VK_DOWNLINK_SESSION_STATUS` | 更新单个会话状态 |
| `0x83` | `VK_DOWNLINK_PERMISSION_REQUEST` | 新权限请求 |
| `0x84` | `VK_DOWNLINK_NONE` | 校验 LED 数据长度，当前不应用到硬件 |
| `0x85` | `VK_DOWNLINK_NONE` | 校验旋钮灯环数据长度，当前不应用到硬件 |
| `0x86` | `VK_DOWNLINK_PLAY_SOUND` | 播放反馈音 |
| `0x87` | `VK_DOWNLINK_DISMISS_PERMISSION` | 移除权限请求 |
| `0x88` | `VK_DOWNLINK_NONE` | 校验帧尺寸和像素数据长度，当前不显示帧数据 |
| `0x89` | `VK_DOWNLINK_NOTIFICATION_LIST` | 更新通知列表快照，不主动抢占当前页面 |
| `0x8A` | `VK_DOWNLINK_SET_VOLUME` | 设置设备音量（0-100） |
| `0x8B` | `VK_DOWNLINK_SET_MUTED` | 设置全局静音状态 |
| `0x8C` | `VK_DOWNLINK_SET_SOUND_MAPPING` | 设置事件对应的内置音效 |
| `0x8D` | `VK_DOWNLINK_SETUP_ACTION_RESULT` | Setup/Agent 操作结果 |
| `0x8E` | `VK_DOWNLINK_SESSION_LIST_CLEAR` | 清空本地会话并开始 BLE 增量同步 |
| `0x8F` | `VK_DOWNLINK_SESSION_UPSERT` | 新增或更新单个会话 |
| `0x90` | `VK_DOWNLINK_SESSION_REMOVE` | 移除单个会话 |
| `0x91` | `VK_DOWNLINK_SETUP_STATUS_UPDATE` | 更新工具与 Hook 安装状态 |
| `0x92` | `VK_DOWNLINK_TIME_SYNC` | 同步 Unix 毫秒时间戳和 UTC 分钟偏移 |
| `0x93` | `VK_DOWNLINK_YOLO_CONFIG_UPDATE` | 同步 YOLO 开关、规则和自动允许通知开关 |

协议字段采用小端序；字符串为 `uint16_t` 长度前缀加 UTF-8 字节。超出本地数组容量的列表元素仍会被完整读取并丢弃，避免后续字段错位；过长字符串会安全截断并补 `\0`。具体结构以 `vk_protocol.h` 和 `vk_protocol.cpp` 为准。

会话同步支持两种方式：

- 兼容模式：`0x81 SESSION_LIST_UPDATE` 一次发送完整会话列表和活动索引。
- 增量模式：先发送 `0x8E SESSION_LIST_CLEAR`，再发送若干 `0x8F SESSION_UPSERT`；会话关闭时发送 `0x90 SESSION_REMOVE`。

`0x8C SET_SOUND_MAPPING` 支持 `builtin:alert`、`builtin:ding`、`builtin:buzz`、`builtin:click`，以及用于关闭某个事件声音的 `mute` 或 `off`。

## 串口调试桥

AI Agent 操作优先通过 BLE 上行发送。如果 BLE 未连接，固件会在串口输出一行 JSON：

```json
{"type":"agent_action","request_id":1,"action_id":1,"tool":"codex","command":"install_hook","daemon_port":19280}
```

主机端可以向串口回写结果：

```json
{"type":"agent_action_result","request_id":1,"success":true}
```

设备收到后会更新对应按钮状态。

进入 AI Agent 页面时，如果 BLE 不可用，固件还会输出状态查询：

```json
{"type":"agent_status_request","request_id":1,"daemon_port":19280}
```

当前串口接收逻辑只解析 `agent_action_result`。完整的工具检测和 Hook 状态列表应通过 BLE `0x91 SETUP_STATUS_UPDATE` 返回。

## 字体与中文显示

示例直接编译目录中的 LVGL C 字体资源：

| 字体 | 用途 |
| --- | --- |
| `usr_montserrat_12` | 12px 拉丁文本、导航符号和 Font Awesome 图标，回退到 `lv_font_montserrat_12` |
| `usr_montserrat_14` | 14px 拉丁文本、导航符号和 Font Awesome 图标，回退到 `lv_font_montserrat_14` |
| `AlibabaPuHuiTi_Bold_14px` | 主屏和会话选择页的会话名称 |
| `AlibabaPuHuiTi_Regular_12px` | 主屏最后消息、会话摘要和通知详情正文 |
| `AlibabaPuHuiTi_Regular_14px` | 通知列表及通知详情中的会话名和状态文本 |

中文是否能显示取决于对应 Alibaba PuHuiTi `.c` 文件的实际 cmap，而不是字体文件开头生成命令注释里的 `--symbols` 参数。新增字符后应重新生成字体并确认生成文件中存在对应的 `U+XXXX` 字形和 cmap 条目。

## 目录说明

```text
VibeKeyboard.ino          主程序、屏幕切换、按键分发、BLE bridge
vk_protocol.*            VibeKeyboard BLE 二进制协议
vk_sound.*               ES8311 后台播放、音量/静音和事件映射
ui_vibe_keyboard.*       主会话屏
ui_select_session.*      会话选择弹窗
ui_notify_screen.*       通知列表
ui_notify_detail_screen.* 权限详情
ui_setup_screen.*        Setup 菜单
ui_agent_screen.*        AI Agent 工具页
ui_sound_screen.*        声音页
ui_yolo_screen.*         YOLO 页
ui_keys_screen.*         按键映射页
ui_about_screen.*        关于页
usr_montserrat_*.c       拉丁文本、导航符号和图标字体
AlibabaPuHuiTi_*.c       中文字体
sounds/*.inc             内置 WAV 字节数组
```

## 常见问题

**烧录后没有真实会话数据**

需要主机端连接 BLE 并发送 `VK_DOWNLINK_SESSION_LIST_CLEAR` + `VK_DOWNLINK_SESSION_UPSERT`
增量会话同步，或兼容旧版 `VK_DOWNLINK_SESSION_LIST` 全量同步。没有主机端时，固件只显示默认占位数据。

**AI Agent 按钮一直显示 Sending**

设备已经发送了 setup action，但没有收到对应 `request_id` 的 `VK_DOWNLINK_SETUP_ACTION_RESULT` 或串口 `agent_action_result`。

**主机端收不到设备事件**

确认已订阅 Event characteristic 的 notify，并且设备端 `NimBLEServer::getConnectedCount()` 大于 0。未连接时 `send_uplink()` 会直接返回 `false`。

**Command 写入后 UI 没变化**

检查数据包 tag、长度、小端序和字符串长度前缀。无效包会被 `vk_decode_downlink()` 丢弃。

**串口出现 `queue full` 或 `rejected invalid packet`**

`queue full` 表示 BLE 回调接收速度超过主循环消费速度；应降低下发频率或合并更新。`rejected invalid packet` 表示包为空或超过 2048 字节。日志会同时打印最后一个无效包长度。

**时间一直显示 `--:--`**

主机需要订阅 Event notify，接收 `0x0A TIME_SYNC_REQUEST`，再通过 `0x92 TIME_SYNC` 返回 Unix 毫秒时间戳和 UTC 分钟偏移。若板载 RTC 在线，固件会在尚未收到 BLE 时间时使用 RTC。

**中文显示为空白或缺字框**

先确认该标签使用的是 `AlibabaPuHuiTi_*` 字体，再检查生成字体的 cmap 是否真的包含对应 Unicode 码点。字体生成命令中列出字符，并不保证源字体具有该字形。
