# ADR-278: A scene file could not author a light, and the key it wrote instead was ignored in silence

**Status:** Accepted
**Date:** 2026-09-18

> A scene file cannot author a light. Fix that.

The Lighting Lab (ADR-272) found it while mapping the pipeline. `Composition::fromJson` read
twenty-four top-level keys and `lights` was not one of them; there was no `NodeKind::Light`; and
unknown top-level keys were ignored rather than refused. A light came into existence in exactly
four places — `LightRig::expand`, `importLight`, `Composition::updateEcologyLights` and
`defaultKeyLight()` — and none of them was a scene file.

Two lab fixtures had written one anyway.

```json
"lights": [{"name": "key", "type": "directional", "direction": [-0.35, -0.72, -0.6],
            "color": [1.0, 0.97, 0.92], "intensity": 4.0, "castsShadow": true}]
```

Both `examples/labs/lod-geometry-lab.scene.json` and
`examples/labs/visibility-culling-lab.scene.json` carried it verbatim and both were lit by
`defaultKeyLight()` instead — **19.2 degrees** away, at intensity 3 rather than 4, at 5600 K rather
than the neutral 6500 K. Somebody wrote what they wanted, the file accepted it, and the renderer
ignored it. ADR-225 one level up: not a setting the application fails to keep, but a setting it
never had.

---

## 1. Two instances make a rule

This is the second of the pair, and the other half arrived the same week. ADR-274 found that
**no scene file in this repository declares a socket** — `sockets` is parsed and appears in no
`examples/**` scene. A written key nobody parses, and a parsed key nobody writes. Both were
invisible for one reason, and it is not that either author was careless: **the format accepts
things it does not act on, and says nothing either way.** A format that will not tell you which of
its keys are live cannot be audited by reading a file, only by reading the parser, which is what
finding both of these actually took.

So the feature is the smaller half of this document. The generalisable half is §4.

## 2. A top-level array, and not a `NodeKind::Light`

The spelling the two fixtures already used is the one implemented, extended to every
`PunctualLight` field. Where `LightRig`'s file format already had a name for the same quantity —
`color`, `temperature`, `tint`, `intensity`, `castsShadow`, `contactShadow`, `shadowStrength`,
`softness`, `volumetric` — the rig's spelling wins, because two names for one quantity across two
lighting formats is exactly how `coneDegrees` happened one format over (§4). Angles are **degrees**,
like every other angle a person writes here: a node's `rotation`, a rig's `azimuth`, `elevation`
and `cone`.

A `NodeKind::Light` was the other candidate, and it is the better answer to one question: a light
that should ride a moving object wants a transform, a parent, a name in the hierarchy and
visibility. It is rejected, with two pieces of evidence rather than a preference.

**It is not free, and the cost is not in `scene/`.** `scene::NodeKind` appears at **148 sites in 16
files** — `composition.cpp` 92, `graph/builtin_nodes.cpp` 18, then `world_builder`, `world_editor`,
`brush`, `ui_script`, `world_probe`, `world_edit_panel`, `world_edit`, `application`,
`engine_tools`, `control_panel`, `world_context_menu`, `engine`, `camera_director`. Every
enumeration of that list is somewhere that would have to decide what a node which draws no geometry
means: what the brush does to it, what the context menu offers, what `nodeKindName` calls it, what
the asset browser shows, what a `Group` containing one flattens to.

**And the one node-attached light path that already exists does not do the thing a node kind is
for.** `importLight` puts a glTF asset's lights into `NodeRange`, and `applyParameters` refreshes
them every frame — but only their **intensity and colour**. `NodeRange` has `restLightIntensity` and
`restLightColor` and no rest position or direction, so an imported lamp's beam stays at the
rebuild's transform for ever. The comment above that loop says "The node also carries the light
*with* it: a moved lamp lights where it now is," and it is not true. It has never shown up in a
frame because no asset in this repository carries a KHR_lights_punctual light — which is precisely
why a new node kind built on that machinery would have inherited the defect on day one with nothing
to catch it.

So the transform question is answered where it is cheap: **an authored light may name a `node`.**

```json
{"name": "headlamp", "type": "spot", "node": "rover",
 "position": [0, 1.2, -2], "direction": [0, 0, -1], "outerCone": 22}
```

The light is then authored in that node's local frame, and `applyParameters` re-places its
position, direction and up from the node's world transform every frame; a hidden node's light goes
out, the same rule the asset lights follow. That is a transform, a parent, a hierarchy name and
visibility — four of the four — bought with one optional string and no thirteenth case anywhere.

It also makes the glTF path's gap **visible rather than theoretical**: the regression that proves a
light rides a moving node would fail against the asset-light machinery as it stands. Fixing that
path is a rest transform per asset light and is left undone deliberately, because there is nothing
in the repository to test it on, and a fix with no arm is ADR-182's subject.

## 3. Precedence, written down because four sources with no stated order is how this happened

In the order the lights are added, which is the order they sit in `scene_.lights`:

| # | source | when |
|---|---|---|
| 1 | a glTF asset's own lights, per node | `rebuild` |
| 2 | **the scene file's `"lights"`** | `rebuild` |
| 3 | `defaultKeyLight()` | `rebuild`, **only if** there is no rig *and* 1 and 2 produced nothing |
| 4 | `LightRig::expand` | `applyParameters`, every frame, after the camera is final |
| 5 | the procedural ecology | `applyParameters`, every frame, capped by `kMaxEcologyLights` |

Two consequences worth stating as sentences rather than leaving in a table.

**Authoring one light turns the default key off; authoring none keeps it.** That is the behaviour
the two fixtures believed they already had, and it needed no new code — `addedKeyLight_ =
!lightRig_ && scene_.lights.empty()` was already the rule, and the authored lights simply reach it
first.

**A rig is appended alongside the authored lights, not instead of them.** A glTF lamp and a rig
already coexist, and this is the same relationship: a rig lights the *subject* relative to the
camera, an authored light lights the *world* at a fixed place. A scene that wants only its own
lights simply has no rig.

**What this does not do, and it is a real gap.** An authored light gets no registered parameter, so
it cannot be keyed, modulated or dragged, where a rig light can. That is deliberate under ADR-271's
boundary — `saveProject` writes the scene **by reference**, so a control that re-authored a light
would have to rewrite a scene file on every drag, and `glowmere-stylized.scene.json` backs two
projects. The moment a UI control wants to edit one, ADR-271's own revisit trigger applies and the
answer is to register the field, not to write the scene. Nothing in this branch requires
re-authoring a scene to change anything.

**Round trip.** `toJson` writes the array back, or a save silently deletes the author's lights —
ADR-207/230's world-effects bug arriving in a new place, and the same reason
`environment["lightRig"]` writes an in-memory rig out in full. `name` and `type` are always
written, because a file a person reads should say what kind of light it is; everything else only
when it differs from the `PunctualLight` default, so a scene round-trips to what it authored rather
than to a twenty-six-key dump of a struct, and a scene that authors no light emits no `lights` key
and stays byte-identical.

## 4. The half that is worth more: a key nobody reads is not silent

`core/json_keys.hpp`. `unknownKeys(obj, known)` returns the keys of an object that are not in a
list and do not begin with `_`; `warnUnknownKeys` logs one warning each, naming the key, the object
and the file.

**A warning rather than a refusal**, and the reasons are evidence:

* Nine scene files carry a `_note`, deliberately and usefully. A leading underscore is this
  repository's comment convention, so it is exempt **by rule** rather than by a special case in
  each parser.
* A file written by a newer build and read by an older one should lose a feature, not a world.
  The report has to survive being wrong about the future.

**`unknownKeys` is a pure function and the key lists are public** (`scene::sceneFileKeys()`,
`sceneEnvironmentKeys()`, `sceneSkyKeys()`, `sceneLightKeys()`, `rigFileKeys()`, `rigLightKeys()`),
for the reason `particleExtentFromRadius` lives in `scene/` and not in the panel (ADR-271): the
decision is the testable thing, and a test that has to fish a warning out of a log is a test of
spdlog. It also makes the list's own failure mode checkable — a key the parser reads and the list
omits warns about a **correct** file, and the first spurious warning is the one that gets the check
switched off. `tests/unit/test_scene_authored_lights.cpp` holds the lists against all 56 scenes and
21 rigs that ship.

Wired into: the scene root, `environment`, `environment.sky`, a `"lights"` entry, a rig's root, a
rig's lights. The node `animation` block's hand-rolled version of the same loop — the one that came
out of `"lodCount"` configuring a single-rung ladder — is now one call to it.

`LightRig`'s spot angle is ADR-272 §7's report, closed here. A rig writing `"coneDegrees"` still
gets the 45-degree default from `"cone"` — that is what "unknown" means — but it no longer does so
in silence.

### What it found on its first run

Fifteen scene files write **`"volumeNoiseAmount"` where the parser reads `"volumeNoise"`** — the
C++ field's name instead of the key's:

`glowmere-valley-2`, `glowmere-valley-2-multicam`, `glowmere-stylized`, `glowmere-atmospherics`,
`world`, `terrain`, `grove`, `moonrise`, `_tier1`, `_tier1big`, `_skyonly`, `hyperspace`,
`infinite`, `machine`, `reassembly`.

Every one of them has volumetrics on (`volumeDensity` 0.00055 to 8) and asks for 0.4 to 0.55 of
noise. Every one of them runs at 0. **The saved projects agree**, which is the part that makes it
certain rather than likely: `glowmere-stylized.json` and `glowmere-valley-2-multicam.json` both
carry `"scene/volumeNoise": 0.0` — the application writing back the value that was really in
effect, into a document that sits beside a scene claiming another.

**Left alone deliberately.** Correcting the key turns volumetric noise on in fifteen shipped films,
including two Glowmere projects that share one fingerprinted scene; that is an art-direction change
with a re-bake behind it and it is not this branch's to make. It is now reported by name on every
load, which is the whole point of the mechanism, and
`tests/unit/test_scene_authored_lights.cpp` holds the count at exactly 15 so that *fixing* it is
what changes the test rather than *forgetting* it.

## 5. What it moved, measured

Both lab fixtures now obey their own files. Neither lab's **assertions** moved, because
`test_lod_gpu.cpp`, `test_lod_ladder.cpp`, `test_visibility_gpu.cpp` and
`test_visibility_culling.cpp` all build their scenes in code and none of them loads a fixture. What
moved is the two fixtures' rendered appearance, which is what a person opening `--lab lod` sees.

One frame each at 1920x1080, t = 1/60 s, `--disable post`, arm and control rendered by the **same**
binary — the control being the same file with its `"lights"` key removed, so the difference is the
key and not the build. Load average 3.67.

| | LOD fixture | | Visibility fixture | |
|---|---|---|---|---|
| | default key | the file's | default key | the file's |
| frame mean | 0.0629 | 0.0633 | 0.2112 | 0.2183 |
| rms contrast | 0.0343 | 0.0340 | 0.0790 | 0.0886 |
| p99 | 0.1073 | 0.1076 | 0.4187 | 0.4886 |
| bright centroid y | 0.4938 | 0.4941 | 0.5264 | 0.5345 |
| pixels differing | **999,995 of 2,073,600 (48.2%)** | | **752,202 of 2,073,600 (36.3%)** | |
| worst channel | **158** of 255 | | **192** of 255 | |
| difference bbox | whole frame | | (0, 574)–(1919, 1079) | |

The frame *means* barely move — the LOD fixture is mostly sky and unlit ground — and a threshold on
the mean alone would have reported "no change" on both. The percentiles and the pixel count are
where it is: the visibility fixture's p99 moving 0.4187 to 0.4886 is the extra stop, and its
difference is confined to the ground plane and what stands on it, which is the half of the frame a
key light reaches.

**And the structural quantities did not move, measured rather than assumed.** The identifier AOV
(`--aov id`: which instance drew in which pixel, and therefore which rung and which cull outcome)
is **byte-identical** across the change, on both frames of both fixtures, while the colour frames
differ. That pair is the whole claim: one buffer that must change did, one that must not did not,
through the same run.

## 6. The tests, and why each one has a control

ADR-182, and this defect is the purest example of it in the tree. A test that loads the LOD
fixture and asserts "it has a directional light called `key` that casts a shadow" **passes on the
bug** — that is a description of `defaultKeyLight()`. The only assertions that can fail when the
feature is missing are ones naming a quantity where the file and the default *disagree*, and there
are exactly three: direction, intensity, temperature. Colour is (1, 0.97, 0.92) on both, so colour
is deliberately not what any arm turns on.

`tests/unit/test_scene_authored_lights.cpp`, 8 cases, 139 assertions:

| case | arm | control |
|---|---|---|
| a lab fixture is lit by its own file | the fixture's direction, intensity, temperature | the same file with `"lights"` textually removed, asserted to deliver the **default's** three numbers, plus the 19.2-degree separation stated so no tolerance can span it |
| authoring replaces the default | one authored light, no light named `key` at all | a scene authoring none gets `defaultKeyLight()` at intensity 3 |
| a rig is not replaced | rig + authored light = 2 lights, both findable by name | (the two cases above) |
| save and reload | the emitted `lights` array is the fixture's own spelling, key for key, and reloading it delivers identical lights | a scene authoring none emits no `lights` key |
| a light rides its node | position at the node, direction rotated by it; then the node moves and the light follows; then the node hides and the light goes out | the **identical light without `"node"`**, which stays at the origin pointing down -Z |
| a malformed light fails the file | six refusals: no name, unknown type, unknown role, negative intensity, zero direction on a spot, a two-element vector; plus a duplicate name | the same document made valid loads, and two differently-named lights load |
| an unknown key is reported | `unknownKeys` returns exactly `coneDegrees`, `lightz` | every known key absent from the report, `_note` exempt, a non-object returns nothing |
| the key lists match what the parser reads | all 56 scenes and 21 rigs swept | `otherFindings == 0` and `volumeNoiseAmount == 15` — the one genuine finding pinned rather than tolerated |

**The proof the first case cannot pass on the default**, which is the arm this whole ADR is about.
With the `"lights"` key deleted from the shipped fixture:

```
CHECK( got.x == Approx(want.x).margin(1e-5) )   -0.353209f == Approx( -0.34984... )   FAILED
CHECK( got.y == Approx(want.y).margin(1e-5) )   -0.883022f == Approx( -0.71967... )   FAILED
CHECK( got.z == Approx(want.z).margin(1e-5) )   -0.309058f == Approx( -0.59973... )   FAILED
CHECK( key->intensity == Approx(kAuthoredIntensity) )        3.0f == Approx( 4.0 )    FAILED
CHECK( key->temperature == Approx(kAuthoredTemperature) ) 5600.0f == Approx( 6500.0 ) FAILED
REQUIRE( std::regex_search(text, m, lightsKey) )                          false       FAILED
1 test case | 1 failed;  13 assertions | 7 passed | 6 failed
```

Six of thirteen, and the sixth is the control arm reporting that it could not find a `"lights"` key
to remove — which is the belt to the other five's braces. The file restored, all 139 pass.

One trap recorded because it cost a run: the control's regex was first written
`\n [ ]*"lights": \[[\s\S]*?\n [ ]*\],`, which stops at the *inner* `],` of the direction array and
leaves a file that is not JSON. The control then failed as a parse error — and a control that fails
for the wrong reason looks exactly like an arm that works.

---

## Consequences

* `src/core/json_keys.{hpp,cpp}`: `unknownKeys`, `warnUnknownKeys`.
* `src/scene/composition.{hpp,cpp}`: `Composition::AuthoredLight`, `authoredLights()`,
  `setAuthoredLights`, the `"lights"` key in both directions, per-frame node attachment, and the
  four public key lists.
* `src/scene/light_rig.{hpp,cpp}`: `rigFileKeys()`, `rigLightKeys()`, and the two warnings.
* `tests/unit/test_scene_authored_lights.cpp`.
* Two lab fixtures change appearance; their labs' documents carry the measured before and after.
  No fixture file changed byte for byte, so no scene fingerprint needed refreshing.
* Fifteen scenes now warn about `volumeNoiseAmount` on every load. That is the mechanism working,
  not a regression, and it is pinned at fifteen by a test.

## Revisit triggers

* **A UI control that edits a light.** Then ADR-271's boundary applies in full and the answer is a
  registered parameter over the authored value, not a scene rewrite.
* **An asset that carries a KHR_lights_punctual light.** Then §2's unfixed glTF gap has something
  to test it on, and it should be closed — `NodeRange` needs a rest position and direction.
* **A light that wants to ride a skeleton joint.** ADR-274 settled that `jointTransform` is
  entity-local, not world; `"node"` resolves to a composition node, and a joint is one level below
  that. It is a natural extension and it is not this.
* **The `volumeNoiseAmount` fifteen.** Correcting the key is one `sed` and fifteen changed films.
  When somebody decides to, the test's `== 15` is the thing that has to move with it.
