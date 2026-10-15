# Managed services and runtime applications

WishOS owns UI parsing/layout/rasterization/presentation, AI request admission and
result application, storage placement/transfers, diagnostics, time and outbound
network policy. Applications supply UI/interaction descriptions and invoke these
services. Concrete cloud/model/Harness protocols stay in the independent Hub.

## AI transactions

`/services/ai` accepts input and `target: application|system|both`.
The service owns the request independently of its originating VM. The origin is a
receipt destination, not the task lifetime. A result is `wishos-result/1`: a small
application payload plus typed system effects referencing verified local assets.
Whole results are validated before effects; model prose is never executed as an
OS command. Application replies and system receipts are separate. Application
replacement occurs after completion notification reaches the event loop. A missing
origin cannot retarget the reply to a new VM. Callers can explicitly cancel before
irreversible effect admission; already accepted external work may require outcome
reconciliation.

Large results use server staging and bounded transfers, never code/base64 images
inside the control envelope. Novel/source downloads happen at the Hub. The Hub
builds content indexes, extracts required JSON/XML fields and renders complex
content as appropriate. Small bounded HTTP requests may run locally through system
HTTP. A UI-only result is a presentation artifact, not a forced app installation.

## Storage

Device package storage policy lists ordered target roots and byte quotas. Removable
storage is preferred; internal Flash fallback is explicitly enabled and bounded.
Media-present-but-full/unsupported is not equivalent to absent: fail instead of
silently switching storage unless policy explicitly permits it. Transfers reserve
capacity, write fresh version paths in chunks, verify digests and commit before
activation. Queries never prepare or scan media. Restoring usage requires explicit
preparation, not optimistic counters. Recording retention remains a separate policy;
allowing asset Flash fallback does not enable recording-to-Flash.

## Diagnostics and telemetry

Local structured logs have severity, layer, source, code, time and optional owner.
They use a bounded RAM ring with observable overwrite/drop counts. Persistent and
remote sinks are optional, rate-limited batches; logging never synchronously waits
for network or media in a driver/ISR. Operation receipts are not log records.

Telemetry is opt-in/configurable independently for boot registration, aggregate
usage and errors. Registration carries the device identifier once per boot after
network availability, with an idempotent boot receipt. Usage/errors omit stable
identifiers, free text, prompts, URLs, recordings and file contents. Transport IP
is still visible to the receiver, so these are identifier-free payloads rather
than a claim of guaranteed anonymity. Endpoint: https://telemetry.wishos.dev/v1/ingest.
Disabling stops telemetry delivery and clears pending telemetry; local diagnostic counters remain independent. Error aggregation is bounded;
there is no promise to upload immediately after power loss or a fatal CPU crash.

## Network and time

Proxy policy is independent of SSID association. HTTP CONNECT/SOCKS5 establish a
TCP tunnel, then TLS authenticates the original target. Existing connections keep
their admitted settings; changes apply to subsequent connections. UDP/NTP is not
silently sent through a TCP proxy. NTP maintains UTC; timezone presentation uses an
explicit POSIX TZ rule and can be configured without changing monotonic timers.

## Implementation boundaries

Current APIs: `/services/ai`, `/sys/transfers`, `/sys/logs`, `/sys/telemetry`,
`/services/time`, `/net/proxy`. The AI broker admits one typed system effect per
result; it can be an app activation, a presentation-only document, or a declared
resource call. Application data may accompany it for a `both` target. The server
coalesces results from one Harness operation; the device retains bounded effect
receipts so simultaneous steer requests cannot apply the same effect twice.
Cross-boot replay of a previously prepared system effect requires reconciliation.

Transfer metadata stays small. Streams resume within the current boot using the
same identity, file index, verified offset and digest. Complete files are renamed
from staging only after SHA-256 validation; no app reference points at a partial
package. The device does not fetch arbitrary origin download URLs. The separate
Hub fetches/stages them and sends chunks. Internal Flash is an explicit fallback;
its quota also applies to direct filesystem/NFS growth through the provider.
LittleFS mounting, formatting and space scans run on native workers; status reads
use cached capacity and expose staleness. This keeps provisioning and downloads
from running deep filesystem scans on the system event-loop stack.

Local JSON decode currently has a 4096-byte input ceiling in addition to the VM
memory budget; UI documents have their own advertised parser limit. Arbitrary XML
or HTML DOM parsing is not a native capability. Upstream extraction/rendering is
required when content exceeds the discovered limits. These are implementation
bounds, not claims about the hardware capacity of every future device.

Logs default to 16 RAM records with a 20-record/second admission cap, configurable
within the memory policy. Records have severity/layer/source/code, monotonic time
and optional application owner; long UTF-8 messages retain valid character edges.
Trace-level successful calls omit arguments and result bodies. Overwrite and rate
suppression are observable. There is no automatic log write to Flash; explicit
collectors may read/export records. Event completion receipts remain separate.

Telemetry defaults off. Boot registration carries the device/boot identifier and,
when diagnostics is enabled, a normalized reset reason. Usage reports have only
independent random deduplication IDs and numeric counts: AI request admissions,
application resource calls, uptime, input actions, input-active duration and error
reports. Input-active duration is a declared 60-second activity window, not a
claim about human attention. Errors may be counted at several reporting boundaries;
these counters are diagnostics, not billing. Configurations do not upload arbitrary
log text, prompts, files, audio or credentials. Upload is bounded background HTTPS;
confirmed receipt is distinguished from mere transport success. Disabling cancels
future delivery; already transmitted bytes cannot be recalled.

The initial management UI exposes NTP/timezone, proxy, download policy, local logs
and separate telemetry switches. It remains a runtime package. The current C6
board has only a 44 KiB writable internal partition; permitting fallback cannot
make a large novel fit there. Larger-Flash devices select their data partition and
quota in their own package. No SD recording silently falls back to Flash.

The current ESP32 native compositions explicitly select a 12 KiB system task
stack; this is a compiled native-integration resource, not an application VM limit
or a universal MCU requirement. Input deferral buffers and rule records allocate
only when needed and release with their lifecycle. Native TLS handshakes reserve
peak headroom before allocation; stable WebSocket sessions do not retain unused
handshake headroom, and reconnect reacquires it before another handshake.

Telemetry `flush` is an explicit asynchronous operation for an application or
power policy that wants to send a final batch. It observes enabled categories and
a timeout; it does not turn telemetry on. Default periodic delivery avoids sending
an empty aggregate immediately after boot registration. Unsent RAM counters can
still be lost on reset/power loss; this is best-effort analytics, not durable usage
accounting. Normalized reset reasons distinguish power-on, software, watchdog,
panic, brownout and deep-sleep reset where the native adapter provides that fact.

Generated binary images and other local server output can be streamed to Hub
`POST /api/os/artifacts/upload` with `application/octet-stream`, using the same
Hub authorization. This shares bounded staging, digesting and cleanup with URL
fetches; it does not require base64 or a public source URL. Agent tools perform
heavy rendering/extraction upstream according to the discovered device format.
The returned artifact ID is referenced by a managed result, not by a source URL.

## Layer map and connectivity

| Layer | Owned components | Runtime changes |
| --- | --- | --- |
| Platform and native drivers | MCU/RTOS integration; bus arbitration; display/audio/input/sensor/network/storage protocols; native workers and electrical lifecycle | Only advertised settings; new native code may need a build |
| WishOS system | Capability/configuration ABI; bounded script execution/workers; VFS; application catalog/lifecycle/home entry; events/conditions/completions; UI loading/layout/fonts/images/presentation; managed AI/result application; transfers; time; proxy; logs/telemetry | System resource calls validate and apply supported settings |
| Device package | Selected drivers, wiring/topology/partitions/resource defaults; initial application/source references and system input bindings | Runtime policy where advertised; native topology is build-selected |
| Runtime applications | Launcher, voice flow, reader, settings and user apps; page contents/styles/help/navigation/input meanings; explicit local or remote actions | Replace files and startup references without changing the kernel |
| Separate Hub/MCP/Harness | Session/provider adapters, ASR, heavy extraction/rendering/indexing, origin downloads and staged artifacts; MCP discovery/calls and Agent Skill | Server settings/adapters and Harness-authored packages |

Applications invoke `/services/ai`; the system owns its transport and typed result
handling. MCP is the upstream tool interface, not a separate device execution layer.
A connection and a model request are distinct: deterministic local work does not
call an Agent. Provider/session configuration is upstream; the device controls the
system connection and route. A provider SDK is never embedded into a UI package.

Offline applications and installed resources continue to work. An admitted AI task
waits in the bounded RAM outbox until reconnection or its deadline; its ID survives
link reconnection, not device reboot. Expiry is a failed completion, not success.
Ordinary HTTP requests return transport/status/parser-limit outcomes; applications
decide whether and when to retry. Side-effecting operations with uncertain outcomes
require receipt reconciliation before replay. No unlimited persistent offline queue
is implied. UI feedback is application content; system transport callbacks do not
force a particular offline page.

AI completion means the declared system operation completed. Application activation
is separate from any later display refresh or remote action initiated by that new
application. A presentation effect follows the display/presentation completion
contract. On e-paper, BUSY must finish before another panel command; the shared SPI
bus is released while the panel independently refreshes. User input continues to be
captured, while its dispatch policy and page behavior are configurable.
