# ADR-618: A save turned fifteen layers off on the shipping cast — a key the parser read and the writer never wrote

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-615 (one-ended contracts), ADR-617 (documentation that cannot rot), ADR-359 (a foot
layer is a different set of keys), Phase B §7 and §26–§28
**Implemented by:** the `l["weight"]` write in `Composition::toJson`
**Tests:** `tests/unit/test_character_lab_layers.cpp` — "an authored layer WEIGHT survives the
scene file"

---

## The defect

`PoseLayer::weight` was **parsed and never serialised**.

Both halves were individually careful. The parser draws the right line, with the right reason:

```cpp
// A manual layer is one a tool or a test drives, so its authored weight is its opening value;
// a driven layer's weight is written every frame from the seam and an authored one would be
// overwritten before it was read.
layer.weight = layer.drive == PoseLayerDrive::Manual ? *weight : 0.0f;
```

`"weight"` is in the `kLayerKeys` whitelist, so it is an officially-read key and **no "ignored key"
warning fires**. The serialiser writes `name`, `kind`, `drive`, the per-kind blocks, `joints`,
`weights`, `descendants`, `pivot`, `forward`, and then either `maxYaw`/`maxPitch` or `clip` — and it
is careful enough to pull Foot and Reach onto their own path because writing them through the
shared one produced a file that would not load.

It never writes `weight`.

**What neither side states:** the parser assumes an authored weight survives a round trip; the
writer treats weight as per-frame intent, which it is **for every drive except `Manual`** — the one
case the parser singles out.

## Why it matters: this is live, on the shipping cast

`examples/world/glowmere-valley-2-multicam.scene.json` authors **fifteen manual-drive layers, every
one at `weight: 1.0`** — `stride.l`, `stride.r` and `life` on each of five aliens. That is the whole
of Phase B §7's stride warping and §26–§28's secondary motion.

The cycle: load → 1.0 → `toJson` → key absent → reload → `readFloat` default **0.0** →
`PoseLayer::weight = 0.0f`, documented as *"0 = this layer does nothing at all this frame."*

> **One editor save of that scene turns off both stride warpers and the secondary-motion layer on
> all five aliens.** The characters stop stride-warping and stop breathing, the file still loads
> cleanly, and nothing warns.

The layer then resolves `LayerResolution::Inactive` — which, per ADR-615, **no live consumer
distinguishes** from any other reason a layer did nothing. Two documented-but-unread diagnostics
compounding: the one that would have said "this layer is off" is the one nothing reads.

## The fix, and where it is written

One line, and its **position is the decision**. `toJson`'s layer loop has **two** `push_back`
sites — the Foot/Reach early-out and the shared path — so a key added to one is a key the other
drops. The write goes *before* the branch, so both paths carry it and cannot drift. That is
`docs/testing.md` #38 applied at the moment of writing rather than after being bitten: **any fact
that lives in a copied region lives in N copies.**

Guarded by `drive == Manual`, mirroring the parser exactly, because saving a *driven* layer's
weight would persist a transient — whatever the seam happened to have written on the frame the file
was saved.

## The test, and its control

A round-trip assertion on the **shipping scene**, not a fixture, because that is where it bites:
it requires the scene to contain ≥15 manual layers with non-zero authored weight before asserting
anything, so the fixture cannot silently stop covering the case.

**The control is the half that stops this being a bad fix:** a driven layer must **not** gain a
`weight` key. Without it, "the weight round-trips" would have been satisfied by writing every
layer's weight, which would freeze per-frame state into the file.

**Teeth-checked: with the write removed, 30 of the test's 55 assertions fail.**

## Consequences

- **A second defect was found by trying to write the obvious test and is *not* fixed here.**
  `Composition::fromJson` on a `toJson` document cannot reload this scene at all: the document
  carries no path, so a relative reference like `../entities/craft-lights.profile.json` resolves
  against nothing and the load fails with `scene file ''`. **An in-memory round trip is therefore
  not available as a test technique for any scene with relative references**, which is why this
  test asserts on the emitted document. Recorded, not fixed — it needs its own before-and-after.
- **The class is "parsed but not written", and it is the mirror of the one this programme has been
  cataloguing.** Every earlier instance was a field written and never read. This is a field read
  and never written, and it is worse, because the first kind wastes memory and the second
  **destroys authored work**.
- **A key in `kLayerKeys` is a promise the writer must keep.** The whitelist exists so an author is
  told when a key is ignored; a key on the list that the writer drops is ignored *silently and on
  the way out* instead. Any key added to that array should be checked against `toJson`.
