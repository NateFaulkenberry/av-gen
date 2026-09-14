# Phase 9 — lateral clearance

The last piece of §9 that was genuinely new engineering rather than connecting what already existed.

---

## 1. The defect

`world::ClearanceField` kept a camera out of the scenery by lifting it, and lifted it out of heroes
too — by the *horizontal* penetration depth, applied to `y`:

```cpp
const float inside = field.heroPenetration(out.position);
if (inside > 0.0f) { out.position.y += inside; }
```

That is correct for a cut-based film. Being inside a hero means the camera came too close on its way
past, and rising over it keeps the subject in frame where sliding around it would not — the comment
said so and was right.

It is exactly wrong for the continuous-shot mode Phase 8 shipped. A take that dollies toward a
mushroom and orbits it gets **carried over the thing it is circling**, which is a worse failure than
clipping: it looks deliberate.

## 2. The fix, and its shape

**Which way the correction goes now depends on what is in the way.** Ground and canopy lift — there is
nowhere sideways to go from inside a hill, and a camera that rises to clear a treeline reads as
choosing its altitude. Heroes push out sideways, to the nearest point outside their capsule.

`ClearanceField::heroPushOut` returns a planar vector whose length is the penetration.
`ClearanceAdjustment` gains `pushed` beside `lifted`, so a caller can tell the two apart.

Three details that are decisions rather than details:

**Order: push, floor, push again.** The push can land the camera somewhere lower, so the floor has to
apply to where it ends up; and a push out of one hero can land inside another. One extra iteration,
not a loop — two heroes overlapping enough to trap a camera between them is a staging problem, and a
solver that hides it is worse than a bake that shows it.

**On a hero's axis there is no direction**, and the fallback is +X. Deterministic rather than correct,
because "correct" does not exist there and an unseeded or NaN direction would make a bake
non-reproducible, which is the one thing a baked camera may not be. Tested.

**The smoothing smooths the offset, not the position.** The vertical pass can use "never below the
floor each point needed"; the lateral pass cannot, because that has no meaning when neighbours were
pushed in different directions. Smoothing the *offset* — which is zero away from the obstruction —
leaves an untouched point exactly where it was authored, endpoints included. The first version
smoothed positions and moved the far end of a forty-metre dolly by 0.9 m when the only thing in the
way was at the middle. After smoothing, clearance is **re-asserted**, which is the only way to be sure
the average did not put a point back inside what it was moved out of.

## 3. Evidence

A forty-metre dolly straight through an eight-metre hero, cleared:

- **every point ends up outside the hero** — penetration under 0.05 m along the whole path;
- **it went around, not over** — 9.2 m of lateral deviation and under 0.5 m of rise, where the old
  correction produced about 9 m of rise and no lateral movement at all;
- **the endpoints are untouched**, byte for byte;
- **it is deterministic** — a second run reproduces every point to 10⁻⁵.

Negative controls, because a correction that fires when it should not is worse than one that does not
fire: a path with no heroes in the field is lifted and **not moved sideways at all**, and a path with
nothing in the way is returned byte for byte.

### A test asserted the old contract, and changed with it

`"A camera inside a hero is lifted over it"` is now `"...is pushed out of it, not over it"`. The
contract changed deliberately and the assertion changed with it, with the reason written where the
assertion is rather than in a commit message.

### And a fixture lied

The first version of the new test reported 3.3 m of rise and failed. The rise was real and correct:
`flatMap()` is a *generated terrain*, not a plane — the name is older than the fixture — so a path at
a constant height above the ground at the origin had its endpoints inside a hill, and the "rise" was
the terrain lift doing its job. The test now flies above the highest ground along its own length, so
the hero is the only thing in the way. **The code was right and the fixture was wrong**, which is
worth recording because the first instinct was to loosen the threshold.

## 4. What this does and does not buy

It buys the orbit. A continuous take can now dolly to a hero and arc around it without being carried
over it, which is what §9's continuous-shot mode was for.

It is **not** a camera collision system, and deliberately so. It knows terrain, a statistical canopy,
heroes and clearances — the same four things it always knew. It does not know about buildings, water,
moving entities, or anything that arrives after the shot is cut. `docs/auto-director.md` said that
before this change and still does.

## 5. What is left in §9

- **Per-hero cinematic regions** — approach direction, orbit arc, entry and exit transitions.
- **Camera diagnostics** — path, target, hero volumes, speed, progression.
- **Arc-length parameterisation** — speed within a shot is nearly uniform already; this is polish.
- **The panel's appearance**, which I cannot see and have not claimed.
