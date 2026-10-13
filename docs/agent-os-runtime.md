# Agent OS 可执行框架

状态：Arduino ESP32-C6 产品端口与独立 C3 运行时端口共用系统代码；主机使用真实 LVGL、Lua 和驱动状态机进行测试。C3 已做运行时实测，C6 新板的显示、音频、并发和功耗仍需实物验收。此文件说明已实现接口；更长的系统设计文档包含未来目标，不能混同。

## 运行时更新

UI 引擎输出普通 RGB565 区域；六色色值映射和控制器方向转换位于独立 E6 显示适配层，后续其它显示器可替换该适配层。

固件包含渲染引擎、系统调用和硬件驱动。AI 交付的是文件：UI JSON、图像、字体及 Lua 行为程序；更新这些文件不重新编译或烧录固件。

- UI：LVGL 9.3 渲染，支持 Brookesia JSON UI 0.1.x 的明确子集。没有引入完整 Brookesia 框架，也没有宣称支持它的全部绑定、变体或 screenFlow。
- 行为：Lua 5.4.8，`on_start()` 和 `on_event(e)`，通过 `notes.call(path, method, args)` 调用公开资源。`notes.subscribe(path)` 订阅事件；`notes.encode/decode` 操作小型 JSON；不开放自定义元表/析构回调，避免退出应用时绕过执行预算。返回 true 表示消费事件，否则继续执行规则。
- 静态规则：`/sys/rules` 将事件和条件映射到资源操作；`/sys/timers` 发出定时事件。简单应用可以不用 Lua。
- 资源：VFS 提供统一路径。内置系统内容只读；用户文件可以放 RAM、可用的 PSRAM 或 SD。不会为显示自动写入 Flash。
- 发布：先上传新版本目录及依赖，最后 `/sys/ui open`。更新当前页面用 `history: replace`；`refresh` 不增加返回栈。文件读取租约阻止显示过程中覆盖正在使用的资源。

显示队列接受只代表排队。以 `/sys/jobs` 和 `/dev/display` 的结果确认呈现成功；解析失败保留此前的 LVGL 对象树，物理屏幕也不启动新的刷新。导航路径可能先于呈现更新，`presented_path` 区分实际呈现页。

## 内存与图像

| 输入 | 当前处理方式 | 约束/取舍 |
|---|---|---|
| UI JSON | VFS 读取，运行时构建 LVGL 对象 | 每文档 16 KiB、32 节点、8 层；构建上限可在固件调整，不是硬件固有限制 |
| JPEG | LVGL TJPGD 按 MCU 块解码 | 不支持所有 JPEG 编码变体；复杂照片推荐先转换 |
| PNG | LVGL LodePNG | 仅小资源：文件与解码后 RGBA 各不超过 16 KiB；大 PNG 明确拒绝 |
| SVG | 上位机 `compile_os_image.py` + Resvg | 生成可分区读取的 LVGL `.bin`；当前板端不声称支持完整 SVG 解析 |
| `.bin` 图像 | 标准 LVGL RGB565 或 RGB565A8 文件，按区域读取 | 文件可以大于连续 RAM；透明度单独一平面；存 SD 或其他随机读取卷 |
| 原生显示帧 | 分块接收，直接写显示控制器 | E6 为 172800 字节；不落文件、不需要整帧 RAM |
| 字体 | 内置压缩字形，或从 VFS 注册 NMF1 字体 | 主机编译字号/字形索引；当前 4 个注册槽、每次最多 1152 B 字形缓冲 |

固定绘制缓冲是 **7680 字节 RGB565 + 5760 字节原生条带**。这不是 UI 总内存：还需要对象树、JSON、字体/图像解码器与任务堆栈。实际可用内存由 `/sys/stats` 报告；Lua、HTTP 和文件配额单独受限。复杂控件效果、透明度、字体和多任务组合仍需新板测峰值，不能将缓冲大小冒充整体占用。

主机测试把一张 480×720 RGB565 图像（691212 字节）交给真实 LVGL：最大单次读取 960 字节、累计读取 1372322 字节，发生 22313 次文件读取。它节省内存，但从 SD 读取整屏图片时反复小读可能严重增加延迟。小图标继续用 LVGL 资源；整屏照片或预排版页面优先由上位机生成所选驱动的原生帧。当前 E6 原生帧 172800 字节，可从 SD 顺序读取并显示，或直接流式送屏。

无 SD 且没有足够 RAM 时，上位机可以完成复杂 SVG/照片排版，再流式提交原生显示帧。不能承诺在小 RAM 内任意复杂度的 HTML/SVG、无限节点或无限纹理。设备返回支持列表与预算，AI 据此调整资源；不允许静默显示半张图。

## UI 子集

节点：`viewScreen, container, label, button, image`。布局：绝对位置、横向/纵向 flex；`placement` 中设置位置/尺寸。按钮节点负责视觉，实体键行为通过规则或 Lua 定义。

样式：`bgColor, bgOpacity, textColor, textAlign, font, fontSize, padding, paddingLeft, paddingRight, paddingTop, paddingBottom, radius, borderWidth, borderColor, imageOpacity`。颜色为 `#RRGGBB`；不透明度 0–255；尺寸支持数字、`dp`、`sp`。实际屏幕色彩由选中的显示驱动决定。

图像和外部 JSON 相对于所属文件目录解析。未支持的节点/样式/布局返回明确错误。当前没有完整 CSS/DOM、百分比尺寸表达式、跨页 query/anchor 状态和动态数据绑定引擎；程序可以改写新版本 JSON 并刷新，实现动态内容。字体缺字会报错，AI 可注册包含相应字形的外部字体或预渲染资源。

`/sys/ui app_load {file}` 原子替换应用规则组并选择策略作用域，返回 entry；它与 `open {path}`、`/sys/runtime load {file}` 分开，避免显示队列阻塞应用装载。

预制套装只是硬件包中 `startup.script` 默认引用的运行时应用及其资源。E6 的默认 Lua 入口通过公开接口装载上述 manifest，再打开 entry；系统没有另一条固定欢迎页启动路径。保存的运行时启动设置优先于出厂引用；成功手动加载应用后，尚未执行的延迟启动会取消。页面、规则和按键调用可全部替换，无须改变原生服务间必要的协调。

规则组加载先验证入口页面，失败保留原规则和当前页面；manifest 不指定规则 ID，避免覆盖无关规则。应用退出、替换或故障会清理其条件监视、绑定和视图。定时、条件、网络和计算完成事件携带目标实例元数据，旧事件不能触发替换应用的规则或回调；系统事件日志仍可查询这些事实。正在分派的事件只使用分派开始时已有的规则，新装载规则从下一个事件生效。

## 资源和方法

请求发送到 HTTP `POST /os/invoke` 或 USB CDC 的逐行 JSON：

```json
{"id":"request-1","path":"/sys/stats","method":"read","args":{}}
```

通用资源目录：`GET /os/devices` 或 `/sys/capabilities list`。请求上限 4096 字节；文件数据每块 1024 字节。响应中的未知传感量为 null。

| 路径 | 主要方法/功能 |
|---|---|
| `/sys/config` | describe/read/validate/apply；字段范围、默认/系统/应用来源、revision CAS、按需 NVS 持久化 |
| `/sys/stats` | 内存、最大连续块、CPU 采样/未知原因、队列与丢失计数 |
| `/sys/storage` | 物理 Flash 分区、卷/提供者、RAM/PSRAM 配额、SD 容量；不支持在线改变物理分区 |
| `/sys/bus` | read/speeds；主控可达时钟及当前端点时钟，设置由 `/sys/config` 的系统作用域管理 |
| `/sys/cache` | resolve：只计算路径和返回提供者快照，不访问存储；prepare：显式创建缓存目录。两者均不预留文件空间或执行淘汰 |
| `/fs` | mounts/stat/list/read/write/mkdir/rename/remove；offset/size 使用十进制字符串保留 64 位精度 |
| `/sys/ui` | capabilities/fonts/font_register/font_unregister/app_load/create/open/update/activate/close/refresh/back/read |
| `/sys/runtime` | load/restart/stop/read/configure；模块、生命周期、持久启动配置、内存/指令预算 |
| `/sys/workers` | submit/read/cancel/release；受限后台 Lua 计算、可用核心亲和性、结果事件 |
| `/sys/events`, `/sys/rules`, `/sys/timers` | 有界事件日志、条件动作、定时事件 |
| `/dev/display` | drivers/read/present/scene/clear，以及原生帧 begin/chunk/commit/abort；recover 仅在就绪后解除故障 |
| `/dev/mic` | read/start/stop/read_chunk；时长、采样率、保存方式、溢出行为可配置 |
| `/dev/sd` | read/probe/mount/off；只用卡寄存器握手选择链路频率，卷识别另行进行；卡无格式时链路仍可就绪 |
| `/net/default`, `/dev/wifi` | 连接、状态及平台支持的功耗/TX 设置 |
| `/net/http` | 有界异步文本请求，HTTP/HTTPS；HTTPS 目前需要调用方提供 CA；返回作业和完成事件 |
| `/dev/buttons`, `/dev/imu`, `/dev/usb` | 按键/IMU事件、传感数据、CDC 状态 |
| `/dev/battery` | read/write；驱动声明其实际提供的测量属性 |
| `/sys/conditions` | read/write/remove；所有驱动共用的属性条件、持续时间、回差和变化事件 |
| `/sys/power`, `/sys/wake`, `/sys/clock`, `/sys/board` | 功耗、唤醒、时钟、实际 BSP 的引脚/限制 |

配置例：

```json
{"scope":"app:interactive","expected_revision":1,"persist":false,"changes":[
  {"resource":"/sys/input","settings":{"during_display_busy":"dispatch"}},
  {"resource":"/dev/display","settings":{"pending":"latest"}},
  {"resource":"/dev/mic","settings":{"max_duration_ms":null,"storage":"history"}}
]}
```

先 describe/read 获取实际 revision 和字段；validate 通过后 apply。`max_duration_ms: null` 关闭用户时长上限，不代表储存容量无限。`storage: retry` 表示保留待投递录音；当前不自动识别目标 Harness 或执行网络重试。

SPI 物理配置只接受 `system` 作用域。例如将 SD 设为 Auto、上限 25 MHz，屏幕维持固定 16 MHz：

```json
{"scope":"system","changes":[{"resource":"/sys/bus","settings":{"sd_spi_mode":"auto","sd_auto_ceiling_hz":80000000,"display_spi_hz":16000000}}]}
```

SD 的链路探测与卷挂载严格分开。`/dev/sd probe` 用低速 CMD0/8、OCR、CID、CSD、SCR 与可用时的 CMD6 状态建立卡身份和模式；候选频率下重复比较寄存器响应，并启用 SdFat 的 SPI CRC 检查。此过程不读用户扇区、不创建临时文件、不写卡，因此与空白卡、只读卡和分区布局无关。`/dev/sd read` 分别给出 `link_ready`、卡声明的速度/写保护、已选分频、是否超规格、`mounted` 和卷错误。寄存器握手只能证明该命令路径在有限采样内可通信，不能证明日后文件读写的长期可靠性。

默认 Auto 的时钟上限为 80 MHz，但实际选档还受主控时钟、卡当前模式限制：默认速度模式最高 25 MHz，CMD6 声明并成功切换 High Speed 后标准上限为 50 MHz。ESP32-C6 当前 80 MHz 时钟源经分频后在这两个上限内的最高实际档位分别为 20 MHz 和 40 MHz。`/sys/bus speeds` 返回主控本身可产生的完整档位。`fixed` 要求精确可达；超出卡当前模式的频率默认不用于挂载。显式设置 `sd_allow_experimental: true` 可进行超规格寄存器握手，状态标记 `out_of_spec`，此时卷默认不挂载。只有另行设置 `sd_allow_experimental_io: true` 才允许尝试超规格文件 I/O；80 MHz 寄存器应答不代表 SD 协议保证 80 MHz 文件读写。

文件访问或 `/dev/sd mount` 才会读取分区与文件系统；Auto 依次尝试 MBR 分区 1–4 和无分区卷，也可用 `sd_partition` 固定选择。未格式化、文件系统不支持或当前卷不可读时，卷挂载失败但 `link_ready` 保留。CSD 写保护位会使卷只读；未声明写保护也不等于已验证能写。SD 新配置在下一次 `probe` 或重新上电后的首次 SD 文件访问时生效。屏幕独立使用固定时钟，并只在显示空闲边界变更。

## 录音与储存

RAM 网络队列和 SD 队列独立，录音块有 sequence/sampleStart，缺失分别计数。SD 文件为独立块 IMA ADPCM 单声道、伴随增量索引和最终元数据。`retry/history/off`、网络队列、溢出 stop/drop、储存错误 continue/stop 可设置。最后一块写盘完成前不能断外设电源。

SD 使用 SdFat 2，支持 FAT16/FAT32/exFAT。录音索引为 `NAI1` 头与定长记录，不需要事后扫描整段录音。书籍/长文的页面、字符位置索引由上位机预生成；板端按字节位置读取。目录分页目前仍按目录顺序跳过游标，不是常数时间索引。

NFSv3/TCP 已通过 ESP32NFS 1.3.0 接入 `/services/nfs`，默认停止，按需启动。HTTP 文件管理器继续使用同一 VFS。SD 总容量查询使用快照；启动 SD 共享及 NFS FSSTAT 在必要时读取卷空闲簇元数据，首次扫描可能耗时，绝不用于卡存在性/链路探测。录音保留策略已支持空间/条数上限及删除最早记录；自动投递、通用缓存淘汰和多应用管理仍待集成。

## 硬约束

- E6 BUSY 未结束不得发新屏幕命令；超时进入故障，不能强行 reset、断电或以 force 绕过。
- 屏幕物理刷新期间释放 SPI；SD 可取得短事务。录音 I2S 与 SPI 并行。
- 当前板 SD 电源和麦克风 CHIPEN 共用；共享者正在使用时不能切断。
- 整机关机先排队清白，完成后才驱动关机管。关机或休眠遇到录音落盘、网络作业或显示占用会拒绝/延后，而非截断操作。
- ADC 新版 NMOS 功能由 BSP 声明。当前新制板包已启用；被拉低时只代表外部 5V 接入，不能据此推断充满。电池电压在恢复稳定后重新发布。
- 实验波形、寄存器操作与逆向工具不属于公开接口/源包。

## 构建与验证

依赖：Arduino ESP32 3.3.11、ArduinoJson 7.4.3、SdFat 2.3.0、LVGL 9.3.0、Adafruit GFX 1.12.6。Lua 5.4.8 已包含源代码和许可。构建入口 `tools/build_agentic_os.sh`，不自动烧录。当前硬件包的 4 MiB Flash 使用 0x3D0000 字节应用分区（无双槽 OTA）；剩余物理数据分区可被发现，但尚未作为可写 VFS 提供者启用。

测试入口 `tools/test_agentic_os.sh`。覆盖资源调用/规则、策略事务/作用域、VFS/64位位置、BUSY故障约束、真实 LVGL 文件热替换/返回栈/大图按区域读取、Lua 系统调用/事件/指令和内存预算、主机传输与 MCP 桥。主机测试不能验证新板布线、实际电流、录音质量、屏幕时序或 TLS 并发峰值。


## 统一驱动描述与条件机制

`/sys/capabilities list/describe` 枚举驱动声明的属性类型、单位、访问权限、操作、事件和约束。能力描述与实时数据分开；没有提供的能力不进入声明。现有主要外设使用同一描述结构。

`/sys/conditions` 是通用系统机制：依据已声明属性建立 above/below/equal/change 条件，统一处理持续时间、回差及变化事件；`/sys/rules` 或 Lua 将事件连接到动作。驱动不用各自实现一套阈值监视 API。系统低电量策略也使用同一条件引擎。

新增 ADC 电路的电气恢复过滤保留在驱动内部，上层无需操作其恢复步骤。规则只处理驱动完成采样后的属性。参见 [统一资源模型](../skills/agentic-notes-hardware/references/resource-model.md)。

### 系统接口约束

仓库根目录 `AGENTS.md` 是后续实现和审查的约束。当前实现遵循以下边界：

- 配置字段声明 `scope` 和 `apply_time`；在线修改与重启恢复使用相同的类型、作用域和平台校验。整组校验失败不提交任何字段；持久化失败不提交内存配置。恢复无效配置时保留默认配置，并由 `/sys/config read` 的 `restore_valid` 反映。
- SD 寄存器握手、卷挂载、文件操作分别执行；状态查询返回缓存的容量快照。换速、重新探测、卸载和断电尊重活动文件租约。只读状态在挂载之后、持有 SPI 锁时再次检查，避免首次访问绕过权限。链接缓存按卡、协议模式、主机时钟、配置和算法版本校验；不将短握手称为完整读写稳定性验证。
- `/sys/cache resolve` 不执行目录创建；需要写缓存时先显式调用 `prepare`。未安装的可选存储不可被自动当作空存储使用；文件 I/O 失败与 EOF 分开返回。
- `/net/http` 完成结果含 `transport_ok` 与 `http_ok`；仅 HTTP 2xx 且传输完整才产生成功作业，HTTP 4xx/5xx 不作为成功。返回正文仍可读取。开始下一请求前保留上一请求的完成事件。
- 录音的持久化与采集完整性分开：`stored` 表示已提交收到的文件数据，缺失样本数与 `capture_failed` 保存在元数据中，不能将它解释为无缺失录音。元数据短写或同步失败产生 `storage_failed`。
- `normal` / `low_power` 是可配置的电源预设：`normal_cpu_mhz`、`low_power_cpu_mhz`、`normal_wifi_save`、`low_power_wifi_save`。预设在下一次模式请求时生效；实际 CPU/Wi-Fi 状态可单独查询。当前 BSP 的可选 CPU 频率属于硬件适配层能力，不是上层系统统一限制。

这些行为已加入主机回归测试；目标编译不等于新板上的物理验证。

### 按需 NFS 系统服务

`/services/nfs` 提供 `read/start/stop`，配置由 `/sys/config` 的同名资源管理，系统作用域，下一次服务启动生效。单次 `start` 也可以传入配置覆盖，停止后不保存这些覆盖；默认不开机自启。可以通过系统规则/按钮/UI 调用这些方法。

```json
{"path":"/services/nfs","method":"start","args":{"provider":"user","export_path":"/sd","read_only":false}}
```

这里的 `user` 是现有 SD 提供者，已经挂载的分区沿用系统设置，不新建另一个 SD 驱动。也能选择 `system` 或 `ram`，以及安装了的其它提供者；系统内置资源与易失 RAM 使用 `read_only:true`。RAM 不冒充可持久化介质。当前每次共享一个提供者，停止后可以换另一个。

可调参数：共享提供者与导出路径、只读、端口、块大小、带宽预算（0 不限速）、每次轮询网络字节预算、目录返回条数、TCP 连接槽数量、空闲/记录/重放超时。范围通过配置 schema 查询。默认 TCP 2049 同时承载 NFS 和 MOUNT；该服务只提供 NFSv3/TCP，不额外监听 portmapper。

Linux 客户端示例（IP 从服务 `read` 返回值取得）：

```sh
sudo mount -t nfs -o vers=3,proto=tcp,port=2049,mountport=2049,nolock DEVICE_IP:/sd /mnt/notes
```

macOS：

```sh
mount_nfs -o vers=3,tcp,port=2049,mountport=2049,nolocks DEVICE_IP:/sd ~/mnt/notes
```

挂载点目录需要先存在。客户端先卸载，再调用 `/services/nfs stop`。强行停止会关闭现有连接；已经成功确认的持久化写入已由提供者同步，不另设隐藏的未提交写缓存。

服务运行时固定其存储挂载，重新探测、换速、卸载、SD 断电返回占用错误。Wi-Fi 断开/切换、休眠和整机关机需要先停止服务；正常 CPU/Wi-Fi 省电预设仍可调整。已配置的低电量自动关机先停止 NFS，以免共享服务阻止该关机策略。SPI 仅在实际文件访问期间占用，不在等待客户端时持有。UI 正在使用的文件不可由 NFS 改写；活动录音文件也被保护，互不相关的文件可继续访问。提供者操作仍可能受到实际 SD 读写长尾延迟影响，网络轮询预算不是硬实时保证。

`read` 返回正在使用的配置、下次启动的配置、收发字节、请求/重放/超时计数及服务已分配堆内存。堆统计不含共享 TCP/IP 栈的 socket/pbuf。停止释放会话、句柄池和协议缓冲。

本机 HTTP/脚本/录音修改共享存储后，当前桥接保守地使原 NFS 句柄失效，发出 `handles_invalidated`；客户端可能需要重新挂载。NFS 自身的写入保持会话。当前不提供覆盖已有目标的原子 rename、持久 EXCLUSIVE CREATE、Unix 所有权/时间设置、稀疏文件扩展；不通过删除原文件来伪装原子替换。已启用 SdFat UTF-8 长文件名并实现标准路径和文件系统名称比较，中文可通过 NFS/HTTP 访问。当前桥接仍保守拒绝带 `~` 的 NFS 路径以及本地修改路径，不声明完整短别名支持。默认 64 KiB 预算与 32 KiB 剩余堆保留量共同约束协议和路径身份内存；运行中可通过 memory 调整，低于当前占用的修改被拒绝。停止失败保留 stop_pending 与存储租约，不能虚报已释放。

验证：新增主机 NFS 帧级挂载/查找/读写测试、真实回环 TCP 只读挂载与分片测试、文件租约/失败回滚/停止资源释放测试；设备无线链路、SD 实际吞吐与录音/显示共存仍需新板验证。不会自动烧录现场屏幕。

### 通信驱动与执行位置

本地运行时继续使用 Lua 5.4.8；UI、脚本和资源可动态更新。主机 TypeScript/JavaScript 服务通过通用远端请求桥接；没有声明设备本地支持 JavaScript 或 TypeScript。

新增 `/net/tcp`、`/net/udp`、`/net/ws`、`/net/http-server`、`/dev/ble`、`/services/remote`。HTTP 客户端支持有界流式接收。设备事件、定时器、规则及 Lua 都能调用这些接口。详细参数与生命周期见 [通信操作指南](../skills/agentic-notes-hardware/references/connectivity.md)。HTTP 服务端动态路由使用 202 + 请求 ID/结果查询；WebSocket 当前提供客户端角色。TCP 当前接受数值 IPv4 地址；UDP 支持 IPv4/IPv6。

`/sys/radio` 持久化选择 `thread`、`zigbee` 或 `off`，重启生效。同一固件包含两套 IEEE 802.15.4 协议栈，启动后仅注册所选驱动。应用与生态服务不进入驱动层。当前 Zigbee 构建支持 router/coordinator，APS 数据包、入网窗口和收发事件；Thread 提供 Operational Dataset 配置、角色、IPv6 网络。默认选 Thread，但不自动加入网络。

构建脚本在私有构建目录对 SDK 3.3.11 的 OpenThread/Zigbee 回调符号分别命名，通过 IRAM 分发器交给启动时选中的协议。工具核对回调符号集合、记录输入/输出归档哈希，不修改已安装 SDK，也不忽略重复符号。SDK 版本变化必须重新核对 ABI。两套栈不同时初始化；这不是同射频多协议并发实现。

固件使用自带 `partitions.csv`：单应用分区 0x3D0000、Zigbee 存储分区和有限剩余 Flash。首次部署需要使用该分区表；不会自动改写已连接设备。使用 `tools/build_agentic_os.sh` 生成包含两套驱动的镜像。Arduino IDE 的默认链接流程尚不执行这段 SDK 回调适配，构建使用脚本，原应用仍使用 Arduino API。

上层边界：本地做确定性定时、请求、解析、按钮逻辑；主机服务做重处理；Harness 负责需要推理/工具的任务。远端路由可由服务模块扩展，不要求每次请求调用模型。设备完成回执、HTTP/网络成功、Harness 接受和任务完成分别表达。家庭账号、设备品类、厂商生态与面板逻辑均由上层应用提供。

### Wi-Fi 多配置与无线状态汇总

`/sys/radio read` 统一显示 Wi-Fi、BLE 与 IEEE 802.15.4 状态，修改仍走各自接口。`/dev/wifi` 新增 `describe/configure/profiles/profile_set/profile_remove/disconnect/resume_auto`；`connect` 支持已保存 profile 或临时 SSID。实现优先级选择、失败退避、手动保持、可配置主动择优、迟滞/确认/最短驻留、异步扫描/关联以及显式断开后的自动连接抑制。默认保持健康连接；手动选择在掉线或连接失败后释放，恢复按优先级自动选择（仍尊重自动连接开关）。显式锁定模式才会持续重连指定 SSID。八个 profile 是当前构建容量，通过接口声明。

Wi-Fi 使用单份配置持久化，自动决策只改变 RAM 状态。旧单 SSID 凭据迁移为 legacy profile。BLE 使用独立配置与版本号，配置修改不立即重启活跃 BLE 会话，支持持久化开机启动、暂存 profile、明确 restart 和一次性 start 覆盖。保存失败和校验失败不提交候选配置。具体参数、默认行为与命令见 [无线配置指南](../skills/agentic-notes-hardware/references/wireless-config.md)。

构建与测试从固定的已验证 ESP32NFS v1.3.0 提交生成依赖快照，避免另一个库开发会话的未提交改动进入产品测试；升级该依赖需独立验证桥接兼容性。`ESP32_NFS_LIBRARY` 可显式指定另一个待验证版本。

## 硬件包、脚本调用与后台执行

板级接线、启动 UI/脚本与默认配置已迁到 `hardware/agent-os/boards/e6-nova-c6`，原生适配入口迁到 `hardware/agent-os/ports/esp32-c6-e6/Port.inc`。`NOTES_OS_BOARD` 选择包，构建只生成独立 sketch，不修改公共系统代码。自制同类接线板修改包即可；新 MCU/显示器/外设需要相应原生端口，不把 C6 固件冒充可直接烧录到 S3/C3。当前仅有 C6/E6 的产品适配端口。详见 [硬件包接口](../hardware/agent-os/README.md)。

上层 `notes.call` 与外部 `/os/invoke` 共用 Core 驱动入口及关机/占用检查。新增 VFS 相对模块导入、开机脚本配置、重载与 `on_stop`、保真 JSON null/空数组、二进制 hex/base64 编解码、精简分页能力发现。脚本创建的定时器和规则带运行实例 owner，退出/替换/故障清理，并隔离此前残留的定时事件。录音增加 readable 通知，无须硬编码轮询周期。

`/sys/workers` 的计算 VM 与主应用独立，通过输入/结果传递数据，不能直接访问设备。FreeRTOS 适配器以低优先级运行，每个 VM 执行片段主动让出调度；支持选择当前平台可用的核心，C6 只暴露单核。工作器内存、总指令与任务槽均有上限，运行中取消是协作请求，未结束不允许释放或整机休眠。Lua 模式匹配函数因可绕过 VM 钩子长期回溯而未开放，提供有限长度的字面搜索。详见 [运行时应用接口](../skills/agentic-notes-hardware/references/runtime-apps.md)。

尚未完成：参考设备的原生 LCD/触摸/音频编解码适配、C3/S3 产品固件、通用持久多应用管理、录音自动投递与确认清理、C6 产品端口的可写内部 Flash 挂载以及新板实测。C3 运行时端口已有 LittleFS 提供者。执行器/FOC 不在本轮实现范围，开放资源注册接口允许以后以驱动接入。硬件包结构和主机模拟双核工作器测试不是这些参考设备的实机移植证明。

### 参考硬件的移植边界

以下是项目公开说明的核对结果，用作端口设计检查，不表示已经移植或烧录：

| 参考项目 | 已确认的适配需求 |
|---|---|
| [4.2 寸 TF 电子书](https://oshwhub.com/shukkkk/4-2-mo-shui-ping_copy_copy_copy) | GxEPD2 类电子纸、TF 文件；不采用其导入时全书建索引的策略 |
| [OnePage](https://oshwhub.com/movecall/project_ucjwpaya) | ESP32-C61HR2、4.26 寸电子纸、16 MB Flash/2 MB PSRAM、ADC 按键梯、PDM 麦克风、SD |
| [MiaooAim](https://oshwhub.com/team_voosogmo/project_fxbcjhaa) | ESP32-S3、多种尺寸/颜色/控制器电子纸、可选 PSRAM、四键 |
| [智能手表](https://oshwhub.com/coconet/smart-watch-multi-function-termi) | S3 双核、ST7789 240×280、CST816S 触摸、SPI SD、RTC、音频播放 |
| [C3 天气时钟](https://oshwhub.com/xnzl/esp32c3-made-desktop-weather-clock) | ESP32-C3、2.8 寸 ST7789；无需强制 PSRAM、IMU 或三键 |
| [小智 1.3 寸版](https://oshwhub.com/eda_ndykqjivo/xiao-zhi-ai-jing-jian-ban-1-3-cun-ye-jing-ping) | 页面 BOM 为 ESP32-S3-WROOM-1-N16R2，LCD；未凭名称推断完整屏幕脚序 |
| [AI 小智](https://oshwhub.com/hnbc315/ai-xiao-zhi) | S3R8、ES7210/ES8311、扩展传感器、摄像头和 IO 扩展芯片 |
| [AI 相框](https://oshwhub.com/yc_chen/ai-smart-photo-frame) | S3/PSRAM、触摸 LCD、双麦克风、音频输出、SD、不同开关电源拓扑 |
| [智能旋钮](https://oshwhub.com/ran-pang/knob) | 仅作为不同输入输出和资源需求的参考；不实现 FOC、执行器或其控制应用 |

这些差异不应出现在上层应用的 GPIO 常量、固定三键假设、固定色彩格式或 CPU 核号中。驱动发布实际能力，应用发现后选择。触摸事件和更多输入设备可以作为新资源发布；摄像头/音频输出等尚未实现的驱动不在当前设备目录中冒充可用。常用接口族不限制新的资源类别。

### 运行时并发与失败恢复验证（2026-10-13）

新增应用 owner 回收钩子。脚本创建的 TCP listener/accepted socket、UDP endpoint、HTTP 动态路由及请求、后台计算任务归属运行实例；停止、替换、异常均触发清理，跨实例修改句柄被拒绝。工作器先请求取消，后台线程结束后由主循环回收，不能强行释放运行中的 VM。C6 HTTP/WS/麦克风同步加入 owner 生命周期；HTTP 与录音使用取消/停止请求，等待网络任务结束或录音落盘。Wi-Fi 配置、BLE/NFS 这类显式系统服务仍按各自生命周期管理，不把应用退出解释为回滚整机配置。

修复计算结果在 `lua_resume` 返回后进行转换的路径，转换现在处于 Lua 保护边界；结果内存不足会报告失败。协程分配失败同样回收 owner 资源，重复退出保持幂等。驱动响应对象未能分配时拒绝调用；调用后生成响应失败则报错，不能据此假设副作用未发生，重试前应查询实际状态。

新增 `notes_failure_test`：对每个 Lua 分配位置注入持续失败，分别覆盖加载/回调/退出、JSON 调用桥与计算返回值转换，验证分配清零和定时器清零；注入仅针对申请/增长，缩小重分配遵循 Lua/ArduinoJson 必须成功的分配器契约；两个真实主机线程的容量、取消、跨 owner 冲突、异常退出回收；24 个计算任务分批双线程执行时，主循环继续处理 HTTP 与定时器。使用 AddressSanitizer/UndefinedBehaviorSanitizer。另以 ThreadSanitizer 跑相同并发路径（Lua 静态库未插桩，不能视为全库线程证明）。

`os_http_lifecycle_test` 执行实际 `NetworkService.h`，使用受控传输与线程安全队列后端，覆盖满队列反压、阻塞传输期间取消、延迟退出、孤儿任务回收、任务创建失败及后续成功流式请求。它验证生命周期算法，不冒充真实 TLS/Wi-Fi 验收。原有真实回环 TCP 测试以及显示 BUSY、SD/NFS 租约、资源冲突测试继续运行。实机无线稳定性、物理音频并发和持续功耗仍待新板验证。

### 应用层与后续适配

仍未闭环：AgenticOS 音频到现有 Harness 的完整应用接入和确认/重试机制、录音持久化后端仍直接使用 SD；应用发布事务/回滚与完整多应用生命周期；C6 产品端口无 SD 时的可写持久 VFS；网络首次配置界面仍是基础控制台；文件管理下载/上传占用和流式预算仍需进一步统一；原生文件系统长尾和整机实机并发/功耗验收。现有 Lua 调用与后台计算不等于这些目标已经全部完成，页面和 Harness 工作流程应通过运行时应用补齐。

### 当前仓库与调试板

本目录的本地 Git 只跟踪公开 AgenticOS 源码、硬件包、测试和构建工具；`hub/` 是独立服务端 Git，包含自身渲染工具、字体和依赖清单。E6 实验、个人配置、数据库和设备数据不进入任一仓库。MCP `hardware_discover` 分页读取全部接口；`hardware_invoke` 继续使用通用资源 ABI。

系统运行于 Arduino ESP32 的 FreeRTOS，不自行实现另一个 RTOS。`/sys/resources` 分开管理片内/外部内存：VM 采用可配置上限并按实际增长计费，原生任务的固定预留单列；NFS、RAM/PSRAM 文件及显示缓存也使用共同分配器。PSRAM 不能满足原生内部内存需求。物理剩余堆仍包含 SDK 的独立分配情况。原生实时采集与普通后台计算分工保持不变。

录音默认 `storage:off`：没有 SD 不保存，也不转写内部 Flash；RAM 队列满可丢弃并统计缺失。启用 SD 存储时可设置 `storage_max_bytes`、`storage_max_count`、`storage_on_limit:stop_storage/delete_oldest`；当前受限目录索引容量 128 条，动态按需分配。清理仅操作带系统录音元数据的记录及其索引，尊重文件占用；采集与写盘状态分开。没有卡且允许存储失败继续时，实际退为 RAM/网络录音并报告原因。写盘改在主循环分块执行，避免后台任务直接竞争文件租约表。

RAM、网络和 SD 使用同一 IMA ADPCM 独立块：160 样本块为 91 字节（原 PCM 为 320 字节），包含各块解码状态，缺块不导致后续无法解码。约同等 RAM 下，16 kHz 网络队列由 48 块扩到 144 块，约 1.44 秒；48 kHz 时约 0.48 秒。实际丢失仍按 sequence/sample_start/dropped_samples 报告，不承诺无网络可无限录音。Hub 解码保持到需要 PCM/ASR 的边界。

初始管理网页已移到每个板包的 `/setup.html`，包括网络、文件操作和 MCP/Agent/LLM API 端点配置。保存端点不等于完成连接或鉴权；具体客户端由初始应用或上位服务执行。参考 C3 包没有屏幕和麦克风，不声明这些能力，使用同一 Core/Lua/Workers/VFS/Wi-Fi/NFS 源码；其 `format` 只在显式系统调用时执行，不在挂载失败后自动格式化。

实机验证已在独立 ESP32-C3 上执行：联网、局域网动态 HTTP、中文 LittleFS 文件读写、Lua 定时器与后台计算、无限循环/内存超限后的恢复、NFS 启停释放；MCP 官方客户端经过 MCP 服务端到设备 HTTP，发现 20 个真实资源并完成文件写入/读取/删除。最新测试保留系统默认的 20,000 B 堆余量策略；两个小型测试脚本按需申请 24 KiB 和 16 KiB 实例配额，本轮（2026-10-14）最低剩余堆 43,344 B；结束后 managed/reserved 均为 0。另验证了无 PSRAM 时固定 external 配置被拒绝并保留原策略，以及 RAM 文件的实际占用/删除计费。额外验证了运行时替换网页首页、退出后恢复默认页。资源不够的更大并发请求被明确拒绝并回收，不通过降低系统预留来绕过准入。C3 没有连接显示器和麦克风，压缩编解码与录音保留规则通过主机测试，不将此作为 E6 显示/录音实测。

真机调试修复了两个 Arduino 适配问题：关闭 AP 使用 `WiFi.AP.end()` 避免 `softAPdisconnect(false)` 隐式重新启用 AP；STA 使用全信道扫描并按信号选择同一 SSID 的接入点。USB CDC 接收缓冲与 4 KiB 协议请求对齐。临时 C3 不再备份 Flash，已删除先前产生的备份。


### 当前显示与基础运行时包

E6 端口提供蓝黄双色、快速彩色、高质量彩色三个固定复合驱动，同时保留
控制器原有六色驱动。上层发现色表、输入格式、预计时间与适用范围；不提供
原始波形或寄存器接口。模式准备/切换、两平面回放、BUSY 和错误收尾归驱动。
当前复合模式使用可重放 UI/scene/文件；无存储单次网络流仍使用六色驱动。
细节与先前板子的参考时间见 [E6 显示驱动](../skills/agentic-notes-hardware/references/e6-display.md)。
新板尚未完成光学、录音与并发功耗验收，不能把主机驱动序列测试当成实屏结果。

板包的普通 startup 引用选择基础 Lua 应用，页面/行为/显示驱动选择均在
`assets/suite` 和 `assets/ui` 内。十页同时提供横竖版，欢迎页可选择热点、
已保存网络或离线入口；其它页提供网络地址、服务配置摘要、存储、电源状态、
录音与休眠。服务接入仍需配置上位应用。包里的单应用 64 KiB 上限来自该套
脚本的测量，不改变全系统默认配额，也不一次占用 64 KiB。

图标是小型索引色文件，经同一 VFS 逐行读取；字体沿用方格像素对齐的
主机编译流程。外部字库使用可随机读取的 NMF1 字形索引，不在设备执行
无界 TrueType 解析。图片解码失败传递给显示事务，不把缺图提交为成功。


### 可编译替换的显示组件

`DisplayScheduler.h` 统一管理请求队列、视图版本、完成回执与资源释放；
`drivers/e6/` 和 E6 端口绑定处理具体色表、像素布局、BUSY 和硬件时序。
三种复合模式属于同一个 `e6-ist7601` 组件的 profiles，上层 `driver` 参数
选择 profile。替换底层屏幕需要原生适配和重新编译，页面/行为仍可运行时替换。

完成事件区分 `transport_complete`、`device_ready`、`frame_latched`、
`unchanged`。`on_event` 在服务循环内执行，并用 job/view/revision 关联请求；
不会在 DMA/VSYNC 的 ISR 内调用用户脚本。LCD/OLED 并不被一概视为同步完成，
连续扫描的显示器也不因一次请求完成就释放扫描缓冲区。具体协议见
[显示接口](../skills/agentic-notes-hardware/references/display-interface.md)。
当前真正接入的硬件驱动仍是 E6；其他完成模型使用同一个调度器做主机行为测试，
并不代表已实现或实测各型号 LCD/OLED/LED 驱动。
