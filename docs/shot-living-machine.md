# The Living Machine: how the shot is built, and what fought back

The second of the four master scenes re-authored as a shot. It follows the recipe in
[shot-hyperspace.md](shot-hyperspace.md) — manual exposure, a light rig, a procedural sky, a
composition block, a cue arc on the timeline — and hit all five of that note's traps plus three of
its own. The three new ones are worth reading before touching Reassembly or Infinite.

## What it was

Frame 480 of the old version: a featureless white cylinder, ringed by orange discs, floating in
black. Every part of the machine had `emissiveIntensity` between 0.15 and 3.0 and there was no
light rig, no environment and no floor, so emission was doing the work of a material and a key at
once and the frame had one value in it. `mean 0.168, rms 0.267, highlight_frac 0.105`, and the
brightest thing was the machine's *body*.

## The shot

Forty seconds, one continuous move around and in.

| Beat | Time | Intent |
|---|---|---|
| Cold | 0-11 s | Wide and low. The deck runs to the horizon, the piers are barely there, the machine is a small dark drum with a dim slot of light in it. |
| Ignition | 11-23 s | The camera comes in and rises. The belt brightens, the vents start, the rings speed up. |
| Reveal | 23-33 s | Down and around to the left, close enough that the belt fills a third of the frame and the filaments read individually. |
| Settle | 33-40 s | Up and back. The machine returns to a whole object, still burning, still turning. |

The four beats are cue presets morphing over 2, 7, 7 and 6 seconds. They move the internals'
emission, the atmosphere, the bloom and halation, the rig's key, and the three director macros —
nothing else. The macros then fan out to the fields and the emitter. **No parameter is written by
both a cue and a macro**, which is the only reason the arc is predictable.

## Composition

Everything is at metre scale: the machine is 21 units tall, the deck is 560 across.

- **deck / deckPlates** — a 560-unit floor with a 15 x 15 grid of plates on it, a clear circle
  14 units around the machine. It exists so shadows land somewhere and so the eye has a ground
  plane to measure against.
- **piers** — twelve 20 x 220 boxes at radius 152. Cut off by the top of the frame at every point
  in the move. They are the only reason the hall reads as enormous.
- **catwalk / handrail / struts** — a walkway ring at y = 24, radius 24, with a hairline handrail
  at radius 38 on eight supports. A walkway is the one object in frame whose size a viewer already
  knows, so it does more for scale than the piers do.
- **housing / housingCrown** — twenty cast ribs from y 0.6 to 8.6 and twenty more from 12.2 to
  19.2, leaving a 3.6-unit **open belt** in between. This is the subject.
- **capTop / capBase / bands / beltRings** — machined steel caps and four shrink bands, two of
  them closing the belt top and bottom.
- **rings** — four thin polished tori at radius 12.2, tipped a few degrees off axis and turned by
  the vortex field, so they wobble rather than spin invisibly about their own symmetry.
- **filaments** — forty rods of radius 0.06 standing in the belt, backlit. The finest thing in the
  frame, against the largest.
- **core / coreDiscs** — a deformed cylinder and seven fins, the only emissive geometry.
- **pipes / debris** — the near depth layer: pipework and cast-off blocks scattered over the deck
  out to radius 118 and 152, dark, in silhouette.
- **vents** — a low-rate alpha-blended emitter around the base, warm at spawn, cold and gone by
  the end of its life.

The camera is keyed on the timeline with its target held **off the machine's axis** — +5.5 at the
establish, −5 at the reveal — so the subject is never centred and crosses the frame during the
move. Four slow LFOs add a drift the audio never touches.

## Lighting

`examples/lightrigs/industrial-cold.rig.json`, re-aimed from the project rather than edited: a rig
is a shared file and four scenes use it. IndustrialCold ships with its key nearly overhead and
almost on the camera axis (azimuth 20, elevation 68), which front-lights everything in view and
flattens the frame completely. The project moves it to azimuth 118, elevation 34 — a raking
three-quarter backlight — and drops the fill and the ambient:

```json
"lightrig/IndustrialCold/key/azimuth": 118.0,
"lightrig/IndustrialCold/key/elevation": 34.0,
"lightrig/IndustrialCold/ambientIntensity": 0.03
```

The rig's warm sodium practical is moved *into* the machine (distance 0.78 subject radii,
elevation 3, 1900 K) so that the warm wash on the drum and on the deck around it is an actual
light rather than a glow painted on a texture. Emission does not illuminate anything in this
engine; if the internals are to appear to cast light, a practical has to be there.

Exposure is **manual** at f/5.6, 1/50, ISO 400 with +1.7 EV of compensation. The lens is a 30 mm
at f/2 on a full-frame sensor.

## Atmosphere

`volumeDensity` runs 0.0011 to 0.0031 across the arc over a 260-unit march, shaped by a `vent`
radial field so the volumetric belongs to the machine rather than blanketing the hall.
`fogDensity` is 0.0032 to 0.0042 with `fogColor` at 0.016 — see trap 2 below for why that number
matters more than the density does.

## Materials

Two library programs by path and one authored in the scene:

- `../materials/dark-steel.material.json` — caps, catwalk, struts, pipes.
- `../materials/brushed-metal.material.json` — bands, belt rings, rings, filaments, handrail.
  Anisotropic and glossy; it is what makes the polished parts read as a different substance from
  the housing.
- `castIron`, inline — dark iron with a triplanar casting grain, rust in a few stained places and
  a worn metal edge. The library's `oxidisedMetal` was tried first and is wrong for this subject:
  its rust layer covers roughly half the surface in a bright orange-brown, which on an 18-unit
  housing reads as camouflage, not as a casting.

The deck, piers, debris and internals use plain materials. The core deliberately has **no
program**: a program's `emission` output replaces the material's, and the cues and the audio need
`procedural/core/material/emissive` to be live.

## Audio: what actually listens

Twelve routes. Seven are LFOs driving the camera drift, the core's noise deformer, the ring
rotation and the housing's pulse effector — those carry the shot. Three are audio, all through
long envelopes: `audio.rms` to `macros/energy` (400/1500 ms), `audio.spectralCentroid` to
`macros/heat` (500/2000 ms), and `audio.bass` to the core's emission (90/1200 ms). Two are events:
an onset bursts the vents, and `beat.phrasePulse` kicks the wave field's origin so a slow pulse
travels out through the ribs once per phrase. The old version had eleven routes of which every one
was audio, including treble straight onto a material's emission.

## The traps

**1. A slot is a channel, not a window.** The housing was first authored as thirty vertical staves
1.0 wide and 3.2 deep at radius 8. A gap 0.8 wide at the bottom of a 3.2-deep channel only shows
what is behind it from within about nine degrees of the radial, so the core was sealed in from
every angle the camera actually uses — with `procedural/core/material/emissive` set to **20** the
frame did not change by a single pixel value. Hiding the housing showed the core blazing. Ribs 1.25
deep open the same gap to about thirty-three degrees, and a horizontal belt has no acceptance cone
at all. If an interior light is not showing, check the aperture ratio of whatever is in front of it
before touching the emission.

**2. The fog colour is what silhouettes against what.** Distant geometry washes towards
`fogColor`, so a fog colour brighter than the sky behind it turns every distant object into the
*brightest* large shape in the frame. The piers rendered as pale slabs at 0.18 sRGB with a
background of 0.04 behind them, and no amount of lowering their albedo helped: with fog off they
measured 0.02. The fix was the fog colour (0.048 to 0.016), not the density, and a sky whose light
is at the zenith with a dark horizon — which is both what a big interior looks like and what stops
every up-facing surface catching a grazing reflection of the brightest part of the environment.

**3. The frame is darker than it looks.** Twice I "fixed" a deck that read as bright grey and was
actually at 0.09-0.19 sRGB. Against a frame that is 70% shadow the eye reads any large uniform
area as light. Sampling pixels (`tools/image_stats.py`, or reading the PNG directly) settled in
seconds what looking settled wrongly. The reverse also holds: `mid_frac` near 0.84 in one pass was
a real problem the eye had stopped noticing.

Plus the five from the Hyperspace note, all of which applied: cue presets outrank the scene file;
`lightRig` at the top level; the automatic meter opening on a dark frame; volumetrics raising the
black floor; and the `disc` emitter's plane (here it is the right plane, since the vents ring the
base).

## Engine changes

**`materialPrograms` entries may name a file.** `graph` and `lightRig` already accepted either an
inline object or a path; material programs did not, so `examples/materials/*.material.json` existed
as a library that nothing could reference and Reassembly and Infinite carry pasted copies. An entry
that is a string is now resolved relative to the scene file (or through the asset registry) and
loaded by `MaterialProgram::loadFile`; a file with no `name` is named after itself, minus the
`.material` suffix. `--export-bundle` copies referenced material files and rewrites the paths.
Covered by *"Composition takes material programs inline or from a file"* in
`tests/unit/test_composition.cpp`.

**A perf note, not a fix.** The procedural sky is rebuilt whenever its hash changes, and with
`useKeyLight` on and a `followCamera` key the sun direction changes every frame, so a moving camera
rebuilt the cubemap, irradiance and prefiltered chain every frame — about 8 ms. This scene sets
`"useKeyLight": false` (an interior has no sun anyway) and the offline render went from 16.5 to
40 fps. Any scene with a rig and a moving camera is paying this.

## What still does not work

- **Depth layers are inert.** The `composition.layers` block is parsed, validated, hashed and
  serialised, and `CompositionData::layerAt` is called by nothing but a unit test. `density`,
  `contrast`, `saturation` and `detail` reach no generator, no shader and no grade. So are
  `targetScreenPosition` and `framingStrength`. The block in `machine.scene.json` is honest about
  the intent and currently documents nothing that happens; the depth in this shot comes from fog,
  from the near pipes and debris, and from where the camera is put. Only `focalPoints` and
  `exclusions` do real work, through the fields they publish and through light-rig sizing.
- **A scene file's `camera.fov` is a no-op.** `LensSettings::useExplicitFov` defaults false, so
  `effectiveFovY()` takes the focal length and ignores the `fov` the scene names — every example
  scene asks for 44-60 degrees and renders at 37.8 unless the project sets
  `camera/lens/focalLength`. The comment in `scene_types.hpp` says scenes that only set
  `fovYRadians` keep working; they do not. glTF cameras have the same problem
  (`gltf_loader.cpp:570`). I did not fix it because making `fov` win would silently re-frame
  Hyperspace, which is tuned on a 42 mm lens set from its project. This scene sets its lens in the
  project, like Hyperspace does.
- **`--export-bundle` still drops a scene's `lightRig` and `graph`.** `visitSceneFileAssets` and
  `bundleScene` walk node assets, the environment map and (now) material program files, and nothing
  else. Pre-existing; out of scope here, but it means this scene does not bundle.
- **The belt clips.** At the Reveal cue the centre of the belt is at 1.0 with an orange corona
  around it — 0.1% of the frame. It reads as white-hot rather than as blown, but it is clipped, and
  a more careful answer would shape the falloff across the core's height instead of relying on the
  tone mapper.
- **The deck is the largest mid-tone in the frame and it is featureless.** The plate grid gives it
  seams and nothing else; it has no grime, no wear at the plate edges, no puddles. A material
  program with a decal or a cavity mask would earn its place there and I ran out of judgement
  before I ran out of levers.
- **The catwalk struts occasionally bisect the subject.** Eight supports at radius 38 with a 22
  degree offset keeps one off the belt at the four cue positions, but nothing guarantees it through
  the LFO drift, and at around t = 20 s one sits close to the machine's left edge.
- **One catwalk spoke reads as a floating wedge.** The beam pointing towards the lens is
  foreshortened to a triangle and lit warm from below by the belt while the rest of the ring stays
  dark, so between roughly t = 0 and t = 10 s there is an orange shape near the top of the frame
  that does not obviously belong to anything.
- **The piers are a repeating stripe.** Twelve identical boxes on a circle read as a pattern when
  several are in frame at once. They want two or three silhouettes, not one repeated.
- **The vents look like coals, not steam.** The emitter is doing the right thing at the base but
  the particles are large and warm enough that at the Reveal they read as a bed of embers rather
  than as escaping gas. Correct for a furnace; not what the node is called.
- **Nothing in the scene reacts to a phrase visibly enough to notice.** The `beat.phrasePulse`
  route moves the wave field's origin and the resulting ripple through the ribs is, honestly, below
  the threshold of a viewer's attention at these camera distances.

## Measuring

The arc, at 960 x 540, 24 fps, on `tools/make_test_audio.py --seconds 44`:

| Frame | Before (mean / rms / highlight / clipped) | After |
|---|---|---|
| 48 | 0.233 / 0.283 / 0.147 / 0.0125 | 0.080 / 0.101 / 0.004 / 0.0001 |
| 240 | 0.145 / 0.237 / 0.080 / 0 | 0.080 / 0.134 / 0.014 / 0.0002 |
| 480 | 0.168 / 0.267 / 0.105 / 0 | 0.102 / 0.155 / 0.024 / 0.0003 |
| 720 | 0.162 / 0.264 / 0.103 / 0 | 0.070 / 0.141 / 0.018 / 0.0009 |
| 936 | 0.108 / 0.170 / 0.028 / 0 | 0.073 / 0.124 / 0.013 / 0.0002 |

The shape to read is not the mean but the pairing: contrast rises 0.10 → 0.16 through the move
while the mean stays between 0.07 and 0.10 and the highlight fraction climbs from 0.4% to 2.4%.
That is "the frame gets darker and the fire gets bigger", which is what the arc is supposed to be.
Clipping is under 0.1% everywhere; before, the first beat clipped 1.25% of the frame.
