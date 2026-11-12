# ADR-2030: A crater's hash states the clamp it was made with; two hosted-runner timings measure a quiet round

- Status: Accepted (eng/ci-fixes, 2026-11-12: CI and the nightly Sanitizers workflow).
- Code: `craterHashTerm` and its use in `applyCraters` (`src/scene/lunar_world.cpp`).
- Tests:
  - the lunar world's existing cases (`[lunar-world]`, `[portal]`, `[pass2]`, `[pass3]`) exercise it: each
    generates craters west and south of the origin, which is what UBSan reported;
  - "the two-stage search is worth it only at a scale nothing here has" (`[motionscale]`): interleaved rounds;
  - "Mixing a piece costs what a load costs" (`[arrangement][performance]`): the fastest of three mixes.

## Context

**The Sanitizers workflow failed every night from 2026-11-01.** Every gating report in every job was one line:

```
src/scene/lunar_world.cpp:135: runtime error: -392.734 is outside the range of representable values of type 'unsigned int'
```

`applyCraters` seeds each crater's rim noise with `hashU(uint32(c.x * 13) ^ uint32(c.z * 7) ^ seed)`. A crater west
or south of the origin has a negative coordinate, and converting a negative float to an unsigned int is undefined
behaviour. It came in with the Moon (ADR-1882) on 2026-10-30 and reached main on 2026-10-31 with the portals merge;
the 2026-10-31 nightly passed, the 2026-11-01 nightly was the first to fail.

- The ASan jobs fail on any `runtime error:` line.
- The UBSan job halts on the first report, so each of its three shards died at its first lunar case: on 2026-11-11
  it ran 1552 of 4667 cases.

What the code actually did depends on the machine:

- Apple silicon's Release build converts with `fcvtzu` (checked in the binary): it clamps, so a negative reads 0,
  NaN reads 0, and anything past the top reads 0xFFFFFFFF.
- An x86 build converts through a 64-bit signed conversion and keeps the low 32 bits, so -392 reads 4294966904.

Every Moon and Mars frame was made and approved on Apple silicon, with the clamp.

**CI's push gate failed on a timing ratio.** "The two-stage search is worth it only at a scale nothing here has"
read a 4.90x speedup against its 5x bound on a hosted runner, on 2026-11-12. Over 28 hosted runs the ratio ranged
from 4.9x to 17.5x, with a median of 10.5x. The failing run's linear scan was ordinary (49 ms). Its two-stage
search read 10.0 ms, against 3-5 ms on most runs.

The test timed five rounds of the linear scan, then five of the two-stage search. At a million samples the second
block lasts about 50 ms, so one slow spell on a three-vCPU runner running three shards can cover every one of its
rounds.

**The private-asset nightly failed on a mixdown timing** (2026-11-09). One mix of six minutes of audio read
2851 ms against a 2000 ms bound. The same mix reads 50-400 ms on nearly every hosted run.

## Decision

1. **The clamp is written out, not replaced.** `craterHashTerm(v)` returns 0 for a negative or NaN, 0xFFFFFFFF
   from 2^32 up, and the conversion otherwise: exactly what `fcvtzu` did.
   - Every crater on Apple silicon is unchanged, bit for bit, so no approved frame moves. Measured in the Release
     build: a digest of the heights, chunk positions and uvs of the test's small Moon and of the default 4 km Moon
     is identical before and after (17053680404733405433 and 16619912358071711510), and the wrap below changes both.
   - An x86 build now draws the same craters as the Mac.
   - The alternative, a wrap through `int32_t` like the file's integer hashes, is the more obvious hash. It was
     rejected because it re-seeds the rim wobble of every crater with a negative coordinate (three in four) in
     scenes the owner has already approved.
2. **The two-stage timing interleaves its rounds.** Each of nine rounds times one linear scan and then one two-stage
   search, and each side keeps its own minimum.
   - A slow spell now costs a round of each, not every round of one.
   - The bounds (under 16.7 ms, more than 5x) are unchanged.
   - This is the form "the per-character cost of the motion stack" already uses, for the same reason.
   - Measured with the process stopped 3 ms in every 4 during random 40-300 ms spells: the old form read 2.6x to
     13.0x over 20 runs and failed once; the interleaved form read 9.2x to 11.0x and never failed.
3. **The mixdown bound reads the fastest of three mixes.** A quadratic loop is slow on every mix, so the guard still
   catches what it exists for. The 2000 ms bound is unchanged.

## Consequences

- The UBSan job can now run the whole default set rather than a third of it, so a report the lunar one was hiding
  would surface. A local UBSan run on this commit reached 3867 of the 4773 cases (it was stopped, not finished) and
  reported nothing; the rest is proven only by the next nightly.
- Assertion failures under the sanitizers are still listed and still do not gate, as the **Gating** paragraph of
  `docs/development/ci.md` ("Sanitizers") describes. They are:
  - wall-clock budgets;
  - the 10 s job waits in `test_world_builder.cpp`: TSan and ASan report nothing in them, and the worlds come out
    the same once the jobs are given the time;
  - one bit-exact mesh digest that an unoptimised build cannot reproduce ("the canyon mesh is unchanged by the
    shared RTIN mesher's extraction").
- "The per-character cost of the motion stack, measured" failed four private-asset nightlies (2026-11-03 to 11-07)
  on its 10% margin and has passed every night since. It is unchanged, and it can fail that tier again.
