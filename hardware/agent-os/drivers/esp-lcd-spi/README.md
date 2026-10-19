# ESP SPI LCD presentation binding

The device composition owns panel initialization, SPI host, electrical settings
and callback registration. The shared binding owns raster scheduling, leased
frame buffers and the transport completion receipt.

For the pinned ESP-IDF 5.5.5 backend, `esp_lcd_panel_io_tx_color` queues the color
transfer. Its SPI implementation emits `on_color_trans_done` after the final
transaction completes. The caller retains the DMA source until that callback;
application replacement and queue policy preserve this native completion barrier.
Both current compositions use a transaction queue depth of one.

The public panel-IO API provides no bounded transfer cancellation. Parameter
transactions and IO deletion drain pending transfers with `portMAX_DELAY`.
A timeout waiting for an interrupt is not evidence that DMA released its source.
Driver diagnostics and recovery retain that source until completion or a proven
native teardown boundary. Normal application admission failures remain distinct
from a broken interrupt/peripheral execution chain.

Sources: [SPI IO implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/spi/esp_lcd_panel_io_spi.c)
and [public buffer-lifecycle contract](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_lcd/include/esp_lcd_panel_io.h).

The completion signal is a device-owned static binary semaphore, initialized
before panel IO registration. It allocates no heap and uses no task-notification
index. Each 8-row DMA block fits the host's declared maximum transfer size;
submission failure therefore cannot follow an earlier unmarked color fragment.
The signal is drained before submission and consumed after the real final IRQ,
including when the IRQ arrives before the worker begins waiting. Initialization
failure keeps the device unavailable and starts no SPI bus/IO resources.

For this pinned SPI IO implementation, the post callback ignores the boolean
returned by `on_color_trans_done`. The adapter gives the semaphore from ISR and
explicitly requests `portYIELD_FROM_ISR` when a waiter wakes. It does not add a
polling tick per block. The semaphore survives native worker replacement; buffers
remain retained through the same DMA completion boundary, with no timeout/free.

The native executor pins initialization and every transfer worker to the same
explicit core selected for `spi_bus_config_t::isr_cpu_id`. Its compiled
`WISHOS_LCD_NATIVE_CORE` defaults to core 0 on a single-core target and core 1 on
a multi-core target; only an available explicit core 0 or 1 is accepted. Runtime
applications do not select this native core. `AUTO` and sampling an unpinned
initialization task's current CPU cannot guarantee this invariant across task
migration. This keeps the pinned SDK's polling/queued SPI ownership transitions
and final interrupt completion on the same execution core, while retaining the
real DMA completion signal and source leases. It introduces no polling delay or
SDK lock patch. Hardware stress validation remains separate from host affinity
and launch-failure regression tests.
