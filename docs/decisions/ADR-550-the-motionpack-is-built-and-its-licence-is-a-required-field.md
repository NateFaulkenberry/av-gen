# ADR-550: The MotionPack is built, and its licence is a required field

**Status:** Accepted
**Date:** 2026-09-20
**Supersedes the design half of:** ADR-542 (which proposed the format; this is what was built)
**Related:** ADR-086 (rigs are copied per node), ADR-192 (86% of each alien file is animation),
ADR-540, ADR-546 (contacts and phase), ADR-548 (retargeting), ADR-549 (BVH)
**Implemented by:** `src/scene/motion_pack.{hpp,cpp}`
**Tests:** `tests/unit/test_motion_pack.cpp` — 9 cases, 241 assertions

---

## What was built

```
character.motionpack/
  pack.json      version, skeleton digest, PROVENANCE, and the clip index
  skeleton.bin   joints: parents, rest transforms, names
  clips.bin      the animation, channel for channel
  meta.bin       per clip, per frame: phase
  features.bin   (Phase C)  named, not written
  model/         (Phase E)  named, not written
```

Four decisions are worth recording because each was a choice between two defensible options.

### 1. The licence is a refusal, not a field

`buildMotionPack` **fails** when provenance has no licence or no source. Not a warning. ADR-542
argued this and the test leads with it, because a test that only checks the happy path passes on an
implementation that writes the field and never reads it.

The error message says what to do about it rather than only what is wrong: *"set it, or mark the
source REQUIRES_REVIEW and say why in `notes`"*.

### 2. `RequiresReview` is a state, and it is the default

Redistribution has **three** values, not two, and the third is the one that matters. Phase 0's
survey found sources whose terms are genuinely ambiguous — a dataset whose page contradicts itself
about BY versus BY-SA, a "free" pack whose licence text does not exist. For those the honest answer
is neither yes nor no.

**A reader that finds the field absent or unrecognised returns `RequiresReview`, never `Allowed`.**
Defaulting the other way would turn a field nobody filled in into a shipping permission, and the
test strips the key out of a written `pack.json` to prove it does not.

Provenance is **per clip**, not per pack, because a pack may mix a CC0 character's own takes with
CC-BY corpus motion, and `MotionPack::redistribution()` takes the **worst** state: one forbidden
clip makes the whole pack unshippable.

### 3. Channels, not a resampled pose matrix

ADR-542 sketched "resampled poses at a fixed rate". What is stored is the clip's channels as they
are. A pack's clips have already been through a resampling — the retarget, or the BVH import — and
resampling again to store them is a second lossy step for no gain. The analysis that *is* carried
(contacts, phase, travel) is carried precisely so a runtime does not redo it at load.

### 4. A skeleton digest, so the wrong rig is loud

FNV-1a over every joint's name, parent and rest transform. A pack played on a rig it was not built
for is the silent-wrong-character failure; the digest makes it an error. It is not cryptographic —
the question is "is this the same skeleton", not "has someone tampered with it".

## Evidence

| arm | result |
|---|---|
| no licence | **refused**, and the message names `REQUIRES_REVIEW` as the alternative |
| no source | refused, and the message says "ancestry" |
| the same fixture with the fields filled | builds — so the refusal is about the field |
| `redistribution` key deleted from a written pack | reads back as **`RequiresReview`**, and the report says so |
| one forbidden clip among two | whole pack `Forbidden`, validation **FAIL**, report says "must not ship" |
| round trip | skeleton, every channel's every key, **every phase sample**, contacts, and the provenance compared field by field |
| digest | a different leg length and a renamed joint both change it; the same skeleton twice does not |
| version + 7 | refused: "cannot be read by this build" |
| truncated `clips.bin`, wrong magic, missing file | each refused, naming what ran out |
| **the real 26-clip Glowmere pack** | builds, `Allowed`, validation **PASS**, 1,700+ frames, 0 clips without contacts, round-trips |

## What this does not do yet

`features.bin` and `model/` are named and not written — ADR-542 said design for versioning and do
not implement fields that are not needed. Nothing here is shaped around a neural architecture, and
a pack *generated* by one must stay readable without it (Phase E §10, F §88).

Nothing yet reads a pack at runtime. `SkinnedRig::clips` is still a per-instance copy; pointing it
at a shared immutable pack is the change that fixes ADR-192's memory scaling, and it is a separate
unit with its own risk.

## Revisit triggers

* The first pack built from a corpus rather than from one character: `provenance` becomes a real
  list rather than a list of one, and the per-clip index starts earning its keep.
* Runtime consumption: when `SkinnedRig` points at a pack instead of copying clips.
* `features.bin` (Phase C) and `model/` (Phase E) — both need a version bump and neither needs a
  format change.
