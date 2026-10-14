Describe the change and which devices use it.

For a new device or native driver, include:

- Board id, MCU, schematic/source, pin map and electrical assumptions.
- Reused/new driver ids, initialization and shutdown order, bus/resource ownership.
- Supported operations, configuration boundaries, asynchronous completion and error behavior.
- Initial runtime package/reference; application behavior stays replaceable.
- Host regression results, selected-target build, and physical tests actually performed.
- Dependency versions and source/license provenance for any new code or data.

For a wiring/UI variant, reuse the existing port and drivers. Do not copy the OS kernel.
