# ADR-604: Every character carries its own copy of the animation pack, and at a hundred characters that is 300 MB

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-603 (the §47 baselines), ADR-550 (the skeleton digest), Phase B §48
**Implemented by:** nothing — this records a measurement and names where the fix belongs
**Tests:** `tests/unit/test_multi_character.cpp`

---

## Context

§48: "Ensure static/shared data remains shared … **do not duplicate large animation data.**"

§47 measured that the per-character *time* does not grow with the character count: +0.9% from one
character to a hundred. That is a necessary condition and not a sufficient one, and it could not
have found this, because duplicated data costs memory and cache — not cycles in a loop that never
touches the duplicates. **Timing could not see it; reading the header could.**

`scene::SkinnedRig` holds:

```cpp
struct SkinnedRig {
    std::string name;
    Skeleton skeleton;                  // by value
    std::vector<AnimationClip> clips;   // by value
    ...
```

Measured on `glowmere-valley-2-multicam.scene.json`, after ticking the composition three times
(rigs are installed on update, not on load — counting them straight after `loadFile` reports a
confident zero, which is `docs/testing.md` family C):

| signature | instances | each | clips |
|---|---|---|---|
| 28 joints × 1 clip | 6 | 0.05 MB | differ |
| 30 joints × 1 clip | 3 | 0.05 MB | byte-identical |
| **90 joints × 26 clips** | **5** | **3.01 MB** | **byte-identical** |

14 rigs, **15.45 MB of skeleton and clip data, of which 12.12 MB — 78% — is a byte-identical second
copy.** "Byte-identical" is an FNV-1a digest over the actual key times and values, not over names
and sizes; the first version of the test compared names and sizes, which two rigs could satisfy
while animating differently in every key.

The five aliens are five *different* GLB files — scout, diver, elder, ranger, pilot — with different
meshes. They ship the same 26-clip pack each. So the duplication is not only a runtime instancing
problem; it is in the assets, and one instance of each character already carries five copies.

## Decision

**Record it with the number that makes it matter, and do not refactor it inside Phase B.**

The 12 MB is not itself alarming. The extrapolation is: §47 profiled a hundred characters, and a
hundred characters of this rig is **301 MB of animation data of which 298 MB is the same bytes**.
That is the number this ADR exists to put on the record, because the present scene is small enough
that nobody would act on 12 MB and a future scene will not be.

The fix belongs at **load**, not at use: `assets::AssetRegistry` already keys loaded assets by
source path, so a clip pack keyed the same way, held as `std::shared_ptr<const
std::vector<AnimationClip>>`, gives every rig loaded from the same file the same storage. Two things
make it tractable and one makes it risky:

- Tractable: every consumer already takes `const std::vector<AnimationClip>&` — `sampleClip`,
  `PoseLayerStack::apply`, `AnimationPlayer::sampleInto` all do — so the call sites do not change,
  only the member's type and the loader.
- Tractable: ADR-550's skeleton digest already exists to decide whether content belongs to a rig,
  which is exactly the identity question a shared pack needs answered.
- Risky: `SkinnedRig::analyse` writes `clipPhases` and `clipContacts` **parallel to `clips`**, and
  `rootMotion` binds by clip index. Those are per-rig results derived from shared input, and a
  refactor that shares the input must not accidentally share or desynchronise the derived arrays.
  That is the part to get wrong, and it is the reason this is not a five-line change.

## Consequences

- The test asserts that the duplicate exists and is more than a quarter of the total. That is
  deliberately a recorded finding rather than an aspiration: if someone lands sharing, this fails
  and says so. The alternative is that the number quietly changes and nobody learns it was ever
  true.
- **The other half of §48 is in good shape and is asserted too**: per-instance state is genuinely
  per-instance. Two rigs from the same file have different storage, and posing one leaves the other
  untouched. `Pose`, `PoseLayerStack` state, velocity, phase and targets are all per-instance
  already, which is what §47's +0.9% was really measuring.
- The farm pack's 28-joint rigs are the interesting control: six instances whose clips **differ**.
  Sharing keyed on source path would correctly not share those, which is a small piece of evidence
  that the path key is the right key.
