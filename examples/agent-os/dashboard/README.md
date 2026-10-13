# Runtime UI example

1. Run `npm ci --prefix tools`, then compile the SVG: `python tools/compile_os_image.py examples/agent-os/dashboard/icon.svg examples/agent-os/dashboard/icon.bin` (Python requires Pillow).
2. Upload this directory to a fresh VFS version path with `Client.publish_ui` from `tools/os_client.py`; select an available SD or RAM mount. It enqueues `ui.json` only after all files are written. A successful enqueue is not display completion.
3. Optional: `/sys/ui app_load` the uploaded `app.json`, then `/sys/runtime load` the uploaded `main.lua`. These explicitly replace the default bindings. The Lua example assumes three buttons and an available SD card; adapt after capability discovery.
4. Edit any layout/style/image or Lua file on the host, publish a new version and open with `history: replace`. No firmware rebuild. Use `back` for history and `refresh` to re-read the current file without adding a history entry.

`icon.bin` is a generated deployment asset, not a source bitmap baked into Flash. SVG compilation happens before transfer; the device reads its image regions from the file. Large full-screen photos should be composed by the host into the selected display driver's native frame format, then presented from an SD file or streamed to the display. Avoid using an LVGL full-screen RGB565 image file on SD in the current E6 backend: its narrow-band renderer performs many small reads.
