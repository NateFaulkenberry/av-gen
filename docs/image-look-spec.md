# Image / Look — revised implementation spec

**Status:** ready to start. Supersedes the owner's original §51–89 brief, which was written before
the 2026-09-18/19 session. The owner's intent is unchanged; the sizing is not.

**Why this revision exists.** The original spec assumes the engine has no authoritative image state,
no documented pass order, no disabled-path guarantee, and no second renderer. All four assumptions
are now false. Roughly half of "Phase 1 Foundation" is already built, and the night's work moved
ground the spec's audit was meant to map. Building to the original text would mean creating a second
state object beside `scene::PostSettings` — which is the one thing the spec's own §52 forbids.

---

## 1. What already exists — verify before building

**A single authoritative state object.** `scene::PostSettings` (`src/scene/post_settings.hpp`, 195
lines) already holds exposure, bloom, halation, anamorphic, colour grading, lens distortion and
chromatic aberration, depth of field, tilt-shift (ADR-079), motion blur (ADR-040), output effects,
tone mapping and output. Plus `PostParameters`. **This is §52's object. Extend it; do not create a
rival.**

**A fixed, documented pass order.** `post_processor.hpp` states it, and `docs/image-formation.md`
(388 lines) documents it:

```
scene HDR -> [metering of the pre-exposure image] -> [exposure] -> [defocus] -> [motion blur]
          -> [lens distortion + chromatic aberration]
          -> [bloom: prefilter, downsample, energy-conserving upsample]
          -> [halation pyramid + anamorphic streaks: the "wide" tier]
          -> composite (bloom + wide tier + colour grade) -> [fxaa] -> [sharpen]
          -> HDR result for tone mapping
```

That satisfies most of §51.2's frame flow and pass table. **The audit's job is now to verify and
extend that document, not to reconstruct it.**

**The disabled-path guarantee, already true.** `post_processor.hpp`: *"Passes run only when their
settings are active; with everything off and a unit exposure the input is returned unchanged."*
That is §60. It exists. **Your job is to keep it true, with a test that proves it.**

**Tone mapping.** `TonemapOperator { AcesFitted, AgX, Reinhard, PbrNeutral, Clamp }`, default AgX
(ADR-039). §52.2's operator list is already here. **§89: do not replace AgX without evidence.**

**Auxiliary targets.** ADR-035 gives emission, velocity and object-id targets; selective post
already uses them and *"silently falls back to the luminance-only behaviour"* when absent. That is
§64/§65's capability-driven allocation, working. `RenderSettings::aovNames()` is the AOV vocabulary.

## 2. What the night of 2026-09-18/19 changed, which the original spec cannot know

- **ADR-345** decoupled lighting from visible background and **appended four vec4s to
  `FrameUniforms`** — the most widely shared uniform block, read by every pass including shadow
  views. Anything you add there follows that precedent: append, never insert.
- **ADR-347** made fog take its colour from the sky's own horizon, and corrected a fog density that
  was 15× too high. Aerial perspective is therefore *already* partly what §68.1 asks for. Measure
  what atmospheric integration would add on top of it before building a second mechanism.
- **ADR-351/352/353** added a second renderer. `src/pathtrace/` produces its own linear HDR and AOVs
  and writes EXR. §75's "both renderers produce compatible signal names" is now a live requirement,
  not a future one — and the path tracer's AOV names were already reconciled against
  `aovNames()` with a test that fails if the vocabularies drift. Extend that test; do not fork it.
- **ADR-352** records that the glTF BRDF **gains up to 68% of its energy at grazing angles** and is
  deliberately kept faithful. It compounds per bounce. **Relevant to §68.3 local contrast and §68.1
  atmospheric integration**, both of which operate on scene-referred values that may be hot.
- **ADR-355** found the editor frame was 350 ms because of a per-frame vertex rescan. §82/§83's
  performance work must measure the **live editor** (`--profile-cpu`, which prints CPU stages only
  there) and not only the headless path. Every diagnosis made headless has been blind to those
  stages.

## 3. Revised phase plan

### Phase 0 — Audit (do this first; it is cheap and it is the deliverable)
Produce `docs/image-look-audit.md`. **Start from `docs/image-formation.md` and verify it**, marking
each row confirmed or corrected. Then add what it lacks:
- The **colour-space map** §51.2 asks for, in explicit names — scene encoding, lighting output,
  where exposure is applied, bloom extraction, AgX in and out, grading space, grain, display, HDR
  output. No "linear-ish".
- The **resource table** — producer, consumers, format, lifetime, resolution — over the transient
  pool.
- **Where the two renderers agree and differ** on the same signals.
- The **Phase 0 baseline**: the eight captures §51.2 lists, as committed scene arms that regenerate.

### Phase 1 — Extend the state model
- `ImageLookIntegration` (§52.1) is genuinely new: atmospheric, colour, light wrap, local contrast.
  **Add it to `PostSettings`**, or to a member of it, with §53's metadata and §54's namespaced IDs.
- Everything else in §52.2–52.6 is a **mapping exercise** onto fields that exist. Where a field is
  already there under a different name, keep the existing name and record the mapping; §54 says do
  not rename existing parameter IDs without migration.
- §55 presets and §56 versioning are new and small.
- **Register every new parameter** and prove it round-trips. Four subsystems shipped unreachable in
  one session; a day/night cycle ran for hours with no parameters at all.

### Phase 2 — Cinematic integration
Unchanged from the owner's §68, in their order: atmospheric, colour, local contrast, light wrap.
**But measure against ADR-347's fog first** — some of §68.1 may already be in the frame.

## 4. Constraints that are not negotiable

- **§60 / §87: with integration at zero, the image is unchanged.** Not "close" — prove it with a
  hash of the float buffer before tonemap, with a control that perturbs one setting and shows the
  hash moves. This is the milestone.
- **One state object.** No UI-only state, no renderer-only state, no separate offline artistic
  configuration (§75, §76).
- **Tone mapping happens exactly once** (§58.5).
- **No CPU readback for exposure** (§66). If one exists, that is a separate finding, not a licence.
- **Numerical agreement is not proof of visual alignment.** Every serious bug in the session that
  preceded this was invisible in numbers and obvious in a frame.
- **Do not register parameters for features not built.** The mirror of ADR-225 and the opposite
  failure to the four unreachable subsystems.

## 5. First concrete action

Read `docs/image-formation.md` and `src/scene/post_settings.hpp` in full, then write
`docs/image-look-audit.md` §1 as a verification of the existing pass table — each row confirmed,
corrected, or missing — before touching any code.
