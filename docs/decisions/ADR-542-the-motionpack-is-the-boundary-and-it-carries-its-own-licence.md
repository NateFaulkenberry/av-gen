# ADR-542: The motionpack is the offline/runtime boundary, and it carries its own licence

**Status:** Proposed (Phase 0 research; not to be implemented before review)
**Date:** 2026-09-20
**Related:** ADR-005 (glTF canonical), ADR-019 (project system, relative asset references), ADR-020
(offline rendering), ADR-064 (the job system), ADR-086 (rigs are copied per node instance),
ADR-192 (the alien pack), ADR-353 (a prebuilt binary because building it needs a compiler we do not
have), ADR-540, ADR-541
**Full analysis:** `docs/design/autonomous-character-animation.md` §8, §9.4-9.5, §10

---

## Context

Three separate problems turn out to have one answer.

**1. There is no way to get motion onto a rig.** `Importer::importClips` fills only the rigs built
in the same `loadGltf` call (`src/assets/gltf_loader.cpp:342-364`). No retargeting of any kind
exists in `src/`.

**2. Clip memory does not scale.** `Composition` deep-copies the whole rig per node instance —
`SkinnedRig rig = src;` (`src/scene/composition.cpp:4879`) — with the stated and correct reason
that "two nodes on the same character file are two characters, and they must be able to be doing
different things" (`:4873-4875`). The *pose* must be per-instance. The *clips* need not be. At
156,921 keys × (4 + 16) bytes, one alien's clip data is ~3.5 MB, so five characters cost ~17.5 MB
and two hundred would cost ~700 MB.

**3. Licensing is the binding constraint on content, and a rule in a document gets broken.**

The licence survey found a genuinely usable corpus and a large minefield either side of it.

**Shippable, verified from primary sources:** **100STYLE** (CC BY 4.0 per the Zenodo record — four
million frames of stylized locomotion), **ACCAD Open Motion Project** (CC BY 3.0, *direct from Ohio
State*), **CMU** (*"You may include this data in commercially-sold products, but you may not resell
this data directly, even in converted form"*), **Kenney** (CC0), and this repository's own alien and
farm packs (CC0). **Mixamo** and **Quaternius** may be *embedded* but not redistributed as assets,
and Adobe's General Terms §17 forbids training on Mixamo content at all.

**Not shippable:** **AMASS** (*"prohibits the use of the Dataset to train methods/algorithms/neural
networks/etc. for commercial use of any kind"*), **SMPL/SMPL-X** (same terms, and patented),
**LAFAN1** (CC BY-NC-**ND** — *"you do not have permission under this Public License to Share
Adapted Material"*, and a retargeted database is Adapted Material), **Human3.6M**, **KIT-ML** (no
licence text exists), **HumanML3D**, **AI4Animation** (no licence file; README says research and
education only), **SAMP** (*"You may not redistribute the Research Materials"*).

Two traps that cost real diligence:

* **You cannot launder permissiveness out of AMASS.** The MPI umbrella is the floor, not the
  ceiling: **CMU data inside AMASS is more restricted than CMU data from CMU.**
* **An MIT `LICENSE` on a motion-dataset repo covers the processing scripts, not the motion.**
  HumanML3D is MIT and contains no motion data, because AMASS forbids redistributing it.

And the generative chain: MDM, MotionDiffuse, MLD, T2M-GPT, MoMask, MotionGPT, MotionLCM,
StableMoFusion and MotionCLR are all trained on HumanML3D and/or KIT-ML, which derive from AMASS.
**Their output is not shippable**, whatever their code licence says.

## Decision

**One versioned file crosses the offline/runtime boundary, and it is the only thing that does.**

```
character.motionpack
  pack.json      version, source skeleton hash, joint names, BUILD PROVENANCE, LICENCE
  skeleton.bin   normalised skeleton: joints, parents, rest pose, bone lengths
  clips.bin      resampled poses at a fixed rate, shared read-only by every instance
  features.bin   float32 [frames x D], normalised and weight-folded at build time
  meta.bin       per frame: clip id, local time, phase, contacts, tags
  ranges.json    per clip: name, tags, loop flag, blend regions, authored stride speed
  model/         optional network weights, flat versioned binaries
```

Four consequences, each answering one of the problems above:

1. **`avgen-motion-build` is an offline tool** and is the only thing that reads a `.glb`, `.fbx` or
   `.bvh` for motion, retargets, detects contacts, extracts phases and trajectories, builds
   features, and validates. `src/app/job_system.hpp` already provides staged progress, an honest
   ETA and prompt cancellation for exactly this shape of work.
2. **`SkinnedRig::clips` becomes a reference to an immutable shared pack**, not a copy. The pose,
   palette, player and layers stay per-instance; the clip data is shared. This is the fix for
   problem 2 and there is no other one.
3. **`pack.json` carries a required `license` field and the build fails without it.** Not a
   warning. This codebase has a standing lesson that an unreported no-op is the defect
   (`src/scene/pose_layers.hpp:255-259`, `JointMask::missing`, `LayerResolution`, `IkStatus`,
   `PathStatus`). A pack also records every source file, its licence, and the transform chain
   applied — a derived database whose ancestry cannot be printed is one nobody can clear.
4. **If a pack carries a model, the model is a flat blob with a real header, not ONNX.** §10
   measured the alternatives on this machine: ONNX Runtime's macOS arm64 dylib is 41.8 MiB, is
   4-10× slower than Accelerate on a 3-layer 512-wide MLP, and makes no reproducibility promise;
   Core ML pays a **~0.23 ms dispatch floor per evaluation regardless of work**; any synchronous
   GPU path pays a **275 µs Metal round-trip that is insensitive to the work inside it**. A
   hand-written forward pass is 23.9 µs, and 10.4 µs through `cblas_sgemv`. ONNX stays as the
   *export* format out of PyTorch and is converted at bake time.

   The header must carry: magic + format version, an architecture hash, the normalisation
   statistics, the **feature schema** (bone order, joint count, layout, latent dimension, rotation
   convention, units), the **timestep assumption**, a training-data provenance ID, and the numeric
   contract (fp32, `-ffp-contract=off`) plus a golden input→output hash. Holden's own `nnet_load`
   has none of these and will load a mismatched file into plausible garbage, silently.

**Python, PyTorch and any training framework are build-time tools, in the same category as a shader
compiler. The runtime links none of them.**

## Rejected alternatives

* **Keep reading `.glb` at runtime and add a retarget-on-load step.** It puts a minutes-long
  offline pass on the load path, it cannot be validated, and it would make a character's motion a
  function of the importer version rather than of a file somebody can inspect.
* **Ship ONNX Runtime.** Measured above. AV Gen has exactly one precedent for a prebuilt ML-adjacent
  binary — OpenImageDenoise — and ADR-353 records its cost: 51 MB, `OFF` by default, unbuildable
  from source without oneTBB and a third-party compiler. That is the ceiling on what a dependency of
  this shape is worth, and a 3-layer MLP is far below it.
* **A licence field that warns instead of failing.** A warning in a build log is how research
  assets ship.
* **Share the whole `SkinnedRig` between instances to save memory.** ADR-086 already refused this
  and was right: two characters must be able to be doing different things. Sharing the *clips* is
  the part that was never the problem.
* **Track licensing in a spreadsheet.** It is not in the artifact, so it is not in the build, so it
  is not checked.

## Consequences

* The one unit everything else depends on — retargeting — has an owner and an output format.
* Character clip memory stops scaling with instance count, which is the only credible answer to the
  brief's "possibly hundreds of lightweight autonomous characters".
* A shipped pack can be audited: "which corpus is this, under what terms, transformed how".
* 100STYLE and ACCAD become reachable, which is what turns ADR-540's content gate from closed into
  merely expensive.

## Revisit triggers

* A dataset arrives whose terms permit training but not a derived database, or vice versa — the
  `license` field needs to express more than one verb.
* The model half is ever needed: re-run §10's measurements before choosing a runtime, because they
  are hardware- and toolchain-specific and the conclusion is a measurement, not a principle.
* Anyone proposes Mixamo as training data: Adobe General Terms §17 forbids it, and the permission
  and the restriction come from different documents.
