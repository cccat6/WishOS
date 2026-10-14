# Audio capabilities and execution placement

Discover the input, recognition and output resources separately. A microphone does
not imply a speaker, a keyword model, acoustic echo cancellation or deep-sleep
voice activation. Native composition registers the engines and physical outputs it
actually supplies. Recognition runs only in the power states its adapter declares.

The native layer owns PCM capture, independent compressed blocks, output decoding,
queues, clocks, amplifier control and supported local signal processing. It does
not select a conversation service or decide what a detected word means. Runtime
applications own recording gestures, wake actions, interruption, navigation,
endpoint selection and playback content. ASR, Harness execution and synthesized
speech normally run on an upstream service. Local prompt files do not need cloud
access. No cloud call follows automatically from a detector event.

## Capture and detection

Use `/dev/mic` for recording and `/dev/speech` when advertised for detection. The
input adapter reads the microphone once and shares PCM. Concurrent consumers must
use a compatible capture format; a conflict is rejected without stopping the
existing consumer. The C6 adapter currently shares mono PCM at a common sample
rate. Its detector engines use 16 kHz. Recording at 48 kHz cannot simultaneously
attach a 16 kHz detector without a resampling adapter.

`/dev/speech describe` lists engines, their parameters, events and model words.
`start {engine,parameters}` creates an application-owned session. `stop {}` stops
that session. `read` reports its session identity and active state. Stopping an
application releases its detector, while a separate recording consumer can remain.
`failed` reports input/backend failure; it is not a wake event.

- `energy-activity.v1` emits `activity_started` and `activity_ended`; parameters are
  `threshold_dbfs`, `attack_ms`, and `release_ms`. This is amplitude activity, not
  speech understanding or keyword recognition.
- A compiled WakeNet engine advertises its model words and supported threshold.
  An application cannot turn an arbitrary string into a trained wake model.
  Other recognition backends may expose runtime command vocabularies through their
  own declared parameters. Inspect the current engine rather than assuming that
  C3, C6 and S3 have identical models or resource requirements.

Use `notes.on` to bind detector events to application actions. For example, an app
may start recording on a word, show a page, submit a remote request, or do nothing.
Do not automatically disable detection during playback; the application's policy
and the adapter's duplex/AEC capability determine that combination.

## Playback

When `/dev/speaker` exists, inspect its rates, channels, codec formats, packet
limits, buffering and duplex capability. Start with
`{sample_rate_hz,channels,encoding,buffer_blocks}` and retain `playback_id`.
Current generic decoding supports `pcm16le` and independent `ima-adpcm-block`.
It does not imply that an arbitrary MP3/Opus stream is accepted; a different
compiled decoder can advertise additional formats.

Send `write {playback_id,sequence,hex}` with sequence starting at zero.
`audio_would_block` means retry that same unaccepted sequence after draining.
Already accepted sequences are acknowledged as duplicates, not played twice.
A gap is rejected. `finish {playback_id}` closes input; completion follows only
after queued PCM and the output backend have drained. `need_data` is a readiness
notification; queue status remains readable even if an ordinary event is lost.
Use `notes.request(...,'start',...,callback)` to observe completed/failed playback.
Acceptance, queued bytes, DMA drain and acoustically verified sound are distinct.

`pause`/`resume`/`stop` require the same playback identity. Stop discards queued
output when the native worker handles it; already transmitted samples cannot be
retracted. `volume {gain,mute}` changes digital gain (0..1) at the next decoded
block. A hardware codec may publish its own output controls. On application exit,
the worker closes the stream and releases buffers and the hardware channel.

Input and output share peripherals only through the native codec adapter. A
standalone I2S output cannot seize a controller owned by an incompatible input.
Duplex codecs coordinate both directions in one adapter; the application never
configures GPIO or native I2S handles. The current E6/Nova board has no speaker,
so it does not register `/dev/speaker`.

Capture frames carry signal roles and a common sample clock. The SZPI reference
uses `MMR` (two microphones plus a playback-reference channel), whereas the current
C6 mic adapter provides `M`. The portable detector interface preserves this layout;
an incompatible input change ends detection with an explicit failure. Never treat
R as a third microphone or claim AEC merely because the wire exists. Detector
processing duration/sample counters are observations of that pipeline, not total
system CPU utilization.
