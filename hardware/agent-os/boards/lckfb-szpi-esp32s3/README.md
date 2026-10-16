# LCKFB-SZPI-ESP32-S3-VA

此设备包选用同一个 WishOS 内核，通过独立的原生组合接入实际接线。
ESP32-S3 平台本身不隐含此板的外设或 UI。启动器、页面和交互为
`../../runtime/lckfb-suite` 中的普通文件系统应用，可在运行时替换。

官方依据：[开发板介绍](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/introduction.html)、
[SD](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/sd-card.html)、
[LCD](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html)、
[IMU](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/attitude-sensor.html)。

|连接|GPIO / 地址|
|---|---|
|用户键（GPIO0 启动绑带）|0，低有效|
|共享 I2C SDA / SCL|1 / 2；100 kHz|
|QMI8658 (QST Rev A 文档参数) / FT6336 / PCA9557|0x6a / 0x38 / 0x19|
|LCD MOSI / SCLK / DC / BL|40 / 41 / 39 / 42|
|LCD CS|PCA9557 bit 0；SPI3 专用总线|
|功放使能 / 摄像头待机|PCA9557 bit 1 / 2，上电保持关闭/待机|
|SDMMC CLK / CMD / D0|47 / 48 / 21，1-bit|
|I2S MCLK / BCLK / WS / ADC 输入 / DAC 输出|38 / 14 / 13 / 12 / 45，共用 I2S0 时钟|
|ES7210 / ES8311|I2C 0x41 / 0x18；两只麦克风和一条播放参考输入|
|GC0308 XCLK / VSYNC / HREF / PCLK|5 / 3 / 46 / 7|
|GC0308 D0..D7|16 / 18 / 8 / 17 / 15 / 6 / 4 / 9|
|Flash / PSRAM|16 MiB / 8 MiB OPI|

LCD 初始化先设置扩展器为 CS 高、功放低、摄像头待机。软件复位 LCD 后再
将 CS 拉低，然后初始化 ST7789，反相、交换 XY、镜像 X。背光 GPIO42 低有效，初始化完成后拉低开启。SDK SPI 主机负责
总线仲裁。像素从共享 UI 渲染器生成，DMA 缓冲区保留到传输完成回调；
`transport_complete` 表示传输完成，不承诺 TE 同步锁存或人眼观察结果。

SDMMC 的 `probe` 只初始化卡协议并检查状态，不依赖分区、文件或写权限，
也不测吞吐。Auto 在设备声明的 40 MHz / 20 MHz / 400 kHz 范围内尝试；
实际频率受卡能力影响。固定频率不允许静默舍入。`mount` 单独识别卷，
不格式化。当前 Arduino SDK FatFs 卷适配提供 FAT12/16/32，不能据此声称
支持 exFAT；SPI/SdFat 适配的文件系统能力与它分开声明。

所有主机/文件访问共用该 SDMMC 主机互斥量。挂载、卸载、探测和容量扫描
在原生工作任务中执行，事件回调在系统循环派发。已打开的应用文件、卷固定
租约、NFS 服务阻止卸载；状态查询仅返回快照，不扫描卡。文件访问保留错误
而不是把失败当作 EOF 或空目录。中文路径与 Unicode 大小写使用 FatFs 规则。

6 MiB 为代码预算，约 9.875 MiB 为可写 LittleFS；首次刷写文件系统镜像与以后
只更新固件是分开的。现有配网/服务/文件网页由普通文件提供。用户键长按回到
启动器由独立系统入口保证，具体阈值可通过 `/sys/navigation` 配置。

本包的普通 LittleFS 资源包含完整 Notes Sans SC TrueType 字体（29,285 个映射字符），
字号按需生成，字体通过 VFS 分段读取。字体、启动器和页面均可独立更新；
不会把整份字体装进 RAM。GPIO 按键边沿独立捕获时间戳，手势和系统返回
在事件循环处理，渲染耗时不能把短按变成长按。

音频组合通过同一 I2S 双向主机协调 ES7210 与 ES8311，所有使用者必须采用
相同采样率。录音输出为独立可解码的 IMA ADPCM 块，缓存由资源管理器分配，
有 PSRAM 时可使用 PSRAM。输入通道角色为 MMR：两只物理麦克风加有线播放
参考；有参考输入不代表已经提供回声消除。播放完成以 DMA 排空为依据。
`/dev/speech` 提供能量活动检测和所选编译模型 Hi ESP，具体操作由运行时应用定义。

`/dev/camera` 通过原生工作任务初始化、采集和停止；RGB565 图像分块读取，
显式释放或随应用退出归还 SDK 帧。SCCB 与 Wire 共用 IDF I2C 主机。
NimBLE 主机分配器按同版本 SDK 的外部内存选项重新编译，控制器仍使用片内
内存；构建会核验 SDK 版本和源文件摘要，不忽略符号冲突。

当前实机证据包括：Flash/PSRAM、SD 链路/读写/随机读取/重挂载、LCD DMA
完成、IMU/触摸寄存器采样、HTTP 并发、NFSv3 中文大小写路径访问、运行时
应用与卷的租约。双麦克风及播放参考通道均完成三秒压缩采集且无丢块，用户
已确认提示音清晰；摄像头彩条和实际帧均已取回，唤醒模型能持续处理输入。

验收尚未结束：背光已由用户确认持续显示；多字号中文完整，按键单击与
四角触摸坐标已复测，五秒语音采集无丢块。实际唤醒词识别仍需确认。蓝牙扫描与 Wi-Fi 共存、重复启停及提高保留内存后的停止清理已实测。
完整字库的多字号中文测试页已由用户确认，重启后也保持零缺字替代。此组合的可选 SD 录音持久化与无线睡眠生命周期尚未完成。
