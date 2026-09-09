# Spatial data and attributes

Status: research (2026-09-09). Decision: ADR-024.

## 1. What the surveyed systems store per point

| System | Core attributes | Extensible attributes | Storage |
|---|---|---|---|
| Unreal PCG | transform, bounds, density, steepness, seed, color | metadata attributes (typed: bool/int/float/vec2/3/4/quat/transform/string/name/soft object) | SoA arrays + metadata table keyed per point |
| Houdini | P (and N, pscale, orient, Cd, id by convention) | any typed attribute on point/vertex/prim/detail | per-attribute pages (SoA) |
| Blender GN | position, radius, id | any typed attribute on domains (point, edge, face, corner, instance) | SoA `CustomData` layers |
| Notch clones | position, rotation, scale, colour, index | effector outputs | internal |

Consensus: **structure-of-arrays, typed by name, with a small conventional core** and any number of user attributes. Domains matter (point vs primitive); we need only the point domain now, with the enum ready for others.

## 2. Type set

bool, int, float, vec2, vec3, vec4, colour (vec4, tagged for UI). Future: string, quaternion (store as vec4 now), matrix. Attribute *names* are strings; lookups happen at graph-build time, not per element.

## 3. Memory layout and the GPU

- CPU: `std::vector<T>` per attribute, `count` on the set. Adding an attribute never touches others; deleting/filtering points compacts every column together.
- GPU: the renderer needs a fixed record for instancing; today `InstanceRecord` (position, rotation, scale, id/index, four randoms, colour, emissive) at 96 bytes. Keeping a *fixed projection* for the hot path is what PCG's GPU nodes and TouchDesigner instancing also do (a fixed set of channels). Extra attributes can be packed into optional side buffers (`extra0…3` as vec4) without redesign.
- 1M points × 96 B = 96 MB; per-frame regeneration on the CPU is out (ADR-029 moves point processing to compute for large counts).

## 4. Operations

Per attribute: set, add, multiply, remap, clamp, normalize (min/max of the column), smooth (neighbour average along index or spatial later), noise, randomize (hashed by seed/id), lerp, fit, threshold, compare. Per cloud: transform/rotate/scale/translate, filter (density, attribute compare, distance, probability, bounds), delete, scatter (jitter), sort (by attribute or axis), merge, duplicate, sample (every n-th / by count), resample (spline). All pure; all deterministic via `hashInstance(seed, id, channel)`.

## 5. Determinism and ids

Points carry `id` (stable within a generator) and `seed` (generator seed mixed with id). Filters keep ids; merges renumber only the appended set; sorts keep ids. Randomness is `hash(seed, id, channel)` so a point's random values do not change when its neighbours are deleted (PCG and Houdini behave the same).

## 6. Serialisation

Clouds are usually *generated*, so the project stores the generator and operator list, not the data. Explicit clouds (hand-placed points) serialise as JSON arrays per attribute with a type tag; large clouds may later use a binary side file. Sources: PCG metadata docs (above), Houdini attributes docs, Blender attribute reference (https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/attributes_reference.html — accessed 2026-09-09).
