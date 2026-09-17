# The Character Animation Lab

A controlled environment for diagnosing character problems, and — more importantly — the set of
contracts that says which layer owns a given symptom.

Governing decision: [ADR-260](decisions/ADR-260-the-character-runtime-has-three-positions-and-only-one-of-them-is-drawn.md).
Related: ADR-086 (skeletal animation), ADR-161 (root motion), ADR-192/198 (the packs), ADR-240
(the rotation representation), ADR-204 (clip key spans).

## Start here when a character looks wrong

The single most useful fact in this document:

> **A character has three positions. Grounding writes one of them. The renderer draws a different
> one.**

| position | definition | written by | read by |
|---|---|---|---|
| `state().position()` | `anchor + travel` — the **simulation** | navigation, actions, grounding, the director tier | crowd separation, path validity, triggers |
| `visualPosition()` | `state().position() + motion.position` — the simulation **plus this frame's behaviour offsets** | every behaviour, through `MotionOffset` | staging, points of interest |
| node `position` parameter | what `applyOffsets` writes | `EntityWorld::update` | **the renderer** |

The last two agree. The first does not agree with either. So:

* A diagnostic that asserts something about `state().position()` **cannot** tell you whether the
  character is drawn in the right place. This is not hypothetical — it is how an 18 cm ground
  penetration survived: the simulation was grounded to within 3 mm the whole time.
* Any claim about grounding, clipping, floating or contact must be made against the node's position
  parameter, or against `visualPosition()`, which is the same number.

## The contracts (brief §34)

Answered from the code, not from intent.

**Does animation control world movement?** No. Clip playback poses the skeleton and nothing else. The
model-space the joint matrices land in is the glTF file's own scene space; the entity's transform
places the character in the world and the joints do the rest.

**Root motion — if present, where does it go?** Nowhere. It is not extracted (ADR-161). Five clips per
alien do carry real root translation — `Dying_forward` (0.985 m), `Dying_1_backpack`,
`Dying_1_no_backpack`, `Crazy`, and `Landing` (−0.567 m vertically) — and for those the mesh travels
away from its node while the node stays put. The locomotion clips are all in-place, which is why the
code-driven path works. `Landing` is the one Glowmere actually plays.

**Does locomotion control entity translation?** Yes, and it is code-driven. Behaviours write
`state.travel` directly; `Gait` selects an `Activity` and a playback rate, never a position. There is
no velocity vector on an entity — velocity is polar, `(speed, yaw)`.

**Does grounding modify entity position, visual position, or both?** Height goes to the **simulation**
(`state.travel.y`). Pitch and roll go to the **visual** (`motion.rotation.x/.z`) and never touch
`state.yaw`. Grounding therefore cannot see, and cannot correct, any vertical visual offset.

**Does terrain affect character orientation?** Partially and deliberately. `slopeAlign` (0.55) is the
fraction of the way from upright towards the surface normal, clamped to `maxTilt` (34°) — "a walker
leans into a slope, it does not become part of it". **This lean is currently wrong for any heading
that is not due north or south; see "Known defects".**

**Does collision modify velocity, desired velocity, or final position?** Final position, in three
stages: steering avoids, crowd separation converts overlap into a capped *speed*, and
`resolvePenetration` pushes out of anything still overlapped, clamped so a deeply embedded body walks
out over several frames. There is no physics — no bodies, no contacts, no integration. A character's
collision shape is a **vertical cylinder** (`bodyRadius`, default 0.45 m). There is no capsule and no
mesh collider.

**Does navigation provide a path or move the character?** A path. `Navigator` and `NavGrid` are pure
query services; the *behaviour* integrates position itself.

## Test zones

`examples/lab/character-animation-lab.json` — neutral ground, studio light, no ecology, no
atmospherics, no post. The character is the subject.

| zone | what stands there | what it is for |
|---|---|---|
| A | four aliens, one clip each (Idle, Walking, Running, Idle_turn) | a bad pose has nowhere to hide |
| C | `Walking` (in-place) beside `Dying_forward` and `Landing`, each on a red origin pip | **root motion made visible**: a mesh that has walked off its own pip is carrying displacement the engine does not extract |
| R | the same asset and clip at yaw 0/90/180/270 | §24, local-vs-world mistakes |
| S | the same asset at 0.5× / 1.0× / 2.0× | §23, unit assumptions |
| I | one of each farm animal | §32, telling an asset bug from a systemic one |

The picture is the smaller half of the lab. The diagnostic weight is in
`tests/unit/test_character_lab_*.cpp`.

## Regression tests

| file | what it pins |
|---|---|
| `test_character_lab_bob.cpp` | a walking body is never drawn below its own ground; and the companion case showing the simulation position *cannot* police that |
| `test_character_lab_grounding.cpp` | the drawn terrain surface versus the analytic one grounding queries, per LOD |
| `test_character_lab_slopes.cpp` | the heading-independence of a slope lean (`[!shouldfail]`), and the gimbal lock that proves why |
| `test_character_lab_inventory.cpp` | root-motion measurement across all 16 animated assets and 168 clips; clip key spans |

Existing coverage worth knowing about before writing anything new: `test_skeleton.cpp` (the core
animation suite), `test_alien_locomotion.cpp` (clip inventory, stride speed, cross-fades),
`test_farm_locomotion.cpp`, `test_stylized_wanderer.cpp`, and `tests/support/stride_speed.hpp` —
which measures a clip's implied stride speed off the toe joints and is the right tool for any
foot-sliding question.

## Known defects

**Slope posing is heading-dependent, and cannot be fixed where the tilt is computed.**
`quatFromEulerDegrees` composes `Rz(roll) · Ry(yaw) · Rx(pitch)`. Pitch is body-frame, roll is
world-frame, and yaw — the middle angle — gimbal-locks at ±90°, where `up.z = sin(pitch)·cos(yaw)`
is identically zero. On one patch of 10° ground the lean ranges 2.91°–7.24° with heading. There is no
(pitch, roll) that fixes it; the repair is architectural. See ADR-260.

**`Landing` carries −0.567 m of vertical root translation that is ignored**, at the moment a jump
resolves. Latent for the other four travelling clips, which no scene names.

**The grounding surface is not the drawn surface.** Grounding queries analytic `WorldMap::height()`;
the renderer draws a chunked LOD approximation. Mean deviation 0.012 m at LOD 0 rising to 0.590 m at
LOD 3. Do **not** "fix" this by grounding against the drawn mesh: LOD depends on camera distance, so
a character's height would depend on where the camera is, and a scrub would stop equalling a play
(ADR-091).

## Unsupported (brief sections that describe capabilities this engine does not have)

* **Root motion extraction, blending, additive animation, animation masks** — none implemented.
  The state machine is named states over clips plus cross-fade times; it holds no conditions, no
  parameters and no blend graph.
* **Physics collision** — no rigid bodies, no contacts. Obstacles are vertical cylinders in a uniform
  grid; the character is a disc.
* **Authored collision geometry** — obstacles come only from scatter layers and hero points. A scene
  cannot author a wall or a step as a collidable primitive, so brief §20's wall/cube/pillar/step/rock
  cannot be built as authored props.
* **Authored slopes** — ground comes only from `WorldMap`, an analytic noise field. A clean 10°/20°/
  30°/40° ramp cannot be authored; the slope tests instead *search* the real map for points at those
  angles, which has the advantage of testing the production terrain path.
* **Joint attachment** — `ISkeletonQuery::jointWorldTransform` is declared and **has no implementor**.
  `Entity::setSkeleton` is never called, so `Entity::socketTransform` resolves sockets against the
  entity's own frame rather than against a bone. See below.

## The attachment-point contract (brief §38)

For whoever integrates the tractor beam. The character runtime already declares the seam this needs;
it is simply not wired.

* `Entity::socketTransform(std::string_view socket, scene::Transform& out)` is the **authoritative
  world-space semantic position** a consumer should ask for. That is the call to use — not a
  reconstruction of the model transform, and not `visualPosition()` directly.
* It resolves through `Entity::setSkeleton(const ISkeletonQuery*)`. **Nothing implements
  `ISkeletonQuery` and nothing calls `setSkeleton`**, so today every socket silently falls back to the
  entity's own frame. A consumer cannot currently tell a real joint answer from the fallback.
* If a beam anchors on a position today, the one that matches what is drawn is `visualPosition()` /
  the node `position` parameter — **not** `state().position()`, which omits every behaviour offset
  (Glowmere's saucer carries a `drift` of radius 2.4 m, so the two differ by metres).
* Whatever the beam needs — a designated attachment point, an animation-space transform, a special
  root-motion policy — belongs in this contract as a named socket, not as a special animation path
  for abduction. Implementing `ISkeletonQuery` against `SkinnedRig::pose` would make
  `socketTransform` mean what it says, and is the natural place for that work.

## Performance

Debug visualisation must stay off the production path. The `World` panel's `Debug` tab already
carries roughly two dozen visualisations (bounds, normals, entity origins, skeletons, transform
trails) — extend those rather than adding a parallel system. Nothing in the lab's regression tests
runs in a shipping frame: they are CPU-only, offline, and drive the production entity and animation
code directly.
