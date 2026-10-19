# Stems + multichannel: hardware test procedure (the Volt 876, or any multichannel interface)

This is what to run when the interface is connected. Results go in `06-report.md` §Hardware; a step that was not run is
recorded as not run. Nothing here is required by CI.

## What you need

- **The interface:** the Volt 876, connected over USB and powered, with UA Console (if installed) in its default
  routing.
- **A signal source per test:**
  - one source is enough if it is moved from input to input: a microphone, a guitar, a synth line out, or a phone
    playing music through a cable;
  - for the musical test, a mixer with discrete group or aux outputs into separate interface inputs (see "Routing"
    below).
- **ADAT inputs (channels 9-24):** an ADAT source (another interface or a preamp with optical out). Without one, these
  channels can only be checked for enumeration and silence.

## 1. Discovery (no signal needed)

```
build/release/src/avgen --list-audio-devices
```

Record:
- the exact name;
- the id (the Core Audio UID);
- the input channels **per stream** (for example `8+8+8` or `24`);
- the nominal rate.

Then change the Volt's rate to 96 kHz in Audio MIDI Setup or UA Console, and record the channel count again (ADAT
halves at 88.2/96 kHz). Set it back to 48 kHz.

**If the inputs span more than one stream,** the plain path (no sources) hears only the first stream (ADR-1522), and
the multichannel path hears all of them.

## 2. Per-input independence (a signal moved input by input)

```
build/release/src/avgen --input-meter "Volt 876" 120
```

1. Plug the source into input 1. Play for about five seconds. Only channel 1 should rise above the noise floor.
2. Move it to input 2, and so on through input 8.
3. Record, per input: the signal used, the peak on its own channel, and the highest peak on any other channel
   (crosstalk).
4. Watch the status columns. `drops` and `gaps` should stay 0, and `overloads` should stay 0 or near it.

## 3. Unplug and replug

Start `avgen --input-meter "Volt 876" 60`:
1. Pull the USB cable at about 20 s. The line should say `DISCONNECTED`. Record how quickly.
2. Plug it back in. The meter keeps the old (dead) device; it does not reopen, by design. Then run the live editor
   test below.

In the live editor:
1. Open a live project.
2. Live panel: choose the Volt under Audio input, then tick channels 1-4 in the Inputs table.
3. Pull the cable. The panel should say **Disconnected ... waiting for it**, and `stem.*.active` should read 0.
   Nothing else should be opened in its place.
4. Plug it back in. Within about a second the device should reopen and the meters should move again.

Record each step, and whether the channel map survived.

## 4. Live arms of the benchmark

Repeat arms A1, A3 and A6 of `05-benchmarks.md` with `--input "Volt 876"`:
- A1: no sources;
- A3: `--input-sources 8`;
- A6: `--input-sources 8h` with MIDI and projection.

Have a signal on at least inputs 1-4, so the analysis works on real audio. The log's `live input sources:` line gives
the callback, drops, gaps, analysis cost and onset latency.

## Routing: getting independent musical sources into the inputs

AV Gen analyses what arrives on each input; it cannot know what the source is. To get the drum bus, the bass and the
synth on separate inputs:
- **On a mixer:**
  - Route each part to its own group or aux bus.
  - Use pre-fader if the main mix fader should not change the analysis, post-fader if it should.
  - Patch each bus output to its own interface input.
- **From a DAW on another computer:** send each stem bus to its own physical output, and patch those outputs to the
  interface inputs.
- **The main mix:** if it also comes in (say on inputs 7/8), leave those channels unticked. The main `audio.*` analysis
  is the average of every channel the device has.
- **Keys:** label each ticked channel with the key the scene's routes use (`drums`, `bass`). Those are the same names
  an offline stem of that key publishes.
