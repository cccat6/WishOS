# Optional sampled-device adapters

These drivers are optional native adapters in the same repository. They use WishOS
resource calls and completion events; they do not define the operating system or
an application UI. A device composition chooses instances, pins, I2C addresses,
clock, power sequencing and which resource paths to register.

Implemented adapters:

| Adapter | Exposed capability |
|---|---|
| GXHTC3 | Temperature/humidity, normal/low-power conversion, CRC checking, sleep between conversions |
| QMC5883L | Three magnetic axes in microtesla, selectable range/rate/oversampling, overflow reporting |
| QMI8658C | Three acceleration and three angular-rate axes, die temperature, native timestamp, exact profile-dependent settings |
| FT6336 | Up to two contacts with stable ids, raw coordinates and contact metadata |
| PCA9557 | Private native dependency for selected control lines; not unrestricted application GPIO access |

The `Sampling` helper exposes `describe`, `read`, `configure`, `measure`, `cancel`.
`describe` and `read` perform no bus access. `read` returns the last valid sample
and its timestamp; a later failed conversion does not rewrite that sample.
`notes.request(path, "measure", {}, callback)` receives correlated `completed` or
`failed` events. Ordinary runtime timers/rules choose sampling schedules and
threshold actions. An application does not need to know I2C addresses or registers.

The native composition serializes `invoke()` and `poll()` on its service loop.
Physical transports supply a monotonic clock; conversion delays begin after the
command has actually completed, with allowance for clock quantization. Every I2C access goes through `I2cBus::Transaction` and the supplied physical-bus
arbiter. Waiting for conversion/data-ready never retains the bus. Pending owned
measurements are cancelled on application exit; deferred cleanup is visible and
continues through native polling. The ESP32 `WireI2c` adapter uses a shared semaphore,
an explicit bus frequency and bounded native I/O timeout.

QMI8658C revisions have different gyro ranges and nominal ODRs. The native device
configuration selects `Rev09` or `RevA`; no MCU-model inference or silent rate
rounding occurs. Normal measurements never enable self-test. Application heading,
sensor fusion, gesture interpretation, UI and environmental dashboards stay in
runtime applications. Configuring a continuous IMU/magnetometer measurement mode
is distinct from requesting a snapshot; disabling IMU axes is explicit configuration.

Sources used for register behavior:

- [LCKFB C3 peripheral tutorial](https://wiki.lckfb.com/zh-hans/szpi-esp32c3/beginner/peripheral-routines.html)
- [QST QMI8658C Rev A](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf)
- [QST QMC5883L Rev A](https://qstcorp.com/upload/pdf/202202/13-52-04%20QMC5883L%20Datasheet%20Rev.%20A%281%29.pdf)
- [GXCAS GXHTC3](https://gxcas.com/prodetail.html?model=GXHTC3)
- [LCKFB S3 resource list](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/)

The published S3 resource table lists QMI8658, FT6336, ES7210, ES8311, camera and
SDMMC. GXHTC3 and QMC5883L are documented on the C3 sibling. Both sensor families
are supported as independent optional drivers; they are not falsely advertised as
soldered peripherals on the S3 reference device. Host protocol/lifecycle tests are
not physical validation or a complete firmware port for either reference device.
