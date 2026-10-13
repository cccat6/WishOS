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
true consumes an event before rules. Timers and rules created through the script's
calls belong to that VM; replacement, stop and fault remove them. Omit timer ID to
allocate a free ID. Script-created TCP listeners/accepted sockets, UDP endpoints and HTTP routes/requests
are also owned and automatically removed on replacement, stop or fault. Their
handles cannot be mutated by a different script owner. Script-owned compute jobs
are cancelled cooperatively and reaped after completion; the replacement app does
not receive their stale completion events. The C6 HTTP client/WS/microphone adapter
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
