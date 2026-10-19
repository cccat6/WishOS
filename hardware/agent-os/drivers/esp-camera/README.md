# ESP camera driver

Reusable DVP camera lifecycle adapter. The device composition supplies wiring,
sensor model, supported SDK auto controls, framebuffer configuration and startup
defaults. Applications use `/dev/camera`; preview loops and shutter behavior are
ordinary runtime application code.

`configure` applies while stopped. Its whole input is validated before changes.
Optional `auto_exposure`, `auto_white_balance` and `auto_gain` are advertised only
when the selected sensor/SDK implementation supplies them. `warmup_frames` and
`warmup_ms` are independent unsigned 32-bit settings: `start` returns ready after
both thresholds are met. Startup runs on the retained native worker, returning
every discarded SDK frame; it holds the shared sensor-bus transaction only while
applying sensor controls. The elapsed condition uses the SDK 64-bit monotonic
clock, including durations that cross the 32-bit `millis()` wrap.

Warmup is configurable settling time, not a measurement of optical convergence.
The LCKFB GC0308 composition currently selects four discarded frames and 500 ms,
with AEC/AWB/AGC enabled. The installed Arduino ESP32 3.3.11 camera archive exports
real GC0308 setters for all three controls; current upstream GC0308 uses page-zero
register 0x22 bits 0/1/2 respectively. Older v2.1.4 used dummy setters, so changing
SDK snapshots requires checking the actual implementations. RGB565 output format
is selected by the SDK; no custom color correction or sensor-register patch is
part of this driver.

Successful `capture` retains one SDK framebuffer until explicit `release` or
owner cleanup. The result includes frame identity and SDK boot-monotonic capture
timestamp. `copy_frame` copies to an existing volatile destination; persistent
storage remains a separate explicit application action. Other application owners
cannot stop or consume a live owner's capture. Failed SDK deinitialization keeps
its resources and initialized state for a later explicit stop retry. Cancelling a
blocking SDK capture becomes effective at its safe return boundary.

Primary references:

- [ESP camera driver and framebuffer API](https://github.com/espressif/esp32-camera/blob/master/driver/esp_camera.c)
- [GC0308 SDK controls](https://github.com/espressif/esp32-camera/blob/master/sensors/gc0308.c)
- [GC0308 default register sequence](https://github.com/espressif/esp32-camera/blob/master/sensors/private_include/gc0308_settings.h)

`notes_camera_startup_test` executes the real adapter with controlled SDK calls:
startup thresholds, 32-bit clock-boundary crossing, bus release during capture,
cancellation, setter/frame failure, frame ownership/copy, and deinit-failure retry.
These host checks do not establish physical exposure or white-balance quality.

Framebuffer admission follows `camera_config_t.fb_location`: PSRAM framebuffers
reserve external memory; DRAM framebuffers reserve internal memory and default
`psram_dma` to false. This adapter's compiled target is ESP32-S3, whose fixed SDK
provides the direct PSRAM-DMA API. Enabling that option with DRAM framebuffers is
rejected before configuration changes. The selected framebuffer count is included
in the complete storage estimate and exposed in metadata; the native internal
workspace remains a separate reservation. Existing one-buffer PSRAM estimates
remain unchanged. These are admission estimates, including the existing per-frame
64 KiB allocation allowance, while the SDK's actual allocation result remains
authoritative. The fixed SDK's `cam_config`/`cam_dma_config` allocate every frame
with INTERNAL or SPIRAM caps according to `fb_location`, and direct PSRAM DMA adds
frame-tail/alignment storage; the non-direct path has a separate internal DMA
buffer. Driver tests cover DRAM with no external heap and multiple framebuffers
that exceed one-buffer-only headroom without retaining partial reservations.

For the fixed ESP32-S3 SDK, non-direct capture adds one internal DMA buffer
bounded by `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX` (32768 bytes in Arduino 3.3.11),
so its workspace estimate is the existing 8192-byte control/SDK-task workspace
plus that SDK bound. It no longer uses an unrelated 98304-byte estimate. For the
supported RGB565 geometries, the SDK selects 3840-byte nodes and a 30720-byte
non-direct DMA buffer with eight 12-byte internal descriptors (96 bytes). The
existing control workspace already covers more descriptor storage in direct
QVGA capture (40 descriptors, 480 bytes), alongside the SDK's 4096-byte task,
control state and queues. The retained WishOS native executor has its separate
16384-byte reservation; the framebuffer region/count reservation is separate as
well. Actual allocation failure and fragmentation remain SDK-authoritative.
These byte bounds apply to the pinned SDK and require renewed bridge validation
when the SDK configuration or supported pixel geometries change.

The ESP32-S3 clock helper applies the compiled `xclk_freq_hz` exactly. It first searches both clock sources for an integer divider, preferring
PLL160M (20 MHz: N=8) then PLL240M (24 MHz: N=10). If neither is integral,
it reduces the exact remainder fraction and accepts it only when the hardware
6-bit numerator/denominator can represent it: 25 MHz is N=6+B2/A5 from PLL160M,
and 23 MHz is N=6+B22/A23. An unrepresentable frequency such as 23000001 Hz is
rejected before powering or allocating capture resources; it is never rounded.
Other ESP camera platforms retain their SDK clock path and do not publish an
unimplemented exact-clock or applied-frequency claim. After SDK init
and before sensor setup/startup discard, it enables the selected source, uses the
SDK RCC guard and CAM HAL to set source/divider, requests the shadow-register
update and reads the fields back. `read.clock` reports requested/applied nominal
frequency, source frequency and divider after the native operation settles.
The source reference remains until SDK deinitialization succeeds. The hardware
clock correction belongs to this S3 driver; no OS-level clock rule is introduced.
The nominal register result is distinct from an oscilloscope measurement.

This follows the pinned SDK's `hal/esp32s3/include/hal/cam_ll.h` clock formula and
source selectors, plus its native DVP driver's source-enable/RCC procedure:
[ESP-IDF S3 CAM HAL](https://github.com/espressif/esp-idf/blob/v5.5.5/components/hal/esp32s3/include/hal/cam_ll.h),
[ESP-IDF DVP clock initialization](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c).
Startup discard settles any transition frames before capture is exposed; the
adapter does not cancel an active application capture or change clock midstream.

For the actual Arduino 3.3.11 qio_opi image, `versions.txt` pins ESP-IDF commit
`b774170ff46`. Its S3-specific `esp_clk_tree_enable_src` is a no-op returning
ESP_OK for both enable and disable; it does not gate the PLL or change a source
reference count. The helper's balanced source lifecycle records its own successful
SDK enable call and keeps it until successful camera teardown. Releasing it does
not disable the PLL shared by CPU, LCD or other peripherals on this SDK. The
selected PLL240M alias is SOC_MOD_CLK_PLL_D2, whose frequency query returns half
the actual BBPLL frequency. This platform implementation, rather than the generic
API header's reference-count description, determines the behavior:
[fixed S3 clock implementation](https://github.com/espressif/esp-idf/blob/b774170ff46/components/esp_hw_support/port/esp32s3/esp_clk_tree.c).
The actual linked qio_opi archive's enable/disable routine was independently
checked to contain only a successful return.
