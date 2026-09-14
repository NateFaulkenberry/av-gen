# Glowmere Valley 2 — handover

Branch `agent/glowmere-valley-2`, 14 commits from `main`. Tree clean, full suite green:
**1,610 cases, 508,082 assertions, 0 failures.**

---

## 1. What it is now

A selectable showcase example — **Glowmere Valley 2** in `examples/index.json` — that is a
640 × 640 m valley with a river entering at one map edge, meandering 700 m down a descending course,
and leaving at the opposite edge. Thirteen vegetation families are distributed on a riparian ladder
keyed to height above the water table. Six procedurally searched hero mushrooms stand in habitat
pockets along the bank. The Wanderer walks it, the Visitor UFO flies it, and the Auto-director can
cut a continuous take through it.

It renders at **10–14 ms** against a 16 ms ceiling — parity with the painterly Glowmere it succeeds,
on four times the readable ground. (Read that with the ±3 ms band of §6.)

| | |
|---|---|
| scene | `examples/world/glowmere-valley-2.scene.json` |
| project | `examples/world/glowmere-valley-2.json` |
| **configuration** | `tools/make_glowmere_valley_2.py` — regenerates both; the authored numbers live here |
| hero parameter sets | `examples/organisms/glowmere2-heroes.json` |
| materials | `examples/materials/glowmere2-{cap,tissue,painted-ground}.material.json` |
| docs | `docs/glowmere-valley-2/` (this directory), `docs/auto-director.md` |
| ADRs | 170–175 (see §7 for a numbering collision) |

## 2. Renders

**`build/glowmere-valley-2/` — 1920 × 1080, eight frames.** The three asked for:

| | |
|---|---|
| the opening shot | `build/glowmere-valley-2/01-opening.png` |
| the valley axis | `build/glowmere-valley-2/03-downstream-axis.png` |
| the elder, close | `build/glowmere-valley-2/07-elder-closeup.png` |

Also `02-upstream-axis`, `04-high-oblique`, `05-elder-and-pool`, `06-east-wall`,
`08-bloom-closeup`. Regenerate with
`tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.capture][glowmere2]"`.

## 3. Against the brief

**Done.** §3 audit and reuse matrix · §4 world composition, the traversing river, the valley
corridor, composition zones, negative space · §5 habitat-driven vegetation with deterministic
generation and coverage budgets · §6 the budget held by representation rather than deletion · §7 the
hero mushroom generator, candidate search, banded scoring, diversity selection and six serialised
winners · §8 the three primitive heroes removed, the Wanderer and UFO kept and promoted · §9 the
rename, shot modes, the settings panel, continuous-shot velocity, lateral clearance · §10 audited ·
§11 verified clause by clause.

**Not done, deliberately.** Per-hero cinematic regions, camera diagnostics, arc-length
parameterisation — polish, and better decided against this handover than absorbed into it. Emissive
veins, subsurface approximation and controlled wetness on the hero materials (§7's material list).
Curvature-aware river banks (needs a `Feature` field that does not exist). Field-of-Neighborhood
competition and per-species Poisson spacing — not rejected, just not needed once the habitat band
and the existing proximity and cluster fields were doing the structural work.

## 4. Open items, in priority order

1. **The water route ranges need ears.** Seven routes drive water glow, ripple, swell, sparkle, foam
   and river motes. They were tuned against a 7 m stream; this river is 700 m the camera travels
   along, and Phase 6 already halved the water's reflection and specular for the same reason. I left
   them and flagged them: `docs/glowmere-audio.md` records that none of these mappings has ever been
   auditioned with sound, and guessing ranges without hearing them would be inventing numbers that
   look authored.
2. **The Auto-director panel's appearance.** I cannot see ImGui. It compiles, the settings are
   plumbed, and the behaviour is tested headlessly — shot-length bounds change the shot count, mode
   changes whether shots are pinned, the seed changes the cast, the wide lens reaches the baked keys,
   invalid settings are refused. **None of that is a claim about layout.**
3. **The map edge is visible in the valley's notch.** `03-downstream-axis.png`: where the valley
   opens out downstream, the horizon between the two walls is the end of a finite map. Perimeter
   ridges close three sides; this gap is the fourth and wants one more feature or a fog adjustment.
4. **The elder's stem has a visible seam** along its length in `07-elder-closeup.png` — the sweep's
   duplicated seam column. Cosmetic, one-line-ish, not investigated.
5. **A measurement harness with a drift detector.** §6.
6. **Three ADRs to renumber at merge.** §7.

## 5. My read on the upland

Phase 7 widened bushes to 38 m of HAR and grass to 17 m, and the east side now carries bushes and
fungi rather than pines alone. **I think it is right and I would not push it further without a
reason.** The brief asks for negative space as a design feature, the heroes and the river both draw
the eye west, and an upland that is quiet is what makes the valley floor read as dense. The frames
that made me want to change it were wide obliques, which is the one viewpoint the scene is not
composed for.

If somebody disagrees after looking at `04-high-oblique.png`, the lever is two numbers in
`HAR_BANDS` — and the sign is known in advance, so the question is not the delta but whether the
absolute still fits (§6's corollary).

## 6. Measurement, which is the part I would most want carried forward

Three findings, each bought with a wrong conclusion:

- **The GPU lock serialises agents, not the device.** A timing taken beside an open `avgen` window
  measured a contended GPU and read as a 3.8× regression (ADR-170).
- **Between invocations of one binary over one byte-identical scene: 10.945 / 11.272 / 13.697 ms.**
  A 25% spread with everything the protocol asks for satisfied. **Every frame time in these docs
  reads with a ±3 ms band unless it says it was interleaved.**
- **Interleaving must be counterbalanced.** Co-locating arms in one process is not enough: an arm
  that always runs second always pays for whatever drift accumulates. Fixed-order interleaving
  reported that hiding six mushrooms made the frame 2.3 ms *slower*.

And the tool that found all three: **a causally impossible result is a free diagnostic.** Hiding
geometry cannot make a frame slower, so the arm indicted the method instantly. The converse is worth
having deliberately — an arm whose sign is known in advance is a test of the method; and when the
sign is known, the delta is often not the question.

What is still missing is a **drift detector**: measure arm A, run every arm, measure A again, void
the run if the two A's differ by more than the effect. Counterbalancing *averages over* drift; that
*detects* it, which is why a counterbalanced run can still return an impossible sign and give no
warning.

## 7. ADR numbering — needs resolving at merge

| mine | note |
|---|---|
| ADR-170 | the GPU lock does not establish exclusivity |
| ADR-171 | "Glowmere Valley" names two scenes |
| ADR-172 | an aesthetic score component is a band, never a maximum |
| **ADR-174** | **collides.** Mine is "Vegetation is banded on height above the water table"; the Tree of Life's ADR-174 is a different document |
| ADR-175 | a searched organism is a parameter vector the scene owns (`PrimitiveKind::Generated`) |

ADR-173's provisional status was lifted in Phase 5 when its own falsifying experiment was run.

## 8. Things a reader should not have to rediscover

- **"Glowmere Valley" names two scenes**, and the one the index literally calls that is the wrong one
  (ADR-171). Everything here succeeds `glowmere-stylized.scene.json`.
- **Removing small and distant instances is free and buys nothing; removing large near ones is the
  entire budget.** Banding thirteen layers moved the frame 0.3%; ten negative-space glades took a
  third off it.
- **A dead authored field is dangerous to wire naively, because nothing has ever checked its units
  against its consumer's.** `preferredCameraElevationDegrees` is in degrees;
  `Shot::startElevation` is a height in multiples of the orbit radius — not even the same kind of
  quantity. **The next dead field somebody finds should be assumed to carry the same hazard.**
- **Resolving is not the same as reaching anything.** A route can bind, modulate a real material
  program, and reach no surface, because nothing in the scene draws with that program. The
  dangling-name check cannot see it.
- **A selector that scores candidates independently cannot enforce a constraint on the selection.**
  "Is this the second warm thing in the frame" is a property of the set.
- **A component with no variance across the population is not a criterion.**

## 9. What I would not sign off myself

The water ranges (§4.1) and the panel's appearance (§4.2) need a human, and I have said so at every
phase rather than letting "done" stand. Beyond those: **nobody has watched this scene move.** Every
frame in §2 is a still, the Auto-director's continuous take is asserted numerically and has never
been rendered as a sequence, and the audio has never been played against any of it. The scene is
correct as far as it has been checked and it has not been *watched*, which for an audiovisual
showcase is the difference between finished and finishable.
