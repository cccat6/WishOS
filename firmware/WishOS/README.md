# WishOS 固件

这里提供 WishOS 的便携运行时和系统服务。当前两个产品设备包为立创实战派 ESP32-S3 与 Xingzhi Cube 1.54 TFT，具体接线和驱动组合位于 `hardware/agent-os/`。

从仓库根目录运行 `WISHOS_BOARD=<设备包> tools/build_wishos.sh`。构建器生成所选设备的头文件、组件和锁定依赖，再编译固件。初始 Launcher、字体和网页随独立 LittleFS 镜像交付，后续通过文件/应用接口更新。

支持的 Flash 下限为 4 MiB；当前两台 S3 使用 16 MiB Flash 和 PSRAM。共享资源管理器按平台能力选择内存，DMA和SDK任务使用其实际要求的区域。

录音采用独立可解码的 IMA ADPCM 块。音频、显示、输入和存储通过统一资源接口供应用调用。NFS 使用锁定的 ESP32NFS 依赖，具体版本见 `tools/dependencies/`。

请从[开始使用](../../docs/getting-started.md)、[运行时 API](../../docs/agent-os-runtime.md)和[设备支持](../../docs/supported-devices.md)继续阅读。第三方许可保存在 `licenses/` 和依赖快照中。
