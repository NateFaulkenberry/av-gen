# ADR-422: A deformation operator declares whether its velocity is derivable

- Status: Accepted (2026-09-20)
- Extends ADR-035 (the five colour targets, one of them velocity), ADR-023 (procedural geometry),
  ADR-421 (the deformer stack's editor), ADR-225 (a setting the application does not keep is not a
  setting), ADR-385 (a stated reason is not evidence).
- The interface between the deformation half of the Reality/Temporal/Digital brief and the temporal
  half. Agreed with the coordinator before either side built on it.

## The contract

> **A deformation operator declares whether it preserves vertex correspondence. A preserving
> operator joins `deformChain` and gets its motion vectors for free. A non-preserving operator must
> author velocity itself, and if it does not, every temporal effect downstream is wrong.**

It is stated as a contract rather than a convention, and it is a **type** rather than a comment,
because a comment is what the last seven of these ADRs were written about.

## Why the line falls where it does

The coordinator's original framing was that deformation without velocity would break *every*
temporal effect. It does not, and the correction is the useful part.

ADR-035's scene pass already writes a velocity target, and `shaders/procedural.wgsl` already fills
it, the only honest way there is:

```wgsl
var pPrev = deformChain(srcPos, n, nRef, inst, proc.prevInfo.x, object.prevModel);
out.prevClip = frame.prevViewProj * vec4<f32>(pPrev, 1.0);
```

The whole chain is run a second time, at last frame's time and through last frame's object matrix.
Read it closely and the contract falls out: `pPrev` is computed from **`srcPos`**, the source mesh's
vertex. It is *the same vertex evaluated at two times*. That is why the existing stack's motion
vectors are correct without anybody having thought about them, and it is exactly what an operator
that changes the vertex set destroys.

A twist has `srcPos` at *t* and at *t−dt*, so its velocity is a subtraction. A fragmentation has a
triangle that did not exist last frame, or existed as part of a different shell, or has been
re-indexed: there is no `srcPos` to evaluate, the subtraction is between unrelated points, and what
lands in the velocity target is noise. Motion blur smears along it and temporal accumulation
reprojects along it, so **the artefact appears in the consumer rather than in the operator that
caused it** — the worst property a defect can have, and the reason this line is worth drawing before
either side builds.

So the brief's §4–§7 and §30–§34 — melt, liquify, twist, bend, spiral, pinch, bulge, wave, ripple,
fold, and the world-scale versions of them — are vertex-preserving and need nothing from the
temporal agent. §18–§25 and §35–§37 — geometry glitch, triangle stretch, ribbons, fragmentation,
explode→reform, dissolve→melt→reconstruct, voxelize, pixelate-geometry, object→particles→object,
object→voxels→particles — are the subset that has to author velocity, and the only subset the two
halves have to agree about.

## Decision

`src/scene/deformation_contract.hpp`. `VertexCorrespondence` is a two-valued enum — `Preserved` or
`Broken` — and `DeformationOperatorTraits` takes it as a **required, explicit constructor argument
with no default**.

Spelled as an enum rather than a `bool` because `bool preservesVertices` at a call site reads as
`true` or `false` with no hint of what it decides, and what it decides is whether the frame's
velocity target means anything.

### Why there is no default, at all

This is the part worth arguing, because "give it a sensible default" is the obvious review comment.

A default of `Preserved` gives a fragmentation operator **silently wrong velocity** — which is the
exact failure, landing on the downstream effect rather than on the operator. A default of `Broken`
makes every ordinary operator pay for authored velocity it does not need; being wrong that way is
visible immediately, which means somebody would "fix" it by flipping the default back to the
dangerous one.

So neither is the default and the question has to be answered. This repository's recurring defect is
**a field nobody filled in behaving exactly like a field somebody filled in with the wrong answer** —
ADR-225, ADR-350, ADR-375, ADR-385, ADR-392, ADR-420 and ADR-421 are seven instances of it in
different clothes. The cheapest place in the world to break that chain is a constructor that will not
compile without the answer, and `test_deformation_contract.cpp` asserts exactly that with
`STATIC_REQUIRE_FALSE(std::is_default_constructible_v<...>)`, plus `explicit` so that `f({})` is not
an answer either.

### `EffectorOp::Velocity` stops being silent

The coordinator named this as the trap the contract is for, and it is: *"a silently-skipped op is the
hook somebody reaches for first."*

`EffectorOp::Velocity` and `EffectorOp::Attribute` exist, serialise, round-trip, appear in the World
panel's effector list, and are implemented on the CPU path in `spatial::applyEffectors` — and
`procedural_renderer.cpp:1469` drops them on the floor, in the only pass a rendered frame takes. An
author who sets one gets an effector that is listed, saved, and inert. A third silent skip sat on the
same line: an effector whose field did not reach the GPU field table was also dropped without a word.

They are **still skipped** — making a velocity effector work needs somewhere to put a velocity, which
is this contract's whole subject — but they are no longer skipped *quietly*. Each is reported once
per object, field and op rather than per frame, because a message repeated sixty times a second is a
message nobody reads, which is the same failure seen from the other end. That is ADR-225's
distinction: the honest interim state of a setting the application does not keep is a stated problem,
not a still picture.

## Consequences

**The contract's own quotation is checked.** Its argument rests on what `procedural.wgsl` does, and
a quotation is a stated reason like any other (ADR-385). So the test reads that shader and requires
the four load-bearing facts to be in it — `deformChain(srcPos`, `proc.prevInfo.x`,
`object.prevModel`, `out.prevClip` — with a control string that is certainly absent, so the four are
not passing because `find` always succeeds. If somebody rewrites velocity derivation, this fails and
the contract gets re-read rather than quietly becoming fiction.

**`kExistingDeformerTraits` is a claim about seven operators and is checked rather than asserted.**
All seven existing kinds are `Preserved`, which is why ADR-421's editor needed no velocity work at
all; `deformPoint`'s signature is `vec3 -> vec3` and every kind goes through it.

**What this does not do.** It adds no operator, no shader and no rendered pixel. It does not make
`EffectorOp::Velocity` work; it makes its not working audible, and marks it as where an authored
velocity will land. And it does not decide *how* a non-preserving operator authors velocity — a
per-fragment attribute, a second vertex stream, or the effector op — because that decision should be
made by whoever writes the first one, against a real case, rather than invented here for none.
