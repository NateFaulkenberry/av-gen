# ADR-573: an effect with no control is the same defect as a control with no effect

Status: accepted. Date: 2026-09-21. The fog brief's §27, and an audit of §19, §25 and §26.

## Context

This batch was meant to be an assessment: check §18, §19, §25, §26 and §27 against the code rather
than assume, because assuming is what produced four dead knobs in this kind. Two of the five turned
out to be satisfied and one turned out to be the mirror image of the defect I had been finding.

**§19, depth integration: satisfied, and checked rather than believed.** `fs_march` clamps its
`maxDistance` to the distance to the first surface (`worldAt(ndc, sceneZ)`), so fog stops at
geometry; `fs_composite` upsamples with a depth-aware bilinear filter that weights each neighbour
by how close the depth it marched is to this pixel's, so fog does not bleed across silhouettes.
Camera → fog → tree and camera → tree → fog both integrate correctly. Nothing to build.

**§27, local light response: the capability exists, has shipped for months, and no artist can
reach it.**

## The finding

`Environment::volumeLocalLights` gates the clustered local lights' contribution to the march
(ADR-053). It defaults to 0. It is read from a scene file, written to a scene file, and uploaded to
the shader in `glow.y`.

**It had no registered parameter.** No parameter means no panel row, no automation curve, no
modulation route, and no way to change it other than editing JSON by hand. `volumeMaxDistance` --
how far the march goes -- was in the same state.

Both are used:

| control | shipped scenes with it above 0, of 90 |
|---|---|
| `volumeLocalLights` | **12**, including every Glowmere scene |
| `volumeMaxDistance` | **32** |

Forty-four scene-settings, every one of them typed into a file by hand because the editor had no
row for them. §27 calls local lights in fog *"a major visual upgrade for Glowmere and Tree of
Life"*; Glowmere has been using it since before the brief, invisibly.

**This is the mirror of the defect this branch keeps finding.** ADR-561's eye hole, ADR-565's
`detailAmount`, ADR-571's `Contrast`: a control the artist can move that reaches nothing. This is a
thing that reaches the picture and the artist cannot move. Both are "declared and disconnected",
and ADR-421's rule -- *a control that does nothing teaches an artist that the system is broken* --
has an equally expensive converse: **a capability with no control teaches an artist that the system
cannot do it.**

## Decision

`volumeLocalLights` and `volumeMaxDistance` are registered parameters, drawn in the Environment
panel beside the rest of the volumetrics. Nothing else changes: both keep their defaults and both
serialise as they did, so every shipped scene renders identically.

`fogHeightAmount` is in the same state and is **deliberately left alone**. Its assignment carries a
comment saying the omission was a decision -- *"nothing about them is animated, so they are copied,
not picked"* -- and it is ADR-058's surface rather than this brief's. It is named here because that
reason is a claim about today: it gates the whole of ADR-058's surface/volume coupling, it is above
0 in **9 of 90** shipped scenes, and an artist cannot reach it either. Whether that stays a
decision is ADR-058's owner's to say.

## Consequences

- **The test asks the question a registration test does not.** `test_environment_panel` already
  checked that the paths exist; that would have passed for a parameter that is registered, drawn,
  keyframed and then dropped on the floor. So `test_composition` now **moves the parameter and
  checks the Environment the renderer is handed follows it**, and checks a save writes the moved
  value. Break demonstration: restoring the copy gives 1.70 where the parameter was set to 0.25 --
  registered, drawn, and silently ignored.
- **Two versions of that test failed before it worked, against working code, and the reason is
  worth keeping.** The panel writes a parameter's **base**; `Composition::applyParameters` reads
  its **final**; and `ParameterSet::resetFinals()` -- "final = base for every parameter" -- is what
  turns one into the other at the start of the engine's modulation pass. A test that moves the base
  and expects the environment to follow is testing the modulator's absence. A second attempt called
  `Modulator::applyRoutes` instead, which modifies only the finals a route targets and leaves every
  unrouted parameter's final exactly where registration set it. **Driving a seam the way the engine
  drives it is the difference between a reachability test and a test of the harness**, and the two
  failures are in the case's comment so the next person does not pay for them again.
- **The audit generalises and is one command.** Every `env.X = ...` assignment in
  `Composition::applyParameters` either reads a parameter (`pick(...)`, `->value()`) or copies the
  authored setting. The second list is the set of things an artist cannot reach:

  ```sh
  grep -oE '\\benv\\.\\w+ = [^;]{0,80};' src/scene/composition.cpp | grep -v 'pick(\\|->value()'
  ```

  It returns `shadowCascades`, `volumeMaxDistance`, `fogHeightAmount`, `styledAmbientFloor`,
  `wind`, `fogDensity`, `fogColor` and `skyIntensity`. Some are deliberate and say so; the point is
  that the list can be read in a second and had never been.

## Revisit when

- **`fogHeightAmount` is decided.** See above.
- **§25 and §26 are taken.** Assessed and not built in this batch: §25 asks for scattering colour,
  absorption colour, height colour and distance colour, and the medium has three colours through
  its depth and no height or distance term; §26 asks emission for a height influence, and
  `mediumEmissionAt` has a density influence and none for height. Both are additive and neither is
  blocked; they were not done because this batch found a live defect and spending it on the defect
  was the better trade.
- **§18 is taken.** Terrain-aware fog is the one remaining structural question in the brief, and
  the extension point to try first is `volumeDensityField`, which already binds a named scalar
  field into `volumeDensityAt` and needs no new binding.
