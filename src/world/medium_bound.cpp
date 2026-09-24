// The march's per-slot bound, on the CPU (ADR-566): the transliteration of the first half of
// `mediumInterval` in `shaders/volume.wgsl`.
//
// **Why it exists.** ADR-562 §4 gave every medium a ray interval -- a vertical cylinder the march
// clips its steps to -- and that interval is a CLAIM: *every non-zero sample of this medium's
// field lies inside this cylinder.* A bound that claims too much only costs field evaluations, and
// costs nothing else at all, because the march's step positions do not depend on it: the same
// `steps` over the same `maxDistance`, with the interval deciding only whether a step evaluates
// the field or skips it. A bound that claims too LITTLE deletes part of the medium, and it deletes
// it in the way that is hardest to see -- the bank is still there, still soft-edged, still the
// right colour, just shorter than the number the artist typed.
//
// That asymmetry is the design rule this file is built on: **when a bound is uncertain, be
// generous.** The only currency it spends is samples that were going to be taken anyway.
//
// **Why it is a C++ file at all.** Until ADR-566 the claim lived only in WGSL, so the only thing
// that could check it was a rendered frame -- and a rendered frame cannot tell you a bank is
// three times too short unless you already know how long it should be. Here the bound is a
// function of the packed lanes, so `test_medium_bound.cpp` can take the field's OWN support,
// measured by sampling `fogShapeAt` on a grid, and assert containment directly.
//
//   **A bound is a claim about the support of a field, and the field is what settles it.**

#include "world/atmospherics.hpp"

#include "core/tornado.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {

MediumBound mediumBound(const MediumSlot& m) {
    MediumBound b;
    const float radius = m.lane[0].w;
    if (radius <= 0.0f) {
        b.radiusXZ = -1.0f; // the shader's "this slot is off" sentinel
        b.yBot = 1.0f;
        b.yTop = -1.0f;
        return b;
    }
    // `breathAmount` widens the radius by at most its own amount, and every field in this family is
    // compactly zero past a normalised distance of 1.35.
    const float breath = 1.0f + std::max(m.lane[3].x, 0.0f);
    const float thickness = std::max(m.lane[1].x, 1e-3f);
    const float depth = std::max(m.lane[4].x, 0.0f);
    const glm::vec3 centre(m.lane[0]);
    const auto kind = static_cast<EffectKind>(static_cast<int>(m.lane[15].x + 0.5f));

    // ---- the tornado (ADR-580) ---------------------------------------------------------------
    //
    // The transliteration of `mediumBoundOf`'s tornado arm, and it is the case the cylinder was
    // chosen for: a column 80 m across and 800 m tall is 1% of its own bounding sphere.
    //
    // **`lane[0]` means something different for this kind and that is deliberate.** `lane[0].w` is
    // the HEIGHT, not a radius, and `lane[0].xyz` is the GROUND CONTACT, not a centre. This
    // function and the shader's are the only two places that read both conventions, so they are
    // the only two places the two could be confused -- which is the argument for them being
    // transliterations of each other rather than two derivations.
    if (kind == EffectKind::Tornado) {
        const float height = radius; // named for what lane[0].w carries here
        // The radius curve is a quadratic Bezier, so it never leaves the convex hull of its three
        // control values -- the largest is a provable bound, not an estimate.
        const float widest = std::max(m.lane[1].x, std::max(m.lane[1].y, m.lane[1].z));
        const float funnel = widest * (1.0f + std::max(m.lane[2].x, 0.0f) + std::max(m.lane[3].x, 0.0f));
        const float skirt = m.lane[1].x * std::max(m.lane[4].x, 1.0f) * (1.0f + std::max(m.lane[4].w, 0.0f));
        const float cloud = m.lane[1].z * std::max(m.lane[8].w, 1.0f);
        // The axis is a curve: the lean displaces the top and the wobble swings it, and both move
        // the whole column sideways WITHIN the bound rather than deforming it. The lean is the
        // tornado's answer to the wind it subscribes to (§68) and it is applied by the packer, so
        // by the time a slot reaches here the bend is already in `lane[7]` and this covers it.
        const float lateral = std::sqrt(m.lane[7].x * m.lane[7].x + m.lane[7].y * m.lane[7].y) +
                              std::max(m.lane[7].z, 0.0f);
        b.radiusXZ = std::max(funnel, std::max(skirt, cloud)) + lateral;
        // Vertically the field is compactly supported: nothing above `h = 1.08`, and nothing below
        // the funnel's tip or the debris cloud's rounded underside (ADR-706) -- asked of the field's
        // own `supportBelow`, which reads only lanes 3 and 4, so the bound and the early-out are
        // one expression. The shader's twin is `tornadoSupportBelow`.
        tornado::TornadoUniforms u;
        u.t3 = m.lane[3];
        u.t4 = m.lane[4];
        const float below = tornado::supportBelow(u);
        b.yTop = centre.y + height * 1.08f;
        b.yBot = centre.y - height * below;
        return b;
    }

    if (kind != EffectKind::VolumetricFog) {
        b.radiusXZ = radius * breath * 1.35f;
        b.yTop = centre.y + thickness * 3.0f;
        b.yBot = centre.y - depth - thickness * 3.0f;
        return b;
    }

    // ---- the fog bank and its five primitives -----------------------------------------------
    //
    // THE DEFECT THIS FILE WAS WRITTEN FOR. `fogEllipticalRadius` normalises the long axis by
    // `radius * bankLength`, so a bank's support reaches `bankLength` times as far along it in
    // metres. The bound did not carry the factor, so at the default `bankLength` of 1 it was
    // exactly right and at the control's top end of 6 it cut five sixths of the bank's length off
    // -- and the artist sees a shorter bank, not an error.
    const float along = std::max(m.lane[13].y, 0.05f);
    const FogShape shape = fogShapeKindOf(m);
    // The sphere is the one primitive that ignores `bankLength` (see `fogPrimitiveDistance`), so
    // it is the one whose bound must not carry it. Every other shape reaches `radius * along`
    // along the long axis and `radius` across it, hence the max with 1.
    const float reach = (shape == FogShape::Sphere) ? 1.0f : std::max(along, 1.0f);
    b.radiusXZ = radius * breath * reach * 1.35f;

    if (shape == FogShape::Bank) {
        // A bank has no vertical semi-axis: `rim` is horizontal and the VERTICAL PROFILE is the
        // only term that makes it end in Y. That profile's upper tail is an exponential, so where
        // it ends is a function of `heightFalloff` -- which the old constant `3 * thickness` did
        // not ask about either. At the default falloff of 1.4 the constant happened to be right
        // (0.37% of the column's optical depth lay above it); at the control's low end of 0.05 it
        // cut 82% of a bank away.
        //
        // So: solve `exp(-h * falloff) = 0.01` for h, which leaves 1% of the column outside and is
        // below what a frame can show. The floor keeps a steep bank at the old three thicknesses;
        // the ceiling is where a bound this generous stops being worth the samples, and at it the
        // clipped fraction is `exp(-40 * falloff)` -- which reaches 13% only at the very bottom of
        // the falloff range, where the "bank" is global haze rather than a placed volume.
        const float falloff = std::max(m.lane[14].z, 0.01f);
        const float bias = std::clamp(m.lane[14].y, 0.0f, 1.0f);
        const float base = -thickness + 2.0f * thickness * bias;
        const float hTop = std::clamp(4.6f / falloff, 3.0f, 40.0f);
        b.yTop = centre.y + base + thickness * hTop;
        // Below the densest layer the profile is `exp(-(3h)^2)`, which is 1e-9 by h = -1.5.
        b.yBot = centre.y + base - depth - thickness * 1.5f;
        return b;
    }

    // A closed primitive ends where its own surface ends, and the profile can only make it
    // thinner -- `mix(1, profile, influence)` is at most 1 and never widens the support. The
    // sphere is normalised by `radius`; the other four by `thickness`.
    const float half = (shape == FogShape::Sphere) ? radius : thickness;
    b.yTop = centre.y + half * 1.35f;
    b.yBot = centre.y - depth - half * 1.35f;
    return b;
}

} // namespace avgen::world
