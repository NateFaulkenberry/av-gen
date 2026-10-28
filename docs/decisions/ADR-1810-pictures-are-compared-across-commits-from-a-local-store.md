# ADR-1810: Pictures are compared across commits, from a local store

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation R4 (gap G3), approved by the
  owner.
- Code: `tools/picture_check.py` (`capture`, `compare`, `list`). Docs: `docs/testing.md` (Determinism).

## Problem

AV Gen catches a *performance* regression across commits (the live baseline gate, ADR-1608) and a *nondeterministic*
picture within one build (determinism hashes, in-run differential tests). Nothing compared the picture of one commit
with the picture of another, so a change that altered the look without changing the cost passed every gate. Committed
golden images are not an option: the reference scenes use commercial assets and the repository is public.

## Decision

- **A local, uncommitted store.** Default `~/Library/Application Support/avgen/picture-store`, overridable with
  `AVGEN_PICTURE_STORE` or `--store`. The tool refuses a store inside the repository.
- **`capture --label <name> [--bin <build dir>]`** renders three fixed frames of each of the five reference live
  scenes (the perf gate's `SCENES`) through the offline renderer, `avgen --headless --render`, at 7, 20 and 33 s into
  each scene's measured window, 960x540 by default, each render under `tools/gpu-lock.sh`. The offline path is the
  deterministic one: the same build renders the same bytes. `manifest.json` records the revision and dirtiness of the
  checkout the binary was built from, the binary, size and tier. `--bin` may point at another checkout's build, so
  "before" can be main's binary.
- **`compare <A> <B> [--tol 2] [--heatmaps]`** reports per frame the share of pixels whose largest channel moved by
  more than `--tol` levels, the mean absolute change in levels, the largest change and its bounding box, with a
  verdict: **SAME** (byte-identical), **WITHIN** (at most 0.1% of pixels beyond the tolerance and a mean of at most 0.1
  level: rounding-scale), **DIFFERS**. Exit 1 if any frame differs, 2 if the captures do not line up (missing frames,
  another size or tier). `--heatmaps` writes a difference image beside each DIFFERS frame, in the store.

The metric is deliberately simple (per-pixel channel differences, no perceptual model): it needs no dependency, its
numbers are exact and readable, and the calibration below shows it separates rounding from a real change.

## Consequences

Calibration (960x540, the five scenes, three frames each, numbers below):

- **A clean re-capture of the same build: SAME or WITHIN on all 15 frames** (13 byte-identical; two Rift frames move
  by at most 1 level on a handful of pixels: Rift's offline frame is not perfectly bit-stable). Exit 0.
- **A seeded 2% exposure change** (`tonemap.wgsl`, `exposure * 1.02`, restored afterwards): **DIFFERS on all 15
  frames**, exit 1. Mean change 0.23-0.79 levels (Aurora 0.61-0.69, Rainbow 0.77-0.79, the dark scenes 0.23-0.31);
  pixels beyond 2 levels 0.00-0.23%. The mean-change criterion is what catches a global shift; the share criterion
  catches a local one. A 0.1-level mean bound sits between the clean run (0.000) and the seed (0.23 and up).
- First real use: R1 (ADR-1807) against main, which found the Aurora ribbon dither reshuffle that the ADR documents.

Limits:

- It compares the **offline** frame. A live-only change (the live sky schedule, the live antialiasing floor) is not
  seen; the live picture is not deterministic (its sky schedule reads a wall clock).
- Same machine only: cross-GPU equality is not promised, so a store is per machine.
- A capture of all five scenes takes several minutes, most of it scene loading (Rift's 25 s per frame).
