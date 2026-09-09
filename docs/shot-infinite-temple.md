# The Infinite Temple: how the shot is built, and what is still wrong with it

The second of the four master scenes re-authored as a shot. Written in the same spirit as
`docs/shot-hyperspace.md`: the beats and the reasoning first, then an honest list of what broke and
what still does not work. All five of Hyperspace's traps applied here; two more turned up.

## The shot

Sixty seconds, one continuous move: a wide on a ruined circular temple, a walk in through the
colonnade to the light at its centre, a hold, and a crane back out.

| Beat | Time | Camera | Intent |
|---|---|---|---|
| Threshold | 0-12 s | r = 340, y = 22 | The temple is a small dark ring on an empty plain. Long shadow bars run toward the lens. Quiet, mean luminance 0.07. |
| Procession | 12-30 s | r = 340 → 85, y → 7 | The colonnade grows past the frame edges and the altar's light appears through the gaps. |
| Sanctum | 30-46 s | r ≈ 41, y ≈ 8 | Inside. Three glowing rings, dust in the light, a near column in silhouette on each side. The peak of the arc: mean 0.18, RMS contrast 0.26. |
| Ascent | 46-60 s | r = 41 → 200, y → 66 | The columns wipe past as the camera rises and pulls back; the temple closes the shot as a whole object with its lintel, the ring structure above it, and the altar still burning inside. |

The beats are four cue presets on the project timeline (morphing over 0, 8, 8 and 9 seconds) plus
six keyed camera tracks. A preset moves the seven world macros, the rig's key intensity, the
exposure compensation, bloom, halation, the vignette and the altar's emission together, so a beat
is one decision rather than a dozen.

## Composition

Five jobs, five distances:

- **floor** — 1400 units square, the only reason the shadows exist as an image. It has to reach
  past the tower ring at 520 or the towers stand on a black void and read as cardboard.
- **fragments** — 18 fallen blocks at r ≈ 104, the near layer, almost pure silhouette
  (baseColor 0.075).
- **columns / innerColumns / arches** — the subject's architecture: 20 outer columns at r = 52
  standing 52 high, 12 inner at r = 30, and one square-section stone ring at y = 54.5 that ties the
  outer colonnade together. The lintel is what makes a ring of cylinders read as a building.
- **corridor** — 210 ruined blocks spiralling from r = 150 to 380, the mid layer, the thing that
  says the temple stands in a landscape.
- **chambers** — 19 towers at r = 520, the far layer, almost entirely fog.

The composition block's focal point ("altar", radius 26, clearance 15) does three real jobs: it
sizes the light rig (`expand()` places lights in subject radii around it), it publishes
`composition.clearance.altar` and `composition.weight.altar` as fields, and `camera/focus/mode: 2`
focuses the lens on it, so the altar is the sharp thing whatever the camera does.

## Lighting

`examples/lightrigs/colonnade.rig.json`, a new rig:

- **sun** — a 2500 K directional at elevation 15, azimuth −104 measured from world +Z with
  `followCamera: false`. It belongs to the world, not to the shot, so the shadows stay put while
  the camera moves through them. At 15 degrees a 52-unit column throws a 190-unit bar; those bars
  are the picture in the wide beats.
- **altar** — a 2100 K point light at the focal point with `volumetric: 1.0`. It is the only light
  in the haze, so the visible scattering is a halo around the subject rather than a veil over the
  frame (Hyperspace's trap 4, restated).
- **sky / rim** — an 8800 K rect fill and a 9600 K rim, both camera-relative, at 0.07 and 0.18 of
  the key. They are what make warm stone read against cool shadow.
- **ambient** — 0.10 at 9200 K. Any more and the shadow bars stop existing.

Exposure is manual (`camera/exposure/mode: 0`) with the defaults, which give an exposure scale of
exactly 1; `camera/exposure/compensation` is the per-beat stop and runs −0.20 to −0.05. The
automatic meter opens up on an intentionally dark frame, exactly as it did in Hyperspace.

## Atmosphere

`volumeDensity` runs 0.0006 to 0.0011 across the shot (macro range 0.00018-0.0016 over a
300-unit march) and the distance fog 0.0024-0.0033. The distance fog does the aerial perspective;
the volume pass does the halo at the altar and the dust shafts near the floor.

Two things had to be true before the fog stopped destroying the frame:

- `fogColor` has to be close to the sky (it is `[0.011, 0.012, 0.019]` against a zenith of
  `[0.004, 0.006, 0.016]`). At the previous `[0.030, 0.038, 0.058]` every distant object faded
  *up* to a pale slate that was brighter than the sky behind it, and the far towers read as
  cardboard cutouts pasted on black.
- per-light volumetric strength had to actually work; see below.

**There are no true light shafts.** The volume march does not sample shadow maps
(`shaders/volume.wgsl` has no shadow term), so a column cannot cut a beam out of the fog. What the
scene delivers instead is shadow *bars on the floor* from the cascaded shadow maps, which is the
same idea done with the mechanism that exists. The brief asked for shafts through the colonnade;
this is the honest substitute, not the thing itself.

## Audio

Eleven routes, against twenty before:

- **six autonomous** — four slow LFOs (0.007-0.033 Hz) drifting the camera position and target and
  the altar's rotation and the ring structure. Nothing in audio touches them.
- **three shading** — `audio.rms` → `macros/energy` (220 ms attack, 1.6 s decay),
  `audio.spectralCentroid` → `macros/color`, `audio.bass` → the altar's emission. All slow enough
  that they read as breathing, not as flashing.
- **two punctuation** — `audio.onset` bursts 220 dust particles, `beat.bar` gives the altar rings a
  small scale kick.

Everything else moves because a field, a deformer or the timeline moves it.

## Two engine bugs, one fixed

**`volumetricStrength` was a silent no-op.** `scene::PunctualLight::volumetricStrength` and a light
rig's per-light `volumetric` were parsed, hashed, serialised, packed into `GpuLight::extra.z` by
`rendering::packLight` — and read by no shader. `shaders/volume.wgsl` lit the fog from
`frame.lights[0]` at full strength whatever any rig asked for. This is the same family of bug as
Hyperspace's `sourceTransform` and top-level `lightRig`: a JSON key that validates, round-trips and
does nothing.

Fixed: `LightUniform::cone.z` now carries the strength (the slot was zero padding), and the march
sums in-scatter over every enabled light weighted by it. `PunctualLight::volumetricStrength`'s
default changed from 0 to 1 — a light lights the air unless told otherwise — so the scenes with
hand-authored or default key lights (machine, reassembly) are unchanged; rigs keep their own
default of 0 and opt each source in. Two regression tests in `tests/rendering/test_volume_gpu.cpp`
cover it; both fail against the old shader.

This changes Hyperspace, which uses `void-core.rig.json` (key `volumetric: 0.12`, practical
`volumetric: 1.0`). Its corridor interior goes from a grey veil to real black — mean 0.151 → 0.096,
median 0.098 → 0.026, saturation 0.61 → 0.80, RMS contrast essentially unchanged at 0.16 → 0.15 —
and the halo at the source survives. It is an improvement, but it is a change to someone else's
finished shot and should be looked at before merging.

**Composition depth layers do nothing.** `CompositionData::layers`, `targetScreenPosition` and
`framingStrength` are parsed, validated, hashed and serialised, and nothing in `src/` reads them:
`layerAt()` has no callers and neither does `framingStrength`. The brief asked for the four depth
layers to "do real work"; they cannot, today. I chose not to build a depth-layer system for one
scene — the layer block is still in the file as the record of the intent, and the depth
relationship is authored by hand instead (per-node albedo, scale and the fog curve). This is a real
gap and the block is currently misleading to anyone reading a scene file.

## What went wrong on the way

1. **A ring of columns seen from outside is a picket fence.** 34 columns at r = 46 formed a solid
   drum; nothing behind them was ever visible, including the altar the composition declares as the
   subject. 20 columns at r = 52 have gaps you can see the light through. Fewer objects, more
   image.
2. **I misjudged the scale of the frame by about 2x.** The temple is 104 units across; at the old
   opening distance of 168 units it already filled 85 percent of the frame. "Wide" for this
   building is 340 units. Every early attempt at an establishing shot was actually a medium.
3. **The `arches` node was 20 bent boxes** that read as small black hooks from any raised angle.
   One torus with `minorSegments: 4` at r = 52 is a square-section stone architrave, reads as
   architecture, and is 19 fewer instances.
4. **Point-primitive motes render as flat camera-facing quads.** At `pointSize` 0.42 with a scale
   effector on top they were legible cards, not dust — the "blizzard" was as much a size problem as
   a count problem. They are now `pointSize` 0.12 with a probability filter at 0.075, about 60
   instances (down from several hundred), sized and brightened by `composition.weight.altar` and
   scaled to zero inside
   `composition.clearance.altar` by a `scale`/`multiply` effector.
5. **`filterAttribute` can never see a composition field.** The old motes node had
   `filterAttribute` on `composition.clearance.altar`, which fired a warning every frame: point ops
   run at rebuild against point *attributes*, and clearance is published as a *field*. An effector
   is the mechanism; the warning is gone.
6. **The procedural sky's `showBackground` defaults to false**, so the scene was drawing the flat
   `background` colour and the sky existed only as IBL. Turning it on gave the shot its horizon,
   its silhouette and its sun — and then immediately over-lit everything until `sky.intensity` came
   down to 0.10.
7. **Cue presets outrank the scene file** (Hyperspace's trap 1) and world macro targets outrank
   cue presets, because a macro target is a route applied every frame. Two of my presets set
   `scene/volumeDensity` directly while `macros/atmosphere` also targeted it; the preset value did
   nothing. Ownership is now disjoint: macros own the seven knobs' targets, presets own the macros
   plus the rig intensity, exposure, bloom, halation, vignette and the altar's emission.

## What still does not work

- **The far layer is the weakest part of the frame.** The 19 towers at r = 520 still read as flat
  dark slabs rather than as buildings; they have no silhouette detail and no ground shadow at that
  distance, and in the wide beats they form a slightly regular ring. They are better than the
  bright cutouts they were, but they are set dressing, not depth.
- **The ruin field is too regular.** `corridor` is a spiral distribution, so the blocks fall on
  visible arcs on the floor in the closing crane. It wants scattering, not a spiral.
- **The Ascent beat's first four seconds are a mess.** The pull-back leaves the sanctum through the
  colonnade, so around t = 50-54 s the near columns sweep across the whole frame and the altar is
  hidden behind whichever one happens to be centred. It reads as motion rather than as a shot.
- **The altar is a light, not an object.** Three glowing tori with nothing between them: at the
  Sanctum beat the core clips to flat white and there is no form to look at inside the glow. It
  carries the frame because it is the only bright thing, which is not the same as being good.
- **There are no light shafts**, for the reason above. The single highest-value image the brief
  asked for is not in the shot.
- **The floor's `weatheredStone` never reads.** The program is on it, but at a grazing angle under
  a low key the triplanar and micro-detail are below a pixel; the floor is effectively a flat
  albedo. On the columns at close range it works well.
- **The mote billboards are still visibly square** when one crosses a bright background. They want
  a soft radial falloff, which point primitives do not have.
- **The 60-second arc is measured, not felt.** Mean luminance 0.070 → 0.131 → 0.096 → 0.179 →
  0.094 → 0.104 with contrast peaking at 0.26 at the arrival and no clipping anywhere. That is the
  shape I intended, but the numbers say nothing about whether the Procession beat earns its
  eighteen seconds, and I think it does not: it is one continuous dolly with no event in it.

## An accident worth knowing about

The scene used to rebuild the procedural sky *every frame* — 8-12 ms of the frame budget — because
the sky's sun direction follows the key light and the key light was `followCamera: true`, so it
moved a fraction of a degree per frame and invalidated the cube. Moving the sun to
`followCamera: false` (done for the shadows) also stopped that: the sky now builds twice per render
job. Any world whose key follows the camera is paying this cost.

## Reproducing

```
python3 tools/make_test_audio.py /tmp/track_temple.wav --seconds 64 --bpm 96
./build/debug/src/avgen --headless --project examples/infinite/infinite.json \
  --audio /tmp/track_temple.wav --render /tmp/temple --range 0:60 --format png
python3 tools/image_stats.py /tmp/temple/frame_000900.png
```

The project renders 1920x1080 at 30 fps. For iteration, copy it and override
`render.width/height/fps` to 960x540 at 24; a 60-second preview sequence takes about 32 seconds.

## The Ascent is an edit, not a move

The final beat used to pull back from the altar, which meant leaving through the ring of columns:
whichever column centred hid the subject, and for four seconds the frame was a picket fence. Rising
first only moved the problem to the architrave. Pulling out from inside a colonnade always exits
through the colonnade.

The answer is a cut. The sanctum framing holds to 50 s on a `step` key, and the shot cuts to a wide
outside the temple and cranes up and back from there. `KeyInterp::Step` on a camera track is the
timeline's cut, and it is a better tool for this beat than any camera path.
