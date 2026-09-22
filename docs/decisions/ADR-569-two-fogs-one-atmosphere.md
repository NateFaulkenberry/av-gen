# ADR-569: two fogs, one atmosphere, and they cross at 79 metres

Status: **finding, no behaviour change.** Date: 2026-09-21. Phase D of the Fog Bank brief
(§5, §6, §7's Horizon Density).

## What this is

A measurement and a question for the owner, not a change. Nothing in the tree moves because of
this ADR. It exists because the answer decides what §7's last control should be, and because
"fixing" it would change thirty-six shipped scenes, which is not an agent's call.

## The finding

The engine renders fog twice, with **two different laws and two independent densities**, and both
claim to be the air in front of the camera.

| | where | density | law |
|---|---|---|---|
| **surfaces** | `applyFog`, `shaders/common.wgsl` | `Environment::fogDensity` | `exp(-(d · density)²)` -- exponential **squared** |
| **the march** | `fs_march`, `shaders/volume.wgsl` | `Environment::volumeDensity × volumeAbsorption` | `exp(-∫ extinction ds)` -- Beer--Lambert, exponential **linear** |

ADR-058 made them share the **height geometry** -- and that was a real improvement -- but it did
not make them share the density or the functional form, and nothing has since. The two are set by
different parameters that no code relates.

**They agree at exactly one distance and disagree everywhere else.** Setting the two transmittances
equal gives a crossover at `d = volumeDensity · volumeAbsorption / fogDensity²`. Across the
thirty-six shipped files that set both:

| crossover | scenes |
|---|---|
| 6.6 m | `glowmere-valley-2-multicam.json` |
| 20--63 m | `alien-wander`, `reassembly`, `hyperspace`, `constellation`, `infinite` |
| **79.3 m** | **eleven Glowmere files plus the three foot-IK labs** -- the modal value |
| 88--156 m | `snow`, `grove`, `machine`, `world`, `tide` |
| 275 m -- 4.1 km | ten others |

Median 79.3 m, over the original file glob; see the corrected population below for how many of
these are shipped scenes rather than probe arms. What that means in the flagship scene, `glowmere-valley-2.scene.json`
(`fogDensity` 0.0055, `volumeDensity` 0.006, `volumeAbsorption` 0.5):

| distance | surface transmittance | march transmittance | ratio |
|---|---|---|---|
| 100 m | 0.739 | 0.787 | 1.1x |
| 400 m | **0.008** | **0.383** | **48x** |
| 1200 m | 0.000 | 0.056 | -- |

A *surface* 400 m away is 99.2% fog colour. The *medium* in the same 400 m passes 38% of what is
behind it. Beyond the crossover the surface fog is always the thicker of the two, and it gets
thicker faster, because squaring the exponent makes the effective extinction grow **linearly with
distance** -- which is, exactly, a horizon-density control. §7 asks for one by name; the engine
already has one, unlabelled, in a pass that was not thinking about it.

## The population, reconciled -- and it took two people getting different answers to get it right

The first version of this ADR counted "36 shipped files that set both", from a glob over
`examples/**/*.json` that included this branch's own probe arms. Correcting that produced a second
census -- and a *third* person's census of the same question produced different numbers again:
**120 scenes, 57 both, 57 surface-only, 0 march-only** against **90, 30, 23, 1**.

**Neither was wrong. They were two different questions**, and the two axes they differed on were
invisible in both commands:

| axis | one answer | the other |
|---|---|---|
| what counts as **shipped** | all 120 tracked `*.scene.json` | the 90 that are not `_`-prefixed probe scenes |
| what counts as **using a law** | the key is present | the key's value is greater than 0 |

`"fogDensity": 0.0` has the key and does not use the law, and `examples/` holds **30 `_`-prefixed
probe scenes** from several agents' diagnostic tooling. Both axes reproduce exactly:
`--all --present` gives 57/57/0/6 and the default gives 30/23/1/36.

**Branch is not a factor and that was checked rather than assumed**: `main` and `agent/fog` carry
the same 120 tracked `*.scene.json` files with zero differing.

So the census lives in a file now, and this ADR cites the file rather than a shell line:

```
$ python3 tools/fog_law_census.py
branch agent/fog, shipped scenes (no `_` probes), value > 0
  total                      90
  both laws active           30
  surface law only           23
  march law only             1
  neither                    36
  march-only scenes: volumetric-atmosphere-lab.scene.json
  scenes whose surface law is live (option 2's migration): 53
    -- of which 23 have no volumetric density to derive one from
```

**The decision-relevant reading is the non-zero one**, because this ADR is about two laws being
*active* and disagreeing; a key at 0 is an inactive law. Presence matters only for the migration,
where a scene already carrying the key at 0 is trivially converted.

**The one march-only scene, named because it was worth naming:**
`volumetric-atmosphere-lab.scene.json`. Not an artefact of scope -- the only scene in the
repository that exercises the volumetric march without the surface law is the **lab built to
exercise the volumetric march**. Which is its own small finding: outside that lab, the march is
never seen except alongside a surface law it disagrees with.

So the exp-squared law is live in **53** shipped scenes and the march in **31**.

**An inference was put to me that both laws are not really in use -- that one of them ships
everywhere and the other has never shipped at all -- and it does not survive the table above.**
Both laws are in use, in 53 and 31 scenes respectively. What has never appeared in a shipped scene
is a **fog bank effect**, and that is a different object from the Beer--Lambert march: the march is
`Environment::volumeDensity`, which 31 scenes set, while the fog bank is an `AtmosphericEffect` of
kind `fog`, which none do. The example offered as evidence, `tree-of-life-ocean-world.scene.json`,
carries `fogDensity` 0.000135 and **no** `volumeDensity`, so it is one of the 23 surface-only
scenes and demonstrates the surface law being used rather than the march being unused.

**The correction cuts the other way from the inference, and that is the part that matters for the
decision.** Option 2 below -- make the surface pass Beer--Lambert -- was costed at 36 files. It is
**53**, because the 23 surface-only scenes have no volumetric density to derive a new one from and
would each need a number chosen by eye. That makes option 2 more expensive than stated, not less,
and option 3 correspondingly better.

## Why this is a question and not a defect report

`Environment::fogDensity`'s own comment says what it is: *"Distance fog (exponential-squared by
view distance) applied to lit/unlit surfaces after shading; the skybox is untouched."* Exp-squared
distance fog is a deliberate, common stylistic tool, and thirty-six scenes were tuned by eye with
both controls in hand. **It is entirely possible that every one of those scenes looks right**, and
that the artists compensated for the divergence without ever naming it.

What is *not* possible is that both are the model the brief asks for. §5: *"The effect should
conceptually generate a scalar field `density(x, y, z, time)` ... the volume renderer consumes this
field."* §6: *"`density = baseDensity × heightFalloff × distanceFalloff` plus local volume
contributions."* One field, consumed by the renderer. Not two fields with different laws.

**I have not rendered the comparison.** Everything above is arithmetic on the shipped parameters
and the two shader expressions. The arm that would settle how much it matters is a Glowmere frame
with a subject at 400 m, rendered with the surface fog on and off against a march-only reference --
and it needs an eye on it, not a metric, because the question is whether the scenes look right.

## The three ways out, for whoever decides

1. **Leave it, and document it.** Cheapest, and defensible: the two controls do different jobs, one
   is atmosphere and one is a look. Cost: §7's Horizon Density has nowhere to go, because the thing
   it describes already exists in a pass that is not the fog system.
2. **Make the surface pass Beer--Lambert and derive its density from the volumetric one.** One law,
   one density, the brief's §5 satisfied. Cost: **every one of the thirty-six scenes changes**, most
   of them a lot, and Glowmere's depth separation is built on the 48x. This is a re-lighting pass
   across the repository, not a shader edit.
3. **Keep both laws and make the blend explicit** -- a `horizonDensity` control that is honestly
   named as "extra extinction that grows with distance", applied in *both* passes from one number,
   with the existing `fogDensity` expressed through it. Preserves the look, gives §7 its control,
   and makes the second law a stated choice rather than an accident of which pass you are in.

My recommendation is **(3)**, and the reason is the same argument ADR-567 makes: the problem is not
that there are two behaviours, it is that there are two *definitions*. One definition with a knob
that selects the behaviour is auditable; two expressions in two files that happen to agree at
79.3 m is what this ADR had to measure to find.

## Revisit when

- The owner rules. Until then Phase D's remaining §7 control is **blocked on this**, because
  implementing Horizon Density in the march while the surface pass already has one would make
  three laws where there are two.
- Anyone changes `fogDensity` or `volumeDensity` defaults. The crossover moves as the *square* of
  the first and linearly in the second, so a small edit to `fogDensity` moves it a long way.
