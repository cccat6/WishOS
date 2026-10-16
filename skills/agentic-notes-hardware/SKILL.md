---
name: wishos-device-api
description: 发现并操作 WishOS 系统资源，为设备编写运行时应用、界面、事件与条件规则。适用于显示、音频、按钮、传感器、网络、电源、文件与任务；根据当前能力描述操作，不预设芯片、引脚、屏幕或存储器。
---

# WishOS 设备与应用

先读取 `/sys/board`、`/sys/capabilities` 和需要操作的资源描述，再读取当前状态。Hub 提供 `discover_resources`、`describe_resource`、`invoke_resource`；可执行的方法、参数、事件和限制以设备当前声明为准。缺失能力不能假装成功。

## 分层

- 系统负责执行、加载、资源准入、事件、文件、呈现、应用生命周期和上位服务通信。
- 原生驱动负责协议、电气时序与硬件生命周期；设备包选择驱动并定义接线。原生改动可以重新编译。
- 应用负责页面、样式、导航、按钮含义和系统调用顺序，使用 Lua 与动态 UI 文件。应用不得访问原生 GPIO、寄存器或 RTOS。
- 出厂启动套装也是普通应用与资源引用。Launcher 可被用户替换；系统独立保证返回桌面和语音入口可达。
- Harness 管理推理、会话与外部工具。应用显式请求系统调用远端；本地确定性任务留在本地。Skill 本身不授权发送消息或添加外部集成。

## 操作原则

1. 区分硬件事实、编译限制、驱动默认值、用户策略与实际生效状态。完整校验设置后再执行；延后生效需要检查 applied 状态。
2. 使用 `notes.on` 注册输入事件，使用 `notes.request` 获取异步完成回调。接收 ACK 不等于执行完成；保存、传输和物理呈现也分别确认。结果不明时先查询，不能盲目重放写操作。
3. 查询可用内存、最大连续块、存储与租约，按当前资源预算分块处理。不能把一块板的内存或速率当成系统上限。
4. 显示选择已声明的颜色空间、格式与呈现配置。电子纸 BUSY 期间不能打断、复位、断电或发送新命令；输入如何响应、队列如何处理由应用和配置决定。
5. 录音使用独立可解码压缩块。没有录音存储时只用短时 RAM 缓冲并报告丢失，不回退到内部 Flash。有 SD 也由用户决定是否保存及保留规则。
6. 安装应用前预检查。使用设备声明的安装卷与配额；小 Flash 设备默认用 SD，较大 Flash 需设备包明确开放。按不可变版本安装、整文件替换；安装成功与激活成功分别确认。
7. 每个应用说明输入含义，提供简短帮助、首次引导、任务反馈和可发现的返回方式。初始套装的源码地址是参考，不是固定页面规范。

## 按需查阅

| 任务 | 参考 |
| --- | --- |
| 系统调用、状态与声明 | [调用流程](references/driver-guide.md)、[资源模型](references/resource-model.md) |
| 应用加载、模块、后台计算 | [运行时](references/runtime-apps.md)、[应用与 Launcher](references/applications.md) |
| 事件、异步完成、按钮组合与交互 | [回调](references/callbacks.md)、[交互](references/interactions.md) |
| 动态页面、字体、图像与显示配置 | [UI](references/dynamic-ui.md)、[显示契约](references/display-interface.md)；仅 E6 设备参考 [E6 配置](references/e6-display.md) |
| AI 请求、顺序结果、大资源、日志、遥测、时间、代理 | [系统服务](references/managed-services.md) |
| 应用预检查、不可变版本、文件级替换 | [代码包](references/packages.md) |
| Wi-Fi、BLE、Socket、Thread、Zigbee | [无线设置](references/wireless-config.md)、[网络](references/connectivity.md) |
| 麦克风、压缩、唤醒、扬声器 | [音频](references/audio.md) |
| 文件、NFS 与挂载 | [NFS](references/nfs-service.md)、[系统契约](references/runtime-design.md) |
| 编译原生设备支持 | [驱动契约](../../../hardware/agent-os/drivers/README.md)、[传感器](../../../hardware/agent-os/drivers/sensor-sampling/README.md)、[执行器](references/actuation.md) |

用 VFS 路径引用页面、模块、字库与资源。不要隐式格式化用户介质；书籍索引在上位机生成。网络发现、状态读取和链路探测不得写入用户介质。文档中的未来目标不是可调用能力，始终以当前设备的正向声明为准。
