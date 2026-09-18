# ADR-322: Fifteen scenes asked for volumetric noise, and five projects wrote back the zero they got

**Status:** Accepted
**Date:** 2026-09-18

Closes the revisit trigger ADR-278 left open. The parser reads `"volumeNoise"`; fifteen shipped
scenes wrote `"volumeNoiseAmount"`, which is the name of the C++ field
(`VolumeSettings::volumeNoiseAmount`). All fifteen have volumetrics on — `volumeDensity` from
0.00055 to 8 — and asked for 0.4 to 0.55 of noise. All fifteen ran at 0.

ADR-278 pinned the count at fifteen rather than repairing it, deliberately, so that fixing it would
be what moved the test. The owner has authorised the repair.

---

## 1. The half that was not in the report, and that would have made the fix inert

Five projects carry `"scene/volumeNoise": 0.0` in their `parameters` block:

```
examples/world/glowmere-valley-2.json
examples/world/glowmere-valley-2-multicam.json
examples/world/glowmere-atmospherics.json
examples/world/glowmere-stylized.json
examples/composition/glowmere-lyrics.json
```

A project parameter sits over its scene (ADR-264). So correcting the key alone would have left every
one of those films running at exactly the zero it ran at before — and the change would have read as
a working fix everywhere it did not matter, which is the most expensive shape a repair can have.

Those five values are ADR-264 session residue *of this very defect*: the application writing back
the number really in effect beside a scene claiming another. They are **removed** rather than set,
because an absent parameter means "as the scene authors it" — ADR-271's rule for the 56 stale
`particles/*/extent` multipliers, one subsystem over.

## 2. The fossil, left alone on purpose

`examples/world/glowmere-valley-2-song.scene.json` carries the **correct** key at `0.0`. It is this
defect fossilised: "Save Scene As..." photographed a session that was running at zero for this
reason, and wrote the zero out under the right name. Its authored intent is not recoverable from the
file — 0.45 is a guess from its sibling — so inventing one would be an art direction rather than a
repair. It is reported here and not touched.

## 3. What it does to the picture, measured

This is the part the report was wrong about, and it is worth saying plainly: correcting the key was
described as changing fifteen shipped films' appearance, and **the change is far smaller than that
implies.**

`glowmere-valley-2-multicam`, before and after, a frame at five points across the film, rendered
headless at 1920x1080 with the project's own settings (renderScale 2, offline tier) under
`tools/gpu-lock.sh`, load average 17:

| t (s) | SSIM | PSNR (dB) | CIEDE2000 p95 | VMAF | sequence hash before → after |
| --- | --- | --- | --- | --- | --- |
| 6 | 0.9999 | 58.90 | 0.393 | 98.15 | `cd4abc17cf82eacb` → `b716968a5cb6c31a` |
| 20 | 0.9998 | 63.65 | 0.304 | 98.48 | `bd3c68a5f53a70b3` → `7ab509818b6c6730` |
| 45 | 0.9993 | 55.98 | 0.588 | 98.25 | `249ebd5595095b9e` → `79fed4aae63c8de2` |
| 75 | 0.9995 | 57.05 | 0.531 | 98.38 | `2b9b74f38d54af49` → `09539d323fd1b096` |
| 110 | 0.9990 | 49.80 | 0.754 | 97.00 | `935c8da58defa74b` → `232b34a1ccc7e04b` |

And `grove`, which has the densest fog and the highest-frequency noise of the fifteen (density 0.016,
noise 0.5, scale 0.07) and is therefore the upper bound: SSIM 0.9991, PSNR 53.85, CIEDE2000 p95
0.756, VMAF 96.85, `7ab0e5e081101fbc` → `7745ddd456edaaba`.

Every hash differs, so nothing here is two renders that should differ and did not. But a 95th
percentile colour difference of 0.75 is below the ~1.0 that is usually taken as just-noticeable, and
both frames were looked at side by side: the difference is not visible at a glance in either film.

The reason is in the shader and is not a defect:

```
density * (1 + volumeNoiseAmount * (fbm3(p * volumeNoiseScale + t * volumeNoiseSpeed) * 2 - 1))
```

The modulation is **symmetric about the mean**, so the average amount of fog is unchanged and only
its spatial texture moves. At Glowmere's `volumeNoiseScale` of 0.02 that texture has a wavelength of
about 50 m, over a fog of 0.006 per metre. A 45% wobble of a thin fog at fifty-metre scales is a very
gentle thing.

**Nothing looks worse.** Where the difference is visible at all it is a slight clearing of the far
field, which is what breaking up a uniform fog does.

## 4. How the files were edited

Textually, never through `json.load`/`json.dump`. A round trip rewrites about 140 float literals to a
different last digit and buries a one-line change in a huge diff; this has bitten this repository
twice. The scene edits are one line each and the project edits are one deleted line each.

`tools/refresh_scene_fingerprint.py` now stamps **every project that references a scene** rather than
the one beside it. Its own argument, one level up: `glowmere-stylized.scene.json` backs three
projects and one of them is `examples/composition/glowmere-lyrics.json`, which names it
`"../world/glowmere-stylized.scene.json"` — another directory and another spelling, so both the
sibling lookup and a regex anchored on the basename walked straight past it. Five fingerprints
refreshed; the old tool would have refreshed four and left the fifth lying.

## 5. The arms

`tests/unit/test_scene_authored_lights.cpp`'s sweep changes its expectation from 15 to **0**, which
is what ADR-278 built it for. The counter stays named rather than folding into `otherFindings`: a
named zero tells the next reader that this key was the finding and has been closed, where an unnamed
one would report a regression as "some unknown key somewhere".

`tests/integration/test_volumetric_noise_authoring.cpp` adds the two claims the sweep cannot make.

* A synthetic scene proves `volumeNoise` reaches the parameter that feeds `env.volumeNoiseAmount`,
  with the misspelling beside it as a permanent measurement of what the fifteen were doing: 0.45 in
  the file, 0.0 in the engine, no error. That behaviour is unchanged, deliberately — ADR-278 chose a
  warning over a refusal.
* The owner's own film is loaded end to end and asked what it runs, against what its scene asks for
  **read from the file rather than quoted**, so the case cannot drift if somebody retunes the art.
* A sweep says no project carries a zero over a scene that asks for noise, and it reads the scene
  under *either* spelling. That is not tidiness: reading only the key the parser reads would have
  left the case unfailable for as long as the scenes misspelled it — the scene looks as though it
  asked for nothing, so a project zeroing it looks correct. Measured with the scene files put back,
  it passed while every other arm failed. Asking what the author wrote rather than what the parser
  found is what gives it a control, and it then names all five.

ADR-182, against main:

```
volumeNoiseAmount == 0                                  ->  15 == 0
sceneDoc["environment"].contains("volumeNoise")         ->  false
glowmere-atmospherics.json overrides its scene's 0.45 with 0    (and four more)
```

## Consequences

- Fifteen scene files: one key each.
- Five project files: one deleted parameter each.
- Five scene fingerprints refreshed; `tools/refresh_scene_fingerprint.py` reaches every referencing
  project.
- `tests/unit/test_scene_authored_lights.cpp`, `tests/integration/test_volumetric_noise_authoring.cpp`.

## Revisit triggers

- **`glowmere-valley-2-song.scene.json`.** §2. It is the one file still carrying this defect's
  damage, and only the owner can say what it should have been.
- **The next scene saved from a session that was running at zero.** A project photographs the run
  (ADR-264), so the residue can return from a different cause. The sweep in §5 is the guard, and it
  is narrow on purpose: a real authored zero is still sayable, in the scene.
- **The fourteen `examples/world/_bench/` copies.** Generated by `tools/make_bench_scenes.py` from
  `terrain.scene.json` and gitignored, so they inherit whatever the source says. A worktree that ran
  the generator before this change still has fourteen copies carrying the wrong key; regenerating
  is the fix, and the sweeps exclude that directory for exactly this reason.
