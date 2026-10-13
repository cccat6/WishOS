# Runtime application execution

Use `notes.call(path, method, args)` for all driver operations. Its result has `ok`,
`result`, and `error` on failure. Discover available paths first with
`/sys/capabilities list {cursor:0,limit:4,details:false}`, follow `next_cursor` while
`more`, then `describe {path}` only for needed drivers. A small VM should not expand
the complete detailed driver catalog in one allocation. No app imports GPIO, I2C,
SPI, FreeRTOS, board headers or internal waveform tools.

`/sys/runtime load` accepts `file` or inline `source`, optional `memory_bytes` and
`instruction_budget` and `system_call_budget` (default 32 calls per callback, configurable 1–256). The candidate compiles before replacing the existing app.
`on_start` runs only after replacement; its external effects cannot be rolled back.
Read the returned running/fault state. `restart` rereads the current file, so it
includes updated modules. `stop` calls optional `on_stop` with the same limits, then
releases VM memory. Stop does not erase startup configuration.

The callbacks are `on_start()`, `on_event(event)`, and optional `on_stop()`.
`notes.subscribe(path)` / `notes.unsubscribe(path)` control subscriptions. Returning
true consumes an event before rules. Timers, rules, UI-manifest bindings and condition
monitors created through the script's calls belong to that VM; replacement, stop and
fault remove them. System condition monitors remain active. Omit timer ID to
allocate a free ID. Script-created TCP listeners/accepted sockets, UDP endpoints and HTTP routes/requests
are also owned and automatically removed on replacement, stop or fault. Their
handles cannot be mutated by a different script owner. Script-owned compute jobs
are cancelled cooperatively and reaped after completion; the replacement app does
not receive their stale completion events. Targeted event routing uses the event's
instance identity, independently of its path and payload. The same routing applies
to rules; a newly installed rule begins with the next event, not the input currently
executing a rule replacement. The C6 HTTP client/WS/microphone adapter
also closes or requests cancellation/stop on owner exit, with asynchronous tasks
retained until safe. Shared persistent driver settings and explicit system services
(such as NFS and BLE) are not rolled back automatically: stop those deliberately
through their documented lifecycle if the app owns their intended lifetime.
The embedded build limits native parser/call recursion to 32 and reports that limit. Callbacks are bounded work units, not perpetual loops; use events/timers to continue.
Instruction exhaustion faults that VM, not the entire system. Allocation/conversion failures report errors rather than delivering partial events or successful empty results. If a driver already performed a side effect before response allocation failed, inspect its state before retrying. Lua pattern functions
`find/match/gmatch/gsub` are unavailable because their native backtracking bypasses
VM instruction hooks. `table.move` is also omitted: a huge sparse range can execute indefinitely in native C without allocating or reaching a hook. `notes.find(text,literal,start)` provides bounded literal search; basic string slicing/packing and table/math/UTF-8 remain.

Import pure helpers during module loading using `notes.require('lib/helpers.lua')`.
Relative paths resolve against the importing file; an absolute VFS path also works.
Modules return a table/value and are cached per VM by normalized path. Eight modules,
8 KiB per source file; source plus code/data must fit the VM memory budget. Circular
imports fail. Put device calls in callbacks, not module top-level code.
`notes.resolve('ui/home.json')` gives a VFS path relative to the entry file during
callbacks. Modules can capture resolved asset paths at import time.

JSON values need explicit shape where Lua is ambiguous:

```lua
local r=notes.call('/sys/config','apply',{
  changes={{resource='/dev/mic',settings={max_duration_ms=notes.null}}}
})
local no_profiles=notes.array({})
```

`notes.null` represents JSON null; `nil` omits a field. `notes.array({})` represents
an empty JSON array; `{}` is an object. Decode preserves null, empty arrays and string
lengths. `notes.hex/unhex` and `notes.base64/unbase64` preserve zero bytes in binary
Lua strings. Conversion chunks are bounded to 4096 input/output bytes; honor the
smaller chunk limit of the actual driver. `/dev/mic readable` announces queued audio;
drain `read_chunk`, decode `adpcm_b64` with the independently decodable IMA ADPCM packet codec when binary processing is needed, and retain
recording/sequence/sample IDs when forwarding. Consumption rearms the notification.

Configure a boot application separately:

```json
{"path":"/sys/runtime","method":"configure","args":{"startup":{"auto_start":true,"file":"/storage/user/apps/main.lua","memory_bytes":32768,"instruction_budget":20000,"delay_ms":1000},"persist":true}}
```

The provider must be persistent. Missing media, invalid code or insufficient memory
produces a boot error while system services remain available. Without `persist`, the
new startup configuration is not saved for reboot. Reads do not probe files or mount
media. Disable `auto_start` to cancel pending boot admission. The startup delay is
configurable and is not a display-refresh delay.

The factory suite is simply the hardware package's default `startup.script`
reference. Its Lua entrypoint loads page and interaction resources through the same
interfaces used by a user script. Saved runtime startup configuration takes
precedence. A successful explicit `load` cancels a pending boot load, so a delayed
factory start cannot overwrite an already selected user application. It does not
silently persist the new selection; use `configure` with `persist:true` for that.

`/sys/ui app_load {file}` loads a UI/interaction manifest and returns its `entry`.
It validates the entry UI before replacing bindings; call `open {path: entry}`
separately to navigate. Pass distinct argument objects to those operations. UI
`read` reports the loaded manifest and binding count. The manifest's rules omit
explicit IDs: the loader manages group identity without overwriting unrelated rules.
Application exit releases its views and bindings, without interrupting a committed
physical refresh. Pages and interaction choices are files, not privileged factory code.

## Background computations

Use `/sys/workers submit` for calculations that should not run in the event callback.
The source defines `compute(input)` and returns JSON-compatible data. It can come
from inline `source` or a VFS `file`. Supply `input`, `core:"auto"` or an advertised
core number, `memory_bytes` and `instruction_budget`. Current capacity is two jobs,
including retained completed results; free receipts with `release` after reading. If the creating application exits, the system cancels and reaps its jobs without requiring that app to run cleanup code.

Workers have independent Lua states and memory budgets. They cannot call drivers,
subscribe to device events or mutate main-app state. Communicate by input/result:
the main app receives `completed/failed`, reads the result, then invokes the driver.
This keeps all hardware admission, bus arbitration and policy checks on the system
thread. A single-core MCU time-shares; an SMP adapter permits selected core affinity.
Unavailable core requests fail rather than silently changing the core.

The FreeRTOS adapter runs workers at background priority and yields every VM quantum.
`cancel` is cooperative at the next VM hook; do not free a running job or force-delete
its RTOS task. `read` reports completion separately from cancellation requested.
Instruction limits also stop runaway scripts. Active workers block sleep/reboot/
shutdown until completed/cancelled. Native drivers remain responsible for bounded
I/O; VM instruction budgets are not timing guarantees for a native driver call.


Optional `on_prepare()` runs on the candidate VM before the previous app stops.
Use it to validate package files. It permits `/fs` read/stat/mounts and `/sys/capabilities list/describe`
discovery, not hardware mutations. A preparation error leaves the previous app
running. `on_start` is still side-effectful and cannot be rolled back globally.
Application packages can validate their own configuration in this phase. Callbacks reclaim temporary
coroutines at their bounded VM boundary; app and worker memory remain separate.

For a startup file on a removable volume, explicitly configure a preparation
step (up to four) alongside the persisted startup file:

```json
{"startup":{"auto_start":true,"file":"/storage/user/apps/default/main.lua","memory_bytes":null,"prepare":[{"path":"/dev/sd","method":"mount","args":{}}]},"persist":true}
```

The selected driver's mount operation still respects leases and reports missing,
unformatted or unsupported media. Discovery does not mount storage. Preparation
runs at the next boot; failure reports `boot_failed` while system services remain
available. Install a replacement into a new versioned directory, then activate it;
do not overwrite files of an active application one by one.

Instance `memory_bytes` is a ceiling, not a preallocated RAM partition. Omission
(or null for the main runtime) inherits `/sys/resources default_vm_memory_bytes`.
Actual VM growth is charged against the shared policy and the selected physical
heap reserve. Multiple high ceilings do not reserve their sum in advance. Query effective limits before requesting
a larger instance. Resource exhaustion still fails explicitly and preserves other
instances/services. Global resource policy is writable by system callers, not by
a running application lowering its own safeguards.

Memory discovery reports `heaps[]` with capacity, free bytes, largest block and
reserve for each present region. `application_memory:auto` prefers PSRAM for new
VMs; `internal`/`external` request a fixed region. Changing it applies to the next
instance, without migrating a running VM. Internal native work cannot borrow
PSRAM to satisfy an internal/DMA reserve. `managed_bytes` is actual VM/cache use
plus native fixed reservations; `reserved_bytes` counts only those fixed
reservations, and `unallocated_reserved_bytes` is their unspent portion. These
counters do not include every SDK allocation; consult physical heap readings too.
NFS retains its own configurable protocol budget within this shared allocation
policy. RAM/PSRAM file quotas are separate ceilings, not preallocated partitions.
