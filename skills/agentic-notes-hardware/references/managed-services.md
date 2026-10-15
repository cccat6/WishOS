# System-owned AI, resources and diagnostics

Discover `/services/ai`, `/sys/transfers`, `/sys/logs`, `/sys/telemetry`,
`/services/time` and `/net/proxy`. UI parsing, layout, font/image rendering and
presentation are OS services. UI descriptions, styles and interaction choices are
ordinary runtime assets. A UI-only result does not need a new executable app.

## AI request and response

Applications call `/services/ai submit {target,input,request_id?,timeout_ms?}`.
Targets are `application`, `system`, `both`. Text uses `input.text`; recording uses
`input.kind="voice"` plus `recording_id` and an optional archive `file`. The system
owns the task after admission even if the requesting VM exits. It returns an ID;
use the declared async completion contract and read the final result by ID.
Release completed receipts. Returning to the launcher is not a cancellation of
already accepted external work. Explicit cancel can prevent an unadmitted local
effect, not roll back an email already sent by an upstream tool.

Do not implement provider/Harness/ASR protocols in an application. System AI
configuration selects a server route; the Hub implements the concrete provider
protocols. Ordinary bounded HTTP/API work may still use `/net/http`. A deterministic
remote route need not invoke an LLM. Configuring a URL alone is not a successful
protocol connection or working model integration.

The Hub must return a small `wishos-result/1` object. `application` holds data for
the original instance; `effects` holds a typed system operation. The current
broker admits one effect per transaction (sequence independent tasks for more):

- `application`: a verified descriptor `path`, `transfer_id`, optional `persist`
  and `activate`. The system installs and optionally activates it.
- `presentation`: a verified UI document `path`, `transfer_id` and optional
  advertised `driver`. The system renders it without replacing the running app.
- `resource_call`: `path`, `method`, `args` for an advertised capability. Use the
  typed operations above for runtime/app/transfer operations.

A system-target task cannot return application-only data; an application-target
task cannot request system effects. The device validates the envelope before any
effect, emits `result_ready` before replacement, and reports final completion only
after the applicable service receipt. Replies never migrate to a replacement VM.
The system cannot make already executed external side effects transactional.

## Large resources and local parsing

Use `stage_resource` on the Hub to fetch a book/source URL or store generated text.
It returns an artifact ID. Return `files:[{path,artifact_id}]` alongside the managed
result; the Hub negotiates storage, sends bounded binary chunks, and the device
verifies each file before committing the package. Never put whole books, long code,
base64 images or large JSON/XML in the small control envelope. Device restart needs
a new transfer; link interruption within the same boot can resume/reconcile the
current transfer. Immutable version directories preserve previous packages.

Prepare book indexes on the host. Extract only needed JSON/XML fields upstream
when the response exceeds the device's advertised response/VM budget. Full DOM
parsing of arbitrary XML/HTML is not an advertised native capability. Local Lua
JSON decode consumes its VM budget; bounded HTTP does not prove arbitrary JSON
fits. For graphics, generate supported UI JSON or host-rendered files matching the
discovered geometry, palette and codecs; do not assume a six-color screen.

Storage policy chooses ordered provider roots and quotas. Internal Flash requires
explicit `allow_internal:true`; choose a quota that fits the actual data partition,
not total chip Flash. A mounted but full card is not a reason to silently switch
storage. Capacity queries do not mount media. Delivery may explicitly prepare it.
Retained partial directories consume space and are cleaned explicitly, never by
formatting or deleting unrelated files. Recording has independent retention rules;
asset fallback does not permit silent recording-to-Flash.

## Time, proxy and diagnostics

NTP config includes servers, enabled, interval/retry/timeout and POSIX `timezone`.
UTC is the wall-clock basis. Examples: `UTC0`, `CST-8` (UTC+8),
`EST5EDT,M3.2.0/2,M11.1.0/2`. Monotonic timers are unaffected by timezone changes.
There is no bundled IANA timezone database. NTP uses direct UDP; TCP proxies do not
silently tunnel it. Configure a reachable NTP server or explicitly set wall time.

Proxy modes: direct, HTTP CONNECT, SOCKS5, with optional credentials and exact or
suffix-domain bypass. New connections snapshot settings; existing connections are
not silently reset. HTTP/HTTPS and WS/WSS use the same native clients; TLS still
verifies the origin hostname after tunneling. The raw IPv4 TCP adapter requires an
IPv4 proxy address. UDP/NTP tunneling is not advertised.

Logs use a configurable RAM ring, severity, layer, source, code and timestamp.
They have a per-second rate cap and observable overwrite/suppression. Read with
cursors and optional layer/owner filters. Export through files/network explicitly;
there is no automatic Flash log write. Completion receipts remain separate.

Telemetry is independently configurable: enabled, boot registration, usage, errors,
endpoint, interval and retry. It defaults off. Registration sends device/boot IDs;
aggregate reports contain only counters and independent random deduplication IDs.
No prompts, recordings, URLs, credentials or arbitrary log text. Transport IP can
still be visible to the receiver. Disabling stops telemetry delivery; local diagnostic counters remain independent;
bytes already sent cannot be recalled. Do not claim fatal crashes are guaranteed
to be caught or uploaded before power loss.

Use `/sys/telemetry flush` through its asynchronous completion contract when an
application/power policy explicitly wants a final batch. This does not enable an
opted-out collector. Unsent RAM aggregates may be lost on reset or power loss;
registration counts are not user counts, and reset reasons distinguish wakeups.
AI system work remains active after leaving its originating application unless
explicitly cancelled. Use `activate:false` for a package that should only be
installed in the background; choose the application target when the active app
must decide how and whether to present a response.

Generated binary output can use authenticated Hub
`POST /api/os/artifacts/upload` (`Content-Type: application/octet-stream`).
Stream the file bytes with the Harness execution tool; use the returned artifact
ID in the managed result. Do not put large base64 images into model responses.
Image/HTML rendering, archive extraction and large JSON/XML selection happen in
upstream tools. Inspect target format/geometry/decoder capability before rendering;
no fixed E6 size, palette or framebuffer assumption is valid for all devices.
