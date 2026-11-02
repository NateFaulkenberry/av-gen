# CHRONOS: progress

> **Art direction pivot (owner, 2026-11-02): "Temporal Singularity"** (`20-singularity-brief.md`,
> `21-singularity-coordinator-notes.md`). The Devourer creature is abandoned and its scene art removed (ADR-441): the
> creature builds, the D1/D2 scenes and their scripts. Kept: the temporal engine (ADR-1930..1932), ADR-1934 (identity
> gating, for per-figure destruction) and the MPFB figures (`examples/chronos/singularity/build_souls.py`).
>
> | Gate | Phases | State |
> |---|---|---|
> | S0 | recon, plan, Test A (the singularity alone) | in progress |
> | S1 | Phases 2-4: figures, temporal transformation, gravitational distortion (Tests B-D) | not started |
> | S2 | Phases 5-6: feedback and chaos, drums, polish, profile (Tests E-G) | not started |
>
> The Devourer history (D0-D2, docs 10-17) follows, **superseded**.

> **Art direction pivot (owner, 2026-11-01): "The Devourer of Souls"** (`10-devourer-brief.md`,
> `11-devourer-coordinator-notes.md`). The temporal canyon is dropped and its code removed (c83ed2c1, ADR-1933
> superseded). The temporal engine (ADR-1931, 1932, the debug-capture fix) and the shared mesher (ADR-1930) stay.
>
> | Gate | Phases | State |
> |---|---|---|
> | D0 | references, recon, plan, look-dev | **approved** (main at ab41c924 holds the temporal engine and the canyon removal); downloads shortlist `13-devourer-downloads.md` |
> | D1 | Phase 1, the creature (Test A) | **reviewed**: owner chose one more sculpt pass toward inhuman, three-quarter first (`14-devourer-d1.md`, media in `devourer-d1/`) |
> | D1b | the sculpt pass: eyes, teeth and mouth, Rodin skin, proportions | **reviewed** (owner: "kinda looking cool"; the eyes still not in the right position) (`15-devourer-d1b.md`) |
> | D1c | the eyes re-seated in the measured orbits, a readable maw, pores halved, seams welded | **done** (`16-devourer-d1c.md`, media in `devourer-d1c/`); the owner moved the stop: D2 follows without a review |
> | D2 | Phases 2-3, the procession and one consumption (Tests B, C) | **done, awaiting the owner** (`17-devourer-d2.md`, media in `devourer-d2/`): ADR-1934 identity gating, eight MPFB souls, a 15-soul procession, stages A-E; rebased onto main 60d65ba8 |
> | D3 | Phases 4-6, many souls, background, drums, polish, profile (Tests D, E, F) | not started |
>
> Suites on the post-removal head (3d964413, clean worktree): GPU 737 passed, 1 skipped (738/738); cpu-assets 4611
> passed, 29 skipped, 1 expected failure (4641/4641). Both exit 0. The D0 commit after it is docs and a Blender script
> only; the repo-hygiene tests pass on it.
>
> The canyon-era history follows.

Branch `scene/chronos` in `../av-gen-chronos`, from main 214dafd8. ADRs 1930-1969 (used: 1930-1934; next 1935).
Review media: `~/Desktop/av-gen-review/104-chronos/gate<N>/`.

| Gate | Phases | State |
|---|---|---|
| 0 | recon, plan, proof clips | **approved** (all five recommendations accepted) |
| 1 | 1 ring and slit scan; 2 canyon and camera | **done, awaiting the owner** (`04-gate1.md`, media in `gate1/`); ADR-1930 handed back early as 3d73b32c |
| 2 | 3 effects; 4 audio | not started |
| 3 | 5 acts, tuning, perf, full render | not started |

## Gate 0

- The existing temporal family (ADR-410, 1049, 1066) is the base. CHRONOS extends it and does not build a second
  system.
- The gaps that matter:
  - the ring is capped at 32 frames;
  - its depth reallocates when a route moves `frames`;
  - there is no strip store for slit photography;
  - there is no RGB temporal displacement and no region fracture;
  - feedback is FIR-only (by ADR-410);
  - the offline render has no temporal pre-roll.
- The proof clips are rendered from the Strange Attractor flythrough (main):
  - the engine's existing slit;
  - an offline numpy preview of a longer ring and a strip ring.
- Open decisions for the owner are in `03-plan.md` §6.

## Gate 1

See `04-gate1.md`.
- ADR-1930: the mesher extraction, handed back early (3d73b32c, both suites at exit 0 in a clean worktree).
- ADR-1931: the ring (capacity, stride, metadata) and the sampler, plus the debug-capture leak fix.
- ADR-1932: the render pre-roll.
- ADR-1933: the canyon.
- Temporal cost: 0.10 ms at 1080p.
- The clean flight's jitter: p99 0.042, max 0.104.
- The canyon's live frame is 60.6 ms (the fog march is 39.8 ms of it). That is Gate 3's problem.

Lesson recorded: never edit the worktree while a suite runs. The tests read shaders, the ADR index and sources at run
time, so the first run on 3d73b32c was invalid. Final suites run in `../av-gen-chronos-ci`, a clean worktree checked
out at the commit under test.
