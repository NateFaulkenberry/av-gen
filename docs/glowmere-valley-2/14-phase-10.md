# Phase 10 — three dead properties, §11 verified, §10 audited

---

## 1. The three dead authored properties, resolved

All three were found by the panel work in Phase 8: authored, serialised, and read by no code. A
format with dead fields in it is dead weight, and a *panel* with dead fields in it spends the user's
trust. All three are now wired.

### `CompositionProfile::framing` and `headroom`

`framing` is the subject's normalised offset from centre and `headroom` is extra space above it,
which is the same thing as sitting it lower in frame. To put a subject anywhere but centre the aim
moves the *other* way by the screen offset projected out to the subject's depth.

Two things went wrong and both are worth having:

**The offset scales by depth along the view axis, not by straight-line distance.** A perspective
divide is by *z*; using the distance overstates the offset by 1/cos of the off-axis angle. This is a
**fixed point** the iteration converges to and never leaves — six steps of the wrong formula gave the
same 0.00143 residual as three, which is what said the problem was the formula rather than the
convergence.

**It needs iterating.** Moving the aim rotates the basis the offset was measured in.
`Composition::applyFraming` takes three steps for the same reason; this offset is larger and takes
six. The step count was raised because the test asked for it, rather than the tolerance being
loosened to accept three.

Four tests changed, because the contract changed. They asserted `targetAt(t) == subject.position` —
true only while these two fields were read by nothing. They now project the subject through the
shot's own camera and assert it lands *where the composition asked*, which is both the stronger claim
and the one those fields were authored to mean.

### `HeroPoint::preferredCameraElevationDegrees`

Wired rather than dropped: "look up at this one, down into that one" is a real opinion a subject has.
Honoured as a **bias** on the kind's own elevations, both ends shifted together, so a reveal that
rises through its shot still rises.

**And a trap was waiting inside it.** The hero's field is in *degrees*. `Shot::startElevation` is not
an angle at all — it is a height as a multiple of the orbit radius (`orbitPoint` computes
`elevation * |r|`). Wiring degrees straight into a ratio put the camera thirty-four radii in the air,
and the sequence failed its own *"the hero is a speck"* check. **A dead field is dangerous to wire
naively precisely because nothing has ever checked its units against its consumer's.**

## 2. §11, verified rather than assumed

Phase 2 registered the example. Eight phases later "it is still registered" is a claim, so each
clause is now a test against the shipped files:

- it appears in the examples list **through the loader the menu uses**, with a description and a
  category, and **it did not displace the four Glowmere entries that were there before**;
- its project resolves its scene and names its audio, and the stale content hash Phase 2 dropped
  stayed dropped — a wrong hash is worse than none;
- **every route names a node the scene still has** — the failure Phase 5 created and fixed by hand,
  now guarded;
- the heroes are in **strictly descending importance**, which is ADR-072's contract;
- its configuration is one authored file naming seed, river, habitat bands, budget, hero sites,
  clearings and camera.

### It caught a real bug immediately

The heroes were in the wrong order. Appending the six new ones after the survivors left the **UFO
(0.68) ahead of the elder (0.95)**, and `briefFromHeroes` takes the first as the film's *subject* —
the one that gets the builds and the drops. The Auto-director would have cut a film about the
spacecraft. Found by a test asserting the contract, not by watching a film with the wrong subject.

## 3. §10, the audio audit

Twenty-two routes. The chain is unchanged from the painterly scene's architecture — `audio → analysis
→ signals → ModRoute → parameters` — and every route is `add`, unipolar, linear, unclamped.

| group | count | state in this scene |
|---|---:|---|
| hero emission | 4 | repointed in Phase 5 to the new cap and gills; **one was inert and is fixed, §3.1** |
| shared tissue | 2 | `music.beat` and `music.impact` on `glowmereTissue` |
| spores | 4 | the volume was moved to the new valley floor; ranges unchanged |
| wind and volume | 3 | scene-scale, unaffected by the new geography |
| **water and river** | **7** | **the range review this scene actually needs, §3.2** |
| the practical light | 1 | follows the elder, repointed |
| removed | 1 | `procedural/elder-filaments/distribution/radius` — a generated mushroom's gills are not a radial distribution, and no equivalent parameter exists |

### 3.1 One route was inert, and the dangling-name check could not see it

`audio.bass → material/paintedCrown/emissionIntensity` drove the *old* elder's cap program. The
heroes use `glowmere2Cap` now, and **nothing in this scene draws with `paintedCrown` at all** — so the
route bound successfully, because the program is still *carried*, and modulated a program no surface
uses.

This is a different failure from Phase 7's dangling name and the check cannot catch it: the name
resolves. **A carried-but-unused program is a working reference to nothing**, and the only way to see
it is to ask which programs any surface actually names. Repointed to `glowmere2Cap`; `paintedCrown`
and `bushGlow` dropped from the scene's program list along with it, which also returns two of the
eight program slots.

### 3.2 The water routes need a range review, and did not get one

Seven routes drive water glow, ripple, swell, sparkle, foam and the river motes. They were tuned
against a **7 m stream**; this scene's river is **700 m of channel** that the camera travels along.
The same `+1.3` on `water/glow` is now applied to two orders of magnitude more surface, and Phase 6
already had to halve the water's reflection and specular for exactly this reason.

**They are left as they are and flagged.** Retuning them is a look-development pass that wants the
audio actually playing — `docs/glowmere-audio.md` records that none of these mappings has ever been
auditioned with sound — and guessing at ranges without hearing them would be inventing numbers, which
is the thing this project's registers exist to prevent.

### 3.3 One relationship got substantially better by accident

`glowmereTissue` carried the scene's two headline musical routes and, in the painterly scene, was
named by **three scatter layers — 1,771 of 113,560 instances, 1.6%**. In Glowmere Valley 2 it is
named by those three layers **and by five of the six heroes' gills and undersides**. The beat and the
impact now reach the objects a viewer is looking at, which is what those routes were for.

## 4. What is left

- **Per-hero cinematic regions** and **camera diagnostics** — §9's remainder.
- **Arc-length parameterisation** — polish on polish; speed within a shot is nearly uniform.
- **The water routes' ranges**, §3.2, which want ears.
- **The panel's appearance**, which I cannot see and have never claimed.
