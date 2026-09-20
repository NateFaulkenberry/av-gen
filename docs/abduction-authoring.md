# Authoring an abduction — the canonical sequence and the contract it rests on

**Status:** current as of 2026-09-19 (ADR-384, ADR-385).
**Applies to:** any `stage::ScenarioDesc` in which a craft stops, deploys something, acts on a body
and leaves again. The Glowmere abduction is the worked example; nothing here is Glowmere-specific.

Read `src/stage/staging.hpp` first for what a scenario, a beat, a cue and a step are. This document
is only about the *ordering* — the thing that was wrong, twice, in ways every cheap check passed.

---

## The canonical sequence

```
 1. the craft approaches the abduction location
 2. the craft reaches its final position          <- the endpoint is the authoritative transform
 3. the craft becomes stationary                  <- measured, not assumed
 4. the beam deploys
 5. the craft stays stationary
 6. the animal is lifted
 7. the animal fades out at the top of the beam
 8. the abduction completes
 9. the beam shuts down and drains
10. the craft stays stationary until the beam has stopped emitting
11. the craft resumes its authored departure
```

As beats, which is how the director spells states:

| beat | what it is | the craft |
|---|---|---|
| `acquire` | a `find` binds `target` | wherever it was |
| `approach` | `lookAt` then `moveTo` | **moving** |
| `beam` | the column lights | **stationary, gated** |
| `abduct` | the lift, the glow, the fade, the retire | **stationary** |
| `depart` | the column fades and drains | **stationary** |

There is no separate `STOPPED` state and there does not need to be one: the gate on `beam` *is* the
stop, and it is a measurement rather than a state you can forget to enter.

---

## The five rules, and what enforces each

### 1. A `moveTo` ends where it resolved

Enforced in `staging.cpp`: a `MoveTo` whose written position is more than a centimetre from the
point it resolved returns `StepStatus::Failed` with the distance in the reason.

This is not a formality. It was false for a year: `clearance` was applied to the position written on
*every* frame including the last, while the completion test was the tween parameter, so an approach
with `cruiseClearance` 34 against `hoverHeight` 23 reported `Done` 11.000 m above its destination and
the next beat put the craft where it should already have been, in one frame. See ADR-384 for the
measurement and ADR-385 for the fix.

**What you must not do:** author a `clearance` and then reason about where the move ends. The
clearance is a constraint on the *path* and is zero at both ends of it. If you want the craft to end
high, say so in `height`.

### 2. The beat that deploys the beam declares what has to be still

```json
{ "name": "beam", "stillRoles": ["actor"], "stillSpeed": 0.05, "cues": [ ... ] }
```

While any named role's body is measurably moving, the beat's cues do not advance **and do not age** —
their durations are not consumed by the wait. The gate latches: once the beat has started, the
craft's own authored wobble does not re-close it.

**Why a gate and not just correct ordering:** beats are sequential, so `beam` begins the frame
`approach` ends, and `approach` ends when its `moveTo` says `Done`. A `moveTo` whose goal is a
*walking animal* is glued to that animal at `progress == 1`, so it is travelling at the animal's
speed, exactly on time, having genuinely arrived. Every individual claim is true and the invariant is
false. Measured at 4.264 m/s in one film cycle of five. No amount of authoring care catches that;
only a measurement does.

### 3. The stationary phase holds the transform it was handed, and does not re-resolve it

```json
{ "kind": "follow", "name": "hover", "relative": true, "hold": true, "duration": {"param": "hoverSeconds"} }
```

`relative: true` with no `to` means *hold exactly where you are*. Every beat after the approach uses
this, so the approach's endpoint is the authoritative stationary transform for the whole beam phase —
which is what makes the phase continuous by construction rather than by two numbers agreeing.

**What you must not do:** give a stationary `follow` a `to` role. A station re-resolved from a body
that is still moving is a station that differs from where the craft is, and the craft goes there in
one frame. That is the same defect as rule 1 wearing different clothes; it cost 0.94 m/s at the exact
frame the beam lit, and the fix was deleting `"to": "target"`.

### 4. The beam catches the animal before it lifts it

```json
{ "role": "target", "steps": [
  { "kind": "follow", "name": "caught", "relative": true, "hold": true,
    "duration": {"param": "hoverSeconds"} } ] }
```

A cue in `beam` that pins the target where it stands. Without it the animal walks for the whole
deployment and the lift has to drag it sideways: measured, `test_beam_lab`'s
`worstCornerLate <= bodyReach + kOnAxis` failed by 3.6 mm the moment the craft stopped chasing it.
It is also what the beam should *look* like.

### 5. The fade and the retire are in **one cue, in that order**

```json
{ "role": "target", "steps": [
  { "kind": "set", "name": "glow-rise", "target": "emissiveBoost", "from": 0.0,
    "to": {"param": "glowIntensity"}, "duration": {"param": "glowRiseSeconds"} },
  { "kind": "set", "name": "fade", "target": "opacity", "from": 1.0, "to": 0.0,
    "duration": {"param": "fadeSeconds"}, "ease": true },
  { "kind": "set", "name": "glow-fade", "target": "emissiveBoost", "to": 0.0,
    "duration": {"param": "glowFadeSeconds"} },
  { "kind": "retire", "name": "vanish" } ] }
```

Steps inside a cue are sequential, so `retire` **structurally cannot** run before the fade has
finished. Put the retire in the parallel cue that does the lift and it fires on its own schedule and
can beat the fade to the animal — which is exactly what it did before ADR-385, where `retire` was the
whole disappearance: one frame, fully lit, gone.

`Retire` also puts back every parameter the scenario drove through that role, to the value it found,
before it hides the body. A retired body never comes back, so anything left on it is a photograph of
one run that nothing will ever clear — and a save taken afterwards ships it.

---

## The fade, and its controls

`nodes/<name>/opacity` is an ordinary node parameter, so it is keyframeable, modulatable and
presettable like any other. It multiplies the asset's own `material.opacity`, and — the half that
matters — it **promotes the entity's `alphaMode` to `Blend`** while it is under 1 and puts the
asset's own mode back when it is not. `shaders/pbr_shade.wgsl` reads

```wgsl
let alpha = select(1.0, baseColor.a, alphaMode > 1.5);
```

so an OPAQUE material's alpha is discarded outright, and every farm GLB in the repository is
authored OPAQUE. Driving the number alone renders a perfectly solid animal.

The brief's four controls, as they exist:

| the brief | here |
|---|---|
| Fade Duration | `fadeSeconds`, a scenario parameter |
| Fade Start | where the `fade` step sits in the cue — after `glow-rise`, so it begins as the glow peaks |
| Fade Curve | `"ease": true` on the `set` step: smoothstep instead of linear |
| Fade Strength | the `to` value. 0 is a full dissolve; 0.15 leaves a ghost |

The look is not a plain transparency ramp: `glow-rise` takes `emissiveBoost` to `glowIntensity`
first, so what dissolves is an already-glowing body, and the residual glow is taken back down
afterwards. That is built from parameters that already existed; no new VFX system.

**Author the three durations to land with the lift.** `glowRiseSeconds + fadeSeconds +
glowFadeSeconds` should equal `abductSeconds`. If the sum is longer the beat simply waits and the
craft holds a moment more, which is safe; if it is shorter the animal hangs at the top, visible,
having already been retired — which is not.

---

## Before you ship a change to any of this

* `avgen_tests "[sequence]"` — the eight regression cases in
  `tests/unit/test_abduction_sequence.cpp`. They load the **project**, not the scene, because a
  project's parameters are applied over its scene (ADR-264) and a test of the scene alone is a test
  of a film that does not ship.
* `avgen_tests "[staging],[abduction],[beam],[director]"` — 127 cases, including the alignment and
  particle-drain work this sequencing sits on top of.
* `avgen_tests "[.abduction-instrument]"` — hidden, prints rather than asserts. The per-beat table,
  the per-frame discontinuity list, the T-1.0/T-0.5/T/T+0.1 table around every beam activation, and
  a control arm. Read it when a number moves and you want to know which beat moved it.
* `python3 tools/check_project_integrity.py` — and if you edited a scene,
  `python3 tools/refresh_scene_fingerprint.py examples/world/<scene>.scene.json` first. Editing a
  scene makes every project that references it stale, including nine `_diag-water-*` files nobody
  remembers.

And the standing one, which is not a test: **render it and look.** A black frame and a nearly-black
frame are identical in a hash, and an opacity that never reaches the blend pipeline is identical to
one that does in every number on the CPU.
