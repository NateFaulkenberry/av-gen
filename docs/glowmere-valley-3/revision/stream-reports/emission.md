# Emission stream (ADRs 903–906): the agent's final report

The finishing agent reported this on 2026-09-26. Branch `agent/emission`, final commit
`eca06448`, 13 commits on `0b623b88`. The coordinator merged it into `integrate/revision` as
`7b030ac4`, resolving one index conflict and one stale `TriggerClock::bind` call. This is the
agent's report, lightly trimmed.

## Suites
| Suite | Result | Exit |
|---|---|---|
| Full CPU | 3,533 cases: 3,513 passed, 19 skipped, 1 failed as expected (the shouldfail) | 0 |
| Full GPU, under the lock, after the CPU suite | 518 cases: 517 passed, 1 skipped | 0 |

## ADRs
- **903:** `emissiveBoost` is the emission lane every drawable of a node takes, after its program.
- **904:** an instance's emission variation is applied once, as a rotation of the hue it shows.
- **905:** a scatter layer, a material layer and the ecology light are parameters that reach the pixels.
- **906:** a field's clock can start at the latest musical event.

## GPU proof (each case with a control arm)
- **Boost ×3 on each drawable kind:** program-lit procedural ×2.98, plain procedural ×3.00, mesh ×3.00, SDF ×3.01. The old lane leaves the program-lit box at ×1.00.
- **Routed boost:** ×2.98 with the signal up, byte-identical at rest.
- **With an FXL Glow of gain 2:** ×5.99.
- **Variation applied once:** gain 0.5 gives ×0.502 (it was ×0.25).
- **Layers:** a scatter layer's gain and hue act on that layer alone, and a material layer's intensity reaches the pixels.
- **A wave timed from a marker:** byte-identical before the marker, and seek-exact.

## Where each control lives
| Control | Parameters panel | World panel Inspector |
|---|---|---|
| `nodes/<terrain>/scatter/<layer>/emissionGain` | nodes → `valley/scatter/fungi` → **glow** | click a mushroom → scatter → `fungi/glow` |
| `.../hueOffset` | same section → **hue shift** | `fungi/hue shift` |
| `.../emissiveFieldAmount` | same section → **light wave** | `fungi/light wave` |
| `nodes/<n>/emissiveBoost` | nodes → `<n>` → **glow boost** | the node → `glow boost` |
| `scene/ecologyLight` | scene → **light cast by glowing plants and fungi** | Atmosphere → environment |
| material layer `emissionIntensity` | material → `<p>/layer/1/fireflies` → **glow** | Materials → `<p>` → `1/fireflies/glow` |
| `field/<name>/trigger/*` | field → `<name>/trigger` → "fires every N beats" … | Fields → the field → trigger |

## Scenes whose look changes
Before/after strips are in `~/Desktop/av-gen-review/emission-adr903-906/`.
- **Tree-of-life night film:** stars and motes get their authored 1.9–2.2×. That touches 0.30% of pixels, which brighten ×1.22.
- **Ocean world:** its procedural stars now dim with the day/night cycle (×0.88 at sunrise).
- **glowmere-stylized and glowmere-lyrics:** the filaments' downbeat routes are live (×1.022).
- **The GV2 family:** the elder routes are live; the lamellae brighten ×1.017–1.024 at the downbeats. GV3 has no route on these nodes.
- **ADR-904:** `terrain.json`'s glowing fronds read as the program's green; GV3 changes in 0.04–0.21% of pixels at 30 s.
- **ADR-905:** GV3's three dead arcs now show as unknown targets at load.

## Defects found, not fixed
- The ecology light takes its colour from the layer's `emissiveColor`, not the program's, so GV3's fungi cast purple light while showing teal.
- Terrain water and grid entities take no emission lane.
- The path tracer has no hue lane.
- An event route's peak depends on the frame rate.
- `tools/gpu-lock.sh` and the engineering rules disagree about locking CPU suites.
- The full CPU suite now takes about 2 h 10 min run serially. `ctest -j 4` is available.

## How GV3 should use this
**1. Mushroom lanes per layer (`fungi`, `shelf-fungi`, `beacons`),** under `nodes/valley/scatter/<layer>/…`, each also a JSON key on the layer.
- **`emissionGain` (glow):** a different source per layer, with the depth within ±30%.
  - fungi: kick → add 0.25–0.35, attack ≤10 ms, decay 250–400 ms.
  - shelf-fungi: `audio.mid` → add 0.2, attack 30 ms, decay 600 ms.
  - beacons: treble or claps → add 0.3, attack 10 ms, decay 180 ms.
- **`hueOffset` (hue shift, in turns):** key it per section, and never route audio into it.
  - +0.05 to +0.08 in the suspension and break;
  - 0 in the grooves;
  - −0.06 to −0.10 at the drop.
  - Past +0.08 the teal clips.
- **`emissiveFieldAmount` (light wave):** 3 is subtle and 8 is clear. fungi 6–10, shelf-fungi 4–6, beacons 3–4.
- **`hueField`:** lower it from 0.16 to about 0.06–0.08; ADR-904 made the hue spread real.
- **Re-save the project** to drop the obsolete `material/glowmereTissue/op/9/input/*` values.

**2. A light wave through the mushrooms on the drop.** Measured in a copy of GV3: each crossed
mushroom brightens ×1.3–1.43. Add this field node:
```json
{ "name": "drop-wave", "kind": "field", "position": [-12, 0, 52], "field": {
  "name": "drop-wave", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
  "wavelength": 40, "waveSpeed": 20, "waveWidth": 0,
  "falloff": {"kind": "smoothstep", "inner": 180, "outer": 300},
  "trigger": {"source": "marker", "name": "drop"} } }
```
Then add `"emissiveField": "drop-wave", "emissiveFieldAmount": 8` to the fungi layer (6 on
shelf-fungi, 4 on beacons).
- The band is about 11 m wide; each mushroom is lit for about 0.5 s.
- **Other triggers:** `{"source": "beat", "everyN": 4, "offset": 0}` fires every bar; `{"source": "musicEvent", "name": "impact"}`; `{"source": "repeat", "period": 14.769, "phase": 74.306}` fires every 8 bars from bar 41.
- **One front per field.** To layer a drop ring with per-bar rings, name a `compound` field with `"combine": "max"` whose children are the triggered waves.

**3. `scene/ecologyLight` (GV3 uses 1.4):** key it to 0.8 in the break and about 2.0 at the drop, or route the kick with add 0.3–0.5. Set the fungi layer's `emissiveColor` to `[0.08, 0.95, 0.53]`, so the light it casts matches the teal it shows.

**4. Hero boosts reach program-lit parts,** e.g. kick → `nodes/elder-2-under/emissiveBoost`, add 0.4, attack ≤10 ms, decay about 300 ms. The boost is not inherited, so route each part. It multiplies with an FXL Glow.

**5. The dead arcs:**
- Move the tracks on `material/glowmereFirefliesCrown/emissionIntensity` and `…Scaled/emissionIntensity` to `…/layer/1/fireflies/emissionIntensity`, multiplying every key by 16 (0.25–2.1 becomes 4–33.6).
- Delete the `paintedGround2` emission track.

**6. Previews against finals:**
- Every value is in metres, seconds or turns.
- At 960×540 the 0.28 m fungi are specks (0.1–0.2% of s33's frame). At 4K each covers about 16 times the pixels, so waves and hue shifts read much more strongly.
- Judge amounts on 1080p or 4K stills of one wide shot and one medium shot over marsh. Don't raise amounts to make previews read.
- Event routes peak higher at 30 fps, so keep attacks at 10 ms or less.
- Bloom halos are sized in pixels.
