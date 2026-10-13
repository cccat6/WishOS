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

`assets` maps VFS paths below `/system` to package text assets. UI documents, Lua
entry/modules and default policy snapshots are compiled read-only. User apps/assets
can instead be replaced at runtime on writable providers. `/defaults.json` is a
normal `Policies` restore snapshot, not an unchecked bypass; saved settings override
package defaults. Optional `startup.script` references an ordinary Lua entrypoint.
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
