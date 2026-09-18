# ADR-277: The only way to look at a post stage was to write a GPU test, and two readbacks were quietly wrong

**Status:** Accepted
**Date:** 2026-09-18

The HDR / Exposure / Bloom Lab (lab #8 of ADR-261's suite; `docs/hdr-lab/README.md`). One
instrument, two fixes, and one finding recorded in ADR-279 rather than acted on.

---

## 1. `--post-stages`: the capture that existed and could not be reached

`PostProcessor::armCapture` / `takeCapture` have been in the class since the Glowmere water-lattice
investigation (ADR-159, `docs/post-artifact-forensics.md`). They keep a handle on every intermediate
the chain renders, and the header explains exactly why: *"a post chain can only be judged on its
final frame, which is how three plausible fixes for the water artifact were shipped without anyone
knowing which stage produced it."*

Two GPU tests could arm it. Nothing else in the program could. The specification for this lab ends
on the sentence **"every intermediate stage should be inspectable"**, and that was true of the class
and false of the binary: a person looking at a production frame and asking "which stage is that
glow" had to write C++.

So `--post-stages <dir>`, offline render only, beside `--aov`:

```
avgen --headless --composition … --size 1280x720 --range 0.5:0.5 \
      --render out --post-stages stages
```

writes one scene-linear EXR per stage at the resolution the chain chose, named by the chain's own
label — `frame_000000.bloom-down3.exr`, `frame_000000.wide.exr` — plus
`frame_000000.stages.json` with each stage's extent, peak and mean luminance and the frame's
exposure scale, EV100 and metered luminance.

Three properties were deliberate.

**The names are the chain's, not the flag's.** `captureStage` already labels every target
(`bloom/prefilter`, `halation/up2`, `composite`); the file name is that string with `/` replaced,
so a stage cannot be renamed in the export without being renamed in the chain.

**Absence is information.** The exposure pass is skipped when the scale is within 0.1% of 1; the
halation and wide targets only exist when those tiers are on. A skipped stage is simply not in the
manifest, rather than exported as an identity image — which would be a claim that a pass ran.

**Arming does not change the picture**, and that is checked rather than asserted: the only
difference is `CopySrc` on the pyramid and wide targets, which nothing samples, and the same render
with and without the flag produces `sequence hash 7a3636ff855ad06c` both times.

It is a diagnostic and it says so on stderr: one blocking map per stage per frame, so a sequence
rendered with it on is not a sequence whose timings mean anything. Use `--range t:t`.

What it buys, immediately, from a command line rather than from a test: the shipped chain's bloom
pyramid over the lab fixture has a mean of 3.560672 at `bloom/prefilter` and 3.556233 at
`bloom/up0` — 0.12% over eleven passes. ADR-039's energy-conserving upsample, measured.

---

## 2. `resetExposure()` did not discard the metering copy already in flight

**The claim in the header:** auto-exposure state *"is part of render state: reset it when a render
job seeks or a scene is swapped so an offline render reproduces a live one exactly."*

`resetExposure` cleared `exposureState_`, `haveMeasurement_` and `measuredLuminance_`. It did not
clear `meterPending_` — the flag saying a copy of the **previous** frame's metered luminance is
still on its way to the readback buffer — and `takeMeasurement` maps whatever is pending at the top
of the next `run()`. So the frame after a reset was handed `hasMeasurement = true` carrying the old
scene's reading, and `updateAutoExposure` walked toward its target.

**Measured** (`test_hdr_lab_gpu.cpp`, *"a reset meter reports the frame it was reset for"*): a
frame metered at 8.0, then `resetExposure()`, then a black frame — the chain reports **8.0** where
it must report "nothing metered" (−1). The control is a chain that has never metered anything,
through the same call sequence: it reports −1. Without that control the arm would pass on a chain
that had simply never worked.

**Fix:** `meterPending_ = false`, with the reason in the comment. Dropping the flag drops the copy;
the buffer is written again before it is ever read, and an unmapped buffer with a completed copy in
it is not a hazard.

**Blast radius: no deliverable moves.** `Engine::resetCameraState` fires at load, before any frame
has been metered. What changes is a process that renders more than one scene — every test process,
and the editor.

---

## 3. `renderToImageFloat` read the output's extent out of the scene target

ADR-251 found exactly this in `RenderJob::renderOne` and fixed it there, in words worth repeating:
the supersampled EXR *"was the right size, the right format, scene-linear and full of the wrong part
of the picture."*

The same mistake was still in `SceneRenderer::renderToImageFloat`, which read back `width × height`
— the **output** size the caller passed — out of `hdrOutput_`, which `resize()` sizes to
`output × renderScale`. At `--supersample 2` it returned the top-left quarter of the frame. Nine
test files call it, including three forensic suites.

**How it was found matters more than the fix**, because this is the shape of defect that produces a
wrong *finding* rather than a failure. The HDR Lab's probe for §5.2 of its document first reported a
bloom halo **four times wider** at render scale 2. That would have been a headline. It was a corner
of the picture. With the extent corrected the same probe reports 0.506 — a real result, in the
opposite direction, an eighth the size, with a control beside it that does not move.

**Fix:** read `hdrOutput_.GetWidth()/GetHeight()`. A caller measuring in normalised coordinates is
comparable across render scales as it stands; a caller measuring in pixels was already measuring the
scene target rather than the output.

---

## 4. Reported and not changed

Three things this lab measured and deliberately left alone, each with the measurement in
`docs/hdr-lab/README.md` so a later decision starts from a number:

* **The bright pass gates on luminance and the tone curve compresses per channel.** A pure blue
  needs 13.93× the radiance of a neutral to cross the same bloom threshold — exactly
  `1 / 0.0722` — and a violet 5.79×. Glowmere's emissive content is tuned against that, and the
  one-line change that reconciles it moves every frame ever graded on this engine. The lab
  contributes the number and a test that pins the *current* rule, so a change of basis is
  deliberate.
* **The bloom pyramid's reach is a pixel count.** ADR-279.
* **A scene file cannot author an emissive above 50.** `material/emissive` is registered with a
  hard maximum of 50 and the parameter clamps an authored 256 with no warning; `baseColor` and
  `emissiveColor` are clamped to [0, 1], so this is the only route above unit radiance from a scene
  file. ADR-225's defect in an authoring format, in `procedural.cpp`, which is not this lab's file.

---

## 5. Also

`testsupport::PostBench` — the post chain driven directly over a CPU-written HDR texture — moved
from `test_post_artifact_forensics_gpu.cpp` to `tests/support/post_bench.hpp`, unchanged except that
it now hands back RGBA floats instead of the water investigation's luminance `Field`, because this
lab's questions are per-channel. The forensics suite's nine cases and 913 assertions are identical
after the move, and its printed stretch sweep still matches the after-fix table in
`docs/post-artifact-forensics.md` §8 line for line.

`labs::overlaysFor(LabId::Hdr)` stays empty, and now says why in its own case rather than sharing
the Volumetric Lab's: debug geometry is drawn **into the HDR target, before the post chain**, so an
overlay line above `post/bloom/threshold` goes through the bright pass and back out over the
picture. The Rendering Lab draws nothing because an overlay in a frame it grades becomes an artifact
it reports; this lab draws nothing because its overlay would be *inside* the measurement.
