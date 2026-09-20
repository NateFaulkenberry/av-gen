#pragma once

// The deformation contract (ADR-422): what a deformation operator owes the frame it moves.
//
// ## The one sentence
//
// **A deformation operator declares whether it preserves vertex correspondence. A preserving
// operator joins `deformChain` and gets its motion vectors for free. A non-preserving operator must
// author velocity itself, and if it does not, every temporal effect downstream is wrong.**
//
// That is a contract and not a convention, which is why it is a type rather than a comment.
//
// ## Why it exists, mechanically
//
// ADR-035's scene pass writes five colour targets, one of which is **velocity**, and the frame
// carries `prevViewProj` and a per-object `prevModel`. `shaders/procedural.wgsl` fills that target
// the only honest way there is: it runs the whole deform chain a second time, at last frame's time
// and through last frame's object matrix, and writes `out.prevClip` from the result.
//
//     var pPrev = deformChain(srcPos, n, nRef, inst, proc.prevInfo.x, object.prevModel);
//     out.prevClip = frame.prevViewProj * vec4<f32>(pPrev, 1.0);
//
// Read that closely and the contract falls out of it. `pPrev` is computed from **`srcPos`** -- the
// source mesh's vertex. It is the *same vertex* evaluated at two times. That is the entire reason
// the existing stack's motion vectors are correct without anybody thinking about them, and it is
// exactly what an operator that changes the vertex set destroys.
//
// A twist has `srcPos` at t and at t-dt, so its velocity is a subtraction. A fragmentation has a
// triangle that did not exist last frame, or existed as part of a different shell, or has been
// re-indexed: there is no `srcPos` to evaluate, the subtraction is meaningless, and what lands in
// the velocity target is the difference between two unrelated points. Motion blur smears along it,
// temporal accumulation reprojects along it, and the artefact appears in the *consumer* rather than
// in the operator that caused it -- which is the worst property a defect can have.
//
// ## Why the declaration has no default
//
// `VertexCorrespondence` is a required constructor argument of `DeformationOperatorTraits` and has
// no default, deliberately.
//
// A default of `Preserved` would give a fragmentation operator silently wrong velocity -- the exact
// failure, landing on the downstream effect. A default of `Broken` would make every ordinary
// operator pay for authored velocity it does not need, and the cost of being wrong that way is
// visible immediately, which means somebody would "fix" it by flipping the default back.
//
// So neither is the default: the question has to be answered. This repository's recurring defect is
// a field nobody filled in behaving exactly like a field somebody filled in with the wrong answer
// (ADR-225, ADR-350, ADR-375, ADR-385, ADR-392, ADR-420, ADR-421 -- seven ADRs and counting). The
// cheapest place to break that chain is a constructor that will not compile without the answer.
//
// ## What is in scope
//
// The deformation brief's §4-§7 (melt, liquify, twist/bend/spiral/pinch/bulge/wave/ripple/fold) and
// §30-§34 are vertex-preserving: they move vertices that already exist, so they join the chain and
// inherit correct velocity. §18-§25 (geometry glitch, triangle stretch, ribbons, fragmentation,
// explode -> reform, dissolve -> melt -> reconstruct, voxelize, pixelate-geometry) and §35-§37
// (object -> particles -> object, object -> voxels -> particles) are not, and they are the ones the
// temporal agent and this one have to agree about.
//
// `EffectorOp::Velocity` is the trap this contract was written for. It exists, serialises, round
// trips, appears in the World panel's effector list, is implemented on the CPU path -- and the GPU
// effector pass, the only one a rendered frame takes, skips it. It is still skipped and now says so
// (`procedural_renderer.cpp`); it is where an authored velocity will land when a non-preserving
// operator needs one.

#include <cstdint>
#include <string_view>

namespace avgen::scene {

// Whether an operator's output vertices correspond, one for one, to the vertices it was given.
//
// Spelled as a two-valued enum rather than a `bool` because `bool preservesVertices` at a call site
// reads as `true` or `false` with no hint of what it decides, and what it decides is whether the
// frame's velocity target is meaningful.
enum class VertexCorrespondence : std::uint8_t {
    // Every output vertex is the same vertex as one input vertex, moved. The operator may be
    // evaluated at an arbitrary time -- which is what `deformChain` does with `prevInfo.x` -- and
    // the difference between two evaluations IS the velocity. Nothing further is owed.
    Preserved,
    // The vertex set is different: created, destroyed, re-indexed, split, merged or re-topologised.
    // Differencing two evaluations subtracts unrelated points, so the operator must author velocity
    // for what it produces. An operator that declares this and writes nothing is a stated problem;
    // one that declares `Preserved` and breaks correspondence is a silent one, in somebody else's
    // effect.
    Broken,
};
[[nodiscard]] constexpr std::string_view vertexCorrespondenceName(VertexCorrespondence c) {
    // Exhaustive, no `default`, as everywhere in this family.
    switch (c) {
    case VertexCorrespondence::Preserved: return "preserved";
    case VertexCorrespondence::Broken: return "broken";
    }
    return "unknown";
}

// What every deformation operator declares about itself.
//
// Held by value on the operator's descriptor, constructed with the answer, and readable by the
// renderer so it can decide whether to trust `prevClip` or to expect an authored velocity. There is
// no default constructor: the answer is the point.
struct DeformationOperatorTraits {
    constexpr explicit DeformationOperatorTraits(VertexCorrespondence correspondence)
        : vertices(correspondence) {}

    VertexCorrespondence vertices;

    // True when the frame's velocity target can be filled by running the operator twice, which is
    // what `shaders/procedural.wgsl` already does for the whole of `deformChain`.
    [[nodiscard]] constexpr bool velocityFromDoubleEvaluation() const {
        return vertices == VertexCorrespondence::Preserved;
    }
    // True when the operator owes the frame a velocity of its own. Exactly the negation, spelled
    // out because the obligation is the thing a reader is looking for.
    [[nodiscard]] constexpr bool mustAuthorVelocity() const {
        return vertices == VertexCorrespondence::Broken;
    }
};

// The seven deformers that exist today (ADR-023, ADR-421) all move vertices that already exist, so
// they are all `Preserved` and all get their velocity from the double evaluation
// `procedural.wgsl` already performs. Named here rather than assumed, because "the existing ones
// are fine" is a sentence that stops being true the moment somebody adds the eighth.
inline constexpr DeformationOperatorTraits kExistingDeformerTraits{VertexCorrespondence::Preserved};

} // namespace avgen::scene
