# Waveform Dimension: the resonance audit

Brief §1. Audited against main `74edc063`, which has ADR-1400 and ADR-1403..1408. This builds on the Resonant
Silk audit (`../av-gen-silk/docs/prototypes/resonant-silk/01-resonance-audit.md`). That audit covers the general
audio-to-pixel path, the persistent modes, the envelopes, determinism, MIDI and routes. None of that is repeated
here. This audit covers only what changes when the field is **three-dimensional** and the geometry is **its level
sets**.

## The short answer

- **3D evaluation: yes.** Every field sample already takes a 3D point. A resonance field is 3D wherever its mode
  shapes are 3D, and the point modes are.
- **Differently oriented families: yes, from one resonator.** A reader's own transform turns the whole figure it
  reads, and its listening window picks a family of modes.
- **Isosurfaces: feasible with no new engine code**, by sphere tracing the field through the SDF renderer's
  `displaceField`. The cost is one full field evaluation per march step. The bake grid that would remove that cost
  is plane-only today, but its kernels are already 3D.
- **Contour sheets: available as is**, through procedural meshes with a Field deformer (`alongNormal`).

## 1. Is the field 3D?

| Mode family | Shape in 3D | Source |
|---|---|---|
| `point` | `R_m(k r) cos(m (alpha - theta))`. `r` is the **3D** distance, so `m = 0` modes are nested spherical shells. For `m > 0`, the azimuth `alpha` is taken about the field frame's **vertical** axis, so the lobes are segments of those shells, split by vertical nodal half-planes, with a regular core on the axis. | `fields.wgsl::resonanceModeShape`, `resonance.cpp` |
| `lattice` | `sum_j cos(k (x, z) . d_j)`: plane waves in the **horizontal** plane only. They are uniform along y (vertical sheets and columns), except for an optional `depth` decay `e^(-abs(y - y0) / depth)`. | same |
| ripple (ADR-1403) | A packet on the **horizontal** distance `rho`: a cylinder in 3D. | same |
| `onset` field (ADR-1116) | A front at `d = age x speed`, with `d` from the field's wave geometry: `geometry: spherical` gives a true **spherical** shell about a point. | `fields.wgsl::audioShape`, `waveDistance` |
| `spectrum` field (ADR-1116) | The history laid out along `d` (planar, cylindrical or spherical). | same |

**Orientation.** Each consumer samples at `q = worldToLocal(p)` of the field it names. A reader that has a
`rotation` (and `position`) therefore sees the resonator's modes, and their centres, turned about the reader's
origin. Combined with a listening window, this gives what the brief asks for: **differently oriented mode families
from one resonator**, one resolve and one mode table. With several readers summed in a compound, the vertical
preference of the point and lattice modes disappears.

## 2. Turning the field into geometry

| Route | Exists? | Notes |
|---|---|---|
| **SDF sphere tracing a level set** | Yes, with a trick | `displaceField` adds `amount x fieldScalar(field, p)` to a child distance (ADR-027). There is no constant primitive, so a level set is built as `plane(y, offset -500)` + F + a `linearGradient` field equal to `-y - 500` (length 1000, strength -1000: both at their parameter limits). The result is exactly `d = F`, to within f32 (about 1e-4 m). `shell` then gives `abs(F) - c`, which is solid about the nodal surfaces. Under a `difference` from a huge sphere it gives `c - abs(F)`, which is solid in the lobes. |
| Material on an SDF hit | Yes | `shadeSurface` runs the material program at the hit, and its `field` op reads the field there. The hit is lit like a mesh. |
| Lipschitz control | Yes | `stepScale`, `epsilon` and `maxSteps`. F is not a distance. Its gradient is about k x A, so the readers' strengths scale it towards gradient 1. |
| Surface nets (`renderMode: mesh`) | Yes, but not live | CPU meshing on every structural change. A field that moves every frame would re-mesh every frame. |
| Particle density isosurface (ADR-1142) | Yes, but not applicable | Its volume is a particle system's density, not a field's. |
| Contour sheets: Field deformer on a mesh | Yes | `alongNormal: true` displaces along the vertex normal. The procedural vertex stage takes a finite-difference normal over the whole deformer chain (`normalStep`, ADR-1401). |
| Field contours drawn on a surface | Yes | The material's `field` op plus `palette` (a cosine of the value) or `smoothstep` gives bands at chosen levels of F, where a surface crosses the field's level sets. |

## 3. What a 3D field costs

From the Silk audit: every reader sample is a loop over its resonator's audible modes. A Field deformer pays that
per vertex, times 3 for the finite-difference normal, and again in any depth prepass. What is new here:

- **Sphere tracing pays it per march step.** On the SDF lit pass that is pixels x steps x readers x modes, plus 4
  more evaluations for the tetrahedron normal at the hit. The depth prepass and shadows each march again, so both
  are off.
- **Listening windows are nearly free to narrow.** A row outside a reader's window (`w <= 0`), or silenced by a
  focus window (ADR-1406's `RES_SILENT`), skips its shape. So three readers over three disjoint bands cost about
  one reader over all of them, not three.
- **The bake (ADR-1407) is plane-only.** `GridField::validate` refuses a volume bake. But `cs_bake`, the CPU
  `step`, `simCellCenter` and `gridTrilinear` all already work in 3D. A volume bake (e.g. 128 x 64 x 128, about
  1M cells, under the 4M-float grid table) would turn every march step into one trilinear fetch. Proposed as
  ADR-1440. The coordinator approved it on the condition that Stage A's direct evaluation shows it is needed.

## 4. Limits that shape the design

- **16 fields** on the GPU. The scene uses about 8: the resonator, 3 family readers, the sum, an excitation
  reader, an onset front and the linear helper.
- **4 children per compound, one level only.** On the GPU a nested compound evaluates to 0. So the figure's sum
  (3 readers plus the onset front) is one compound. The SDF chains two `displaceField`s (F, then the linear helper)
  rather than nesting compounds.
- **4 fields per material program.**
- **48 mode rows in the table, 24 modes per resonator.**
- **Point lobes are always about the field frame's vertical.** A family that turns about another axis needs its
  own rotated reader.
- **Ripples are horizontal.** A 3D travelling front is the `onset` field with spherical geometry (or a `spectrum`
  field for continuous travel), summed into the figure.

## 5. Reuse decision

The scene uses the following unchanged:
- one ADR-1400 resonator with a designed bank (ADR-1404/1405: layout, per-band attack and release, focus sweeps,
  drift and spin);
- readers in their own frames, and the relief channel's signed slow figure plus vibration (ADR-1405);
- the onset field for transient wavefronts;
- the SDF renderer, procedural meshes, material programs and `response.*` routes.

There is no new analysis, no new resonance code and no new renderer. The only candidate engine change is the volume
bake (ADR-1440), and it waits on the Stage A measurements.
