# ADR-202 — Importance is the whole of casting

**Status:** accepted · 2026-09-14
**Supersedes the casting half of:** ADR-072 (strict hero ordering), ADR-201 (there is no primary hero)

## Context

ADR-201 removed the *idea* of a primary hero from the editor: the star marker went, the redundant
hero dropdown went, and importance became the only way to say a subject matters. The director did
not follow. `directFromStructure` still split the cast in two — `brief.hero` and
`brief.supporting` — and the split was categorical:

* the top-ranked hero owned **every build and every drop**;
* the rest shared whatever sections were left, in rotation;
* `kMaxHeroRun` capped how many consecutive sections the hero could take, and `heroHasASection`
  guaranteed it at least one.

None of that reads importance. A subject at 0.95 and a subject at 0.94 are on opposite sides of a
wall, and no amount of raising the second one's slider moves it across — the only thing that moves
it across is being ranked first. Reported plainly:

> "the director still gives the top-ranked hero every build and drop - yeah I dont want this"
> "I only want an importance weight for heros"

Measured on the Glowmere cast before the change: the top hero held **97%** of screen time, with a
longest unbroken run of **22 shots**.

## Decision

One cast, ranked, and importance is the whole of it. `FocalTarget` carries `importance`;
`briefFromHeroes` copies it straight off the `HeroPoint`; `directFromStructure` merges hero and
supporting into a single `cast` and draws from it by **smooth weighted round-robin**:

```
each shot:  credit[i] += weight[i]   for every subject
            winner = argmax(credit)
            credit[winner] -= totalWeight
```

A subject at 0.9 is cast about twice as often as one at 0.45, and — unlike a proportional random
draw — the turns are *spread* rather than clumped, so the film does not hand one subject four shots
in a row by chance. `weight` has a floor of 0.05, because a subject at importance 0 would otherwise
never appear at all, and a hero declared in a scene is a hero somebody wants to see.

Deleted: `heroOwns`, `kMaxHeroRun`, `heroHasASection`, `offset`, `supportingIndex`.

### The seed

The seed sets each subject's *starting* credit, scaled against `totalWeight` — which shifts the
phase of the rotation without touching anyone's share. A re-cut opens on a different subject and
still gives the important ones the same amount of film.

Scaled against the total rather than being a small nudge, because an offset much smaller than one
weight cannot change who wins the first round: the first attempt used a `/2048.0` nudge and the
seed stopped re-cutting the film at all. The suite caught it — "the seed changes the edit" is a
contract, `test_camera_director.cpp` — which is the only reason it is not shipped.

## Consequences

Measured on the same cast, same structure, same seed:

| | before | after |
|---|---|---|
| top hero's share of screen time | 97% | **20.8%** |
| longest unbroken run | 22 shots | **2 shots** |
| distinct subjects on screen | 1 | **4** |

The film is about the cast now. Two costs, both real and both stated rather than absorbed:

**The view swings more.** Uncapped peak view rate rose from **52 to 125 °/s**, because consecutive
shots now look at different subjects and the camera has to turn between them. That is what
`maxViewRate` (ADR-200) is for, and it is now the control that matters most in the panel.

**Two tests encoded the removed contract and were rewritten, not weakened.**
`test_camera_director.cpp`'s "the hero still owns the drop" assertion is gone — who the drop reveals
is importance's business; *that* it is a reveal, and *when*, is still asserted.
`test_cinematic.cpp`'s "a breakdown gets a slow, close shot" compared two shots' path lengths, and
rotating the cast made subject geometry a confound (25.4 m against a 25.1 m budget, while every
other claim in the case still held). It now runs against a cast of one, so the comparison is about
the shot and nothing else — a stronger control, not a wider margin.
