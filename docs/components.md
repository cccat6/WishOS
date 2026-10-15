# WishOS components and Agent guidance

| Layer | Ownership | Composition and discovery |
| --- | --- | --- |
| System | Runtime/loader, scheduling/admission, events/conditions/callbacks, VFS, presentation, application catalog, transfer and validation coordination | `firmware/WishOS/`; live resource methods/descriptions remain the executable contract |
| Driver | MCU/SDK integration, peripheral protocols/lifecycle, device wiring/topology | Independently selected `hardware/agent-os/platforms/`, `drivers/`, `compositions/`, and `boards/`; a chip name does not imply peripherals |
| Application | Pages/styles/navigation, input meanings, sequences of resource calls, ordinary launcher/voice/photo apps | Runtime files in the selected package or user storage; a startup suite is a replaceable reference with no extra privilege |
| Upstream | MCP, Harness/session adapters, resource processing, model/provider configuration | Independent WishOS-Hub repository; uses the same device resource and package contracts |

The board packager emits `/system/components.json` with format
`wishos-components/1`. `/sys/board read` advertises its path. The index lists only
selected platform/driver/device/startup components and their source reference.
It is compressed with the other system assets and can be read in VFS chunks.
Live `/sys/capabilities` descriptions determine methods, settings, events, limits
and native completions. A static component reference does not grant a capability
or establish physical validation. `/sys/apps` discovers installed user packages.

The device API skill routes to the maintained operation, callbacks, runtime, UI,
storage/network and package references. Hub packages system, application, package
and native-driver guidance for MCP discovery. Native driver guidance is for
compiled contributions; it must not suggest GPIO/register access to runtime apps.
MCP resources/prompts and operation results direct an Agent to the relevant guide.
Descriptions help generation; the preflight and native admission mechanisms
actually reject unsupported or invalid submissions.

Package preflight is shared across local Hub, cloud MCP, managed AI results and
typed file-level updates. See [the package contract](package-contract.md). Native
compile/render checks, catalog commit, activation, callbacks and resource release
are checked separately. No static checker establishes arbitrary program semantics,
future available memory, physical display color, or hardware wiring correctness.
