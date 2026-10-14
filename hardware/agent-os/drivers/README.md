# Native driver contributions

Put a reusable controller/protocol implementation in `drivers/<id>/`, with a
`driver.json` manifest. Pins, board power topology and interactions belong to the
port/board/runtime package respectively. The kernel knows resource methods and
completion contracts, not a closed list of sensor or display model names.

```json
{
  "format": "notes-driver/1",
  "id": "example-display",
  "requires": [],
  "platforms": ["esp32"],
  "dependencies": [],
  "flags": []
}
```

`platforms` and `targets` are optional compatibility restrictions, not capability
advertisements. `requires` names other drivers. A port declares required `drivers`
and permitted `optional_drivers`; a board selects optional drivers using `drivers`.
The resolver rejects cycles, unknown ids and unsupported targets before staging.
Source files are staged at `src/drivers/<id>/`; use `drivers/<id>/...` includes and
root-relative portable headers such as `Core.h`. Only selected native sources are
compiled. Normal headers/sources need no build hook.

For a native archive/SDK integration, optional `build_hook` names a local Python
module. `prepare(context)` may return `libraries` and `flags`; `link(context,
properties)` returns Arduino build-property overrides after compiler properties
are available. Context includes the board, port, stage, output, dependency paths
and a build-local native directory. Hooks are trusted source code reviewed in a
PR. They must use pinned inputs and may not modify the installed SDK or firmware
source. The C6 radio adapter demonstrates callback ABI validation without silently
discarding duplicate symbols. A new hook does not require a kernel branch.

The native port binds the implementation to buses and pins, initializes it in
electrically correct order, and registers its actual capabilities in `Core`:

- Describe supported parameters, read/write restrictions, units and settings scope.
- Validate a complete change before applying it; use the same validation on restore.
- Advertise asynchronous operations with identities, terminal receipts and cleanup.
- Use the bus arbiter for all I/O, including probes/status/recovery; release it while
  an independent operation runs. Retain DMA/source buffers for their real lifetime.
- Respect resource, stream and file ownership during reset, power changes and exit.
- Declare only implemented capabilities. Initialization/protocol success is separate
  from physical feedback and durable storage completion.
- Put initial pages and application sequences in a runtime package. Motors may
  expose bounded commands and stop semantics; navigation/choreography remains app code.

For a wiring-only variant, reuse the native port and change its board manifest. For
a new topology, add a port that implements `notesos::begin/poll`, composes drivers,
and declares its own pin constraints and resource requirements. Do not copy or edit
the portable kernel for a board port. Multiple driver instances are constructed and
registered by that port; selecting a source component does not force one instance.

A PR needs behavioral tests for lifecycle/failure/ownership, a selected-target
build and an honest hardware validation record. Contribute source/license evidence
for any imported data or libraries. See the repository PR template and the parent
README for changed-target CI and per-board release artifacts.
