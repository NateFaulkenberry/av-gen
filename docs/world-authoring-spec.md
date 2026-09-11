# Glowmere — Major World Authoring & Rendering Upgrade

The brief for the world-authoring pass, kept in the repository so the work it describes can be
picked up without the conversation it arrived in. Reproduced as written.

> Make Glowmere feel like a small professional 3D world-building / cinematic scene-authoring tool
> that happens to be deeply integrated with music and audiovisual modulation.

This is a substantial architectural and UX upgrade. The requirements below are **not** isolated
feature requests. Many of these systems will be reused for the music-video project and other
audiovisual worlds.

**Use the existing architecture aggressively. Do not create parallel systems where existing systems
can be extended. Do not settle for the minimum implementation if a more professional solution
exists.**

---

## 0. Before writing code: understand the existing system

Inspect and understand: terrain generation, representation, rendering and height queries; world and
scene representation; entity/object representation; asset loading and placement; ecology/vegetation
placement; collision and spatial queries; water rendering; material/shader architecture; audio
analysis; modulation/signals; timeline/sequencer; camera system; editor UI; selection; transform
tools; undo/redo; serialization; scene persistence; viewport/input handling; culling; LOD;
performance profiling.

Read the existing documentation and ADRs first. **Extend the infrastructure. Do not build
disconnected mini-frameworks.**

---

## 1–7. Navigation and characters

**§1 — Fix world navigation properly.** The alien animates but cannot meaningfully travel. Do not
teleport between random points; build the beginning of a proper world navigation system. It should
determine where it can walk, stay grounded, handle reasonable slopes, avoid solid features, trees,
rocks, structures, water (unless allowed), cliffs, stay inside the world, select valid destinations,
navigate to them, arrive naturally, choose again, and idle/observe occasionally.

**§2 — Navigation representation.** Choose what fits this architecture: (A) a navmesh of walkable
polygons from terrain, slope limits, exclusion geometry, water and obstacles; (B) a terrain-aware
navigation grid carrying walkability, height, slope, obstacle and water state; or (C) a hybrid where
terrain gives continuous height queries and a separate representation gives walkability and
pathfinding. Do not blindly implement a heavyweight general-purpose navigation framework.

**§3 — Terrain collision / spatial queries.** Establish reusable queries — `heightAt`, `normalAt`,
`slopeAt`, `isWalkable`, `isWater`, `isOccupied(x, z, radius)`, `nearestValidPoint`. Use them
throughout. **Do not implement separate terrain logic for the alien, water placement and editor
placement.** This is foundational infrastructure.

**§4 — Character grounding.** Query terrain height each movement update, follow terrain smoothly,
orient to it where appropriate, prevent penetration and floating. Account for slopes, small
variations, movement speed and animation stride. Avoid jitter; interpolate.

**§5 — Obstacle avoidance.** A lightweight spatial representation for navigation-relevant objects
(position, radius/bounds, height, type, walkability) — not heavyweight rigid bodies. Vegetation and
small decorative objects should not necessarily block; large rocks, structures and cliffs should.

**§6 — Alien behaviour.** IDLE → SELECT INTEREST → NAVIGATE → WALK → ARRIVE → OBSERVE → IDLE →
SELECT AGAIN. Interests: glowing plants, water, the UFO, terrain features, points of interest,
scenic locations. Probabilistic and varied. **It should not look like it is following a debugging
waypoint system.**

**§7 — Reusable character behaviour system.** Do not hard-code the alien. Conceptually: Transform,
Animation, NavigationAgent, Locomotion, LookBehavior, InterestBehavior, AudioReaction, Interaction.
Future characters must reuse it; this matters for the music-video project.

---

## 8–16. Water

**§8 — Water needs a complete rendering pass.** It currently reads as a flat opaque surface laid
over the world rather than water occupying a physical channel. Improve shoreline blending, depth
perception, surface variation, reflections/specular, transparency or depth colouration, underwater
appearance, edge integration, motion, visual interest, nighttime appearance and audio reactivity.
**Do not simply make it brighter or bluer.**

**§9 — Water shader.** A stylized water material: depth-based colouration, Fresnel, view-dependent
reflection, subtle surface normals, animated normal/distortion, shallow/deep colour variation,
shoreline fade, underwater darkness, subsurface/transmitted colour, specular highlights, moon
reflection, bioluminescent contribution. Compatible with the existing stylized aesthetic. Not
photorealistic ocean water — **beautiful stylized nighttime fantasy water.**

**§10 — Shoreline integration.** The water/land boundary is too abrupt. Consider depth-based edge
blending, foam, a wet shoreline band, darkening near banks, shallow-water colour, edge distortion,
shoreline vegetation, small rocks, translucent shallows, procedural edge breakup. The river must
appear to occupy the terrain, not sit on top of it.

**§11 — Water currents.** Reusable flow behaviour with `flowDirection`, `flowSpeed`, `flowStrength`,
`turbulence`. Communicate downstream movement with **layered** motion — slow broad flow, smaller
ripples, subtle distortion, occasional local variation. Not one giant scrolling texture. Calm and
magical rather than an obvious shader demo.

**§12 — River flow direction.** Derive or configure a logical downstream direction from terrain
topology. A reusable `WaterBody` (type, flowDirection, flowSpeed) rather than hard-coded UV
movement. Ponds and lakes: no strong direction, subtle circular/ripple motion, wind-driven surface.

**§13 — Floating / drifting water vegetation.** A generic system for objects that float and drift:
lily pads, leaves, petals, flowers, glowing organisms, debris, magical particles. They float at
water height, drift downstream at slightly different speeds, rotate subtly, respond to local
current, and avoid looking synchronized. **Reuse existing Glowmere vegetation assets** rather than
creating new art.

**§14 — Add lilies to Glowmere.** Populate some existing water with deliberate lily/leaf clusters
that sit naturally, drift slowly, vary in size and orientation, occasionally cluster, stay within
the water body, don't clip shorelines, and don't move in lockstep. Use instancing. Make the water
feel inhabited.

**§15 — Water music reactivity.** Reusable water modulation properties. Bass: deeper glow, subtle
surface displacement, stronger underwater particles. Beat: small synchronized ripple, glow pulse,
occasional emission. Highs: tiny luminous particles, surface sparkle. Energy: stronger
bioluminescence, particle density, shimmer. Major transition: temporary ripple/wave event, colour
shift, particle burst. **Keep it tasteful — it should still look like water.**

**§16 — Underwater bioluminescence.** A subtle underwater layer: tiny glowing particles, drifting
organisms, volumetric specks, soft localized glows, depth-dependent visibility. **Do not fill the
river with noise.** The viewer should occasionally notice *"there is something glowing underneath
the water"*, particularly during musical peaks.

---

## 17–28. World editor

**§17 — Major UX upgrade.** Move toward a professional 3D world-authoring workflow: Photoshop brush
behaviour, Unity/Unreal scene editing, Blender manipulation, foliage painting, cinematic
composition. Extract the best interaction patterns rather than reproducing those applications.

**§18 — Asset brush system.** Replace "select asset and click". With an asset selected the viewport
shows a **live ghost/preview under the cursor** communicating position, orientation, scale,
footprint, approximate bounds, placement validity, terrain contact and collision state — *"this is
what will be placed if you click."*

**§19 — Brush preview.** The asset browser should show thumbnails, names, categories, approximate
size, maybe polycount and material preview; for groups, a group thumbnail, contents and footprint.
It should feel like an **artist palette**, not a filename list.

**§20 — Brush modes.** At least: Single, Scatter/Paint (drag to place many), Group, Randomized
group, Eraser, Replace, Stamp. Do not overcomplicate the first implementation, but structure it so
more modes can be added.

**§21 — Brush parameters.** radius, density, spacing, random rotation, random scale, scale range,
alignment to terrain, surface normal alignment, height offset, slope restriction, collision
avoidance, jitter, clustering, seed. Editable without opening a separate dialog. Sensible defaults.

**§22 — Live placement feedback.** Show the prospective placement while hovering. For invalid
placement communicate *why* — red/invalid preview, collision footprint, slope warning, water
warning, outside-world warning. For valid, a clear ghost, subtle footprint and orientation
indicator. **The artist should never guess where an asset will land.**

**§23 — Click-to-select existing objects.** Clicking selects, with a visible outline/highlight,
transform gizmo, object information, hierarchy/group information, asset name, position, rotation and
scale. Do not require finding objects in a hierarchy panel merely to move them.

**§24 — Transform gizmos.** Move, rotate, scale; axis handles, plane handles, world/local
orientation, snapping, numeric transforms, duplicate, delete. Improve any existing gizmo rather than
duplicating it.

**§25 — Multi-selection.** Shift-click, drag box if practical, group selection, selection from
hierarchy. Then move/rotate/scale together, duplicate, delete, group. **Extremely important for
artistic composition.**

**§26 — Groups.** Artists group placed objects; the group moves as a unit and remains editable. This
makes compositions practical rather than individual objects.

**§27 — Duplicate / repeat.** Fast duplication — select, duplicate, move, duplicate again.
Ctrl/Cmd+D, copy/paste, repeat last placement.

**§28 — Undo / redo.** Strengthen if not robust. Placement, deletion, movement, rotation, scale,
painting, grouping, terrain generation changes, water configuration, camera keyframes. **Do not
build an editor where artists are afraid to experiment.**

---

## 29–32. Terrain generation

**§29 — Major upgrade.** Produce hills, valleys, ridges, basins, plateaus, gentle slopes, river
channels, ponds, small lakes and varied elevation. Focus on better procedural generation first, not
a sculpting application.

**§30 — Controls.** High-level artistic parameters rather than hundreds of noise settings: Terrain
Style (rolling hills, valley, basin, mountainous, plateau), Elevation min/max, Roughness, Valley
Strength, Ridge Strength, Water Amount, River Frequency, Pond Frequency, Seed. Use presets. The
artist should quickly generate something more interesting than a flat plane with noise.

**§31 — River / water-aware terrain generation.** A generated river should have a depression,
plausible banks, connected flow, reasonable slopes, water occupying the low region, and vegetation
responding to water proximity. **Do not merely draw a water polygon over random terrain** — terrain
and water should understand each other.

**§32 — Determinism.** Same seed + same parameters = same world. Matters for scene saving, offline
rendering, reproducibility, music-video production and debugging.

---

## 33–41. Cameras

**§33 — Cinematic world authoring.** Glowmere needs proper camera authoring; cameras must be visible
inside the world.

**§34 — Camera gizmo.** Body, lens/front, frustum, up and forward directions, optional FOV
visualization. Visually obvious, selectable, manipulable. **Prefer an editor-native gizmo to a
photorealistic prop.** A free CC0 camera GLB is available at
https://eclair-assets.itch.io/camera-01-free-cc0-vintage-camera-glb-prop — it is a visual starting
point, not the answer. The representation must be *informative*, not photorealistic.

**§35 — Frustum visualization.** When selected, show frustum, field of view, near plane, far plane
and direction. Subtle enough not to overwhelm the scene.

**§36 — Camera path visualization.** Show the path curve, keyframe points, camera orientation,
current position and optional look-at target. Visible in editor mode even when the camera is not
active, so the artist can see *"the camera flies through this valley, passes the UFO, then descends
toward the river."*

**§37 — Camera keyframing.** Integrate with the existing timeline/sequencer. **Do not create a
separate animation timeline.** Keyframe position, rotation/orientation, field of view, focal length,
optional look-at target and camera parameters. The existing timeline becomes authoritative.

**§38 — Interpolation.** Linear, smooth/eased, spline/Bezier. Independent control of position curve,
orientation and FOV. Avoid orientation flips; use quaternion interpolation. Movement must not feel
robotic.

**§39 — Look-at.** An optional reusable look-at target for orbiting subjects, tracking characters,
revealing environments and fly-throughs. The target is itself selectable and editable.

**§40 — Record / fly mode.** Pilot the viewport camera, position it, capture a keyframe, move,
capture another. Minimize friction between *"I found a cool shot"* and *"that shot is in the
timeline."*

**§41 — Camera + audio.** Do not overbuild now, but ensure camera properties can participate in the
modulation system — bass → FOV, beat → shake, energy → movement, timeline → path. Architect
accordingly.

---

## 42–45. Editor structure, interaction, performance, persistence

**§42 — Editor modes.** Consider SELECT, PAINT, TERRAIN, WATER, CAMERA rather than dumping every
control into one UI. The viewport should clearly communicate the current mode.

**§43 — Professional interaction principles.** Immediate visual feedback, direct manipulation,
contextual controls, non-destructive editing, undo/redo, keyboard shortcuts, multi-selection,
snapping, sensible defaults, visible state, minimal modal dialogs, predictable behaviour, fast
iteration. **Do not turn Glowmere into a collection of engineering panels.** The bar: *an artist can
open the editor and intuitively understand how to make the world look better.*

**§44 — Performance.** Painting hundreds or thousands of objects must stay practical: instancing,
spatial partitioning, batching, deferred updates, culling, efficient selection queries and
serialization. **Do not rebuild the entire scene every time the artist places one flower.**

**§45 — Save / serialization.** Painted assets, groups, transforms, terrain parameters, water bodies,
water vegetation, navigation data, camera paths, camera keyframes and necessary editor metadata must
serialize cleanly. Avoid storing transient editor state as scene data unless useful.

---

## 46–47. Testing and visual validation

**§46 — Tests.** Navigation: valid/invalid terrain, obstacle avoidance, terrain height, slope
constraints. Water: flow, water height, shoreline, floating objects, deterministic placement.
Editor: selection, transform, duplication, grouping, undo/redo, serialization. Camera: keyframes,
interpolation, serialization, deterministic playback.

**§47 — Visual validation.** **Do not consider this complete because everything compiles.** Launch
Glowmere and inspect it. Water: does it look like water, feel embedded, move, have depth, is the
shoreline natural, is it interesting at night? Alien: does it travel, stay grounded, avoid
obstacles, feel alive? Editor: can you tell where an object will land before clicking, select and
move existing objects, manipulate groups, paint efficiently? Terrain: can you generate genuinely
different landscapes, do rivers and ponds feel part of the terrain? Camera: can you see where it
travels, and create a cinematic path without fighting the UI?

---

## 48–49. Scope and direction

**§48 — Do not stop at the minimum.** Fix underlying architecture rather than working around it;
implement a better interaction model if you find one; generalize a system where it will benefit the
music-video project. **But do not explode scope into a general-purpose game engine.** The smallest
architecture that creates a genuinely powerful audiovisual world-authoring workflow.

**§49 — Product direction.** Glowmere is becoming a *music-reactive 3D world + audiovisual engine +
cinematic authoring environment*. The long-term workflow:

```
GENERATE WORLD -> SCULPT/AUTHOR TERRAIN -> PAINT/PLACE ASSETS -> GROUP/COMPOSE -> ADD CHARACTERS
-> ADD WATER/ENVIRONMENT -> ADD AUDIO REACTIVITY -> PLACE CAMERAS -> KEYFRAME CAMERA
-> SEQUENCE SHOTS -> PREVIEW WITH MUSIC -> RENDER
```

**The artist should eventually be able to make something visually sophisticated without writing
code. That is the bar for this pass.**

---

## 50. Priority order

1. Alien navigation / terrain-aware movement
2. Water rendering + shoreline integration
3. Water flow + floating vegetation
4. World editor selection / manipulation
5. Professional asset brush / placement preview
6. Terrain generation improvements
7. Camera representation + frustum
8. Camera path visualization
9. Camera keyframing / sequencer integration
10. Polish / performance / UX

Do not sacrifice the core architecture merely to check items off. The result should feel like a
**major generational improvement to Glowmere's ability to create worlds**, not a collection of
unrelated feature additions.

---

## Note on §1, added when this was filed

§1's premise has partly changed since it was written. The walker's failure to travel was diagnosed
and fixed: `Navigator::sample` rejected any ground where something shorter than the walker grew,
which in a meadow of grass and ferns meant everywhere, and separately the `interest` behaviour held
it in `observe` for thirteen seconds at a stretch. It now moves on 52% of frames and covers ~195 m a
minute (`ADR-088`, and the commits around it).

What remains genuinely missing is the rest of §1–§5: **obstacle avoidance is statistical, not
per-instance.** `ClearanceField::canopyHeight` answers "trees about nine metres tall grow around
here", never "there is a trunk at this spot", so a character can walk through a tree. There is no
`isOccupied(x, z, radius)`, no obstacle representation, and no pathfinding — navigation is
straight-line steering with rejection sampling. That is the real §1 work.
