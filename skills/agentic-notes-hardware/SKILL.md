---
name: agentic-notes-hardware
description: Discover and operate Notes OS resources, inspect live system state and constraints, configure behavior policies, and build portable UI or device applications. Use for display, audio input, buttons, networking, power, files, fonts, jobs and conditional behaviors; discover capabilities rather than assuming a particular MCU, display or storage device.
---

# Notes OS resource control

For script modules, startup/lifecycle, binary data and background compute, read [runtime applications](references/runtime-apps.md). Hardware wiring belongs to the deployment package; runtime applications operate exclusively through system resources.

The factory suite is a default runtime application reference and its resources. Replace it through the same application loading/startup interfaces as user programs. Preserve necessary system coordination among the runtime, display, power and native services; changing pages or interaction behavior does not require dismantling those services.

For button gestures, contextual navigation, motion/tap events, clock synchronization and presentation state and resource-policy boundaries, read [interaction applications](references/interactions.md).

For Wi-Fi profile priorities, manual selection ownership and independent BLE configuration, read [wireless configuration](references/wireless-config.md).

For network sockets, BLE, Thread/Zigbee boot selection and local/remote execution, read [connectivity](references/connectivity.md).

Read [driver guide](references/driver-guide.md) for the invocation workflow and [system design](references/runtime-design.md) for layering, configuration, storage and UI integration. Read [dynamic UI](references/dynamic-ui.md) for runtime files, image preparation and limits. The longer design includes future targets; only methods and fields actually advertised by the connected implementation are executable.

Start with capability discovery and current state. Use the available transport/MCP entrypoint; the current prototype bridge exposes hardware_discover and hardware_invoke. Resolve resource instances, interface versions, network/display/audio/input availability and storage mounts. Never require a particular board name, GPIO, three buttons, Wi-Fi or microSD in a portable application.

Separate immutable hardware facts, system correctness constraints, build limits, policy defaults and effective settings. Query parameter schemas and current values before changes. Use validate/apply with a configuration revision when implemented. Read deferred/applied results and effective settings. Do not silently clamp an unsupported request or treat a submitted value as applied.

Observe free memory, largest allocation, storage availability, CPU load with its measurement window, resource leases and queue pressure when available. Unknown telemetry remains unknown; clock frequency is not CPU usage. Reserve or bound resources according to the available budget. Configuration and sampling frequency must not introduce uncontrolled Flash writes or expensive foreground scans.

For compiled display components, presentation profiles and completion callbacks across screen types, read [display interface](references/display-interface.md). For this board's E6 modes and formats, read [E6 profiles](references/e6-display.md). Select an advertised display driver/color space. Read its palette/gamut, latency, geometry, supported inputs and recommended use. Recommendations do not prohibit selecting another supported driver. For a display requiring BUSY completion, do not send any command, reset or power-off while it is busy; timeouts are faults, not readiness. Queue policy and input handling during presentation are configurable independently. Other hardware uses its own completion condition behind the same abstraction.

Do not impose a button mapping, FIFO/latest queue policy, input target page, five-minute recording cap, hold-to-talk behavior, storage destination or retention rule. Discover which policy handlers are supported and apply the requested behavior within real limits. Associate actions, stream blocks and completion receipts with their owners. A configured stop must reach the intended capture; an input event does not imply a hard-coded action.

Recording uses independent IMA ADPCM blocks for short RAM buffering and transport. Without SD, it never falls back to internal Flash; queue overflow may drop samples according to policy. SD saving is optional even when present, with configurable space/count limits and oldest-record cleanup. Recording can route to RAM, network and persistent storage according to policy. Do not claim a long recording fits entirely in RAM. Track missing samples, durability and upload acknowledgments separately. Retry using the same recording identity. State explicitly when the configured combination is unavailable rather than inventing a successful fallback.

Use the VFS and mount capabilities; support FAT32/exFAT only when the current backend advertises them. Never format a card implicitly. NFS server/client are separate optional capabilities. For an advertised `/services/nfs` server, use [NFS service guidance](references/nfs-service.md). Prepare book indexes on the host/cloud, then transfer a versioned content/index package; do not start an unrequested full-book indexing pass on the device. Session and recording indexes can be maintained incrementally while generating data.

System fonts reside in internal storage; user fonts, sizes and glyph coverage may be extended on mounted storage. Discover the font backend and cache limits. Prefer supported open UI frameworks and formats; the implementation uses an advertised Brookesia JSON UI 0.1.x compatibility subset rendered by LVGL 9.3, plus runtime-loaded Lua 5.4.8 for behaviors. Do not use the retired custom UI draft as a released device format.

Navigation, refresh, history restoration, event propagation and asynchronous result routing follow effective application policy. Keep navigation and side-effectful operation identities separate; refreshing an image does not inherently mean replaying an action. Scope-relative paths must resolve through the VFS.

Use the [resource model](references/resource-model.md): drivers declare their actual properties, operations, events and constraints independently of live readings. Omit absent capabilities. Use the common system condition engine and rule engine for thresholds, state changes and actions across all peripherals; do not invent peripheral-specific watch APIs. Electrical conditioning stays inside the driver.

Use task/event receipts to distinguish accepted, running, persisted and physically presented outcomes. Reconcile uncertain outcomes before replaying mutations. Let the selected Harness retain reasoning, sessions and external tools. This skill does not independently authorize messaging people or adding integrations.
