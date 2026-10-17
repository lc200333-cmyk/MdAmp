# Audio engine v1

`AudioEngine` is an independent C++17 implementation using pinned miniaudio
0.11.23 for WAV/MP3/FLAC decoding and Windows WASAPI shared-mode rendering.
The original player is not loaded and none of its decoder or DSP DLLs are used.
OGG/AAC/WMA/MIDI/MOD, streams, CD audio, encoders, ASIO and Winamp DLL ABI
compatibility are not implemented. Renaming an unsupported extension does not
enable its codec. No network requests occur during playback or builds.

The public engine constructor never enumerates or opens audio hardware. `Open`
closes the old file, opens a Unicode local filename, validates a nonempty seekable
stream and initializes a stopped default WASAPI device. A failed `Open` returns
false and supplies diagnostic text, leaving the engine empty. There is no silent
null-backend fallback. `Play` returns false on missing media/device or start error.
The output callback is fixed float32, stereo, 48 kHz; miniaudio converts source
rate/channels to this format. Snapshot format describes output, not source fidelity.

Pause retains the source cursor. Stop resets to frame zero. Seeking is clamped to
the known duration.
`Close()` is idempotent: it releases the device/decoder and empties snapshots;
use it when clearing/replacing a playlist or before COM is uninitialized.
End-of-file sets `ended=true`, `playing=false`; replay resets
to frame zero. The cursor measures decoded frames submitted to the device, not
the exact acoustic playhead (device buffering adds latency). Pause/stop can retain
a short already-submitted buffer tail. The device remains running with silence
while paused/stopped, avoiding device-start races on resume. Reopening and
destruction release the device. There is no gapless playback or crossfade.

The independent DSP path is: decode -> ten peaking biquads -> optional MdAmp DSP
callback -> linear left/right balance -> volume -> finite/clamped PCM -> analysis.
The EQ uses 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000 and 16000 Hz,
Q=1.4, +/-20 dB per band and preamp. Disabled EQ bypasses both bands and preamp.
Independent delay states preserve stereo separation. Coefficients update under
the callback mutex, reset filter history, and can cause a small discontinuity;
there is no parameter smoothing. High boosts can clip; output is hard-clamped,
not passed through a limiter. Spectrum is computed from actual post-gain PCM
using 32 logarithmic Goertzel bands and is not an arbitrary animation.

One mutex protects decoder, DSP and snapshots. A separate operation mutex
serializes open/start/device lifetime. Device start/stop/uninit is never called
under the callback mutex or from the callback. Decoder reads and DSP occur in
the callback and can block briefly on settings/snapshot access: this simple v1
is not a hard-real-time worker/ring-buffer design. Public methods may be called
from the GUI thread. Destruction requires no other thread to call the object.
The plugin callback must not throw, allocate, access the GUI, or call AudioEngine.
`SetDspCallback(nullptr,nullptr)` waits for any existing callback to finish; only
then may its module/user state be destroyed. Plugin code must be trusted.

## Verification

`mdamp_audio_tests` performs synthetic in-memory stereo WAV decoding and seeking,
an independently constructed MPEG-1 Layer III silent frame stream, a valid FLAC
constant-subframe stream with CRCs and exact lossless amplitude verification,
missing/empty file errors, idle engine behavior, and actual sample-level EQ
bypass/unity/boost/cut/preamp/stability/channel-isolation checks without hardware.

Optional hardware probe (quiet, one-second synthetic 1 kHz WAV):

```
mdamp_audio_tests.exe --write-fixture temporary-test.wav
mdamp_audio_tests.exe --audio-smoke temporary-test.wav
```

The hardware probe requires a real default WASAPI endpoint and verifies PCM
callback progress, nonzero signal energy, pause retention, seek/resume/stop,
natural EOF and replay. The probe also repeatedly destroys/reopens the device while another thread
reads snapshots and updates volume/balance (a regression check, not a race detector).
It does not establish acoustic output, perceptual quality or clean-machine
deployment. Fixture files and binaries must not enter Git.

On 2026-10-17 standalone MSVC x64 compilation and all offline checks passed.
The optional WASAPI probe also passed against the machine's default endpoint
using the generated one-second WAV. No acoustic capture was performed. MP3/FLAC
were decoded offline, not separately played through the hardware probe.
