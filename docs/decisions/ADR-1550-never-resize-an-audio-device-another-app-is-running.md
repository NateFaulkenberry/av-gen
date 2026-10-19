# ADR-1550: AV Gen never resizes an audio device another app is running; real-device tests are opt-in

**Status:** Accepted, 2026-10-19 · **Range:** 1550-1559 (coordinator fixes)

## Context

The owner's system audio became glitchy while they played audio and MIDI into live scenes, and it stayed broken until a reboot. Measured on 2026-10-19:

- **The owner's Multi-Output Device.** The music apps play into it. It contains the Volt 876, BlackHole 2ch and the MacBook Pro Speakers, and it was clocked by BlackHole with drift correction off on every sub-device. The owner's setup has since been fixed: the speakers are the clock source and BlackHole has drift correction. That fix is outside the repository.
- **AV Gen resized the shared buffer on every open.** A Core Audio device's buffer size is shared by every client. AV Gen's capture asked BlackHole for a 128-frame period (ADR-1025), and miniaudio sets the device buffer to the period it is given. So every AV Gen open switched BlackHole 512 -> 128 under the running Multi-Output, and every close switched it back. The coreaudiod log shows `handle buffer frame size change, old 512 new 128` on BlackHole's IO context each time.
- **Playback did the same to the speakers.** The player's default 10 ms period resized the default output, which is now the Multi-Output's clock.
- **Test runs hit the real devices too.** The suites opened BlackHole, the speakers and the microphone, and built and destroyed private aggregates on BlackHole. In 6 hours of agent test runs the log counts 78 BlackHole IO starts, 50 speaker starts and 56 test-aggregate starts, each one reconfiguring the owner's running audio.

## Decision

1. **`courteousPeriodFrames(uid, wanted, output)`, in `audio/capture_coreaudio`.**
   - When nothing else is running the device (`kAudioDevicePropertyDeviceIsRunningSomewhere` is false), it returns `wanted`.
   - When something else is, it returns the device's current `kAudioDevicePropertyBufferFrameSize`, so opening the device leaves its buffer alone.
   - An empty `uid` means the default input, or the default output when `output` is set.
   - Off macOS it returns `wanted`.
2. **Every place that sets a device period goes through it:**
   - `AudioInput`'s miniaudio capture;
   - `CoreAudioCapture`'s AUHAL buffer size;
   - `AudioPlayer`'s playback (0, which is miniaudio's default, when free).
3. **Tests that open real audio devices are opt-in:** set `AVGEN_AUDIO_DEVICE_TESTS=1` to run them. Without it they SKIP with that reason. They are the `AudioInput` default-device cases, the `AudioPlayer` playback cases and the four `[coreaudio]` real-device cases. CI runners have no audio devices, so CI loses nothing.

## Consequences

- **Latency.** When BlackHole is already in use by another app (the usual live set-up, music playing through it), AV Gen captures at that app's buffer size, typically 512 frames, instead of 128. That is about 8 ms more audio-to-bus latency than ADR-1025's measurement. With a dedicated interface nothing else is running, AV Gen still gets 128.
- **Developer workflow.** Developers who want the device tests run them deliberately, at a moment when no one is playing audio through the machine.
- **Not covered.** This does not stop a device's sample rate being changed by another client, and AV Gen never changes sample rates.
