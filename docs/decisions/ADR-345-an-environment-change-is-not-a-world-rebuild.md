# ADR-345: An environment change is not a world rebuild

Status: accepted
Date: 2026-09-18
Retires the cost recorded in ADR-343's swap table.

## Context

`setEnvironmentMap` marked the composition dirty, and `dirty_` has no granularity, so changing one
texture id re-flattened the world. Measured on the Tree of Life ocean world: **273–293 ms** for 557
entities and 1,069 meshes, plus 36–55 ms for the new map's IBL chain. A day/night cycle swaps maps
twice per revolution and paid it both times — about a third of a second of main-thread stall, twice
per cycle, and far worse in a Glowmere-sized world whose terrain alone flattens in ~390 ms.

The rebuild was never *needed*. It was how the environment block at the top of `rebuild()` got to
run again.

## Decision

Resolving the environment map is its own step. `Composition::resolveEnvironmentMap()` is the block
that used to be inline in `rebuild()`; `setEnvironmentMap` sets `environmentDirty_` instead of
`dirty_`, and `update()` runs the resolve when it is set.

Measured on the same scene and the same swap: **35.4 ms of IBL chain and no flatten at all.** About
nine times less. The renderer already keys its IBL rebuild on the environment texture id, so a
changed id rebuilds the lighting and nothing else moves.

**Texture ids are cached by path.** `Scene::addTexture` appends: called repeatedly outside a rebuild
it would grow the texture list without bound, and a cycle running for an hour would leak one texture
every two minutes. `rebuild()` clears the cache, because it clears the scene and with it every id
the cache could hand back.

**The resolve runs after `applyParameters`, not before.** The day/night cycle asks for the swap from
inside that call, so resolving first landed it a frame late — and in a one-frame headless render it
never landed at all. Found because the night map simply stopped appearing in the log, which is the
kind of absence nobody thinks to look for. The ordering is load-bearing and the comment says so.

## Consequences

The swap is now cheap enough that `hdriBlend`'s shaping — crossing 0.5 where `hdriIntensity` is
lowest — is belt and braces rather than the thing standing between the cycle and a visible stutter.
It is kept, because 35 ms is still a frame at 30 fps and the quietest moment is still the best
moment to spend it.

What remains is that there is still only one environment cube, so this is a swap and not a blend.
That is ADR-343's note and it is unchanged.
