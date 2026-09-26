# GV3 camera smoothing audit (read-only)

## 1. How the authored rigs are evaluated

**Frame order: no frame of lag.** `Engine::update` does the following in order:
- resets the parameter finals (engine.cpp:5151);
- applies the timeline, including `cameras/<slug>/*` (5152);
- runs `updateBehaviour`, where EntityWorld writes each body onto its node's position final (5166);
- runs `controller_->update` (5193), which calls `Composition::update` → `applyParameters` → resolve (composition.cpp:8126) → `evaluateAuthoredCamera` (8166) → shake (8189-8198);
- only after that, `recordFollowTrails` (7221).

**Which position the camera reads.** It reads `nodeWorldTransform` (3803), which comes from the position param final (`nodeTransform` 5651; `value()` is the final, parameter.hpp:139).
- That final is base + travel + `motion.position` (applyNodeOffsets, entity.cpp:2218-2225). So it includes grounding and the Liveliness stride bob (behaviors.cpp:340-386).
- It excludes joints, the skeleton pose and XFORM effect offsets (`nodeDrawnWorldTransform` 3837 is not used). None of the followed subjects has an XFORM effect anyway.
- **Correction to the brief's premise:** `cast-v2.json` is not the pre-bob simulation. Its `position` is `loco.position = state.position() + motion.position` (entity.cpp:2701; cast_trace.cpp:199). The trace is exactly what the camera reads.

**Eye.** `P(t − lag) + followOffset` (7482-7499).
- The offset is in world axes. `followLocal` rotates it by the drawn rotation, which includes the sway noise (±5° on Ember), nod and slope tilt.
- **Lag** is a pure time delay on a per-frame observed trail, interpolated with lerp for position and slerp for rotation (1209-1316). It applies to the eye only.
- Its value does not depend on frame rate, but it is not seek-exact: the trail is cleared on a backwards jump or a jump over 0.5 s (1252-1257). The camera then runs un-lagged for `lag` seconds after a seek or at the head of a render (1288-1298; camera_rig.hpp:165-170).

**Clearance.** A hard `max(y, surfaceAt(xz) + clearance)` (7507-7516).
- `surfaceAt` is static (terrain_query.cpp:210-217), so there is no water bob.
- But the `max` is a C1 kink: velocity jumps where the floor engages or releases.

**Aim.** `P(t) + aimOffset` (7520-7525). It is always in world axes and undamped. Orientation is a plain look-at with world up. There is no rotational or target smoothing.

**Keyed channels.** Step, linear, `smooth` (Catmull-Rom clamped per component, timeline.cpp:104-108), the eases and bezier (85-117). rig.py uses easeInOut for moves and `smooth` for orbits and paths.

**Shake (ADR-098).** Camera-space band-limited noise, a pure function of time, one global `camera/shake/*` set (camera.cpp:242-307). GV3 uses a single 0.9 s, 7 Hz decaying shake at 177.69 s (look.py:274-279). That is outside every follow shot. There is no other handheld noise.

## 2. Wobble causes, measured

**Method.** I rebuilt every rig from the 20 Hz trace with the engine's own arithmetic.
- Terrain heights came from the prebuilt `avgen_world_preview`. The world hash (33541923bb9d94a1) matches the scene, and the probes match the generator's cache.
- "HF" means the residual after a zero-phase Gaussian low-pass with σ 0.3 s (about 0.44 Hz).
- Offline renders lift entity LOD (detail_limits.hpp:78-82), so a camera change cannot perturb the simulation.

1. **The stride bob, audio-driven.** On a straight walk (s03), Ember's drawn Y minus terrain is a clean 0→37 cm rise at 1.03 Hz.
   - `bounce × speed/stride` predicts 18 cm. The difference is the reaction `audio.bass → liveliness/bounce` (+0.45, op Add; entity.hpp:73) on Ember and Vane.
   - Eight of the eleven follow shots are welded to that bass-driven bob.
2. **The lag turns the bob into a nod (the dominant visible wobble).** The eye bobs 0.3 s after the aim, so pitch oscillates at the stride frequency.

   | Shot | Pitch HF RMS | Pitch p1–p99 | Notes |
   |---|---|---|---|
   | s19 | 1.14° (3.4% of frame height) | 5.3° (15.9% of frame height) | about 1 Hz |
   | s38 | 1.22° | 5.0° (13.3%) | |
   | s18 | 0.39° | | |
   | s13 | 0.22° | | |

   - Removing the subject's vertical HF cuts s19 to 0.22° and s38 to 0.20°.
   - Setting lag to 0 gives 0.000°.
3. **No-lag rigs are rigid translation** (s03, s09, s20, s28, s36).
   - The view direction never changes (0.000° HF).
   - The whole camera bobs 24–35 cm (p1–p99) and takes every start, stop and turn 1:1 (horizontal HF 5–14 cm).
   - The subject is pinned dead-centre while the world bounces behind it. That is the "procedural" lock-on.
4. **Clearance pop.** s20's floor holds for the first 0.35 s, then releases; the eye's vertical velocity jumps from 0.22 to 1.44 m/s in one 50 ms sample. s13 and s25 ride the floor throughout.
5. **Not causes:**
   - the keyed orbits s10 and s17 (0.001°);
   - the shake;
   - fixed-eye aim shots s05, s24 and s27 (≤0.07° pitch, ≤0.15° yaw).

   The clamp in `smooth` interpolation only bites when orbit keys straddle an axis (synthetic 45→135°: 0.5% radius dip, 3 m/s² spike). No GV3 orbit does.
6. **The Director's chase is worse.** compiler.cpp:243-262 builds `followLocal` rigs with offset (0, 2, −4) and lag 0.25 s. On the same trace windows they whip around at the aliens' turn rate:
   - view rotation 144°/s at p95;
   - eye speed about 10 m/s;
   - yaw HF 3.3–6.5°.

## 3. Gap list

**Present:**
- time lag (not seek-exact);
- world or local offset;
- hard clearance;
- keyed eases, Catmull-Rom and bezier;
- spline placement with `lookAhead`;
- shot blends;
- ADR-098 shake.

**Missing for rigs:**
- position smoothing, and separate vertical smoothing;
- target (look-at) damping;
- rotational smoothing;
- velocity prediction (lead);
- soft framing or dead zone;
- acceleration limits;
- a smooth clearance floor;
- a filtered heading for `followLocal`.

**Director side:**
- The cinematic director only bakes eased keys (cinematic.cpp:131-133, and "never shakes", cinematic.hpp:351).
- ADR-158's aim-follow has a one-pole smoother (composition.cpp:1480-1497). It covers the main camera only, is stateful, and is **unreachable**: `aimFollowSmoothingMs_` defaults to 0 (composition.hpp:2070) and nothing in src, tests, tools or apps calls `setAimFollowSmoothingMs`. So nothing is worth reusing there.
- The reusable piece is **HIST (ADR-703)**. It records each subscribed node's drawn transform on the 1/60 grid, is refilled by the ADR-700 checkpoint replay, and is carried in checkpoints. In its own words, "a scrub to second N holds exactly the samples a 60 Hz play to N holds" (history_bank.hpp:11-18).

## 4. Recommendations, ranked

**1. Engine: a filtered follow reference read from HIST (smallest reusable fix).**
- Build one subject reference: a causal, finite critically-damped kernel `h(τ)=ω²τe^(−ωτ)` with T = 2/ω, truncated at 8/ω, applied over the HIST samples.
  - XZ uses `followSmoothSeconds`; Y uses a separate `followVerticalSmoothSeconds`.
  - `followLead` (0..1) adds T × filtered velocity, on **XZ only**.
- Then eye = ref(t − lag) + offset and aim = ref(t) + aimOffset. There is no integrator, so the result is deterministic and seek-exact wherever HIST is.
- **Rule the measurement forced:** eye and aim must share the same filtered reference. Smoothing only the eye's Y, or lagging only the eye (as today), itself creates 0.7–1.0° of pitch wobble. Lead on Y re-injects the bob.
- **Modelled on the trace** with T = 0.3 s (XZ), 0.8 s (Y), lead 1, lag 0:
  - pitch HF on s19 and s38 goes from about 1.2° to 0.000°, and all shots stay at or below 0.063°;
  - eye vertical HF goes from 6–11 cm to 1–2 cm;
  - eye travel still equals the subject's (s19: 23.1 m against 22.8 m), so the camera is not frozen;
  - the subject drifts 2–13% of the frame (RMS) instead of being pinned. Peaks reach 23–33% at abrupt starts on s18 and s19, which is a tuning issue.
- **Files:** camera_rig.hpp/.cpp (fields and JSON), composition.cpp:7482-7525, engine.cpp:5295-5344 (subscribe the follow and aim nodes at depth lag + 8/ω, ≤16 s), and tools/gv3/rig.py.
- Delete `FollowTrail` (composition.cpp:1209-1316; composition.hpp:540-548, 2058-2086), per ADR-442's no-shims rule.
- **To verify:** HIST's replay applies no modulation routes (history_bank.hpp:20-24). Confirm that the reaction-driven bob replays identically. The filter removes the bob anyway, so any residual effect on the camera would be tiny.

**2.** Put the existing `followLagSeconds` on the same HIST read. That alone makes the current lag seek-exact.

**3.** Replace `std::max` with a smooth floor (softplus, about 0.25 m wide) evaluated on the filtered eye (7515).

**4.** Make `followLocal` rotate by the kernel-filtered heading, not the drawn rotation. Then point the Director compiler's chase at these knobs.

**5.** Wire `aimFollowSmoothingMs` to something or remove it. ADR-158's aim-follow could read the same kernel.

**6. GV3 now, without engine work:** set lag to 0 on s13, s18, s19 and s38. That removes the nod entirely, but the 30 cm translation bob remains. Filter at the camera rather than dropping the bass→bounce reaction, since the brief wants more audio reactivity.

**7. Optional, afterwards:** a deliberate operator layer via ADR-098 shake (1–2 cm, about 0.5–1 Hz, about 0.1°), keyed per shot.

## Verification

- **Instrument:** add a per-frame camera-pose track to `avgen_cast_trace`, which already runs the real Engine offline at render fps. Per shot, measure:
  - pitch and yaw HF RMS (in degrees and % of frame);
  - eye vertical HF (cm);
  - subject off-centre RMS and max (% of frame);
  - eye travel ÷ subject travel.
- **Suggested pass bar:** ≤0.1° HF, ≤2 cm vertical, off-centre max ≤20%, travel ratio about 1.
- **Tests** in tests/unit:
  - play to T equals seek to T, to ADR-267's 0.000022 m standard;
  - a shape check between 30 and 60 fps.
- **Scripts** (in /private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/audit/):
  - `rigs.py` and `analyze.py` produce the as-built numbers;
  - `proposal2.py` models the fix;
  - `director_chase.py` measures the Director's chase;
  - the data is in `results.json` and `proposal2.json`.

No repository files were changed.
