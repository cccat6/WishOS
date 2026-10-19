# 立创实战派 ESP32-S3

设备包 `lckfb-szpi-esp32s3` 使用 ESP32-S3、16 MiB Flash 和 8 MiB PSRAM。原生组合声明接线与外设，初始桌面和页面是普通可写文件系统应用。

## 接线参考

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

## 使用

选择本目录的设备包构建。默认单击切换、双击确认、三击回桌面、长按说话并松开发送。亮度默认50%，开机在完整Logo帧写入后点亮背光。

首次安装写入完整固件及初始LittleFS；以后更新固件时保留已有文件卷。

主要外设与实物验证范围见[设备支持](../../../../docs/supported-devices.md)，构建和接入步骤见[开始使用](../../../../docs/getting-started.md)。
