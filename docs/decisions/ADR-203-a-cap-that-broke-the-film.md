# ADR-203 — A cap that broke the film, and the control that was missing

**Status:** accepted · 2026-09-14
**Follows:** ADR-200 (the camera was fast in a way the cap did not measure), ADR-202 (importance is the whole of casting)

## Context

Reported, twice, in nearly the same words:

> "I dont know if max speed is working correctly — even at its smallest value its still moving
> blazing fast."
>
> "try to get it to sit longer on individual heroes before it bounces away. im not sure I can
> control that enough with just the importance param"

And, separately, a warning nobody could act on:

> `sequence 'directed': shot 'drop-3' spotlights 'spire-cap' but it never spans more than 0.026 of
> the frame`

These turned out to be one bug and one missing feature.

### Reproduction

None of this could be reproduced without a human at a GUI, because the Auto-director's settings
existed only in the panel. Two things had to be built before anything could be diagnosed:

* **`--direct` never worked with `--composition` or `--audio`.** The flag was handled between
  `--generate` and `--scene`, thirty lines before the composition loads and sixty before the audio
  does, so `avgen --composition w.scene.json --audio t.wav --direct` failed with "`--direct` needs a
  scene; load a project or generate a world first" *every single time*. It worked only for
  `--project` and `--generate`. Moved below the audio load.
* **`--director k=v,...`** now sets the panel's settings from the command line. Unknown keys are
  refused rather than ignored: a typo that silently measures the default is worse than no flag.

With those, the report reproduces in one command on Glowmere Valley 2, with the user's own panel
values:

```
avgen --composition examples/world/glowmere-valley-2.scene.json \
      --audio assets/audio/glowmere-valley.wav \
      --director mode=continuous,minShot=3.9,minBuildShot=1.6,maxShot=6.8,maxSpeed=0.4,maxSwing=2

auto-director: 15 of 15 shot(s) shortened to hold 0.4 m/s
auto-director: 14 of 15 shot(s) given a longer swing to hold 2 deg/s
direct: sequence 'directed': shot 'finalDrop-10' spotlights 'ember-cap' but it never spans
        more than 0.024 of the frame; at 35 mm it is too far away to be the point of the shot
```

## The defect

`limitCameraSpeed` and `limitViewRate` shorten a shot by pulling its **end** back toward its
**start**. In a continuous take the start is pinned to wherever the *previous* shot left off — so
past a certain point the shot stops travelling to its own subject at all, and the camera spends it
parked across the valley from the thing it is supposed to be about. `Sequence::validate()` then
refuses the sequence, correctly: a hero shot in which the hero is a speck is exactly what that check
exists to catch.

And a refused sequence is never installed. **The previously directed film keeps playing.** So the
cap was not being ignored — it was working so thoroughly that it invalidated the film, and the only
evidence was one line in the status bar. Every value the user could reach produced a camera that had
not changed since the last cut that happened to succeed.

Armed on Glowmere, all arms interleaved in one session:

| max speed | max swing | result |
|---|---|---|
| off | off | installs, peaks at 112.9 m/s / 743 °/s |
| 0.4 | off | **rejected** — coverage 0.025 |
| off | 2 | **rejected** — coverage 0.024 |
| 5 | 15 | **rejected** — coverage 0.025 |
| 20 | 40 | **rejected** — coverage 0.058, against a 0.06 threshold |

Not an extreme-value problem. *Every* non-zero cap destroyed the film.

## Decision

### 1. A cap stops where the shot stops working

Both cap loops keep the shot as it was before each shrink attempt and revert if the result is no
longer shootable — `stillShootable()`, which asks the same question `validate()` asks, through the
same `subjectCoverageAt` sampling, so "still shootable" means exactly one thing in both places.
A cap now slows the cut as far as it can and stops; it never hands back a sequence that will be
refused.

### 2. The cut reports what it does, not what was asked

`Sequence::peakCameraSpeed()` and `peakViewSwing()`, logged after every direct and shown in the
panel. A cap is a *request*, and in a continuous take it is frequently one the geometry cannot
grant: the camera has to cross the ground between one subject's stand-off point and the next inside
the time the music gave the shot, and **that distance over that duration is a floor**. Glowmere's
subjects are 100–300 m apart and its shots run 4–7 s; the floor is tens of metres per second. No
amount of shrinking goes under it, and a slider whose limit is invisible reads as a slider that does
not work.

### 3. `dwellShots` — how long a turn lasts

ADR-202 replaced a hero who owned everything with a rotation that turns once per shot. With a cast
of eleven, the camera left every subject the moment it arrived. Importance cannot fix that, and the
user said so precisely: importance sets **how often** a subject's turn comes round, not **how long**
a turn lasts. Two questions, two controls.

`dwellShots` holds one subject for that many consecutive shots. A dwell is charged to the rotation
once, not once per shot in it, so the two controls stay independent: raising dwell lengthens every
subject's stay without redistributing the film.

It is also the control that makes a slow camera possible at all — consecutive shots on the same
subject have no ground to cross, so dwell is what lowers the floor in §2. Measured on Glowmere, all
arms in one session, caps at 0.4 m/s and 2 °/s:

| dwell | peak speed | peak swing |
|---|---|---|
| — (no caps, dwell 1) | 112.9 m/s | 743 °/s |
| 1 | 42.3 m/s | 387 °/s |
| 3 | 58.0 m/s | 436 °/s |
| 6 | **0.8 m/s** | **37 °/s** |

Dwell 3 being worse than dwell 1 is real and is left as measured rather than smoothed: which shots
carry a spotlight changes with the dwell, and a spotlit shot is one the guard in §1 stops capping
earlier. The useful end of the range is unambiguous — 141× slower at dwell 6 than uncapped.

### 4. The panel

`dwell` leads the Pace block, because it is the control that makes the two below it reachable. The
achieved peaks are shown under the settings. The Shot timing heading reads "Shot timing (moves, not
cuts)" in continuous mode.

**Nothing is disabled in continuous mode.** The question was whether the shot-timing controls apply
there; they do. `groupSections` reads `minShotSeconds`, `minBuildShotSeconds` and `maxShotSeconds`
before either mode is consulted, and they decide how the analyzer's sections become shots. What
changes between modes is what a shot *is* — a cut in an edited sequence, a change of subject and
intent inside one unbroken move in a continuous take — so they are relabelled and re-explained
rather than greyed out. Disabling a live control would have been a worse lie than the label was.

## Consequences

The regression test is `"A pace cap slows the cut instead of destroying it"`. Against the pre-fix
implementation it fails on `REQUIRE(capped->validate().has_value())` — with the coverage message,
not with anything about speed — and on the dwell comparison. It asserts both halves: that a capped
sequence installs, **and** that the cap actually bit, by comparing two cuts built in the same process
differing only in the caps. A guard that gave up immediately would pass the first and fail the
second.

What is *not* fixed, and is a limit rather than a defect: a continuous take at valley scale cannot
be slowed to a few metres per second by the caps alone. The geometry forbids it. The honest controls
for that are dwell, longer shots, and subjects that are nearer each other — and the panel now says
so instead of appearing to ignore the request.
