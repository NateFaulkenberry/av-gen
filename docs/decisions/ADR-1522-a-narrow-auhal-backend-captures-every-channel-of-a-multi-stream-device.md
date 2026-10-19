# ADR-1522: A narrow AUHAL backend captures every channel of a multi-stream device; miniaudio keeps the plain path

- Status: Accepted (eng/stems-multichannel). The owner chose it at gate 1 over patching miniaudio.
- Code:
  - `src/audio/capture_coreaudio.*`;
  - `src/audio/audio_input.cpp`: `listCaptureDevices` stream layout, selection by id;
  - `src/app/engine.cpp`: path choice and `watchInputDevice`;
  - `src/app/input_meter_cli.*`: `avgen --input-meter`;
  - `src/CMakeLists.txt`: the CoreAudio and AudioToolbox frameworks.
- Tests: `tests/unit/test_coreaudio_capture.cpp` (`[coreaudio]`, local: it SKIPs without BlackHole 2ch) and
  `tests/support/coreaudio_rig.hpp`.

## Context

**Measured with a private aggregate device** (built-in mic + BlackHole 2ch, two input streams, a known tone in
BlackHole):
- miniaudio 0.11.25 opened with `channels = 0`, the way `AudioInput` opens it, delivers only the first stream's
  channels.
- Asked for 3 channels, it returns three copies of the first stream.
- Cause: it picks the client format from `kAudioStreamPropertyAvailableVirtualFormats` queried on the device, which
  lists per-stream formats (miniaudio issue #312).
- Both a 10-line patch and a raw AUHAL capture deliver every channel in order.

An interface with analog and ADAT banks (the owner's Volt 876) may present several streams. Whether it does is a
hardware check.

## Decision

**`CoreAudioCapture`.** It asks AUHAL (TN2091) for every channel of the device (the sum over
`kAudioDevicePropertyStreamConfiguration`):
- float32, non-interleaved, at the device's nominal rate (AUHAL does not convert rates, and AV Gen never changes the
  system rate);
- a 128-frame period request (ADR-1025);
- the HAL's `mSampleTime` passed on with every block, so gaps are counted.

**Listeners set atomics only:**
- on the device: `DeviceIsAlive`, `NominalSampleRate`, input `StreamConfiguration`, `ProcessorOverload`;
- on the system: the device list.

They are delivered on the HAL's own notification thread: `kAudioHardwarePropertyRunLoop` is set to NULL once, so a
process with no run loop still gets them.

**The engine's choice of path:**
- On macOS, a device with input sources opens through this backend.
- With none, it opens through the plain miniaudio path, unchanged: the BlackHole 2ch workflow as it was. Editing the
  sources moves an open device between the two.
- Other platforms keep miniaudio for both. WASAPI delivers multichannel as one stream, which is untested here.

**Device lifecycle:**
- Callbacks are never the disconnect signal: on the destroyed aggregate they kept arriving.
- A disconnect makes every source inactive. Nothing else is opened in its place, and the same UID is reopened once it
  is back (checked once a second).
- A format change reopens the device. Sources on channels that no longer exist read inactive.

**Selection and listing:** `listCaptureDevices` adds channels per stream and the rate. `AudioInput::open` matches an
exact id (a saved UID) before the name substring. The Live panel selects by id.

**`avgen --input-meter <device> [s]`** prints every channel's peak once a second, with drops, gaps, overloads,
callback time and a disconnected flag. It is the hardware check.

## Consequences

- **Measured:**
  - The aggregate delivers BlackHole L and R on channels 2-3.
  - A destroyed device reads disconnected about 360 ms after the destroy call (the HAL's notification latency; 5 ms in
    a bare probe).
  - On BlackHole 2ch the main stream matches miniaudio's: 4095 of 4096 samples are bit-identical, and the other
    differs by 1.2e-15 at a zero crossing.
  - Device enumeration costs the UI about 2.4 ms per call, every 3 s.
- **Not yet verified on the Volt 876:** its stream layout, per-input independence, and unplug and replug behaviour.
  These are pending hardware (`06-report.md`).
