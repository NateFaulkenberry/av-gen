# Architecture

Orientation only. Read the referenced headers rather than expanding this file.

## Shape
```
audio -> analysis -> signal bus -> timeline -> modulator -> parameters -> generators -> scene::Scene
                                                                                            |
                                                              +-----------------------------+
                                                              |                             |
                                                    Realtime (WebGPU/Metal)        Path tracer (Embree, CPU)
                                                    src/rendering/                 src/pathtrace/
```
Both renderers consume an evaluated `scene::Scene`. They share the scene contract and nothing else.

## Where things live
- `src/scene/` — `Scene`, `Composition` (flattens nodes to entities), `animation`, `pose_layers`
  (ADR-300: `Aim`, `Additive`, foot IK), `day_night`, `sky`, `water_surface`, `ik`.
- `src/rendering/` — `scene_renderer` (the frame), `representation` (screen-space LOD selection
  with hysteresis, ADR-124), `procedural_renderer`, `water_renderer`, `environment`, `post_processor`.
- `src/pathtrace/` — snapshot, Embree scene (pimpl'd), BSDF, textures, lights, MIS, sampler,
  denoise, albedo probe, `TraceJob`.
- `src/entity/` — behaviours, `decide` considerers, navigation, grounding, locomotion.
- `src/world/` — terrain and chunks (water meshes are per-chunk), effects, atmospherics, ecology.
- `src/assets/` — `gltf_loader` (the only model loader; glTF only), `mesh_lod` (meshoptimizer),
  `exr` (tinyexr, extended with named layers).
- `examples/` — projects and scenes; `index.json` is what the application's example menu reads.

## Things that surprise people
- The multicam camera is **baked**: 328 keys on `camera/position`. `preferredCameraDistance` is a
  clamp band that is inert on playback and live only at bake time.
- Water is built **per terrain chunk** (`buildChunkWater`), not as one sheet.
- The alien rig is nearly flat — `foot.l` is a direct child of `root.x`, with no anatomical FK
  chain anywhere. The farm rigs are properly nested. This bites root motion and IK alike.
- `scene::Entity::material` carries the full glTF material; only the *authoring* surface is thin.
- The sky's sun disc is resolved from the first enabled directional light with role `Key`.
