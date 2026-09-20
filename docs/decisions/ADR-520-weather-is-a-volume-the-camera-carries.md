# ADR-520: Weather is a volume the camera carries, and a particle may have a second life

- Status: Accepted (2026-09-20)
- Extends ADR-015/040 (GPU particles), ADR-025 (field forces), ADR-055 (the wind field),
  ADR-360 (the spawn nonce and the warm-up), ADR-367 (soft particles), ADR-370 (leaf cards).

*Numbered 520: this branch holds 520-539.*

## Problem

Roughly seventy world effects were commissioned and the natural and weather family — rain, snow,
ashfall, a weather front, dust motes, fireflies, pollen — was none of them. Nothing named rain,
snow, ash, pollen, season, vine or overgrowth exists anywhere in `src/`, `shaders/`, `examples/` or
`docs/decisions/`, so nothing constrained how they got built.

What *did* exist was very good and very nearly enough: ADR-015's deterministic pool, ADR-040's
curves, trails, stretching and fog coupling, ADR-025's field forces, ADR-055's wind and ADR-370's
leaf card. Seven things were missing, and each of them is the difference between a weather effect
that works and one that does not.

### 1. A weather system is everywhere, and a pool is finite

An emitter is a box somewhere in the world. Rain is not somewhere; it is everywhere, and what the
camera can see of it is a small moving slice. Authoring a box big enough to cover a shot means
spending a 34 000-particle pool over a square kilometre, at which point there are four drops in
frame. Authoring a small box means the shot leaves it behind the moment the camera moves.

### 2. Recycling by lifetime makes weather fade in and out

Without wrapping, a drop has to be *born* and to *die* inside the frame, so its opacity has to ramp
at both ends to hide the respawn. A field of particles fading in at the top of the frame is the
single most recognisable tell of computer rain.

### 3. Nothing happens when a particle arrives

Particles fall through the ground. Rain has no impact, snow has no settling, grit has no bounce.

### 4. The per-particle size was `mix(0.7, 1.3, r)`

Hard-coded, uniform, and uniform randomness is the quality bar's named failure.

### 5. Every particle in a system falls at the same speed

One `drag` gives one terminal velocity. A field descending in lockstep is "identical particle
motion", also named, and turbulence does not fix it: turbulence perturbs a speed, it does not vary
one.

### 6. A particle cannot blink

Embers do not glow steadily and fireflies do not glow at all — they flash, and the interesting ones
flash *together*.

### 7. A mote is invisible

Dust is not visible because it is bright. It is visible because it is between you and a light, and
nothing in the particle system knew where the light was.

## Decisions

### 1. `volumeFollow` and `volumeWrap`

`volumeFollow` is a per-axis 0..1 weight: the emitter centre becomes
`position + cameraPosition * volumeFollow`. Per axis, so rain can track the camera across a valley
in x and z while staying pinned to the sky in y. All zero is the world-anchored emitter this always
had.

`volumeWrap` re-enters a particle on the opposite face of the box when it leaves. It is a pure
function of position, so it costs determinism nothing, and it is what makes a *steady* fall
possible: lifetime stops being the recycling mechanism, so nothing has to fade.

Wrapping and the splash response below are deliberately independent rather than combined. A drop
that becomes a ring is consumed, and a wrapping system's drops are eternal; an author who wants
both runs two systems — the body of the shower, and a shallow impact layer near the ground. That
is two simple primitives instead of one feature with a special case in it, and it is also better
authoring, because the ripple density then does not have to equal the drop density.

### 2. A collision response, and a splash is a SECOND LIFE

`CollisionResponse { None, Kill, Bounce, Splash }` against `collisionHeight`.

`Splash` re-uses the particle rather than spawning new ones: the drop is reborn in place with
`age = 0`, `life = splashLifetime` and `stage = 1`, and a stage-1 particle draws as a
**ground-aligned expanding ring** whose radius grows as `sqrt(t)`.

One particle, not a burst, for two reasons. The pool is fixed (ADR-015), so spending eight slots on
every impact means a downpour with an eighth of the drops. And a ring is what the eye actually
reads as "something landed there" — three additive dots flying upward is not.

A stage-1 particle returns from `cs_simulate` before the forces, because gravity pulling a ring
back through the plane it is lying on would re-trigger the response every frame and it would never
die. `Particle::pad` became `Particle::stage`, so the struct's size and layout are unchanged and a
system with no collision writes 0 exactly where it used to write 0.

### 3. `sizeVariance` and `sizeSkew`

`variance 0.3, skew 1` is bit-for-bit the `mix(0.7, 1.3, r)` this was. Skew above 1 pushes the
distribution down and gives many small and a few large, which is what a real population of drops,
flakes, motes and embers looks like.

### 4. `dragSizeBias`: scale drives speed

The effective drag blends towards `drag / size`, so a particle drawn large by `sizeVariance` is
also drawn fast. The two variations are then *correlated* the way they are in the world rather than
independent the way two random numbers are. In the snow scene this is the single biggest difference
between a field that reads as falling and one that reads as scrolling.

### 5. One pulse with a `sync` knob

`pulseRate`, `pulseDepth`, `pulseSync`, `pulseSharpness`. `sync` is the whole reason this is one
feature and not two: at 0 every particle is on its own phase, which is an ember bed; at 1 they
share one phase, which is a firefly chorus. `sharpness` is the exponent that turns the sine into a
brief flash with darkness between.

### 6. `clusterCount` and `clusterRadius`

Spawn positions are drawn from N centres instead of uniformly. A centre is a pure function of its
index — and of the system seed, and **not of the frame**. The first version used `rand3`, which
mixes the frame nonce, and the clusters resampled themselves sixty times a second: a uniform field
wearing a costume. The offset the emitter shape produced is re-used as the *cluster's* place rather
than the particle's, so clusters are distributed the way the author shaped the emitter; drawing
cluster centres from their own uniform box would have silently ignored a disc emitter.

### 7. `pauseRate` and `pauseFraction`

A per-particle gate that heavily damps the velocity for a share of its cycle. Flying insects do not
cruise; they move, stop, hang and move again, and at any instant part of a real swarm is still.

### 8. `scatterStrength` and `scatterAnisotropy`, and the sign error in them

A Henyey-Greenstein phase function of the angle between the view ray and the key light, so the same
population is quiet across the light and blazes when you look into it. The key light arrives
through `ParticleFrameContext` from `scene::resolveSky`, which is the same call the sky disc is
drawn from — one rule about which light the sun is, rather than a second rule that can disagree
with the first. An all-zero direction switches the term off rather than normalising a zero vector.

**The gain was wrong, it was found by eye, and then it was measured.** The first version was
`1 + strength * (4*pi*hg - 1)`. That is the *correct* normalisation of a phase function — isotropic
at strength 1 is exactly no change — and it is the wrong artist control, because a phase function
redistributes a fixed amount of light and everything outside the forward lobe therefore comes out
darker. At the strengths this effect wants — 5 and up, because the point is a mote that blazes —
the bracket falls below `-1/strength` and the whole thing clamps to zero:

| scatterStrength | pixels above 200 in `dust-motes.scene.json` |
|---|---|
| 0.0 | 74 |
| 1.0 | 0 |

The motes had not become dimmer. They had been multiplied by a negative number and clamped away.

It is now `1 + strength * 4*pi*hg`. A real mote is lit by the whole sky as well as by the key
light, so it keeps what it had and the key light *adds*. `strength` is monotone — more is always
more — and 0 is still exactly off, which is the property that keeps every existing system
bit-identical. The sun's **hue** is used and not its radiance: `sunColor` already carries an
intensity that is routinely 8, and multiplying a particle by that would have turned "tint it
slightly like the sun" into "make it eight times brighter", which is the unit bug this repository
has now bought five times.

### 9. `emitMaskField`

A scalar `FieldSpec` sampled at the spawn position; the spawn survives with probability equal to
the sample. Soft-edged by construction, which is what a weather front needs — ADR-025's `Kill`
force gives a hard wall of rain with nothing in front of it, and a front arrives gradually. A slot
that loses the roll is born dead and returns to the free list the same frame, so a mask that is
currently zero everywhere does not hold the pool hostage. A name the scene does not define leaves
the mask **off** rather than zero, because "I typed the field name wrong" and "it is raining
nowhere" must not look identical.

## What this did NOT need

No new wind field (ADR-396 has the only one, and `particles.wgsl` already includes it). No new
audio path. No second ray march. No new atmospherics kind — every effect in this family is a
particle system, and `src/world/atmospherics.*` was not touched, which is what let this land beside
the registry work instead of on top of it.

## Reachability

Thirteen of the new numbers are registered parameters, all of them in `forEachParticleParam` in
declaration order (the comment there says why), and all of them drawn by the Edit panel's particle
section from `ui::particleWeatherRows()`. That table lives in `src/ui/particle_weather_rows.cpp`
and not in `world_edit_panel.cpp` for the reason `tests/CMakeLists.txt` states about that file: the
drawing half is not on the unit-test source list, and a table only the drawing half can see is a
table no test can check. `tests/unit/test_particle_weather.cpp` walks the same table and computes
the same paths, so §77's empty box cannot happen silently.

`volumeWrap`, `collision`, `clusterCount` and `emitMaskField` are structural rather than
modulatable and stay scene data, like `shape` and `capacity`.

## Cost

Nothing in this ADR adds a pass, a binding or a march. `cs_simulate` gains three branches, all of
which are `if (x > 0)` on a uniform and therefore coherent across the whole dispatch;
`vs_particle` gains two multiplies and one branch; `fs_particle` gains one branch. The particle
compute budget was 0.26–0.79 ms before and no weather scene in `examples/weather/` moves it out of
that band.

## Verification

- `tests/unit/test_particle_weather.cpp` — save/load round trip through `Composition::saveFile`,
  the default-writes-nothing case, panel-path parity, `applyParticleParameters` coverage, and the
  three validation refusals with their valid neighbours.
- `tests/rendering/test_particle_weather_gpu.cpp` — three arm/control pairs. Each control is byte
  equality, because "off is exactly what it was" is the claim that keeps every existing scene safe,
  and each arm has a premise assertion that the cloud is actually on screen, because a black frame
  and a nearly-black frame are identical in a hash.
- `examples/labs/particle-vfx-lab.scene.json` at t = 2 s renders to sequence hash
  `e52b7fefaa395bcc` on main and after the substrate landed.
