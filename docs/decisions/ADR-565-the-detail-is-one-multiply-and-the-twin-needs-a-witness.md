# ADR-565: the detail is one multiply, the twin needs a witness, and the reserved lane needs a check

Status: accepted. Date: 2026-09-21. Phase B+ of the Fog Bank brief (§13, §14, §44).

**Written after the fact, and that is the first finding.** The decisions below shipped across
`54457e3d`..`80475f09` and seven files cite "ADR-565" for them. The file did not exist. A code
comment pointing at an ADR number that was never written is a dangling reference of exactly the
kind ADR-385 is about -- *a stated reason is not evidence*, and a cited one that cannot be read is
worse, because it looks like evidence. Found while indexing ADR-566; the record is completed here
rather than the citations repointed, because the decisions are real and are worth stating once.

## Context

ADR-563 gave the fog bank an analytic shape and §44's first bar -- *noise off, still looks like
fog* -- became reachable. That leaves three loose ends, and each one is a place where something
could go wrong quietly.

## Decision

### 1. The macro detail is one multiply whose mean is exactly 1

§13/§14: **noise distorts large boundaries, it does not create the density.** So the detail term is
a single low-frequency octave applied as

    1 + amount * (n * 2 - 1)

with three properties that keep it secondary, and each is a property rather than a tuning:

- **identity at zero.** `amount` 0 returns exactly 1.0, so the analytic field is untouched and
  §44's bar stays reachable *by construction* rather than by a small number;
- **mean-preserving by construction.** The vortex's `mix(flatLevel, shaped, cloudNoise)` is not:
  ADR-560 measured it raising the frame's mean luminance by 20.7 levels *with the detail switched
  off*, because its compensation assumed a mean that the billow and turbulence had moved. ADR-389's
  family rule, applied before the coefficient exists rather than after;
- **one octave, at a period tied to the bank's own radius**, so the detail is the same *shape* at
  any size -- ADR-564's size independence applied to structure -- and it cannot introduce spatial
  frequencies the march's sample spacing cannot carry.

Two artist rows, `detailScale` and `detailDrift`, and the packed home for them is **lane 7**: the
vortex packs `(eyeWallWidth, eyeWallGain, cloudNoise, 0)` there and a fog bank has no eye to want
the first two for. `.z` stays `cloudNoise` exactly where `packVortex` put it, so the control an
artist moves is the one the field reads.

**The test of the mean was wrong before the code was.** It failed at +7.1% and the code was right:
it averaged the field over a region comparable to the noise's own period, so what came back was
*where the bank happens to sit in the noise*. Measured across many periods, `E[fbm3]` is 0.50044
and the term's mean is 1.00089. That is `docs/testing.md` 21 -- a window too small for the quantity
to exist in it returns a phase dressed as a statistic, and it reproduces, which is what makes it
convincing.

### 2. A CPU/GPU pair with no parity test is two implementations with a convention

`shaders/fog.wgsl` claims to be a transliteration of `src/world/fog_field.cpp`. Within a day of
writing the pair I added the detail term to the shader and not to the CPU side, and **the two
disagreed for ten minutes**, caught only because an unrelated probe kept failing.

So the pair gets a witness: `tests/rendering/test_fog_parity_gpu.cpp` runs both over the same
packed lanes and compares. Through the **packed lanes** specifically, which is the reason
`MediumSlot` exists as a separate step -- both sides start from bytes identical by construction, so
a disagreement is about the maths and never about how a parameter was read on the way in.

The timing argument, which is why it lands before the detail is tuned rather than after: **noise is
where two transliterations drift silently.** A closed-form disagreement shows up as a shape; a
noise disagreement shows up as a different grain, which looks like a tuning choice. Tuning against
a CPU side that already disagrees with the GPU side you are looking at is the failure this
forecloses. It carries two controls -- samples must land *inside* the bank, and the detail term
must not be the constant 1 -- because a parity test over a field that is zero everywhere passes
perfectly and proves nothing.

### 3. The reserved lane is a checked constraint, not a comment

ADR-562 reserved lane 15 for the kind tag by convention. `agent/tornado` walked into it: sixteen
lanes, sixty floats, wanting sixty-one, and the overflow landed exactly there.

**The failure mode is not the one either party expected.** The tag is written *after* `pack`, so
the tag always wins, the dispatch keeps working, and what disappears is the **packer's** value. It
presents as a wrong appearance rather than a wrong shape -- and that is the expensive kind:
*"renders as a comet" is a bug someone finds in a minute; "the thick colour is subtly wrong" is one
that gets tuned around.*

So a sentinel is written into lane 15 before the call and checked after, naming the kind in a
one-shot warning. Two stores per medium per frame to turn a silent loss into a reported one. **A
reserved lane that nothing enforces is a comment, and sixty-one floats will find it.**

### 4. Adding a dispatch invalidates every probe written against the pre-dispatch path

ADR-563 gave the fog bank its own density function. Every test that sampled the shared one **went
on passing while asserting about a field nothing calls for that kind.** The probe does not break --
that is the whole problem.

Three probes in one file were mis-aimed, not the one that was noticed, and the worst guarded a
*shipped fix* (ADR-561's eye hole): from that commit onward its claim was untested and passing. The
audit is two greps -- files that call the old field, files that construct the dispatched kind -- and
the intersection is the suspect set. `docs/testing.md` 22 carries it. **A boundary is measured, not
estimated**: the prediction that only one *kind* was affected held; the guess that only one *case*
was came from the instance tripped over rather than from a search, and was wrong.

## Consequences

- The fog block grows to eight stored rows; `test_effect_registry` reports it, and the number is
  taken from a count of the declarations rather than from the failing output.
- `test_fog_bank.cpp`'s cases sample `world::fogShapeAt` -- the field the march evaluates for this
  kind -- and not the vortex's.
- The sentinel has a cost of two stores per medium per frame and no other. It is the cheapest
  enforcement available for a constraint whose violation is silent.
- **The dangling-ADR check is now part of finishing a phase**: `grep -ro 'ADR-[0-9]*' src shaders
  tests | sort -u` against `ls docs/decisions`. Seven files cited this number for a day.

## Revisit when

- A second kind wants a macro detail with a different form. The mean-preserving multiply is a
  *family* rule (ADR-389), not a fog decision, and the third kind is the one that tests it.
- The parity harness needs a shape the sample set does not reach. Its controls are counts of
  samples landing inside the field, and a new primitive can pass them vacuously if the points are
  not moved with it -- which is why ADR-566's parity case asserts its own inside-count per shape.
