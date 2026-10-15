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
IDs are retained across network routes. A completed physical operation, a received
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
on every router; the separately configured public-DNS relay remains a fallback.
