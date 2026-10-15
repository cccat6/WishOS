# Recovery ownership and acceptance scenarios

The OS remains reachable independently of a runtime application. Recovery does
not require formatting user storage or reflashing firmware. Physical hardware
failure and native-driver defects are distinct from ordinary connection faults.

| Failure | Terminal behavior | Connector/server responsibility |
| --- | --- | --- |
| Wi-Fi/network interruption | Configured association policy retries; link endpoints reconnect/fail over; local apps and system input keep running | Reconnect using paired identity and observe the current boot |
| Silent broken connection | Configurable WS heartbeats close dead transports; higher-priority route probes run on a worker | Treat lost replies as uncertain, not permission to repeat a mutation |
| Server/Harness answers no task result | Bounded AI queue and whole-task deadline; terminal failure releases download leases and plan resources | Bound adapter calls, preserve native session identity and report admission/completion separately |
| Device restarts or loses power | New boot ID; transient handles/AI plans are invalid; persisted configuration/catalog restore through normal validation | Reject old-boot replies; inspect durable external effects before any new execution |
| File transfer is interrupted | Live same-boot transaction can resume at verified offset; incomplete files cannot activate an app | Resume by request identity/digest or explicitly abort; partial files still consume quota |
| AI sequence is cancelled | Stop future steps; await an already running native operation's completion safely | Do not claim remote external effects were undone |
| Application action hint is lost | Pending action remains in the system; the receiver library recovers it through reads | Do not equate message transmission with application processing |
| Origin exits | System work survives; origin-target application delivery fails rather than reaching a new VM | Explicitly choose foreground when a result should reach a newly launched app |
| Runtime exceeds instructions or memory | Runtime faults and releases owned resources; independent Home/configuration remains | Use discovered budgets rather than repeatedly submitting the same oversized app |
| User launcher fails | Existing fallback path selects device-package launcher | Keep the user's chosen references/data; do not erase them |
| Factory launcher also fails | Stop automatic reload attempts; expose recovery_required and last_error; keep native HTTP/CDC controls | Repair/select an app through the control interface; an explicit retry is allowed |
| SD is absent/unformatted/full or removed | Preserve distinct media errors; explicit preparation and configured asset fallback; no automatic format | Choose a valid storage target or smaller artifact; recordings never silently fall back to Flash |

Configuration changes validate before persistence/effects. An RPC-originated link
configuration drains its response before retiring the connection. Stable control
IDs are retained across network routes. A failed read-only precondition is distinct
from a mutation that was dispatched without a receipt. Safe reads can recover
within a bounded same-boot window; a mutation can wait for dispatch only while
its transport proves it has not been sent. Once sent, loss of confirmation
requires reconciliation rather than automatic replay. A completed physical operation, a received
packet, and a stored artifact are separate facts.

A result program is not a transaction over arbitrary external systems. If power
is lost after a side effect but before its receipt arrives, there may be no proof
of whether it happened. That case is reconciled or explicitly resolved, never
blindly replayed. The same applies when an upstream native Harness owns a session
that cannot safely be resumed by a second process.

Current evidence includes host fault/concurrency/restore tests, long-plan and
lost-notification recovery, application replacement/acknowledgement barriers, native
socket ownership tests, C3 communication tests, and target builds. Real vendor
account acceptance and other physical board validation remain separately tracked.

## Validation on 2026-10-15

The public host suite passed (including ASan/UBSan runtime paths, native-worker
lifecycle/concurrency, application fallback, long JSONL plans and lost hints).
C3 and C6 target compilation are separate from physical acceptance.

`tools/check_device_workflows.py` ran against the disposable ESP32-C3, both with
an IPv4 endpoint and the connector host's `.local` name. Eleven checks passed:
ordered fallback, RPC delivery, idle promotion after a fragmented call, reconfigure
reply-before-retirement, silent-PONG failure, invalid-config preservation, ordered
application/stop/load/foreground handover, a verified 41-step JSONL plan, upstream
deadline/release, actual reset during a partial file transfer with old-boot rejection,
and saved endpoint restoration without reflashing. The E6 experiment board was not
touched. The fixture is independent of a real model provider or hosted relay.

Atomic replacement on this C3 was also refused under simultaneous live-VM/network
memory pressure; the old VM and control interfaces stayed healthy. Explicitly
stopping the old VM first admitted the new one. Completed AI handles must be
released when consumed. These are per-device resource admission observations,
not a universal application-memory ceiling or a reason to weaken system reserves.

Tests use the public 20-second connection-attempt default, allowing the SDK's DNS
wait to finish. Restoration accepts a working configured fallback when proactive
promotion is disabled. mDNS capability does not guarantee multicast reachability
on every router. Repeated local tests included intermittent mDNS timeouts despite
a successful run; an authenticated IPv4 run passed all eleven checks after the
header fix. The separately configured public-DNS relay remains a fallback.

A real public-cloud check also connected the C3 to `relay.wishos.dev` using WSS
with peer verification enabled, and an MCP 2026-07-28 client at `mcp.wishos.dev`
read `/sys/board` with the current boot guard. Its temporary pairing installation
was removed afterwards. This exposed and fixed a native WebSocket header framing
error: extra headers ended in CRLF while the SDK added its own CRLF, prematurely
ending the HTTP headers. Regression coverage now includes authenticated handshakes.
This proves the physical device's TLS/RPC path, not screen/audio operation or a
vendor-model conversation.


## Package and server bridge acceptance on 2026-10-16

Native implementation: `095c002506618d7219defa55a3eb6f3f51ff1639`. The public
host suite, driver-SDK checks and C3/C6 builds pass, including Linux ASan leak
checks. An adjacent allocator review confirmed LVGL handles zero-size reallocations
before entering its backend; the shared resource allocator's null/zero release
was fixed and compile-only admission returns its temporary allocation balance.

The USB-identified disposable C3 passed eight package scenarios: live component
discovery, stable transfer identity, native validation/install/activation, active
revision write protection, bad-code rejection without replacing the old VM,
whole-file replacement with 123,050 unchanged bytes copied locally, stale catalog
rejection, and an actual reset after copying 1,024 bytes. The last committed
revision still launched after reboot. Test files and catalog entries were removed.

The final C3 also passed eleven system workflow scenarios: ordered endpoint
failover, RPC delivery and fragmented-message barriers, response-before-link
retirement, silent heartbeat failover, invalid configuration preservation,
application/system action ordering across replacement, a 41-step streamed JSONL
program, upstream deadline/release, reset during a partial transfer, old-boot
rejection and restoration of saved endpoint policy without reflashing.

The real local MCP client passed eight package scenarios using the default
retain-current policy, including guidance resources/prompts/tool fallback,
preflight rejection before transfer, native admission/activation, single-file
replacement with local copies, rejected bad/stale replacements retaining the old
VM, and VFS lease enforcement. The isolated local service and its test files were
cleaned up. Cloud acceptance is recorded separately; a successful local run is
not evidence of the public relay or a vendor Harness account.

These checks exercise a headless C3 system composition. C6 compilation and SDK
adapter checks do not establish the new physical board's display, microphone,
SD, power circuit, radio coexistence or sensor performance. The E6 research board
was not used. The final local builds use 1,783,683 bytes / 97,412 static RAM bytes
on C3, and 3,990,106 bytes / 214,788 static RAM bytes on C6. Runtime heap and
concurrency availability remain measured, separate quantities; the C6 image is
close to its selected application partition capacity.


The final physical public-cloud run also passed all eight MCP package scenarios
through `mcp.wishos.dev` and the paired WSS relay, using Hub
`a5a6399b91bd376616dfd1bec6b44a55b410b382` / Worker version
`0969e477-1474-4f09-8055-7c9918c12278`. It used the explicit
`stop_current_before_transfer:true, activate:true` policy. Full preflight and
boot/catalog checks precede the stop; unchanged app.json and unchanged.bin were
copied on the C3 while only main.lua was replaced. Bad and stale updates left the
running instance intact. Native transfer, validation, installation and activation
were separately confirmed; temporary files, application and cloud installation
were removed and the prior link settings restored.

The default retain-current policy remains available and passed the real local
MCP test. Under C3 TLS/old-VM memory pressure it can correctly refuse a new native
operation without replacing the running instance. The explicit stop strategy is
upstream deployment orchestration using ordinary system lifecycle calls, not a
C3 exception in the kernel. A later failure can leave the runtime stopped; prior
files and recorded receipts remain available for explicit recovery. System
allocation reserves were not reduced to admit these tests.

A lost precondition-read reply was found during cloud acceptance. Hub now keeps
known-not-dispatched evidence separate from sent/unknown effects, retries only
safe preparation or transport-proven unsent work within a same-boot deadline,
and does not replay uncertain mutations. That shared path has Hub fault-injection
regressions in addition to the physical successful run. An independent 32-read
physical cloud probe also passed. These are protocol/device tests, not vendor
model, account or Harness-session acceptance.
