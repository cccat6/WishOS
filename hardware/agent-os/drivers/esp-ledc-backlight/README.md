# ESP LEDC backlight

This native adapter keeps its configured electrical output inactive before PWM
initialization. A device composition calls `holdOff()` at its early native startup
hook, then `initialize()` configures only the selected LEDC timer/channel.

Panel initialization and valid content are separate gates. `panelReady(true)`
reports a ready controller; it leaves PWM at zero until the presentation adapter
calls `frameReady()` after a successful complete native frame transfer. A rejected,
cancelled or partial first frame leaves the backlight off. `panelReady(false)` and
reinitialization clear the frame gate. Brightness configuration and inheritance
remain available during startup; the saved desired value applies once both gates
are satisfied. Zero brightness remains zero.

The SPI LCD binding signals the frame gate after its final DMA completion, on the
system event loop. It publishes `content_valid` as a read-only display fact. The
ordinary launcher chooses its own startup content and duration; this driver has
no logo, page, timer or application-selection logic.

`read` distinguishes `panel_ready`, `frame_ready`, desired/applied brightness and
`deferred`. The early hook executes after the ESP/Arduino runtime reaches the
application; software cannot set an electrical level during ROM or reset before
GPIO control is available. Hardware pull/bias determines that earlier interval.
