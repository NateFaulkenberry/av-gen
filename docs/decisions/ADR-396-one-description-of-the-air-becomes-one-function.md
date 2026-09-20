# ADR-396: One description of the air becomes one function, and gets the test that names it

- Status: Accepted (2026-09-20)
- Builds on ADR-055 (the wind field), ADR-370 ("a leaf and the branch it fell from are reading one
  description of the air"), ADR-360 (mesh wind), ADR-182, ADR-378 (a tolerance whose form was the
  bug), ADR-385 (a stated reason is not evidence).

## Problem

The field had three implementations.

| where | what it was |
|---|---|
| `src/core/wind.cpp::sampleWind` | the CPU truth |
| `shaders/wind.wgsl::windSampleAt` | the GPU transliteration, reading `frame.*` |
| `shaders/particles.wgsl::particleWindAt` | a hand-maintained third copy, reading `params.*` |

The third had already lost a field. `WindSample` has five members; `ParticleWind` had four —
`phase`, the flutter's spatial phase, was not in it. The coupling between the two shader copies was
a comment: *"the expressions are `shaders/wind.wgsl`'s, term for term and in the same order."* There
was no test tying them together, and ADR-385 is about exactly that sentence: a stated reason is not
evidence.

Nothing in the existing coverage could have caught it. `tests/rendering/test_wind_gpu.cpp` is a
render test that measures where a stalk's tip landed, and it does so in a wind with
`regionAmount`, `regionDrift`, `turbulence` and `gustAmount` **all zero** — deliberately, because
that is the only wind whose answer can be written down. `turbulence = 0` also zeroes the flutter
gain that `phase` drives. It is a good test of the deformation and it is blind to three of the five
numbers the field produces, including the one that was missing.

ADR-378's history in the same file is the reason to be careful here rather than clever: a wind GPU
test went red, three hypotheses were wrong before the cause was found, and the form it replaced had
been *passing a shader perturbed by +15%*.

## Decision

**Two copies, not three, and the second one is the CPU.**

`shaders/wind_field.wgsl` is new and holds the whole transliteration as a pure function of a
`WindField` (the four packed vectors, mirroring `wind::WindUniforms`), a world position and a time.
It declares no binding and includes nothing, so any module can take it.

- `shaders/wind.wgsl` shrinks to 26 lines: it includes `wind_field.wgsl` and adds `windSampleAt`,
  which gathers `frame.windDir/windRegion/windGust/windTurb` and calls `windSampleFrom`. That is
  the one thing a binding-free module cannot do.
- `shaders/particles.wgsl` includes `wind_field.wgsl` directly and calls the same function with the
  copy of the field its own uniforms already carried. `particleWindAt` and `ParticleWind` are
  deleted.

This is the shape `vortex.wgsl` already uses (`sampleVortex(v, p, t)`), and it is why that field
could have a parity test at all.

The include directive does not de-duplicate, so `common.wgsl` → `wind.wgsl` → `wind_field.wgsl` is
the only path for a module with a frame group, and a module must not take both. Both files say so.

**Particle behaviour is unchanged.** The leaves now receive `phase` and do not read it. That is the
right way round: one description of the air, and each consumer decides what it needs from it.
Making falling leaves flutter on the tree's phase is a visible change to shipped scenes and is a
different decision.

## The probe, in two halves

Neither half is sufficient. A correct function nobody calls, and a shared call into a wrong
function, are each green under one of these and red under the pair.

**`tests/rendering/test_wind_parity_gpu.cpp` — the arithmetic is right.** A compute kernel over
`wind_field.wgsl` against `wind::sampleWind`, 131 positions spanning sub-metre to 368 m, on and off
the wind axis, at t = 0, 3.5 and 47.25 s, with every stochastic term on. All five members compared,
`phase` included.

- Guard against agreeing about nothing: 229 of the 393 samples carry a *partial* gust envelope and
  388 carry a turned direction. An agreement about a field that is everywhere zero, or everywhere
  the unturned steady flow, would be an agreement about nothing.
- Guard against a comparison that cannot fail, which is ADR-378's lesson made into a test case: the
  second case compiles the same module with the flutter wavenumber raised by **1%** and requires
  the same comparison to reject it. It rejects **129 of 131 samples on `phase` and 0 on the other
  four members**. So the comparison is sensitive to a one-per-cent error in precisely the member
  the deleted copy had dropped, and the perturbation reached only what it was aimed at.

**`tests/unit/test_wind_one_description.cpp` — it is the only one.** No device: the property is
about text the compiler will be handed, and a rule that needs a GPU in the room is a rule nobody
checks.

- Every `.wgsl` in the tree is include-resolved and searched for the regional wave's coefficients
  (`0.94 * a + 0.34 * c`). Every file must contain that at most **once**. Eighteen carry it; none
  carries two. A fourth hand-written copy is a second occurrence, whatever it is called.
- The body of `windSampleFrom` is extracted by brace matching from `procedural.wgsl` (the branch)
  and `particles.wgsl` (the leaf) and compared byte for byte: 1397 bytes, identical.
- Byte-identity between two absent things is how this kind of test passes without testing anything,
  so the body is required to be over 600 bytes and to contain `out.phase`, `out.strength`,
  `out.gust` and `out.direction` by name; and one arm is perturbed by a single character to show
  the comparator rejects it.
- `fn particleWindAt(` is required to appear zero times, so restoring the copy is a test failure
  rather than a review comment.

## Consequences

- `shaders/wind.wgsl` no longer contains the field. Anything that greps for `windSampleAt` still
  finds it; anything that greps for the arithmetic now lands in `wind_field.wgsl`.
- A module that wants the field and has no frame group takes `wind_field.wgsl`. That is now a
  supported thing to do rather than a reason to copy nineteen lines.
- The rule the unit test enforces is stricter than "particles and meshes agree": it is that the
  arithmetic exists once in the whole tree. The next system that wants wind gets it for free and
  cannot get it wrong.
