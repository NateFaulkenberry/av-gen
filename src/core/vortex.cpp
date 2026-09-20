#include "core/vortex.hpp"

#include "core/noise.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::vortex {
namespace {

float smoothstepf(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ADR-389: an octave whose world period falls below twice the sample spacing cannot be resolved,
// and what it contributes is aliasing rather than detail. `filterWidth` 0 means a point sample --
// nothing is being integrated, so nothing can alias, and every octave is taken.
float octaveWeight(float periodMetres, float filterWidth) {
    if (filterWidth <= 0.0f) {
        return 1.0f;
    }
    return smoothstepf(0.0f, 1.0f, periodMetres / (2.0f * filterWidth));
}

// The shape, and the intermediates a velocity needs, in one place so the two entry points cannot
// drift. Every expression here is in the same order as `shaders/vortex.wgsl`; the parity test is
// what keeps that true rather than the comment.
struct Shape {
    float density = 0.0f;
    float envelope = 0.0f;
    float radialT = 0.0f;
    float depthT = 0.0f;
    glm::vec3 rel{0.0f};
    bool inside = false;
};

// ---- the macro structure (§7-§11) ------------------------------------------------------------
//
// Analytic: trigonometry and smoothsteps, no noise. Band-limited by construction, so unlike the
// fBM stack below it survives the march's 125-metre sample spacing intact -- which is the whole
// reason the picture is built here rather than there.

// §8/§9. The radial profile: a clear eye, a wall standing around it, and the body of the storm
// falling away to the rim. Returns the radial factor of the envelope.
//
// At `eyeWallWidth` 0.22 and `eyeWallGain` 0 this is ADR-374's profile exactly, to the bit.
float radialProfile(const VortexUniforms& v, float rr) {
    const float rim = 1.0f - smoothstepf(0.72f, 1.3f, rr);
    // The eye. `innerVoid` IS its radius -- see core/vortex.hpp on why this is not a second
    // control -- and `eyeWallWidth` is the 0.22 ADR-374 hardcoded, now authored. At the default
    // 0.22 and a gain of 0 this is `smoothstep(innerVoid, innerVoid + 0.22, rr) * rim`, which is
    // ADR-374's profile to the bit.
    const float eyeR = std::clamp(v.v2.x, 0.0f, 0.95f);
    const float wallW = std::max(v.v7.x, 1e-3f);
    // A HOLE, so its boundary is a rise and not a fade: density is ~0 inside `eyeR` and reaches
    // the body of the storm over `wallW`. Smoothstep and not a step, because ADR-369's rule is
    // that there must be no edge anywhere for a hard line to live on.
    const float eye = smoothstepf(eyeR, eyeR + wallW, rr);
    // §9: the wall itself -- a ring of extra density whose crest sits just outside the eye. This
    // is what makes the silhouette read as a hurricane rather than as a hole in a cloud.
    //
    // ADR-389's family rule, and this one is NOT neutralised, deliberately: `eyeWallGain` raises
    // the field's mean, because a storm with an eye wall really does hold more air there. It is
    // the caller's business, and `density` is re-tuned beside it in the same commit rather than
    // the gain being secretly normalised away -- normalising it would make the slider do nothing
    // to the picture's overall weight, which is half of what it is for.
    const float crest = eyeR + wallW;
    const float d = (rr - crest) / std::max(wallW * 1.5f, 1e-3f);
    const float ring = std::exp(-d * d);
    return eye * rim * (1.0f + std::max(v.v7.y, 0.0f) * ring);
}

// §10/§11. Logarithmic spiral bands, at three nested scales.
//
// A logarithmic spiral is r = a e^{b(theta)}, so a point's arm index is `theta - ln(r) / b`, which
// is constant along an arm -- that expression IS the band coordinate and everything else is
// shaping. `b` is the cotangent of the pitch angle, packed on the CPU so the shader does no
// trigonometry to recover it.
//
// Returns a MULTIPLIER whose mean over angle is exactly 1 at any depth (ADR-389's family rule:
// `density` and `emission` are per-metre coefficients calibrated against this field's mean, and a
// band function with a mean of 0.5 would silently halve the medium under them).
float spiralBands(const VortexUniforms& v, float rr, float angle, float t) {
    const float arms = v.v8.x;
    if (arms < 0.5f) {
        return 1.0f;
    }
    const float depth = std::clamp(v.v8.z, 0.0f, 1.0f);
    const float harmonic = std::clamp(v.v8.w, 0.0f, 1.0f);
    // Clamped away from the axis: ln(rr) diverges there, and the eye has removed that region from
    // the picture anyway. Without the clamp the bands wind infinitely fast at the centre and alias
    // no matter how many steps the march takes -- which would be this ADR's own mistake repeated.
    const float rClamped = std::max(rr, 0.06f);
    const float arm = angle - std::log(rClamped) * v.v8.y;
    // The arms turn with the structure. Same `rotationSpeed` the noise uses, so the bands and the
    // filaments cannot drift apart into two storms.
    const float spin = t * v.v1.z;
    float band = std::cos(arms * (arm - spin));
    // §11's nested scales. Three of them, at 3x and 7x the primary arm count and a third and a
    // ninth of its depth -- a structural hierarchy rather than an octave sum, because summing
    // equal-weight sinusoids is how you build noise, which is the thing §0 forbids.
    if (harmonic > 0.0f) {
        band += harmonic * (std::cos(arms * 3.0f * (arm - spin * 1.3f)) / 3.0f +
                            std::cos(arms * 7.0f * (arm - spin * 1.7f)) / 9.0f);
    }
    // Bands wash out at the eye wall, where the flow is a solid ring, and at the outer edge, where
    // the storm frays. Applied to the DEPTH rather than to the density, so the mean stays 1.
    const float reach = smoothstepf(0.0f, 0.22f, rr - std::clamp(v.v2.x, 0.0f, 0.95f)) *
                        (1.0f - smoothstepf(0.85f, 1.25f, rr));
    return 1.0f + depth * reach * band;
}

Shape evaluate(const VortexUniforms& v, const glm::vec3& p, float t, float filterWidth) {
    Shape s;
    const float radius = v.v0.w;
    if (radius <= 0.0f) {
        return s;
    }
    s.rel = p - glm::vec3(v.v0);
    // Breathing applies to the RADIUS, not the density: the silhouette has to move, or it reads as
    // the brightness pulsing rather than the thing breathing.
    const float breath = 1.0f + v.v3.x * std::sin(t * v.v3.y);
    const float depth = std::max(v.v4.x, 1e-3f);
    const float yn = std::clamp(-s.rel.y / depth, 0.0f, 1.0f); // 0 at the mouth, 1 at the throat
    const float mouth = std::lerp(1.0f, std::clamp(v.v4.y, 0.02f, 1.0f), yn * yn);
    const float rr = glm::length(glm::vec2(s.rel.x, s.rel.z)) / std::max(radius * breath * mouth, 1e-3f);
    s.radialT = rr;
    s.depthT = yn;
    if (rr > 1.35f) {
        return s; // outside the funnel entirely, and compactly so -- ADR-369's lesson
    }
    const float wall = std::exp(-(s.rel.y * s.rel.y) / std::max(v.v1.x * v.v1.x, 1e-3f));
    // `below` is not decoration: `yn` clamps to 0 ABOVE the mouth, so without it the throat term
    // evaluates at full value up there and the funnel extends UPWARD as a full-radius cylinder.
    // That was ADR-374's bug, and it is the reason this sampler states `depthT` is 0 above the
    // mouth rather than leaving the caller to find out.
    const float below = smoothstepf(0.0f, -v.v1.x, s.rel.y);
    const float throatFade = 1.0f - smoothstepf(0.55f, 1.0f, yn);
    const float vert = std::max(wall, throatFade * v.v4.z * below);
    if (vert < 1e-4f) {
        return s;
    }
    // ADR-374: the cheap masks BEFORE the noise. The cost of this function is how many samples
    // reach the three fBMs, not how many are taken; the void and everything past the rim are where
    // the answer is already zero and were paying full price for it.
    //
    // §7-§11: the macro structure lives HERE, in the envelope, above the early-out and below any
    // noise. That ordering is the brief's hierarchy expressed as control flow -- macro cyclone
    // structure, then density, then detail -- and it is what makes §52-§56's failure tests passable:
    // turn every noise term off and the eye, the wall, the bands and the funnel are still here.
    const float angle = std::atan2(s.rel.z, s.rel.x);
    const float envelope =
        radialProfile(v, rr) * spiralBands(v, rr, angle, t) * vert;
    if (envelope < 1.0e-6f) {
        return s;
    }
    s.envelope = envelope;
    s.inside = true;
    // Angle advanced by radius makes a spiral; advanced by time makes it turn.
    const float warped = angle + rr * v.v1.y + t * v.v1.z;
    const glm::vec3 q(std::cos(warped) * rr, s.rel.y / std::max(v.v1.x, 1e-3f), std::sin(warped) * rr);
    const float scale = std::max(v.v2.w, 1e-3f);
    // ADR-389, the domain warp: the finer octaves are advected through a low-frequency flow, so
    // their detail is carried BY the spiral instead of sitting on it. Uncorrelated detail on a
    // smooth flow is what the eye calls grain; advected detail is what it calls smoke.
    glm::vec3 q1 = q;
    glm::vec3 q2 = q;
    const float warpAmount = std::max(v.v6.x, 0.0f);
    if (warpAmount > 0.0f) {
        const glm::vec3 flow =
            noise::fbm3Vec(q * (scale * 0.7f) + glm::vec3(t * 0.02f, 0.0f, -t * 0.015f), 11u);
        q1 = q + flow * warpAmount;
        q2 = q + flow * (warpAmount * 1.6f);
    }
    // Three octaves at three rates: macro barely moves, fine detail moves fastest. One rate reads
    // instantly as a screensaver (ADR-371).
    const float strictScale = radius / std::max(2.0f * filterWidth * 4.11f, 1e-3f);
    const float effScale0 =
        filterWidth > 0.0f ? std::min(scale, std::max(strictScale, scale * 0.45f)) : scale;
    float n0 = noise::fbm3(q * effScale0 + glm::vec3(t * 0.013f, 0.0f, t * 0.009f), 29u);
    float n1 = noise::fbm3(q1 * (effScale0 * 3.1f) + glm::vec3(0.0f, t * 0.055f, 0.0f), 53u);
    float n2 = noise::fbm3(q2 * (effScale0 * 9.7f) + glm::vec3(t * 0.17f, 0.0f, -t * 0.13f), 97u);
    const float billow = std::clamp(v.v6.y, 0.0f, 1.0f);
    if (billow > 0.0f) {
        n0 = std::lerp(n0, std::abs(n0 * 2.0f - 1.0f), billow);
        n1 = std::lerp(n1, std::abs(n1 * 2.0f - 1.0f), billow);
        n2 = std::lerp(n2, std::abs(n2 * 2.0f - 1.0f), billow);
    }
    // ADR-389: the band-limit, floored. See the long note in `shaders/vortex.wgsl`: the strict
    // Nyquist scale at the shipped 32 steps erases the funnel, which is the correct answer to
    // "what can 125-metre samples carry" and the proof that the march is starved. The floor takes
    // the worst of the aliasing and leaves the rest of the problem where it belongs.
    const float periodBase = radius / effScale0;
    const float w0 = octaveWeight(periodBase, filterWidth);
    const float w1 = octaveWeight(periodBase / 3.1f, filterWidth);
    const float w2 = octaveWeight(periodBase / 9.7f, filterWidth);
    const float detail = std::max(v.v6.z, 0.0f);
    const float wSum = std::max(w0 + 0.45f * w1 + detail * w2, 1e-4f);
    float n = (n0 * w0 + 0.45f * n1 * w1 + detail * n2 * w2) / wSum;
    n = std::lerp(n, n * (0.55f + 0.9f * n1), std::clamp(v.v2.z, 0.0f, 1.0f));
    // ADR-389: a smoothstep remap, not `pow`, and compensated for the mean it moves. See the long
    // note in `shaders/vortex.wgsl`: `pow(n, c)` has mean 1/(c+1) and a centred smoothstep has mean
    // 0.5 whatever its width, so swapping them without this factor multiplies the medium's mean
    // density by about six and blows the frame out -- the authored `density` and `emission` are
    // per-metre coefficients calibrated against the old mean.
    const float contrast = std::max(v.v2.y, 0.05f);
    const float half = 0.5f / contrast;
    const float curve = smoothstepf(0.5f - half, 0.5f + half, std::clamp(n, 0.0f, 1.0f));
    const float shaped = curve * (2.0f / (contrast + 1.0f));
    // §53, and §5's diagnostic: `cloudNoise` is the weight of the whole fBM stack against a FLAT
    // field of the same mean. At 0 the density is the macro envelope alone and nothing else, which
    // is the render the owner asks to be shown before any detail is added; at 1 it is ADR-389's
    // field exactly. Blended toward the noise's own mean (2 / (contrast + 1) times a half) rather
    // than toward 1, so turning the detail down does not brighten the medium -- the ADR-389 family
    // again, and the reason this is a mix and not a multiply.
    const float flat = 0.5f * (2.0f / (contrast + 1.0f));
    const float weight = std::clamp(v.v7.z, 0.0f, 1.0f);
    s.density = std::lerp(flat, shaped, weight) * envelope;
    return s;
}

} // namespace

VortexUniforms packVortex(const VortexField& f) {
    VortexUniforms v;
    v.v0 = glm::vec4(f.center, f.radius);
    v.v1 = glm::vec4(std::max(f.thickness, 0.01f), f.swirl, f.rotationSpeed, 0.0f);
    v.v2 = glm::vec4(std::clamp(f.innerVoid, 0.0f, 0.95f), std::max(f.contrast, 0.05f),
                     std::clamp(f.turbulence, 0.0f, 1.0f), std::max(f.turbulenceScale, 1e-3f));
    v.v3 = glm::vec4(std::max(f.breathAmount, 0.0f), f.breathSpeed, 0.0f, 0.0f);
    v.v4 = glm::vec4(std::max(f.funnelDepth, 0.0f), std::clamp(f.throat, 0.02f, 1.0f),
                     std::clamp(f.throatDensity, 0.0f, 1.0f), 0.0f);
    v.v6 = glm::vec4(std::max(f.smokeWarp, 0.0f), std::clamp(f.smokeBillow, 0.0f, 1.0f),
                     std::max(f.detail, 0.0f), 0.0f);
    v.v7 = glm::vec4(std::max(f.eyeWallWidth, 1e-3f), std::max(f.eyeWallGain, 0.0f),
                     std::clamp(f.cloudNoise, 0.0f, 1.0f), 0.0f);
    // The pitch angle is packed as its COTANGENT, which is the `b` of r = a e^{b theta}: the
    // shader then needs no trigonometry to recover the spiral, and the artist still types an angle.
    // Clamped away from 0 and 90 degrees because both are degenerate -- 0 is a circle and 90 is a
    // straight radial spoke, and neither is a band.
    const float pitch = std::clamp(f.bandPitchDegrees, 2.0f, 80.0f) * 3.14159265358979f / 180.0f;
    v.v8 = glm::vec4(std::max(f.bandArms, 0.0f), std::cos(pitch) / std::sin(pitch),
                     std::clamp(f.bandDepth, 0.0f, 1.0f), std::clamp(f.bandHarmonic, 0.0f, 1.0f));
    return v;
}

float vortexShape(const VortexUniforms& v, const glm::vec3& p, float t, float filterWidth) {
    return evaluate(v, p, t, filterWidth).density;
}

VortexSample sampleVortex(const VortexUniforms& v, const glm::vec3& p, float t) {
    // A point sample: nothing is being integrated, so nothing can alias.
    const Shape s = evaluate(v, p, t, 0.0f);
    VortexSample out;
    out.density = s.density;
    out.envelope = s.envelope;
    out.radialT = s.radialT;
    out.depthT = s.depthT;
    if (!s.inside) {
        return out;
    }
    // The velocity, derived from the SAME geometry the shape is, which is the whole point of one
    // sampler. Three terms, each with a reason:
    //
    //   tangential  the swirl the shape's angular shear already describes, as a real speed:
    //               `rotationSpeed` is radians per second, so v = omega x r.
    //   radial      inward, strongest at the rim and vanishing at the axis, because a funnel that
    //               pulls uniformly gathers everything into a point and stops looking like one.
    //   vertical    down the throat, scaled by how far in the sample already is, so motes fall
    //               once they are caught rather than being dragged down from outside the mouth.
    //
    // A particle integrating this gets entrainment for free: the tangential term at a shrinking
    // radius is exactly the spiral ADR-380 approximated with an attractor and an orbit force.
    const glm::vec2 planar(s.rel.x, s.rel.z);
    const float planarLen = glm::length(planar);
    const glm::vec2 outward = planarLen > 1e-4f ? planar / planarLen : glm::vec2(1.0f, 0.0f);
    const glm::vec2 tangent(-outward.y, outward.x);
    const float omega = v.v1.z;
    const float radius = std::max(v.v0.w, 1e-3f);
    const float tangential = omega * planarLen;
    // Inward draw: zero at the axis, peaking at the rim. `swirl` sets how tight the spiral is, so
    // it also sets how much of the motion is inward rather than around -- one number, one idea.
    const float inward = radius * std::abs(omega) * std::abs(v.v1.y) * s.radialT *
                         (1.0f - smoothstepf(1.0f, 1.35f, s.radialT));
    const float descent = -radius * std::abs(omega) * v.v4.z * (1.0f - s.radialT) * s.envelope;
    out.velocity = glm::vec3(tangent.x * tangential - outward.x * inward, descent,
                             tangent.y * tangential - outward.y * inward);
    return out;
}

} // namespace avgen::vortex
