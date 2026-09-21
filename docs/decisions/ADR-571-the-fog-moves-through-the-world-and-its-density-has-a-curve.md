# ADR-571: the fog moves through the world, and its density finally has a curve

Status: accepted. Date: 2026-09-21. Phase E of the Fog Bank brief (§15, §16, §17) and §24.

## Context

Three sections of the brief were unreachable in the same way, and the audit that found the third is
the one worth keeping.

**§15/§16, movement.** The macro detail drifted by adding an offset in *noise space*:

    fbm3(p * scale + vec3(drift, drift * 0.3, -drift * 0.7))

Two things are wrong with that and only one of them is visible. The direction is **hard-coded**, so
§16's first control, Drift Direction, did not exist. And because the offset is added after the
scale, its speed in metres per second is `drift / scale`, where `scale` is itself divided by the
bank's radius -- so **one number meant a different speed in every bank**, and the same defect
ADR-564 found in `density` was sitting in `detailDrift`.

**§24, the density curve.** The fog bank declares a `Contrast` row. It has since the kind existed,
and all three of its shipped styles set it -- Valley Mist 1.5, Glowmere Haze 2.6, Dense Bank 3.4.
Since ADR-563 gave the fog its own field, **`fogShapeAt` has never read the number.** An artist
loading "Dense Bank" got a contrast of 3.4 that did nothing at all. Same family as ADR-561's eye
hole and ADR-565's `detailAmount`: declared, set by a preset, unreachable.

**And the census below is the mechanism, not a coincidence beside it.** A dead control in an effect
that **no shipped scene instantiates** cannot be noticed by anybody, because nothing renders the
code path it is dead in. Four of this kind's controls died; the reason all four survived is the
same one, and it is structural rather than a run of bad luck. The cheapest guard against the fifth
is a shipped scene that uses the kind -- which is now the last line of this ADR's revisit list and
is worth more than any test written against the field in isolation.

**And a fact that explains the pattern -- stated with the command that actually produces it,
because the first version of this paragraph did not.**

```sh
grep -rl --include='*.scene.json' '"kind": *"fog"' examples/ | grep -v '/_'   # -> 0
```

**No shipped scene has ever contained a fog bank.** That is why four of this kind's controls have
been able to die unnoticed, and why making the third one live changes no shipped frame.

The published version of this claim quoted `grep -rl '"kind": "fog"' examples/`, which is **not the
command that was run** -- the real one also filtered `/_`, and without that filter it returns 52,
because `examples/treeisland/` is full of this branch's own `_fog-*.json` probe arms. So the record
said "run this, get zero" and running it gets 52. **The finding was right and the evidence
published for it was not**, which is worse than a wrong finding: it is a wrong finding waiting to
be discovered by someone who trusted the paragraph.

Two consequences, and the second is the durable one:

- the 41 tracked `_fog-*.json` probe arms are **untracked** as of this ADR. They are derived,
  `tools/make_fog_arms.py` regenerates them in a second, and `.gitignore` already covered them --
  the patterns simply do not apply to files already in the index. They were findable by anyone's
  census of shipped content and would have kept producing this error;
- **a census of shipped content must say what "shipped" means in the command.** The repository has
  119 tracked `_`-prefixed JSON files and 30 `_`-prefixed `.scene.json` files across several
  agents' probe sets, so `--include='*.scene.json'` and `grep -v '/_'` are both load-bearing. A
  census whose filter lives in the analyst's head and not in the published command is a
  measurement contaminated by the measurer.

## Decision

### §15/§16: an advection, in metres per second, along the bank's own axis

§16 gives the implementation in one line -- *"sample the density field at `position − velocity ×
time`"* -- and written that way the field satisfies

    detail(p + v·dt, t + dt) == detail(p, t)

**exactly**, for every p, v and dt. That identity *is* §15: the structure is carried through the
world rather than regenerated in place, and a field that shimmers cannot satisfy it for any dt.
`tests/unit/test_fog_flow.cpp` asserts the identity rather than describing the motion, with the
control that stops it being satisfied by a field that never moves at all.

`driftSpeed` and `driftVertical` are metres a second. The direction is the bank's own long axis
(`bankRotation`), which costs no control and is the answer an artist expects: a bank lying along a
valley drifts along the valley. `detailDrift` is **removed rather than aliased** (ADR-441).

### §24: a response curve whose defaults are the identity

    s = max(raw − threshold, 0) / (1 − threshold)      // identity at threshold 0
    s = mix(s, smoothstep(0, 1, s), softness)          // identity at softness 0
    s = pow(s, contrast)                               // identity at contrast 1

Three terms, each the identity at its default, so §24 is something an artist opts into rather than
something they have to undo -- and `contrast` reaches the field for the first time since ADR-563.

### The lanes, and the audit that made them free

Both features needed packed space and the fog block looked full at lane 15. It was not:

```sh
grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl | sort -u
```

Lanes **2, 6 and 8** have exactly one reader in the entire march -- `mediumVortexUniforms`, which
feeds `vortexShapeAt`, which is the arm a fog bank never takes. So lane 2 became the drift velocity
and lane 6 the density curve, and the per-kind lane map in `volumetric_fog_effect.cpp` records it.

**That is ADR-562 §9's rule used the right way round for once**: *grep the lane index, not the
feature*, run **before** writing rather than after something broke. The same three greps that found
three defects after the fact found three free lanes before one.

### §17 is NOT done, and it needs a decision rather than a workaround

§17 asks the fog to optionally respond to a flow field. The engine has one authoritative global
wind (ADR-387 §18), and `shaders/volume.wgsl` already reaches it: `common.wgsl` includes
`wind.wgsl`, which offers `windSampleAt(p, t)` off the frame uniform. **The wind is available in
the march for free, with no new binding.**

What is missing is that the drift velocity is computed in the **packer**, and
`EffectResolve::pack(const AtmosphericEffect&, float envelope, MediumSlot&)` is not handed the
resolved flow -- `buildAtmosphericFrame` has `rv.flow` and `rv.flowInfluence` right beside the call
and does not pass them. Coupling the drift to the wind therefore needs **a signature change to a
shared registry hook**, which every kind's packer implements, including `agent/tornado`'s on its
branch.

I have not made that change. It is a one-line edit per packer and it would resolve cleanly, but it
is a shared surface and the tornado is parked, so it is a decision rather than a detail. The
alternative -- sampling the wind inside `fog.wgsl` -- is worse and was rejected: `fog.wgsl`
declares no bindings on purpose, which is what lets the parity harness compile it alone, and
reaching for `frame` inside it would trade a testable transliteration for a convenience.

## Consequences

- **The fog block is thirteen stored rows** (`grep -c '    storedFloat("'` = 12, plus one
  `storedChoice`), up from ten. `test_effect_registry` notices, as designed.
- **Break demonstrations.** Restoring the pre-ADR-571 noise-space offset fails the advection
  identity at 1.098 against 1.003. Packing the drift without the rotation fails the direction case
  at 5.0 against 0.0. Both are in the test's own comments.
- **A case of mine failed correctly and taught the lesson in its own comment.** The first version
  of the contrast assertion sampled the *centre* of the sphere, where the raw density is 1.0 --
  and 0 and 1 are fixed points of `x^k`, so both arms returned 1.0 and the case failed. **A curve
  has to be measured where the curve is.** It samples the rim now.
- **`docs/testing.md` 26**, which cost a rebuild cycle: restoring a file after a break
  demonstration with `git checkout --` restores it to *HEAD*, not to where it was, and silently
  deletes any uncommitted work in it. The next scripted edit then anchored on a deleted line and
  did nothing, also silently. What caught it was `grep -c 'storedFloat("'` coming back 9 where it
  should have been 12 -- a count taken from the code, one minute after the fact instead of forty.

## Revisit when

- **§17's flow coupling is decided.** The change is `EffectResolve::pack` gaining the resolved
  flow; the wind is already in the march and already in `buildAtmosphericFrame`.
- **§16's remaining controls are wanted.** Turbulence, Curl, Swirl, Dissipation, Expansion and
  Contraction are all still missing. Lane 8 is free by the same audit, which is four more floats;
  Curl in particular wants a vector field rather than a constant velocity and is a different shape
  of change.
- **A fog bank appears in a shipped scene.** It never has, and that is why this kind's controls
  keep turning out to be dead. The cheapest guard against the next one is a scene that uses it.
