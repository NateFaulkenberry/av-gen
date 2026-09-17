# The Camera / Framing Lab

**Status:** built
**Covers:** `world::heroSightline`, `world::clearSightlines`, `app::Shot::heldSubjectAt`,
`Composition::setViewportFreeRoam`, the frustum `cullEntityNodes` and `updateTerrainLod` are given
**Reads with:** [engineering-labs.md](engineering-labs.md) (the suite),
[camera-presets.md](camera-presets.md) (the ten presets),
[auto-director.md](auto-director.md), ADR-080 (camera clearance), ADR-091 (two-tier determinism),
ADR-245 (rigs and shots), ADR-246 (the output frame)

The lab answers one question: **is the camera where the shot says, and can it see what it is
framing?** It owns camera evaluation, the frustum handed to the cull, terrain clearance, line of
sight to the subject, and which camera the viewport is showing. It does **not** own which objects
survive that frustum — that is the Visibility Lab — nor which representation a survivor is drawn
with, which is the LOD Lab's.

---

## 1. Line of sight — the half of ADR-080 that was never built

`world::clearPath` keeps the eye **out of** the scenery: sideways out of a hero it is inside, up out
of the ground and the canopy. Nothing ever asked whether something stood **between** the eye and the
hero it was pointed at. A camera perfectly clear of the ground, at the authored distance and
elevation, framing a hero on the far side of a ridge — or behind another hero — passed every check
this repository had.

### What obstruction is, and what the model can honestly say about it

Three kinds of thing can stand in the way, and the world knows them to three different resolutions:

| | resolution | treated as |
|---|---|---|
| **terrain** | exact, analytic (`WorldMap::height`) | opaque, and corrected for |
| **other heroes** | exact capsules | opaque, and corrected for |
| **the canopy** | a statistical height field | **measured and reported, never corrected for** |

The canopy says "trees about nine metres tall grow around here", never "there is a trunk at this
spot". No arithmetic on a height field can tell a trunk from the gap between two of them, so
inventing an extinction coefficient over it would be a magic number in the sense §28 prohibits —
and it would be actively wrong for the shot vocabulary. `ShotKind::Discovery` exists to come in
"from the side and from above, so the subject slides out from behind whatever is in front of it",
and `ShotKind::Drift` is defined as being "about what passes between the camera and the subject".
Both are composed **around** foreground vegetation. A pass that lifted the camera over every canopy
between it and its hero would delete two of the fourteen moves.

### The query

`world::heroSightline(field, eye, subject)` casts nine rays across the subject's silhouette — three
heights up the capsule, three across it at `±r/√2`, the radius that halves a disc's area — and
returns:

* `visible`, the fraction of them that reach the subject past terrain and other heroes;
* `throughCanopy` and `canopyMetres`, reported and never acted on;
* `requiredLift` and `requiredPush`, the minimal eye motion that restores a clear view;
* `blocker`, the hero in the way.

The last `subject.radius` of every ray is excluded: the ground the subject stands on, and the
subject's own body, are not obstructions between the camera and the subject.

### The correction, and why it is not a magic offset

A ray from eye `E` to a point `A` on the subject passes through `P = E + s(A − E)`. `A` is fixed, so
raising `E` by `d` raises `P` by exactly `(1 − s)d`. The lift that clears an obstruction standing
`h` above `P` is therefore `h / (1 − s)`, and the lift that clears them all is the largest of those.
The same lever gives the lateral push. Nothing here is chosen.

Two numbers **are** chosen, and both are read off something that already exists:

* **The clearance margin is `ClearanceField::cameraRadius` (1.2 m).** The lever alone puts the ray
  *exactly on* the obstruction, and a sightline tangent to a hillside is a sightline with a hillside
  across the bottom of it. The first implementation cleared 8 of 9 rays and left the ninth grazing —
  the arithmetic right and the answer wrong. The margin reused is the field's own, which exists one
  level up for the same reason: "a lens sitting exactly on a surface still shows it filling the
  frame".
* **The correction is bounded at 0.25 of the eye's distance to the subject**
  (`AutoDirectorSettings::maxSightlineCorrection`). "Move until nothing is in the way" produces a
  worse shot than a slightly-occluded one as soon as the thing in the way is a hillside: the lift
  that sees over a ridge halfway to the subject is twice the ridge's height above the sightline.
  0.25 comes from the director's own table — `Shot::startElevation` is a height as a *multiple of
  the orbit radius*, exactly this ratio, and every ordinary shot kind composes below it (Establish
  0.22, Orbit 0.24, Approach 0.20, Track 0.12). A larger correction moves the eye further than the
  whole range of elevations the vocabulary works in.

A correction past the bound is **refused and counted**, not clamped. `SightlineResult` reports
`examined / obstructed / corrected / refused` and the worst of each, and the Auto-director logs it.

### Where it runs

`installSequence` in `src/app/camera_director.cpp`, immediately after `clearPath`, at **bake** time.
A directed camera must be identical between a 120 Hz window and a 30 fps offline render, so nothing
here runs per frame. Which subject each baked key is holding comes from
`Sequence::heldSubjectPerKey`, emitted from the same loop as `toTimelineTracks` so the two cannot
drift.

### What "holding a hero" means, and the mistake worth recording

The first measurement filtered shots by `lookMode() == Subject`, copying the rule `installSequence`
uses to decide which shots follow their hero (ADR-158). That rule is right for aim-follow and wrong
here. On the shipped Glowmere cut **20 of 23 shots are handoffs**, and a handoff *does* hold a
subject — the first before the swing and the second after it. The filter examined 3 shots out of 23
and declared the film almost clean.

`Shot::heldSubjectAt(t)` is the question asked properly: `Subject` always holds, `Handoff` holds the
first subject before the swing window and the second after it and neither in between, and `Fixed`,
`Parallel` and `Ahead` hold nothing — those shots are not a claim that anything is on screen and
cannot be "obstructed".

---

## 2. What was measured

All of it with a **hand-written march against `TerrainQuery::surfaceAt` and the raw `HeroPoint`
capsules**, not with `heroSightline` — an instrument that agrees with itself proves nothing. See
`tests/unit/test_director_sightline.cpp`.

### The production reproduction

`glowmere-atmospherics`, continuous mode: **4 of 185 baked keys that hold a hero were framing
`spire-cap` from behind `elder-2-cap`** — one sixteen-metre hero standing in front of another. It
was there before `clearPath` ran and still there after, because the camera was correctly outside
both heroes the whole time. With the sightline pass: **0 of 185.**

| project / mode | keys holding a hero | blind before | blind after |
|---|---|---|---|
| glowmere-valley-2, continuous | 105 | 0 | 0 |
| glowmere-valley-2, edited | 144 | 0 | 0 |
| glowmere-valley-2-multicam, both | 105 / 144 | 0 | 0 |
| glowmere-valley-2-song, both | 105 / 144 | 0 | 0 |
| **glowmere-atmospherics, continuous** | **185** | **4** | **0** |
| glowmere-atmospherics, edited | 196 | 0 | 0 |

### The finding behind all those zeros

A count of zero is not the same as nothing to fix, so the tightest sightline was measured too:

* on the **uncleared** bake the tightest sightline clears the ground by **0.63 m**;
* after `clearPath` it clears by **3.48 m**.

**ADR-080's canopy lift was already doing most of the terrain half of line-of-sight, by accident.**
`clearPath` raises every key to `ground + canopy × 1.12 + 1.2`, which on a wooded map is ten-odd
metres of altitude the shot never asked for, and an eye ten metres up looking down at a hero forty
metres away clears the ground between them by construction. That is a fact about *this* world, not a
property of the system: a hero in a hollow, a taller ridge, or a map with no canopy has no such
protection, and the hero-versus-hero case has none anywhere, which is where the four keys were.

---

## 3. The viewport and the director (§16)

`Composition::setViewportFreeRoam(bool)` is an override on the **result** of `resolveActiveCamera`,
never an input to it — ADR-091's purity rests on that. The two original tests
(`tests/unit/test_viewport_camera.cpp`) assert the one thing that broke: a cut moves a following
viewport more than 50 m and a free-roaming one less than 0.5 m.

`tests/unit/test_camera_lab_viewport.cpp` makes the interaction testable as a matter of course. The
specification's sentence — *"the viewport should not become unintentionally locked to a director
camera merely because a shot/camera changes"* — covers more events than a cut, so every way the
engine can be told about a camera is paired with the invariant that must survive it, and **every
arm has its opposite**: a following viewport must still follow, or "free-roam survived" would be
satisfied by a viewport that never moves at all.

| event | free-roaming | following |
|---|---|---|
| a cut between two authored cameras | does not move (< 0.5 m) | moves (> 50 m) |
| the whole camera direction re-installed | does not move | the new cut takes effect |
| a camera added to the collection | does not move | — |
| a resize, in each of the three canvases | does not move | does not move |
| lock → unlock → lock, in each canvas | reversible, exactly | returns to the director's pose |
| flying the viewport | edits no rig's position | — |

### The three canvases

`ui::PreviewViewMode` is Workspace / Output Frame / Preview. They reach the camera through exactly
two seams — `Camera::projection(aspect)` and `Composition::setViewport` — both fed from the extent
`previewRenderExtent` chooses, so **changing canvas is a change of aspect ratio and of nothing
else**. Asserted, not assumed:

* Output Frame and Preview render at the output's aspect to within a pixel of rounding; Workspace
  renders at the canvas's own, which is a different shape (the control).
* Changing aspect leaves the **vertical** field of view bit-identical and moves the horizontal one
  by a lot. That is what makes the preview the deliverable's framing rather than an approximation of
  it (ADR-246). The opposite convention would make every canvas change a reframe.
* Changing the near and far planes moves the depth terms and leaves both angle terms untouched, and
  the normalised depth stays in 0..1 — this engine's convention, not OpenGL's.

### The gap: free-roam is free of *rigs*, not of *tracks*

`setViewportFreeRoam` redirects `activeCamera_` away from an authored `scene::CameraRig` and back to
`kMainCamera`. A `seq::Shot`'s camera is not a rig: `Sequence::bake` emits ordinary
`camera/position` and `camera/target` keys, and the main camera is what those keys drive — so it is
exactly the camera free-roam hands the viewport back to.

Measured across all ten presets in `tests/unit/test_camera_lab_viewport.cpp`: a free-roaming
viewport travels **exactly as far** as a following one under every preset, and `activeCamera` is the
main camera either way. Free-roam does not shield the viewport from a shot camera.

**This is deliberate on the timeline's side and it is not a bug**: `Application::handleViewportEvent`
*announces* it ("the timeline is driving the camera — Camera > Hand Camera Back to the Viewport")
rather than deleting keys it does not own, and `releaseDirectedCamera` is the gesture that does own
them. What the flag's name does not say is that the two producers of `camera/position` are treated
differently. Recorded here, and locked down by the test, so the next person to widen free-roam knows
which half they are widening.

---

## 4. The frustum handed to the cull

### Planes are extracted twice, deliberately

`world::frustumPlanes` (GPU-free core, used by `cullEntityNodes` and `updateTerrainLod`) and
`rendering::frustumPlanes` (the GPU cull uniform and the shadow cascade fit). `rendering/`
deliberately does not depend on `world/`. Two independent Gribb-Hartmann implementations that are
allowed to drift would show up as an object culled on the CPU and drawn on the GPU, in one scene,
once, with no error anywhere — so the price of keeping both is a test that says they are the same
function, **bit for bit**, over five cameras at real orientations and five aspect ratios.

`test_camera_lab_frustum.cpp` also pins the two properties the rest of the engine assumes and
nothing states: the planes carry **unit normals** (`cull.wgsl` compares `dot(n, c) + w` against
`−radius` in metres, which means nothing otherwise) and they point **inwards**.

### A correction to the brief

There are **two** plane extractions, not three. The third site named
(`scene_renderer.cpp:aabbInsideFrustum`) is a box *test* and a duplicate of `world::aabbVisible`,
not of either extractor. Three box-versus-frustum tests exist — `world::aabbVisible`,
`rendering::aabbInsideFrustum` and the sphere test in `cull.wgsl` — and they are three because they
run in three places under three different dependency rules.

### The aspect floor: correct, and its reason has expired

`cullEntityNodes` builds its frustum at the **exact** viewport aspect. `updateTerrainLod` floors it
at **2.5**. Both read the same `viewportWidth_`/`viewportHeight_`.

Widening is safe in the strong sense — a wider frustum *contains* a narrower one at the same
vertical FOV, which the tests assert rather than assume. The cost, against a grid of chunk-sized
boxes in front of a real camera:

| aspect | exact keeps | 2.5 floor keeps | extra |
|---|---|---|---|
| 2.39:1 | 24 | 28 | +16.7% |
| 16:9 | 21 | 28 | +33.3% |
| 4:3 | 17 | 28 | +64.7% |
| 1:1 | 14 | 28 | +100% |
| 9:16 | 10 | 28 | **+180%** |

It is free only above 2.5, and most expensive in exactly the portrait orientation a short-form
deliverable is rendered at.

**Verdict: the floor was right when it was written and its premise no longer holds.** It dates from
when the composition did not know the extent it would be drawn into. `Engine::update` now calls
`Composition::setViewport` before `applyParameters` runs — in the live path (`Application::update`)
and the offline one (`RenderJob`) both — so the aspect is this frame's render extent, and the entity
cull twenty lines away already uses it with no floor at all. Left in place rather than removed
because removing it changes how much ground is submitted, and the counters that certify this
engine's draw budgets are the Integration and LOD labs' baselines. The case for removing it is the
five numbers above.

### `viewportHeight_ == 0` does not disable entity culling

The brief records that it does. The guard exists — `Composition::cullEntityNodes` returns early on
it — but **the field cannot be zero**: it is declared `1440 × 900` and `setViewport` clamps both axes
to at least 1. The branch is unreachable and entity culling has never been off. Asserted through
behaviour rather than by reading the number back.

### The diagnostic disagrees with the decision (found, reproduced, not fixed)

`RenderObjectDiagnostic::frustumMargins` is the instrument this lab reaches for first: six signed
distances, one per plane, printed by the Cameras panel and diffed by `compareSnapshots`. The question
a person asks them is *why was this culled*.

**They are computed against a different box than the cull decided with, in two ways.**

* `Composition::cullEntityNodes` calls `scene::entityCullBounds(scene_, entity)`, which pads by
  `padFraction = 0.25` and `padAbsolute = 0.25`. `SceneRenderer::render` fills the diagnostic from
  `scene.meshes[entity.mesh].bounds()` through the model matrix — **unpadded**. Every entity's
  margins are therefore systematically tighter than the decision beside them.
* For a **skinned** entity `entityCullBounds` uses the *posed* box, because a posed skeleton reaches
  outside its bind-pose bounds. The diagnostic uses the **bind** box. These are not nearly the same
  volume.

Reproduced in `test_camera_lab_frustum.cpp` on a two-joint bar bent through a right angle, swept over
72 cameras at 15° bearings and three heights: **all 72 have differing margins, worst disagreement
2.03 m, and 48 of them disagree about the verdict** — the panel would print six margins saying
"outside" beside a cull reason of `eligible`, or the reverse.

§37 says which of the two is wrong. **The fix is one line** — hand the diagnostic
`scene::entityCullBounds` — and it is not taken here because the numbers it changes are in
`examples/qa/baselines/*.snapshot.json`, which carry a 49-joint skinned entity and are the committed
baselines of a ladder other labs are measuring against right now. Refreshing them is a GPU run on a
shared device and somebody else's regression surface.

### Two stale comments, fixed

`updateTerrainLod` carried a comment saying the composition "knows the lens but not the viewport it
will be drawn into", that resizing the window did not re-pick LOD levels, and that the cull aspect
*below* was waiting for the same plumbing. Three claims, all stale, and the cull aspect is above —
in the same function as the code that had already fixed it, thirty lines from a comment saying the
opposite. A comment that contradicts its own function reads as authority; the next person to touch
terrain LOD would have plumbed in a viewport that was already there.

---

## 5. Fixtures, cases and controls

**Fixture:** `examples/camera/behaviors.json` — the permanent visual fixture for the shot-level
camera behaviours, one clip, six shots, six seconds each. It predates the lab and is registered
rather than replaced.

**Cases:** `examples/labs/camera/cases.json`, 1–8. Reachable as `avgen --lab-case camera:<n>`.
Cases 3 and 4 are a control/arm pair on the aspect floor; 5 and 6 on the sightline correction.

**Tests:**

| file | what it holds |
|---|---|
| `tests/unit/test_camera_sightline.cpp` | the sightline geometry on a real generated terrain, six cases, each with its control |
| `tests/unit/test_director_sightline.cpp` | the Glowmere measurement and the production regression |
| `tests/unit/test_camera_lab_viewport.cpp` | §16's interaction matrix, the three canvases, the ten presets |
| `tests/unit/test_camera_lab_frustum.cpp` | the two extractions, the plane properties, the aspect floor's cost, the diagnostic's box |
| `tests/unit/test_lab_registry.cpp` | extended: every registered case file now has to parse, not merely exist |
| `tests/unit/test_director_terrain.cpp` | the terrain half of clearance, pre-existing |

**On the shape of the fixtures.** Every camera in this lab stands at a real bearing and a real
elevation, off every axis of the world and off the diagonal too, and none of them is at the origin.
The Visibility Lab's predecessor bug survived every test in this repository because every fixture was
a centred, axis-aligned box, for which the right rule and the wrong rule were bit-identical
(ADR-182). Two of this lab's own fixtures were caught being too small by their own controls — a fan
of boxes that never left the frustum it was meant to leave, and a `cross(forward, worldUp)` that
collapsed for a camera looking straight down — and in both cases the control reported it rather than
the arm passing quietly.

---

## 6. The Glowmere audit (§30)

Two problems were assigned to this lab by name.

**Camera behaviour — line of sight.** Lab: this one. Reproduction:
`glowmere-atmospherics`, continuous mode, 4 of 185 hero-holding keys. Root cause: ADR-080 models
what the camera must stay *out of* and nothing modelled what stood *between* it and its subject; the
camera was correctly outside both heroes throughout. Fix: `world::heroSightline` /
`world::clearSightlines`, run at bake time in `installSequence`. Regression:
`tests/unit/test_director_sightline.cpp`, which carries the arm and its control in the same run.
Verification: 4 → 0 on the project, measured with an instrument that shares no code with the fix,
and 0 → 0 unchanged on the other five project/mode pairs.

**Camera lock and free-roam.** Already fixed before this lab opened —
`Composition::setViewportFreeRoam` applied to `resolveActiveCamera`'s result, with
`Application::ensureFreeCamera` standing down an authored rig and seeding free-roam from the live
pose. Nothing in it was found to be wrong. What was missing was coverage: two tests existed and
covered one event (a cut). This lab did not re-fix it; it made the interaction testable across every
event, every canvas and all ten presets, and in doing so found that free-roam protects against rigs
and not against tracks (§3 above).

**Nothing else in `docs/TODO-glowmere-handoff.md` is an open camera defect.** The camera items there
are about the journey's *composition* — the ending's visual destination, a timestamped review of the
full movie — which are authoring decisions rather than engine behaviour and need a person watching a
render.

---

## 7. Unsupported

**Occlusion of a hero by scenery that is neither terrain, a hero, nor the statistical canopy.** There
is no occlusion culling in this engine and no per-object obstacle representation the camera can
consult: a scatter instance lives on the GPU and there are a hundred thousand of them. A camera
framing a hero through a particular tree cannot be told apart from one framing it through the gap
beside that tree. What it would take is `TerrainQuery`'s `ObstacleField` seam — the interface §5 of
the world-authoring brief describes for navigation — populated with the near-camera scatter, and a
decision about what "near" means that is a budget rather than a geometry question.

**A screen-space projected-bounds overlay.** §16 asks for "frustum and projected bounds" to be made
visible. The frustum half exists — `DebugViewOptions::frustum`, which `overlaysFor(LabId::Camera)`
turns on along with `worldAxes` and `entityBounds`, and which is informative exactly when the camera
is frozen, because then it is the volume the cull used rather than the screen edge. There is no
overlay that draws an object's bounds *projected into screen space*; the numeric answer to the same
question is `RenderObjectDiagnostic::frustumMargins`, six signed distances per object, and §4 above
records that those are currently measured against the wrong box. Fixing the box is the prerequisite:
drawing a projected bound from a diagnostic that disagrees with the cull would make the disagreement
harder to find, not easier.

**A rendered confirmation that a corrected shot looks better.** §37 says the renderer is the source
of truth, and everything above is CPU-side geometry. The four corrected keys on
`glowmere-atmospherics` are asserted as sightlines, not as pixels. Confirming them visually needs a
GPU render of the same second before and after, which is a shared-device measurement this lab has
not taken.
