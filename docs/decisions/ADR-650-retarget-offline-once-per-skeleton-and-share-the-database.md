# ADR-650: Retarget offline once per distinct skeleton, and share the database among characters on that skeleton

**Status:** Accepted
**Date:** 2026-09-21
**Related:** Phase C §42 (retargeted database strategy), §43 (character-specific databases), §41
(multi-character sharing); ADR-553 (a BVH corpus cannot animate the alien rig), ADR-604 (the
duplicated rig memory)
**Implemented by:** `scene/motion_database_io.hpp` (a database per pack, keyed by skeleton digest
and content), `scene/motion_library.hpp` (one immutable asset shared by every character)
**Tests:** `[motionstrategy]` in `tests/unit/test_motion_strategy.cpp` (the measurement, and the
assertion that re-opens this decision), `[motionlib]` (sharing)

---

## Context

§42 asks whether a runtime motion database should hold

- **A:** the source motion, retargeted to each character at runtime,
- **B:** motion already retargeted to each target skeleton offline, or
- **C:** a hybrid,

and requires the answer to be *measured* on memory, load time, runtime CPU, reuse and character
diversity. §43 asks that the architecture support both a shared database and character-specific
packs where practical.

The realistic case for this repository is the Glowmere cast: six alien variants (scout, elder,
diver, pilot, ranger, trooper) and one authored motion set, the scout's 26 clips (1,738 frames).
Per ADR-553, this is the only corpus that animates these rigs.

## Measurement

`test_motion_strategy.cpp`, release build, minima over repeats (ADR-170):

| | A: source + runtime retarget | B: retargeted offline per rig |
|---|---|---|
| cost per posed frame | **197.18 µs** | **2.33 µs** (**84.5×** less) |
| 6 characters at 60 Hz | 71.0 ms of CPU per second | 0.8 ms of CPU per second |
| 60 characters at 60 Hz | **709.9 ms of CPU per second** | 8.4 ms of CPU per second |
| offline cost per distinct rig | none | retarget 347.9 ms + pack/database build 90.8 ms |
| memory, 60 characters | 3.41 MB (shared clips 2.99 MB + database 0.25 MB + a 2.8 KB binding each) | 4.05 MB **per distinct rig** (clips 3.80 MB + database 0.25 MB) |
| per-character setup | bind 0.017 ms | none |

And the fact that decides it for this repository: **all six aliens have the same skeleton digest.**

## Decision

**B, keyed by skeleton digest.**

- Characters whose skeletons share a digest share **one** pack and **one** database: one immutable
  `MotionAsset`, published by a `MotionDatabaseSlot` and pointed to by every character's provider.
  Per-character state is the `MotionMemory` value alone.
- A character with a **distinct** skeleton gets a character-specific pack, retargeted offline
  (`avgen-motion pack --retarget-to`), with its own database (`avgen-motion build-db`). The
  retarget profile is recorded in the pack's provenance and is part of the pack's content digest, so
  the build cache (§82) never confuses two retargets of one source.

This is §43's "support both": a shared database is simply B with more than one character per
digest.

## Rationale

- **Runtime CPU is the binding constraint, and A spends it 84.5× faster.** Sixty characters under A
  cost 710 ms of CPU per second of wall clock, most of a core, on retargeting alone, before any
  search, layer or IK runs.
- **A's memory advantage exists only for distinct rigs, and this repository has none.** With one
  skeleton, B stores one copy (4.05 MB) regardless of how many characters there are. Even per extra
  distinct rig, the saving is small (3.80 MB of clips against A's shared 2.99 MB).
- **B's cost is offline and cached.** About 440 ms per distinct rig, paid once, with the result
  reused whenever the build key is unchanged (0.9 ms verified reload on the scout).

## Consequences

- A new skeleton costs a pack build and disk space for its clips. A new *character* on an existing
  skeleton costs nothing.
- The runtime never retargets. `MatchMotionProvider` poses clips that already belong to the rig, so
  a database built for one skeleton is refused against another: `readMotionDatabase` compares
  skeleton digests, and `checkMotionAsset` compares pack content.
- Style diversity across characters on one skeleton comes from their requests and procedural
  layers, not from separate databases.

## Rejected alternatives

- **A (runtime retarget):** rejected on CPU. At 197 µs per pose, 84.5× B, it makes 60 characters
  cost 710 ms of CPU per second.
- **C (hybrid, e.g. retarget at load time into memory):** adds nothing over B here. It pays B's
  retarget cost on every load instead of once, it loses the build cache, and with one skeleton
  there is no memory to save.

## Revisit triggers

- A cast of many *distinct* skeletons arrives, where memory binds before CPU does.
- The per-pose retarget becomes much cheaper. `test_motion_strategy.cpp` asserts
  `perPoseA > 5 × perPoseB`, so a retargeter within 5× of plain sampling re-opens this decision by
  failing that test.
