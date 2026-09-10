# Glowmere's vegetation as asset families

What this is: the thirteen `quaternius` entries in `assets/manifest.json`, and where every number in
them came from. Written so the numbers can be checked rather than trusted.

Sources, in order of authority:

- `examples/world/glowmere-stylized.scene.json` -- the `scatter` array on the `valley` node. Heights,
  emissive intensities, per-biome densities, scale ranges, tints and lean are read straight out of
  it. This is the authoritative source.
- `docs/glowmere-world-builder-audit.md` sections 1.1, 1.2 and 5 -- the same values, grouped into a
  ladder and a set of families.
- The glTF files themselves, for bounds and triangle counts.

Nothing here is a new artistic decision. It is Glowmere's own art direction, restated in the terms
`assets::AssetDescriptor` uses so the World Builder can reach it.

## Licence

**CC0 1.0 Universal, Public Domain Dedication.** Verified from two files already in the repository:

- `assets/quaternius/License_Standard.txt`, shipped inside the pack, which states the CC0 1.0
  dedication, links https://creativecommons.org/publicdomain/zero/1.0/, and identifies the pack as
  the free tier of the Stylized Nature MegaKit (68 of 116 models) by @Quaternius.
- `docs/asset-library.md`, which already recorded the pack as added 2026-09-09, supplied by the
  user, CC0 1.0, and pointed at that licence file.

All thirteen entries name files that exist under `assets/quaternius/glTF/`. Nothing was added whose
file or licence could not be checked against a file in the repository.

## naturalSize

Measured, not guessed, and measured twice.

The engine's own loader reports mesh bounds: `avgen --headless --scene <file> --frames 1` logs
`bounds [min]..[max]` from `assets::loadGltf`, and `naturalSize` is `max - min`. That convention was
confirmed against an entry nobody wrote for this work: `kenney/tree_tall.glb` reports
`[-0.200 -0.050 -0.231]..[0.200 1.638 0.231]`, and the manifest's existing `naturalSize` for it is
`[0.3996, 1.6877, 0.4614]`.

The same extents were then recomputed independently by decoding the `POSITION` vertex data out of
each `.bin` and walking the node hierarchy. The two agree for all thirteen.

**They do not agree with the glTF accessor `min`/`max` headers for two files.** `Fern_1` and
`Plant_1_Big` carry stale accessor bounds -- `Fern_1`'s header claims a 9.05 x 2.69 x 8.49 m box for
a mesh whose vertices span 2.83 x 0.84 x 2.65 m, a factor of 3.2. Anything that trusts the header
rather than the vertices will get those two wrong.

| entry | naturalSize (m) | triangles |
|---|---|---:|
| `CommonTree_1` | 4.3114, 7.2648, 4.5777 | 6265 |
| `TwistedTree_2` | 10.5629, 18.9492, 9.2026 | 9134 |
| `DeadTree_1` | 6.1489, 9.4955, 5.7487 | 6169 |
| `Bush_Common` | 1.9148, 1.5818, 1.9651 | 900 |
| `Plant_1_Big` | 1.8065, 2.3478, 1.9540 | 360 |
| `Fern_1` | 2.8269, 0.8402, 2.6523 | 288 |
| `Grass_Common_Short` | 0.6389, 1.3341, 0.7373 | 155 |
| `Flower_3_Group` | 1.4885, 2.0548, 1.5911 | 755 |
| `Mushroom_Common` | 0.5637, 0.4635, 0.7791 | 880 |
| `Mushroom_Laetiporus` | 1.3660, 0.7670, 1.1032 | 3216 |
| `Mushroom_Common_Beacon` | 0.5637, 0.4635, 0.7791 | 880 |
| `Rock_Medium_1` | 3.2251, 2.2598, 2.9891 | 342 |
| `Pebble_Round_2` | 0.4495, 0.0942, 0.4111 | 114 |

Note how little the mesh's own size has to do with the size the species wants to be. `Fern_1` is
0.84 m tall as authored and is a 1.4 m plant; `TwistedTree_2` is 18.9 m tall as authored and is a
15 m tree. That gap is the whole reason `preferredScale` exists, and why `naturalSize` is recorded
separately rather than inferred.

## The two numbers that decide how bright a species is

`world::emissionTierFor` picks a **rung** off the profile's `EmissionLadder`, and the composer then
multiplies:

```
layer.emissiveIntensity = rung x clamp(asset.material.emissive, 0, 1) x recipe.lighting.bioluminescence
```

So the manifest holds two separate things:

- **Which rung** -- chosen by `tags` and `category`, in the order `rare`/`accent` > `beacon` >
  `special` > category `fungi` > category `rock`/`crystal` > the depth band. The band is itself a
  tag (`foreground`, `background`, `midground`) when one is present and a height rule when one is
  not.
- **How brightly it burns on that rung** -- `material.emissive`, a 0..1 weight. It can dim a rung
  and can never promote a species past one.

The band is chosen by what the thing *is* -- how far away it reads -- and the weight then says how
bright it is relative to other things at that depth. Those are separate questions and the manifest
answers them separately.

The ladder's rung values were taken from Glowmere in the first place, so most of these species sit
exactly on their rung and weigh 1.0. That is a measurement, not a default. The weights that are not
1.0 are the ones carrying real information:

| entry | tags that pick the rung | rung | weight | intensity | Glowmere |
|---|---|---:|---:|---:|---:|
| `CommonTree_1` | `background` | silhouette 0.035 | 1.0 | 0.035 | 0.035 |
| `TwistedTree_2` | `background` | silhouette 0.035 | 1.0 | 0.035 | 0.035 |
| `DeadTree_1` | `background` | silhouette 0.035 | **0.0** | 0.0 | 0 |
| `Bush_Common` | `midground` | noticeable 0.295 | 1.0 | 0.295 | 0.2952 |
| `Plant_1_Big` | `midground` | noticeable 0.295 | **0.2085** | 0.0615 | 0.0615 |
| `Fern_1` | `foreground` | groundCover 0.0615 | 1.0 | 0.0615 | 0.0615 |
| `Grass_Common_Short` | `foreground` | groundCover 0.0615 | 1.0 | 0.0615 | 0.0615 |
| `Flower_3_Group` | `special` | special 3.94 | 1.0 | 3.94 | 3.936 |
| `Mushroom_Common` | `rare`, `accent` | brightest 6.89 | 1.0 | 6.89 | 6.888 |
| `Mushroom_Laetiporus` | category `fungi` | rare 4.43 | 1.0 | 4.43 | 4.428 |
| `Mushroom_Common_Beacon` | `beacon` | beacon 6.15 | 1.0 | 6.15 | 6.15 |
| `Rock_Medium_1` | category `rock` | inert 0 | 0.0 | 0.0 | 0 |
| `Pebble_Round_2` | category `rock` | inert 0 | 0.0 | 0.0 | 0 |

Verified end to end rather than by arithmetic: composing
`examples/recipes/bioluminescent-valley.recipe.json` produces exactly the intensity column above,
and exactly the heights in the audit, for all thirteen layers.

Three of them deserve their reasoning written down.

**`DeadTree_1` weighs zero, and that matters.** Deadwood is one of the three layers the audit's
section 8 says must stay at exactly zero. Left unweighted it would inherit the silhouette rung and
start glowing at 0.035, which is small but is the difference between "three layers emit nothing" and
"everything emits a little". The weight is the only place that decision can live: the rung cannot
express it, because the rung is shared with the canopy.

**`Plant_1_Big` is a midground plant that is dim for a midground plant.** It is 3.2 m tall, casts a
shadow, carries a 220 m view distance and clusters under the canopy -- midground by every property
it has. It is also, in Glowmere, four and a half times dimmer than the bushes beside it. Tagging it
`foreground` would have reached the ground-cover rung directly and weighed 1.0, but it would have
been a lie about how far away the thing reads, and would have cost it its shadow and most of its
view distance. So: midground band, weight 0.2085. This is the entry that shows why the weight is a
weight.

**`Mushroom_Common` is tagged `rare` and is not rare.** It has the highest fungal density in the
world (0.03 in marsh, 6000 instances). The tag is doing rung-selection duty -- `rare`/`accent` is the
only route to the `brightest` rung -- and the actual abundance is carried by `preferredDensity`,
where it belongs. If `emissionTierFor` ever grows a rung selector that is not a tag, this is the
entry to revisit.

## The beacons: one mesh, two species

Glowmere uses `Mushroom_Common.gltf` twice. At 0.28 m with a violet emissive of 6.888 it is the
`fungi` layer; at 1.5 m with a cyan emissive of 6.15, a 420 m view distance and a density of
0.00055 it is the `beacons` layer. The audit calls this out as a technique worth keeping: the same
mesh at two scales reading as two species.

**Expressed as two manifest entries pointing at the same file.** `Mushroom_Common` and
`Mushroom_Common_Beacon` share `file`, `naturalSize` and `triangles`, and differ in everything that
makes them a species:

| | `Mushroom_Common` | `Mushroom_Common_Beacon` |
|---|---|---|
| `archetype` | `glow_cap` | `beacon_cap` |
| `preferredScale` | 0.28 m | 1.5 m |
| `preferredDensity` | 0.03 | 0.00055 |
| `visualImportance` | 0.72 | 0.9 |
| tags | `foreground`, `rare`, `accent` | `midground`, `beacon`, `landmark` |
| rung | brightest 6.89 | beacon 6.15 |
| `variation.scale` | 0.8 | 0.425 |
| `variation.emissive` | 0.65 | 0.35 |

Two entries rather than one, because the library's unit is a species and not a file: an
`AssetDescriptor` is a mesh plus an intent, `id` is what has to be unique, and `file` was never
required to be. It also falls out correctly downstream -- the composer names a scatter layer after
the asset `id`, so this produces two layers rather than one layer that has to be told to appear
twice, and the asset registry caches the mesh by resolved path, so the second entry costs one more
layer and no more geometry. The alternative -- one entry plus a per-layer scale override somewhere
in the recipe -- would have put the fact that these read as different species somewhere other than
the library that is supposed to know what a species is.

It is the entry a reader is most likely to think is a mistake, so: the duplicated `file` is
deliberate.

## Everything else, and the rule it came from

- **`preferredScale`** -- the layer's authored `height` in the scene, which is what the scatter
  system normalises the mesh to: 14, 15, 12, 1.1, 3.2, 1.4, 0.7, 0.55, 0.28, 0.55, 1.5, 1.6,
  0.28 m.
- **`preferredDensity`** -- the layer's highest per-biome density, since the field means "instances
  per square metre where it is welcome" and the composer re-spreads it across biomes by category
  anyway. So deadwood takes its scree density (0.0009) rather than its forest one, and grass takes
  its meadow density (0.48).
- **`material.tint`** -- the layer's authored `tint` verbatim. This is the albedo family, not the
  emissive colour; the composer sets emissive colour from the profile's palette roles, which is
  where the audit's section 1.2 colours belong.
- **`variation.scale`** -- half the authored scale range, `(maxScale - minScale) / 2`, because the
  composer's model is symmetric about 1.0. Glowmere's ranges mostly are not, so this is the one
  systematically lossy conversion here: `Mushroom_Common`'s authored 0.6..2.2 becomes 0.2..1.8,
  which preserves the spread and moves the floor down. If small fungi start reading as noise, this
  is the number.
- **`variation.yaw`, `lean`, `hue`, `emissive`** -- the layer's `randomYaw`, `alignToGround`,
  `hueRandom` and `emissiveRandom`. Absent in the scene means zero here, not a default: the
  fan-plants and the rocks genuinely have no emissive variation.
- **`visualImportance`** -- the one field with no source in the scene. It is a judgement, and it is
  av-gen's, in the sense the manifest's `note` means. It is ordered by what the audit says the
  picture is about: beacons highest (0.9; they are the thing visible from 420 m), then the bright
  small things, then the canopy that fills the frame, then ferns, grass and pebbles at the bottom.
- **`roughness` and `translucency`** -- also judgements, kept in line with the Kenney entries for
  the same kind of thing: foliage rough and somewhat translucent, fungi smoother and very
  translucent, rock rough and opaque.

## Left out on purpose

- **`audioResponse`** on every entry. `world_composer.cpp` never reads it, the Kenney entries do not
  carry it, and Glowmere's two audio routes are authored in the project rather than per species.
  Filling it in would have been invention dressed as data.
- **The other 55 meshes in the pack.** Only the thirteen Glowmere uses were added. The manifest is
  curated by hand on purpose, and a species nothing has art-directed yet has nothing to say.
- **`metallic`**, left at its default of 0 by omission, as the Kenney entries do.
- **Anything reachable only through a scene property with no manifest equivalent** -- per-layer
  `viewDistance`, `minScreenRadius`, `castsShadow`, `clusterScale`, `clustering`, `proximity`,
  `materialProgram`, `emissiveSparsity` and the `motion` block. The composer derives all of those
  from the depth band, the category and the recipe. Two therefore do not survive the round trip:
  the beacons' 420 m view distance becomes the midground band's 220 m, and the fungi's
  `glowmereTissue` material program is not named at all.

## Where the group sits, and why

The thirteen entries are one new `quaternius` group inside `categories`, after the five that were
already there. The parser maps the five existing group names to a category and a placement tag; an
unrecognised group name supplies neither, so every new entry states its own `category` and its own
band tag rather than inheriting one. That was the point of the choice: the pack stays in one
readable block, the five Kenney arrays are untouched byte for byte, and nothing about these entries
is implied by where they sit in the file.

The manifest's `source` and `note` were updated, because they were the only two statements in the
file that had become false: the library is no longer one pack. `license` still reads `CC0`, which is
true of both.
