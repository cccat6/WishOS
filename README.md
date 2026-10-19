<div align="center">
  <img src="hardware/agent-os/runtime/lckfb-suite/assets/brand/boot-logo.png" width="120" alt="WishOS">
  <h1>WishOS</h1>
  <p>用说话制作自己的小工具，使用时继续修改，保存后直接再用。</p>
  <p><a href="https://wishos.dev">官网</a> · <a href="docs/getting-started.md">开始使用</a> · <a href="https://github.com/cccat6/WishOS-Hub">WishOS Hub</a> · <a href="CONTRIBUTING.md">参与开发</a></p>
</div>

WishOS 是面向 Agent 的嵌入式操作系统。它在设备上运行 Lua 应用、绘制界面、处理按键和触摸，并统一管理声音、文件、网络与外设。你可以让自己的 Agent 制作一个倒计时、信息面板或小游戏，装到设备上使用，再在使用中提出修改。

应用的确定性逻辑在本地执行。语音识别、Agent 任务和较重的资源处理通过 [WishOS Hub](https://github.com/cccat6/WishOS-Hub) 接续用户已有的 Harness 与会话。

## 首批设备

| 设备 | 屏幕和输入 | 已接入的主要能力 |
| --- | --- | --- |
| [立创实战派 ESP32-S3](hardware/agent-os/boards/lckfb-szpi-esp32s3/) — LCKFB-SZPI-ESP32-S3-VA | 320 × 240 彩色触屏、用户键 | 双麦克风、扬声器、摄像头、IMU、SDMMC 卡槽、Wi-Fi |
| [Xingzhi Cube 1.54 TFT](hardware/agent-os/boards/xingzhi-cube-154tft-wifi/) — 方屏小智单键成品 | 240 × 240 彩屏、一个用户键 | 麦克风、扬声器、电池读数、Wi-Fi |

选择与实物接线一致的设备包。其它小智成品可以通过新增设备包适配；芯片相同的设备也可能使用不同的屏幕、引脚和音频电路。

这是首个公开预览版本。两台设备已完成显示、中文字体、单键导航、语音收发与返回桌面的实物验证；立创板还覆盖触摸、相机和 SD/NFS 文件路径。当前验证范围见[设备支持](docs/supported-devices.md)。

## 可以怎样使用

- **制作工具**：向 Agent 描述界面和操作，例如“做一个会议倒计时”。
- **边用边改**：在应用中长按说话，请它调整字号、布局或行为。
- **保存与重开**：设备可安装多个应用，桌面负责选择和打开；本地工具可以离线运行。
- **继续已有对话**：Hub 对接用户的 Harness，会话和已授权工具由 Harness 管理。
- **接入硬件**：应用通过公开资源接口使用屏幕、音频、网络、存储与传感器。

默认单键操作：单击切换、双击确认、三击回桌面、长按说话并松开发送。触屏设备也可直接点选。页面、样式和交互均由运行时应用文件定义。

## 开发与构建

需要 Python 3.10+、Node.js 22+、C/C++ 工具链、CMake 和 Ninja。

```sh
git clone https://github.com/cccat6/WishOS.git
cd WishOS
python3 -m venv .venv
. .venv/bin/activate
pip install Pillow
npm ci --prefix tools
python3 tools/prepare_arduino.py --install-core --host-libraries build/host-libraries --board lckfb-szpi-esp32s3
WISHOS_BOARD=lckfb-szpi-esp32s3 tools/build_wishos.sh
```

方屏小智选择 `WISHOS_BOARD=xingzhi-cube-154tft-wifi`。构建使用锁定的 Arduino ESP32 3.3.11 和依赖快照，输出固件、分区表与初始 LittleFS 文件系统。首次安装写入完整套件，后续固件更新保留已有文件和设置，步骤见[开始使用](docs/getting-started.md)。

运行主机回归：

```sh
ARDUINO_LIBRARIES="$PWD/build/host-libraries" tools/test_wishos.sh
```

测试包含真实 Lua/LVGL 渲染、字体、应用切换、资源释放、并发、故障恢复以及 FAT/exFAT/NFS 文件行为。GitHub Actions 提供手动选择设备的构建入口。

## 代码结构

| 路径 | 内容 |
| --- | --- |
| `firmware/WishOS/` | 运行时、系统服务、资源接口和 UI 渲染 |
| `hardware/agent-os/platforms/` | MCU/SDK 平台接入 |
| `hardware/agent-os/drivers/` | 可复用外设驱动 |
| `hardware/agent-os/boards/`、`compositions/` | 设备接线、原生组件组合与构建配置 |
| `hardware/agent-os/runtime/` | 可替换的初始桌面、应用、字体和页面 |
| `skills/agentic-notes-hardware/` | Agent 能力说明与应用制作指导 |
| `examples/agent-os/` | 运行时应用示例 |
| `tests/` | 行为测试与主机夹具 |
| `website/` | 官网与文档站，部署到 Cloudflare |

系统统一负责加载、调度、完成回调、文件和资源生命周期。应用定义内容、导航及具体操作。设备维护者选择原生驱动与接线，上层应用通过能力发现适配设备。

详细接口见[运行时 API](docs/agent-os-runtime.md)、[应用包](docs/package-contract.md)、[显示与输入](docs/input-and-layout.md)和[设备移植](hardware/agent-os/README.md)。

## 许可

WishOS 原创代码使用 [MIT](LICENSE)。第三方组件遵循各自许可证，随源码和构建包保留来源与许可说明。
