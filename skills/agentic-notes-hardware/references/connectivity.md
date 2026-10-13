# Connectivity and execution placement

Discover resources and schemas on the current boot. Native drivers are compiled once;
application behavior is loaded as Lua + JSON UI + assets. TypeScript/JavaScript service
modules execute on the host. The device does not advertise a local JS/TS engine.

For saved Wi-Fi profiles, priorities, manual overrides, AP settings and independent BLE startup configuration, read [wireless configuration](wireless-config.md).

## Boot radio selection

`/sys/radio read` returns `active_protocol`, `configured_protocol`, and `reboot_required`.
`write {"protocol":"thread"}` (or `zigbee`/`off`) persists the next boot's selection.
The single image contains both native stacks; only the selected 802.15.4 driver is
registered. Stop storage/network services, finish display/recording work, then use
`/sys/power reboot` to apply selection. Changing the setting never retargets an active
interrupt callback. BLE and Wi-Fi have separate lifecycles.

Thread `start` accepts active Operational Dataset TLVs as `dataset_hex`; omit it only
to resume an existing dataset. `read` returns role and IPv6 address; `stop` deinitializes.
Use `/net/udp` with IPv6 for datagrams. Thread membership supplies a network, not an
ecosystem account or application protocol implementation.

Zigbee `start` selects router/coordinator, endpoint, profile and channel mask. `send`
accepts short `address`, destination `endpoint`, `profile`, `cluster` and `hex` APS
payload. Set `consume_aps:true` when application code owns processing of its configured endpoint/profile; the default mirrors packets to the app while retaining SDK processing. `packet` reports incoming APS data; `tx_status` is a delivery receipt, distinct
from remote application success. `permit_join {seconds}` is explicit. `pause/resume`
pause the SDK task; complete release/reconfiguration requires reboot in this SDK.
No ecosystem adapter, account login or vendor appliance schema is built into the driver.

## Network interfaces

- `/net/http request`: bounded inline text response, or `stream:true` with a configurable
  `response_limit`; `receive` drains 512-byte hex chunks. Subscribe to `readable` and
  completion/failure events. Drain all chunks before replacing the job. A full chunk
  queue applies backpressure; unattended streams can time out. `cancel` requests cooperative termination; `release` rejects a running task and releases its queued data/receipt after completion. A script exit cancels its own request and releases it only after task exit. Blocking connection/TLS work can still last until its configured timeout. HTTPS takes `ca_pem`.
- `/net/tcp listen/connect/read/write/close`: four nonblocking IPv4 socket slots.
  `connect` takes a numeric `address`; DNS names are supported by HTTP/WS clients.
  `accepted`, `connected`, `readable`, `closed` and `failed` announce state. Binary
  data uses hex, in chunks up to 1024 bytes. Honor partial writes and `would_block`.
- `/net/udp open/send/receive/close`: numeric IPv4 or IPv6, bounded datagrams. Thread
  uses IPv6. Inspect packet source address/port and consume the readable notification.
- `/net/ws connect/send/receive/close`: WS/WSS client, bounded messages. Configure
  host/port/path, TLS CA and reconnect interval. Consume the queue and observe drops.
- `/net/http-server route`: install GET/POST/PUT/DELETE routes at `/` or below `/app/`, either
  static responses or `dynamic:true`. Dynamic requests return HTTP 202 with an ID,
  emit `request`, and can be read/responded/released through this resource. Clients
  retrieve the response via `/os/invoke`. Six routes/four requests, 2048-byte bodies.
  `stop` removes routes/requests. This shares the existing HTTP listener on port 80.
  An app-owned GET `/` replaces the default management page while that application
  runs; stopping/replacing it restores the package page if no replacement route is
  registered. `/os/*` remains reserved for system transport. Larger pages can load
  additional app routes/resources; the bounded text-response limit still applies.
- `/dev/wifi scan_start/scan_results/scan_clear`: async scan with result paging.
  Existing connection, TX-power and modem-sleep interfaces remain available.

## BLE

`/dev/ble start` supports `mode:"gatt"`, `"hid"`, or `"central"`.
GATT accepts a service UUID and up to four `{uuid,properties}` characteristics;
property bits are READ=2, WRITE=8, NOTIFY=16. `write` sets a characteristic index and
optionally notifies. Incoming writes generate events.
HID accepts `report_map_hex`; applications own the report layout and button mapping.
Input report IDs 1..4 map to indices 0..3. Send a release report as well as a press.
The presenter example is application code, not a fixed board-button behavior.
Central mode provides bounded `scan`/`scan_results`/`scan_stop`, and takes an address/type in `connect`; `gatt_read`, `gatt_write`, and
`subscribe` take service/characteristic UUIDs. These run asynchronously and report
operation events. Stop only after the operation finishes. Pairing/peer behavior and
radio coexistence need target verification; compiled does not mean paired.

## Local programs and remote work

Use local timers, conditions, networking, parsing, rendering and button rules for
repeatable behavior. Do not invoke a Harness on every timer tick just to fetch a URL.
Lua accesses the same resource ABI with `notes.call`, subscribes to events, and loads
from a file without rebuilding firmware. Examples: `examples/agent-os/local-poll`,
`presenter`, and `remote-events`.

`/services/remote submit` carries a route, request ID, payload and deadline. The outbox
is transport-independent, volatile and bounded to four records. Relays `peek` without
consuming, then `complete` using `expected_boot_id`. Apps read results and explicitly
`release` receipts. Do not reuse an ID for another payload. Expiry is observable;
reboot clears volatile requests. Receiving a request is not permission to invent new
external side effects.

`hub/src/os-remote.ts` supplies a host relay and pluggable handlers. `route:"agent"`
can use the existing Hub's bound Harness/session; its initial result is an acceptance
receipt, not task completion. `agent_status` fetches status without running the model.
Other routes are ordinary host service functions (rendering, transformations, vendor
integrations) authored by applications. Keep their request IDs idempotent; generic
handler side effects across host process crashes require application-owned persistence.

Start the relay after building the Hub with `NOTES_OS_URL`, and either
`NOTES_HUB_URL` + `NOTES_DEVICE_ID` or `NOTES_SERVICE_MODULE`, then `npm run os-bridge`.
The module exports `handlers`. Optional `NOTES_HUB_TOKEN` authenticates the existing Hub.
Agent output can use hardware_invoke to push a new UI/file or deliver an application
result; service execution does not depend on a microphone trigger.
