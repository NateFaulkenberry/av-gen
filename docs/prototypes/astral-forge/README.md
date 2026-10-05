# THE ASTRAL FORGE (scene experiment)

A gigantic, impossible entity that assembles out of a field of metal dust, achieves a face for a moment, and
violently dissolves. This is research plus a prototype plus art, not a finished scene. Branch
`proto/astral-forge`, 2026-10-05.

| Doc | Brief § | Contents |
|---|---|---|
| [00-brief.md](00-brief.md) | | the owner's brief, verbatim |
| [01-research.md](01-research.md) | §1, §20.1 | research A-G, what AV Gen already had, and which techniques were adopted, deferred or rejected, and why |
| [02-visual-language.md](02-visual-language.md) | §2, §20.3 | the visual grammar: four scales, form, material, colour, light, camera and motion rules, rejected looks |
| [03-parameter-state-model.md](03-parameter-state-model.md) | §3, §12, §20.4 | coherence semantics, the conductor's state, audio → state, time and seek, live/MIDI contract |
| [04-architecture.md](04-architecture.md) | §15-16, §20.2 | approaches A-E implemented and benchmarked; E (hybrid) chosen; the production path |
| [05-tests-and-assessment.md](05-tests-and-assessment.md) | §18-20 | the six tests with verdicts, performance, what worked, what looked generic, the next iteration |

**In one paragraph.**

- 2M particles are bound by **coherence** to a **latent SDF anatomy** that is never drawn. The eyes bind
  first, because that is where pareidolia starts.
- They are splatted into a **u32 density grid**, and a raymarched **metaball iso-surface** is sharpened
  toward the latent where matter has arrived.
- The surface is shaded as **engraved, tempered metal**: phase-modulated guilloché cut in the warped domain,
  anisotropic reflection of reflection-only strip lights, thin-film temper colour and a grating term.
- **Flakes** glint in compute.
- **Collapse** is a release with momentum and forge heat.
- A **conductor** (a pure function of t) drives it all from a scripted curve, or from the song's sections,
  16-beat phrases, kicks and snares.

**The test song is Trench**, at `assets/audio/trench.wav` (gitignored, never committed). *Fireballs* is the
only secondary check. TEST 01-05 are scripted and silent.

**Code:** `prototypes/astral-forge/`, built with `-DAVGEN_ASTRAL_FORGE_PROTOTYPE=ON` (OFF by default). The
shaders are read from the source tree at run time. `tools/render_clips.sh` renders the six tests;
`--bench` measures. **Media:** `~/Desktop/av-gen-review/37-astral-forge/`.
