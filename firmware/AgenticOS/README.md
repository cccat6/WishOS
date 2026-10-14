# AgenticOS

Runtime-loadable UI and Lua applications above portable resource, policy and file interfaces. Current Arduino BSP targets the shared ESP32-C6FH4 E6/Nova board; the implemented display backend is E6. A separate headless ESP32-C3 validation port uses the same runtime. Nova and other display-controller bindings are not yet implemented.

See [runtime/API status](../../docs/agent-os-runtime.md), [agent skill](../../skills/agentic-notes-hardware/SKILL.md) and [example package](../../examples/agent-os/dashboard).

Hardware packages live in [hardware/agent-os](../../hardware/agent-os/README.md). Select MCU platform, device composition/wiring, reusable drivers and initial runtime references with `NOTES_OS_BOARD`; runtime apps cannot change native wiring. Build: `tools/build_agentic_os.sh`. Test: `tools/test_agentic_os.sh` from the repository root. Neither command flashes hardware. The source directory alone is not a configured sketch: use the generated per-board sketch, with its `BoardPackage.h`, selected native sources and build options. Do not flash this board profile to the older NotesMonitor PCB.

The firmware owns its source tree. It does not compile the legacy NotesMonitor application or any internal waveform/experimental tooling. Third-party licenses are in `licenses/` and the vendored Lua headers; Arduino libraries retain their upstream licenses.

On-demand network storage: `/services/nfs` exports an existing OS provider through ESP32NFS **1.3.0** (MIT), with SdFat **2.3.0**. The build/test scripts snapshot the verified v1.3.0 commit from the sibling repository, ignoring its uncommitted development state; Arduino IDE users install ESP32NFS 1.3.0. NFS starts explicitly and pins its storage until stopped. See the runtime guide for lifecycle, client mount commands and current semantics.

Connectivity build: WebSockets 2.7.2 and Arduino ESP32 3.3.11 are required. The script prepares build-local namespaced 802.15.4 SDK archives and links a boot-selected radio dispatcher; both Thread and Zigbee are in one image. Choose `/sys/radio` protocol, then reboot. The custom partition table is part of the initial full firmware deployment. The installed SDK is never edited. See [connectivity contract](../../skills/agentic-notes-hardware/references/connectivity.md).
