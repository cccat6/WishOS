# Hardware packages

The reusable OS is `firmware/AgenticOS/src`: registry, invocation, rules/conditions,
VFS, policies, application host and Lua runtime. `boards/` owns wiring, electrical
facts and startup assets. `ports/` owns trusted native driver integration and the
platform lifecycle. Runtime applications never edit a pin map, load native code,
create RTOS tasks or access registers.

Build the existing board:

```sh
NOTES_OS_BOARD=e6-nova-c6 tools/build_agentic_os.sh
```

Copy that board directory to make a wiring/UI variant. Set `port` to the existing
port's absolute path, edit `board.json` and its assets, then select the directory
with `NOTES_OS_BOARD=/path/to/my-board`. This changes the generated sketch only;
it never edits core source. `python3 tools/prepare_board_package.py /path/to/my-board`
validates the package before compiling. Pin conflicts, unavailable pins, required
wake/ADC capabilities, missing startup assets and unsupported geometry fail early.
Electrical correctness still requires schematic review; passing this check is not
PCB validation. GPIO8/9 strap loading, USB hardware pins, shared power polarity,
crystal and peripheral voltage assumptions remain native-port responsibilities.

`assets` maps VFS paths below `/system` to package text or binary assets. UI documents, Lua
entry/modules and default policy snapshots are compiled read-only. User apps/assets
can instead be replaced at runtime on writable providers. `/defaults.json` is a
normal `Policies` restore snapshot, not an unchecked bypass; saved settings override
package defaults. Optional `startup.script` references an ordinary Lua entrypoint;
`startup.memory_bytes` is its optional VM ceiling, not a preallocated partition.
The E6 suite requests 65536 bytes; other applications retain the system default.
Its measured 64-bit host startup peak is about 56 KiB; device use differs.
The E6 package's entrypoint loads its UI manifest and opens its entry through public
resource calls. There is no independent native factory-UI boot step. Explicit
`/sys/runtime configure` startup settings override that reference, including when
auto-start is disabled. Inline source is runtime-only; persistent startup requires a
persistent provider. Package changes take effect in a new firmware deployment;
user application references and files change through runtime interfaces.

For a different MCU/display/peripheral combination, add a native port directory
containing `port.json`, its `entry` include and a partition file. Its entry implements
`notesos::begin/poll` through `Hardware.h` and registers native drivers in `Core`.
The port declares its FQBN, compile flags, capacity, required pins, supported GPIOs,
pin constraints, hardware scalar fields, button count and display geometry. A port
can use its own native headers/sources under its directory. The build stages the
port under `src/selected`; include reusable OS headers via `../Header.h`. Native
sources are trusted build inputs, never downloaded and executed by a user app.

The generic package generator does not require C6, E6, three buttons, SPI or an IMU.
The current `esp32-c6-e6` port does require its particular topology. Its screen is
still the E6 implementation: naming the PCB “Nova” does not implement a Nova driver.
Touch, ADC button ladders, rotary input, codec/microphone types, audio output and new
sensors are additional driver instances, discovered through the same open registry;
there is no closed device-type enum. Actuators are not implemented in this project.

The current port uses FreeRTOS: native capture/storage/network tasks, serialized
resource dispatch and optional isolated compute workers. On an SMP port, workers
can request an allowed core through `/sys/workers`; drivers/RTOS own scheduling and
bus locks. No application sets task priority above system work or touches a shared
peripheral concurrently. Lua memory/instruction limits contain application faults;
this is not an MMU sandbox against bugs in trusted C/C++ drivers or the SDK.

`e6-nova-c6` is the product board package. `esp32-c3-runtime` is a headless development-board port using the same kernel, VM, worker, Wi-Fi, VFS and NFS implementations; it declares no absent display/audio devices. Other devices need a
native port, independently compiled and validated on that hardware. A common OS
source tree/API is portable; a C6 binary cannot be flashed onto C3/S3 unchanged.
Use the generated sketch under `build/agentic-os-sketch/AgenticOS` in Arduino IDE.
The build command never flashes a device or copies a reference project's firmware.

The presentation service implements navigation state, view revisions and frame
ordering together with the display adapter. This system coordination may be tightly
integrated; the application chooses what to display, when to navigate and which
supported presentation behavior to request. Background document updates do not
navigate. Binary drawing also uses an explicit surface context. Page definitions,
input meanings and UI flow stay in runtime-loadable user packages. The factory suite
provides a starting experience through the same interfaces, with no built-in page
names or button-to-action rules required by the native services.

VM memory ceilings are configurable resource policy, not fixed RAM partitions.
A package may request a ceiling; otherwise the system default applies. Admission
uses physical headroom, system reserve and shared commitments.

## Resource placement and factory suite

`resources` supplies native startup defaults: `memory_limit_bytes` (null = actual
registered heap capacities), `heap_reserve_bytes`, `external_heap_reserve_bytes`,
`application_memory` (`auto`, `internal`, `external`) and `default_vm_memory_bytes`.
The ESP adapter registers separate internal and PSRAM heaps with matching allocation
callbacks. Auto tries PSRAM for a new VM when available; that VM stays on its chosen
heap. Fixed external placement fails if unavailable. RAM files explicitly use
internal memory; PSRAM files use external memory. NFS and lossless display caches
allocate on demand through the same resource manager. Native task/DMA requirements
remain internal. SDK-owned allocations are reflected in physical heap readings;
they are not all individually tracked by the OS.

The current E6 package targets the manufactured board with the 5 V-controlled
battery ADC clamp enabled. The driver conditions the switching/recovery interval;
applications consume `external_power` and valid battery readings through the common
capability/event interface. External power does not mean charge completion.

`assets/suite/config.lua` selects pages, display profiles, button roles, timer,
rotation and the optional voice route. `assets/suite/main.lua` implements those
choices through public resource calls. `assets/ui/` has portrait/landscape pages;
small indexed icons are streamed from the VFS. Copy the suite to a writable provider
and change the startup reference to replace it without rebuilding. `tools/generate_basic_ui.py`
and `tools/compile_os_font.py` reproduce the packaged assets. See the agent's
[display guide](../../skills/agentic-notes-hardware/references/e6-display.md).

Host asset compiler dependencies are pinned in `requirements-os-assets.txt`.
The packaged native font was compiled from Noto Sans SC Regular, source SHA256
`2fa4a4c87f663273cc6e4c854ab7e145671c2e645a9326f61a01cbf8e767eb82`;
the source is provided via `--font` and the SIL OFL is included next to the compiled
subset. Ordinary firmware builds use the checked-in subset and do not need Python
font dependencies or any private research workspace.

## Display adapter boundary

The portable `DisplayScheduler.h` supplies request ownership, queue policy,
view/revision filtering and completion receipts. Its request type extends
`DisplayRequest`; it deliberately has no frame size, color table, BUSY GPIO or
refresh timer. Native adapters implement the small compile-time contract documented
at the top of that header. They report transport, controller or frame-latch completion
rather than guessing from elapsed time. Driver callbacks set flags or queue native
notifications; the service loop delivers application events.

The E6 engine, fixed profiles and palettes reside in
`firmware/AgenticOS/src/drivers/e6/`. The C6 package binds those engines to GPIO/SPI,
file/scene/UI sources and power in `ports/esp32-c6-e6/E6Display.inc` and
`E6UiSurface.inc`. All E6 profiles share one scheduler and one physical resource.
The native binding also provides `displayInputBusy()`; the shared input policy
uses that state without referencing an E6 phase or GPIO.
The LVGL renderer reads geometry from the selected board package and emits RGB565
regions; each native sink performs its own packing/rotation/transport. It is not
required to use E6's column bands or keep a full frame in RAM.

To port another screen, implement/compose its native engine and binding in a new
port, describe its actual profiles/formats/completion conditions, and select that
port from `board.json` when compiling. No hot driver loading or runtime GPIO editing
is provided. The portable Core, scheduler and runtime application ABI remain the
same. The binding must retain buffers until native I/O has finished reading them;
continuous scanout buffers remain owned by the driver after a presentation receipt.
All physical bus operations use that port's arbiter, with DMA transfers holding their
bus lease only as long as the transfer needs it. An independent EPD optical cycle
does not hold SPI. Recovery and power-down remain native lifecycle operations.

The public [display interface](../../skills/agentic-notes-hardware/references/display-interface.md)
describes event evidence and the distinction between a hardware component and its
presentation profiles. Host fixtures cover immediate transfer, DMA, EPD BUSY and
frame-latched continuous scanout with the same scheduler. Actual non-E6 panel
implementations are future adapters, not hidden capabilities of the E6 build.
