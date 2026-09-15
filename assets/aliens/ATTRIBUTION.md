# Modular alien characters

Six variants exported from a single modular source file.

## Source

`Alien_Modular_animated.blend` — a modular low-poly alien pack rigged with **Auto-Rig Pro** (not, as
its description claims, a raw Unreal/Epic mannequin skeleton). Supplied by the project owner; the
file is not in this repository.

> **Licence: CC0 1.0 Universal (public domain dedication)** —
> <https://creativecommons.org/publicdomain/zero/1.0/>. Recorded 2026-09-15 on the project owner's
> statement that this pack shares its author, and its licence, with the repository's other asset
> packs. The source `.blend` still carries no licence text of its own; this line is the owner's
> assertion of the terms, written down so the next person does not have to ask again.

## What ships, and what does not

The source has 45 objects. Nine are character meshes; the rest is authoring apparatus:

| dropped | why |
|---|---|
| 26 `cs_*` meshes | Auto-Rig Pro control shapes — zero-triangle widgets drawn in the viewport only |
| `char_grp`, `cs_grp` empties | rig scaffolding |
| 289 of 378 bones | control, IK-target and `*_ref` bones. Only the **89 deform bones** are exported |
| `anniversary_lounge_4k.hdr` | a 4K authoring environment — 32 MB of the 33 MB source file |
| `Dots Stroke` material | a Blender grease-pencil default with no user |
| 10 shape keys per head/body | AV Gen has no morph targets (see below) |

The export **deletes** these rather than hiding them: an exporter set to "selected objects" still
walks parents, so a hidden object that is somebody's parent ships anyway.

## The variants

All six share one 89-joint skeleton, one 128×128 texture atlas (BaseColor / Emission / Attributes)
and all 26 animations. They differ in silhouette.

| file | head | body | pack | triangles |
|---|---|---|---|---:|
| `alien-scout.glb` | bare | plain | — | 4,098 |
| `alien-diver.glb` | helmet | spacesuit | — | 5,510 |
| `alien-elder.glb` | exposed brain | plain | — | 5,936 |
| `alien-pilot.glb` | mask | spacesuit | jet | 6,242 |
| `alien-trooper.glb` | helmet | armour | standard | 6,569 |
| `alien-ranger.glb` | mask | armour | jet | 6,601 |

About 1.66–1.79 m tall in the file's own units, so they are placed at **scale 1.0** — unlike
`assets/imported/alien.gltf`, which is authored in centimetres and needs 0.01.

## Animations

All 26, one glTF animation each, named after the source action with the authoring debris removed:
`_Runing` → `Running`, `Jump_runing` → `Jump_running`, `Flying-jet` → `Flying_jet`,
`Dying_forward_` → `Dying_forward`, and the leading underscore dropped from the five oldest.

```
Button_push  Crazy  Dying_1_backpack  Dying_1_no_backpack  Dying_forward  Fall_loop
Fight_head_hit  Fight_idle  Fight_Jab  Fight_leg_kick_1  Fight_leg_kick_2  Fight_punch
Floating  Flying_jet  Idle  Idle_turn  Jump_running  Jumping  Landing  Running
Take_from_floor  Take_from_table  Walking  Walking_crouch  Walking_injured  Walking_low_grav
```

## Two limitations, both the engine's rather than the asset's

**Morph targets are not exported**, because AV Gen refuses them in two places on purpose: the
geometry import warns `morph targets ignored` and the animation import drops morph-weight channels,
since `AnimationPath` has only translation, rotation and scale. Shipping them would add ~104 KB per
variant that nothing reads, and two warnings to every load of every character. The source `.blend`
keeps all ten shape keys per head and body, and `tools/export_alien_variant.py --morphs` puts them
back the day the engine grows a use for them.

**Each variant carries its own copy of all 26 animations**, which is 86% of each file — 3.9 MB of
4.5. That is not an export choice: AV Gen has no way to bind one file's clips to another file's
skeleton (`Importer::importClips` only fills rigs built in the same `loadGltf` call), and
`Composition` deep-copies the whole rig per node instance by design (ADR-086: "Two nodes on the same
character file are two characters, and they must be able to be doing different things"). Meshes and
textures *are* deduplicated per asset path; rigs are not. So six variants cost six copies on disk,
and six of one variant cost six copies in memory. Measured and recorded in ADR-192 rather than
worked around.

## Regenerating

    tools/make_aliens.sh ~/Desktop/Alien_Modular_animated.blend assets/aliens
