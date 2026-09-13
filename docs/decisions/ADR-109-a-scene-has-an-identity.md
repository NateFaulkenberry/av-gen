# ADR-109: A scene has an identity, not just an address

Status: accepted
Date: 2026-09-13

## Context

`SceneRenderer` uploads meshes and textures once and then asks, every frame, whether it can skip:

```cpp
if (&scene == meshScene_ && scene.meshVersion == meshVersion_ && meshes_.size() == scene.meshes.size())
    return;
```

Three claims, none of which is an identity.

An **address** is not one: two scenes are never alive at the same address, but they are very easily
alive at it one after the other. A **version counter** is not one either — every fresh `Scene` starts
`meshVersion` at 0, so the counter says *when* a scene changed and never *which* scene changed. And
a **mesh count** is a coincidence waiting to happen.

Put together: destroy a world, build another in its place, give it the same number of meshes, and the
renderer keeps the first world's vertex buffers until something happens to bump the counter. The same
hole was in `uploadTextures` and in the environment/IBL guard.

The existing regression test for this (`SceneRenderer does not reuse same-version meshes across
scenes`) puts the two scenes *side by side*, so they have different addresses and the pointer check
catches it. The case it cannot reach is the one a project actually performs: one `Scene` object,
refilled.

This is the ninth of the eleven boundary defects in shape if not in origin — a value that identifies
something being inferred from something that merely correlates with it.

## Decision

`Scene` carries a process-unique `identity`, minted at construction by `scene::mintSceneIdentity()`
(a free-running atomic counter starting at 1, so 0 stays available as "no scene yet"). The three
upload guards key on **address, identity and version together**.

`Scene::clear()` mints a fresh one. A Scene emptied and refilled is a different scene wearing the
same object, and the alternative — inferring it from `meshVersion`, which also moves for an ordinary
edit — is the confusion this exists to end.

Copying carries the identity with the content. A copy *is* that scene's data, and two live copies are
separated by the address check. Assigning one scene over another therefore brings the source's
identity with it, which is precisely what invalidates the cache in the refill case.

## Measurements

None to report: the guard is three comparisons of a `std::uint64_t` per frame, and the frame did not
move. What was measured is the defect. With the identity check removed and nothing else changed, the
regression test renders a 0.25-unit cube with a 1.0-unit cube's geometry and the two image hashes are
byte-identical (`0x9d16399fbbe8a983`). With it, they differ.

The test's own control is the other half: the same `Scene` object refilled with the *same* world must
render the same picture, so the test is measuring the geometry and not the reassignment.

## Rejected alternatives

- **Bumping `meshVersion` on construction, or seeding it randomly.** Turns a certainty into a
  probability, and leaves `textureVersion` and the environment guard to be remembered separately.
- **Making the identity a class whose copy constructor mints a new one.** Strictly stronger — even a
  copy would be a different scene — and a trap: `const SceneIdentity snapshot = s.identity;` silently
  renames the thing it was trying to remember, which cost two debugging rounds while writing this.
  The renderer's own cached copy would have minted a new identity on every assignment and re-uploaded
  every mesh every frame, and the regression test would have passed anyway.
- **Keying on a content hash instead.** A hash of a world's meshes is not cheap enough to take every
  frame, and the version counters exist so that it never has to be.
