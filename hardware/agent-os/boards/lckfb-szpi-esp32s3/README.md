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
|BOOT 用户键|0，低有效|
|共享 I2C SDA / SCL|1 / 2；100 kHz|
|QMI8658 (QST Rev A 文档参数) / FT6336 / PCA9557|0x6a / 0x38 / 0x19|
|LCD MOSI / SCLK / DC / BL|40 / 41 / 39 / 42|
|LCD CS|PCA9557 bit 0；SPI3 专用总线|
|功放使能 / 摄像头待机|PCA9557 bit 1 / 2，上电保持关闭/待机|
|SDMMC CLK / CMD / D0|47 / 48 / 21，1-bit|
|Flash / PSRAM|16 MiB / 8 MiB OPI|

LCD 初始化先设置扩展器为 CS 高、功放低、摄像头待机。软件复位 LCD 后再
将 CS 拉低，然后初始化 ST7789，反相、交换 XY、镜像 X。SDK SPI 主机负责
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
只更新固件是分开的。现有配网/服务/文件网页由普通文件提供。BOOT 长按回到
启动器由独立系统入口保证，具体阈值可通过 `/sys/navigation` 配置。

本轮已连接实机验证：Flash/PSRAM、SD 链路/读写/随机读取/重挂载、LCD DMA
完成、IMU/触摸寄存器采样、HTTP 并发、NFSv3 中文大小写路径访问、运行时
应用与卷的租约。LCD 视觉方向/色彩、实际触摸坐标方向仍需目视/触摸确认。
音频 ES7210/ES8311、GC0308 摄像头和此设备的无线睡眠生命周期尚未接入本组合，
不能从存在可复用驱动或 SDK 编译通过推断它们已完成移植。
