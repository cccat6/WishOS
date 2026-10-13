# E6 fixed reference profiles

The fixed data in `E6Waveforms.h` and `E6Palette.h` comes from the publicly
distributed Tickey Android 2.1.0 build 11 package, firmware 2.1.0 / spectra_v2,
source revision reported by that firmware: `ea3037b8a421`.

Source: https://download.eniacelec.com/tickey-app-release-2.1.0-build11.apk
Firmware SHA256: `519fe0e3c585db7d4e7f84870ba44c12b4bef3daf145f23bb4d9fea4811c82c3`.
Each array records its decoded payload hash. The two tune records have identical
payloads and share one array. Palette RGB triples are reference values, not
measurements of the user's current panel. No open-source license for these vendor
data is asserted by this project; their provenance is separate from our driver code.

The driver implements fixed blue/yellow, 55-code fast color and 48-code quality
color programs. It owns preparation, old-pixel state, high/low planes, return
sequences and power transitions. No waveform upload, arbitrary register access,
experiment orchestration or calibration dependencies are exposed to applications.

Previous-board observations: blue/yellow updates about 0.6–0.8 s, fast color about
2.8 s, quality color about 13.3 s. A required clear adds about 3 s; mode return work
is additional. These are estimates from the reference sequence, not guarantees
for the new PCB, temperature or optical calibration. The reference setup uses a
25 C controller temperature input. BUSY completion governs every hardware phase.
