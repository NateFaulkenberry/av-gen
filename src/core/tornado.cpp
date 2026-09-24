#include "core/tornado.hpp"

#include "core/noise.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::tornado {
namespace {

// What counts as "thin" for the edge erosion below, in envelope units. A constant since the
// sixteenth lane became the kind tag; it is the value the control it replaced had settled on.
constexpr float kEdgeWidth = 0.6f;

float smoothstepf(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The shape and the intermediates a velocity needs, in one place so the two entry points cannot
// drift apart. Every expression below is in the same order as `shaders/tornado.wgsl`; the parity
// test is what keeps that true rather than this comment.
struct Shape {
    float density = 0.0f;
    float envelope = 0.0f;
    float radialT = 0.0f;
    float heightT = 0.0f;
    glm::vec3 rel{0.0f};
    glm::vec2 axis{0.0f};
    bool inside = false;
};

float radiusAt(const TornadoUniforms& v, float h) {
    const float hh = std::pow(std::clamp(h, 0.0f, 1.0f), std::max(v.t1.w, 0.05f));
    const float u = 1.0f - hh;
    return u * u * v.t1.x + 2.0f * u * hh * v.t1.y + hh * hh * v.t1.z;
}

float rotationAt(const TornadoUniforms& v, float h) {
    return std::lerp(v.t8.x, v.t8.y,
                     std::pow(std::clamp(h, 0.0f, 1.0f), std::max(v.t8.z, 0.05f)));
}

glm::vec2 axisAt(const TornadoUniforms& v, float h, float t) {
    const float hc = std::clamp(h, 0.0f, 1.0f);
    const glm::vec2 lean = glm::vec2(v.t7.x, v.t7.y) * (hc * hc);
    const float amp = v.t7.z;
    if (amp <= 0.0f) {
        return lean;
    }
    const float s = t * v.t7.w;
    const glm::vec2 w(std::sin(6.2831853f * hc * 0.7f + s), std::cos(6.2831853f * hc * 1.3f + s * 0.83f));
    return lean + w * (amp * (0.12f + 0.88f * hc));
}

float stripes(const TornadoUniforms& v, float rr, float h, float angle, float t) {
    if (v.t5.x < 0.5f) {
        return 1.0f;
    }
    const float depth = std::clamp(v.t5.z, 0.0f, 0.68f);
    const float harmonic = std::clamp(v.t5.w, 0.0f, 1.0f);
    const float phase = angle - v.t5.y * h + rotationAt(v, h) * t;
    float band = std::cos(v.t5.x * phase);
    if (harmonic > 0.0f) {
        band = band + harmonic * (std::cos(v.t5.x * 3.0f * phase) / 3.0f +
                                  std::cos(v.t5.x * 7.0f * phase) / 9.0f);
    }
    return 1.0f + depth * smoothstepf(0.30f, 0.85f, rr) * band;
}

// §27/§20. The transliteration of `tornadoSuction`; see `shaders/tornado.wgsl` for why this is a
// cosine windowed at the radius of maximum wind rather than a loop over N orbiting Gaussians --
// the short version is that a cosine's mean over angle is exactly zero, so the term's mean is
// exactly 1 whatever the count, and `density` is a per-metre coefficient calibrated against this
// field's mean (ADR-389's family).
float suction(const TornadoUniforms& v, float rr, float h, float angle, float t) {
    const float count = v.t9.z;
    if (count < 0.5f || v.t9.w <= 0.0f) {
        return 1.0f;
    }
    const float d = (rr - std::clamp(v.t10.x, 0.0f, 2.0f)) / std::max(v.t10.y, 1e-3f);
    const float window = std::exp(-d * d);
    const float spin = t * (v.t10.z + rotationAt(v, h)) + h * 2.0f;
    return 1.0f + std::clamp(v.t9.w, 0.0f, 0.9f) * window * std::cos(count * (angle - spin));
}

float octaveWeight(float periodMetres, float filterWidth) {
    if (filterWidth <= 0.0f) {
        return 1.0f;
    }
    return smoothstepf(0.0f, 1.0f, periodMetres / (2.0f * filterWidth));
}

// The transliteration of `tornadoDetail`. See `shaders/tornado.wgsl` for the reasoning at length:
// mean exactly 1 so ADR-389's per-metre calibration survives, a co-moving sample frame so detail
// rides the flow with no state, and a band limit that is not a knob.
float detail(const TornadoUniforms& v, float rr, float h, float angle, float envelope, float radius,
             float t, float filterWidth) {
    const float amount = std::clamp(v.t10.w, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return 1.0f;
    }
    const float spin = rotationAt(v, h) * t;
    const float climb = v.t12.y * t;
    const glm::vec3 q(std::cos(angle - spin) * rr, h * 3.0f - climb, std::sin(angle - spin) * rr);
    const float s0 = std::max(v.t12.x, 1e-3f);
    const float s1 = s0 * 3.1f;
    const float s2 = s0 * 9.7f;
    const float a0 = std::max(v.t11.x, 0.0f) * octaveWeight(radius / s0, filterWidth);
    const float a1 = std::max(v.t11.y, 0.0f) * octaveWeight(radius / s1, filterWidth);
    const float a2 = std::max(v.t11.z, 0.0f) * octaveWeight(radius / s2, filterWidth);
    const float sum = a0 + a1 + a2;
    if (sum <= 1e-4f) {
        return 1.0f;
    }
    const float n0 = noise::fbm3(q * s0 + glm::vec3(t * 0.011f, 0.0f, t * 0.008f), 71u);
    const float n1 = noise::fbm3(q * s1 + glm::vec3(0.0f, t * 0.043f, 0.0f), 131u);
    const float n2 = noise::fbm3(q * s2 + glm::vec3(t * 0.15f, 0.0f, -t * 0.11f), 197u);
    const float n = (n0 * a0 + n1 * a1 + n2 * a2) / sum;
    const float contrast = std::max(v.t11.w, 0.05f);
    const float half = 0.5f / contrast;
    const float shaped = smoothstepf(0.5f - half, 0.5f + half, std::clamp(n, 0.0f, 1.0f));
    const float unit = shaped * 2.0f;
    const float edge = 1.0f - smoothstepf(0.0f, kEdgeWidth, envelope);
    const float bite = std::clamp(amount * (1.0f + std::max(v.t12.z, 0.0f) * edge), 0.0f, 1.0f);
    return std::lerp(1.0f, unit, bite);
}

// The condensation shell's cross-section (ADR-580 §5), as a function of a normalised distance `x`
// from an axis: a Gaussian sheath peaking at `x = 1`, an interior fill that starts falling at
// `coreRadius`, and an outer fade that begins past the sheath's crest. The funnel is this profile
// around the funnel radius; the debris cloud is the SAME profile around its own radius (ADR-706),
// so the field still has four named parts with one set of rules between them, not five.
float sheath(const TornadoUniforms& v, float x) {
    const float edgeSoft = std::max(v.t3.x, 1e-3f);
    const float shellWidth = std::max(v.t2.x, 1e-3f);
    const float d = (x - 1.0f) / shellWidth;
    const float shell = std::exp(-d * d) * std::max(v.t2.y, 0.0f);
    const float interior =
        std::max(v.t2.w, 0.0f) * (1.0f - smoothstepf(std::clamp(v.t2.z, 0.0f, 1.0f), 1.0f, x));
    const float outer = 1.0f - smoothstepf(1.0f + shellWidth, 1.0f + shellWidth + edgeSoft, x);
    return (shell + interior) * outer;
}

Shape evaluate(const TornadoUniforms& v, const glm::vec3& p, float t, float filterWidth) {
    Shape s;
    const float height = v.t0.w;
    if (height <= 0.0f) {
        return s;
    }
    s.rel = p - glm::vec3(v.t0);
    const float h = s.rel.y / height;
    s.heightT = h;

    const float skirtHeight = std::max(v.t4.y, 1e-3f);
    const float footSoft = std::max(v.t3.w, 1e-3f);
    if (h > 1.08f || h < -supportBelow(v)) {
        return s;
    }

    const float hc = std::clamp(h, 0.0f, 1.0f);
    s.axis = axisAt(v, hc, t);
    const glm::vec2 planar = glm::vec2(s.rel.x, s.rel.z) - s.axis;
    const float dist = glm::length(planar);
    const float radius = std::max(radiusAt(v, hc), 1e-3f);

    const float edgeSoft = std::max(v.t3.x, 1e-3f);
    const float shellWidth = std::max(v.t2.x, 1e-3f);
    const float skirtRadius =
        std::max(v.t1.x * std::max(v.t4.x, 1.0f) * (1.0f + std::max(v.t4.w, 0.0f)), 1e-3f);
    const float cloudRadius = std::max(v.t1.z * std::max(v.t8.w, 1.0f), 1e-3f);
    if (dist > std::max(std::max(radius * (1.0f + shellWidth + edgeSoft), skirtRadius), cloudRadius)) {
        s.radialT = dist / radius;
        return s;
    }

    // ADR-706: the funnel's lower end is a TIP, not a plane. `foot` still fades the density over
    // `reach +- footSoft`, but the radius closes with it, so the silhouette rounds to a point
    // instead of ending in a horizontal cut at full width.
    const float reach = 1.0f - std::clamp(v.t3.z, 0.0f, 1.0f);
    const float foot = smoothstepf(reach - footSoft, reach + footSoft, h);
    const float tip = std::sqrt(foot);
    const float rr = dist / std::max(radius * tip, 1e-3f);
    s.radialT = dist / radius;

    float funnel = sheath(v, rr);
    const float wallCloud = 1.0f + std::max(v.t3.y, 0.0f) * smoothstepf(0.55f, 1.0f, hc);
    const float cap = 1.0f - smoothstepf(1.0f, 1.06f, h);
    funnel = funnel * wallCloud * cap * foot;

    const float angle = std::atan2(planar.y, planar.x);
    funnel = funnel * stripes(v, rr, hc, angle, t);
    const float suck = suction(v, rr, hc, angle, t);
    funnel = funnel * suck;

    // ADR-706: the debris cloud. The same sheath cross-section as the funnel, around a radius that
    // is a MOUND in height: widest at the ground (`skirtFlare`, a linear flare), with superelliptic
    // shoulders that stay full and then round over, thinning as it rises the way lifted dust does,
    // and a short rounded underside so a column standing on nothing does not end in a disc. See the
    // shader for the two profiles that were rendered and rejected on the way here.
    float skirt = 0.0f;
    if (v.t4.z > 0.0f && h < skirtHeight) {
        const float hd = h / skirtHeight;
        const float rTop = v.t1.x * std::max(v.t4.x, 1.0f);
        float rb = 0.0f;
        if (hd >= 0.0f) {
            const float hd2 = hd * hd;
            const float shoulder = std::sqrt(std::sqrt(std::max(1.0f - hd2 * hd2, 0.0f)));
            rb = rTop * (1.0f + std::max(v.t4.w, 0.0f) * (1.0f - hd)) * shoulder;
        } else {
            const float q = hd / kDebrisUnder;
            rb = skirtRadius * std::sqrt(std::max(1.0f - q * q, 0.0f));
        }
        const float u = dist * (1.0f + shellWidth + edgeSoft) / std::max(rb, 1e-3f);
        const float thin = 1.0f - smoothstepf(0.3f, 1.0f, std::max(hd, 0.0f));
        skirt = v.t4.z * sheath(v, u) * thin * stripes(v, u, hc, angle, t) * suck;
    }

    float cloud = 0.0f;
    const float cloudHeight = std::clamp(v.t9.x, 1e-3f, 1.0f);
    if (h > 1.0f - cloudHeight && v.t9.y > 0.0f) {
        const float ch = smoothstepf(1.0f - cloudHeight, 1.0f - cloudHeight * 0.66f, h);
        const float hb = smoothstepf(1.0f - cloudHeight, 1.0f, h);
        const float cr = std::max(v.t1.z * std::max(v.t8.w, 1.0f) * (0.62f + 0.38f * hb), 1e-3f);
        cloud = v.t9.y * (1.0f - smoothstepf(0.55f, 1.0f, dist / cr)) * ch * cap;
    }

    const float envelope = funnel + skirt + cloud;
    if (envelope < 1.0e-6f) {
        return s;
    }
    s.envelope = envelope;
    s.inside = true;
    s.density = envelope * detail(v, s.radialT, hc, angle, envelope, radius, t, filterWidth);
    return s;
}

} // namespace

float supportBelow(const TornadoUniforms& v) {
    return std::max(0.02f, std::max(std::max(v.t3.w, 1e-3f), kDebrisUnder * std::max(v.t4.y, 1e-3f)));
}

TornadoUniforms packTornado(const TornadoField& f) {
    TornadoUniforms v;
    v.t0 = glm::vec4(f.base, std::max(f.height, 0.0f));
    // The artist types the radius AT MID HEIGHT; the shader wants the Bezier control point, and
    // the two differ. A quadratic Bezier through (rb, rm, rt) passes through
    // `0.25 rb + 0.5 rm + 0.25 rt` at its midpoint, so the control point that puts the curve at a
    // requested `radiusMid` is `2 * mid - (rb + rt) / 2`. Converted here, once a frame, rather than
    // in the shader, once a sample -- and clamped non-negative, because a control point far enough
    // below both ends would fold the curve through zero and turn the funnel inside out.
    const float rb = std::max(f.radiusBottom, 0.0f);
    const float rt = std::max(f.radiusTop, 0.0f);
    const float control = std::max(2.0f * std::max(f.radiusMid, 0.0f) - 0.5f * (rb + rt), 0.0f);
    v.t1 = glm::vec4(rb, control, rt, std::max(f.taper, 0.05f));
    v.t2 = glm::vec4(std::max(f.shellWidth, 1e-3f), std::max(f.shellGain, 0.0f),
                     std::clamp(f.coreRadius, 0.0f, 1.0f), std::max(f.coreDensity, 0.0f));
    v.t3 = glm::vec4(std::max(f.edgeSoft, 1e-3f), std::max(f.wallCloudGain, 0.0f),
                     std::clamp(f.touchdown, 0.0f, 1.0f), std::max(f.footSoft, 1e-3f));
    v.t4 = glm::vec4(std::max(f.skirtWidth, 1.0f), std::max(f.skirtHeight, 1e-3f),
                     std::max(f.skirtDensity, 0.0f), std::max(f.skirtFlare, 0.0f));
    v.t5 = glm::vec4(std::max(f.stripeCount, 0.0f), f.stripePitch,
                     std::clamp(f.stripeDepth, 0.0f, 0.68f), std::clamp(f.stripeHarmonic, 0.0f, 1.0f));
    // `coreRadiusMetres` of 0 means "track the funnel". A fixed core radius against an animated
    // `radiusBottom` is a tornado whose visible funnel and whose fastest air stop agreeing, and the
    // default has to be the one that cannot be wrong rather than a number that happens to suit the
    // default shape.
    const float rc = f.coreRadiusMetres > 0.0f ? f.coreRadiusMetres : std::max(rb, 1e-3f);
    v.t6 = glm::vec4(f.circulation, std::max(rc, 1e-3f), f.inflow, std::max(f.lift, 0.0f));
    v.t7 = glm::vec4(f.lean.x, f.lean.y, std::max(f.wobbleAmount, 0.0f), f.wobbleSpeed);
    v.t8 = glm::vec4(f.rotationBottom, f.rotationTop, std::max(f.rotationCurve, 0.05f),
                     std::max(f.cloudWidth, 1.0f));
    v.t9 = glm::vec4(std::clamp(f.cloudHeight, 1e-3f, 1.0f), std::max(f.cloudDensity, 0.0f),
                     std::max(f.suctionCount, 0.0f), std::clamp(f.suctionStrength, 0.0f, 0.9f));
    v.t10 = glm::vec4(std::clamp(f.suctionRadius, 0.0f, 2.0f), std::max(f.suctionWidth, 1e-3f),
                      f.suctionSpeed, std::clamp(f.cloudAmount, 0.0f, 1.0f));
    v.t11 = glm::vec4(std::max(f.macroAmp, 0.0f), std::max(f.mesoAmp, 0.0f),
                      std::max(f.microAmp, 0.0f), std::max(f.detailContrast, 0.05f));
    // `.w` is the SCATTERING coefficient, not a field value: the appearance had to borrow the
    // slot `edgeWidth` used to hold, because lane 15 belongs to the kind tag (ADR-562).
    v.t12 = glm::vec4(std::max(f.detailScale, 1e-3f), f.climbRate, std::max(f.erosion, 0.0f), 0.0f);
    return v;
}

float tornadoDensity(const TornadoUniforms& v, const glm::vec3& p, float t, float filterWidth) {
    return evaluate(v, p, t, filterWidth).density;
}

glm::vec3 tornadoVelocity(const TornadoUniforms& v, const glm::vec3& p, float t) {
    const float height = v.t0.w;
    if (height <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const glm::vec3 rel = p - glm::vec3(v.t0);
    const float hc = std::clamp(rel.y / height, 0.0f, 1.0f);
    const glm::vec2 planar = glm::vec2(rel.x, rel.z) - axisAt(v, hc, t);
    const float dist = glm::length(planar);
    const glm::vec2 outward = dist > 1e-4f ? planar / dist : glm::vec2(1.0f, 0.0f);
    const glm::vec2 tangent(-outward.y, outward.x);
    const float rc = std::max(v.t6.y, 1e-3f);
    const float x = dist / rc;
    // The `(1 - exp(-r^2/Rc^2))` factor is what makes Burgers-Rott finite on the axis; without it
    // the tangential term diverges as 1/r. The limit there is 0, which is both the physically right
    // answer and the numerically safe one, so the guard returns it rather than a clamp.
    float swirlSpeed = 0.0f;
    if (dist > 1e-4f) {
        swirlSpeed = v.t6.x * (1.0f - std::exp(-x * x)) / dist;
    }
    swirlSpeed = swirlSpeed * rotationAt(v, hc);
    const float a = v.t6.z;
    const float radialSpeed = -a * dist * 0.5f;
    const float axialSpeed = a * rel.y * std::max(v.t6.w, 0.0f);
    return glm::vec3(tangent.x * swirlSpeed + outward.x * radialSpeed, axialSpeed,
                     tangent.y * swirlSpeed + outward.y * radialSpeed);
}

TornadoSample sampleTornado(const TornadoUniforms& v, const glm::vec3& p, float t) {
    const Shape s = evaluate(v, p, t, 0.0f);
    TornadoSample out;
    out.density = s.density;
    out.envelope = s.envelope;
    out.radialT = s.radialT;
    out.heightT = s.heightT;
    if (!s.inside) {
        return out;
    }
    out.velocity = tornadoVelocity(v, p, t);
    return out;
}

} // namespace avgen::tornado
