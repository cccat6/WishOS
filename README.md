# AgenticOS

An embedded application runtime above FreeRTOS, with native resource drivers,
loadable Lua applications, JSON UI, a shared VFS, events and resource budgets.
Hardware packages supply board wiring, native adapters and initial UI/configuration.
Applications use the resource ABI; they cannot change GPIO wiring or create native
RTOS tasks. No local model is required for deterministic application behavior.

This local Git repository tracks the public system only. The `hub/` directory is
an independent Git repository containing the MCP server, Harness adapters and host
rendering pipeline. E6 waveform research and local credentials/data are not part
of either public repository.

- System: `firmware/AgenticOS/`
- Hardware packages: `hardware/agent-os/`
- Runtime examples: `examples/agent-os/`
- Agent guidance: `skills/agentic-notes-hardware/`
- Implemented interfaces and limits: `docs/agent-os-runtime.md`

Build using `tools/build_agentic_os.sh`; select a board with `NOTES_OS_BOARD`.
The default board is `e6-nova-c6`. `esp32-c3-runtime` is a headless development port
for testing the same Core/Lua/Workers/Wi-Fi/VFS/NFS code. Missing display/audio devices
are not falsely advertised. Build output never flashes automatically.

`tools/test_agentic_os.sh` runs host tests, including fault injection and real
filesystem images. Set `ARDUINO_LIBRARIES` to the pinned Arduino dependency directory.
For host image compilation, install Pillow and run `npm ci --prefix tools`.
The current target toolchain is Arduino ESP32 3.3.11; the NFS dependency is pinned
by commit and acquired from its own source repository. See the firmware README for
all dependency versions and the unified C6 radio build.

Recording uses independently decodable IMA ADPCM blocks. Without SD, only bounded
RAM buffering is used; queue overflow is observable. Persistent recording is optional
and supports count/space limits and oldest-record removal. Initial interaction and
service endpoint configuration belong to each hardware package, not the kernel.

Hardware testing must identify the chosen USB serial device before flashing. Do not
operate another active experiment's board. Temporary development boards do not need
Flash backups; test-created files are cleaned up explicitly. A successful host test
or build is not evidence of physical display/audio behavior.
