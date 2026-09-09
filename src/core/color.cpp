// Colour utilities (ADR-030). `shaders/color.wgsl` is a transliteration of these functions; the
// exact formulas are documented in docs/procedural-materials.md ("Colour utilities").
//
// Conventions chosen here (the header fixes the signatures; these are the remaining choices):
//
// * Hue is in turns, wrapped to [0, 1) with h - floor(h) (so -0.25 == 0.75).
// * HSV/HSL follow the Wikipedia "HSL and HSV" article: C = max - min, hue sector by the max
//   channel, V = max, L = (max + min) / 2, S_hsv = C / V, S_hsl = C / (1 - |2L - 1|); a zero
//   denominator gives S = 0 and a zero chroma gives h = 0.
// * OKLab uses Björn Ottosson's published matrices (linear sRGB in and out) with a sign-preserving
//   cube root (cbrt(x) = sign(x) * |x|^(1/3)) so out-of-gamut / negative inputs stay finite.
// * OKLCH: C = sqrt(a^2 + b^2), h = atan2(b, a) / 2pi wrapped to [0, 1); a and b below 1e-8 in
//   magnitude give h = 0.
// * sRGB transfer functions are the IEC 61966-2-1 piecewise curves; the linear segment is used
//   for every value below its threshold (negative values included).
// * Every manipulation function converts through OKLab/OKLCH and clamps the result to >= 0
//   component-wise; the palette and the ramp do not clamp (the ramp mixes in OKLab when
//   perceptual, which clamps like mixOklab does).
// * Ramp: t += offset; cyclic ramps wrap t to [0, 1) and interpolate between the last and the
//   first stop across the wrap; non-cyclic ramps clamp t to [0, 1] and hold the end colours.

#include "core/color.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::color {

namespace {

constexpr float kTwoPi = 6.283185307179586f;

float fract(float x) {
    return x - std::floor(x);
}

float signedCbrt(float x) {
    return std::cbrt(x);
}

glm::vec3 clampPositive(const glm::vec3& v) {
    return glm::max(v, glm::vec3(0.0f));
}

// Hue sector (0..6) from the max channel, shared by HSV and HSL.
float hueSector(const glm::vec3& rgb, float maxc, float chroma) {
    if (chroma <= 0.0f) {
        return 0.0f;
    }
    float h6 = 0.0f;
    if (maxc == rgb.r) {
        h6 = (rgb.g - rgb.b) / chroma;
        if (h6 < 0.0f) {
            h6 += 6.0f;
        }
    } else if (maxc == rgb.g) {
        h6 = (rgb.b - rgb.r) / chroma + 2.0f;
    } else {
        h6 = (rgb.r - rgb.g) / chroma + 4.0f;
    }
    return h6;
}

// Chroma/intermediate/minimum → RGB (the common tail of hsvToRgb and hslToRgb).
glm::vec3 fromChroma(float hueTurns, float chroma, float m) {
    const float h6 = fract(hueTurns) * 6.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(h6, 2.0f) - 1.0f));
    glm::vec3 rgb{0.0f};
    if (h6 < 1.0f) {
        rgb = {chroma, x, 0.0f};
    } else if (h6 < 2.0f) {
        rgb = {x, chroma, 0.0f};
    } else if (h6 < 3.0f) {
        rgb = {0.0f, chroma, x};
    } else if (h6 < 4.0f) {
        rgb = {0.0f, x, chroma};
    } else if (h6 < 5.0f) {
        rgb = {x, 0.0f, chroma};
    } else {
        rgb = {chroma, 0.0f, x};
    }
    return rgb + glm::vec3(m);
}

float srgbToLinear1(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb1(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

} // namespace

// ---- conversions ----------------------------------------------------------------------------------

glm::vec3 rgbToHsv(const glm::vec3& rgb) {
    const float maxc = std::max({rgb.r, rgb.g, rgb.b});
    const float minc = std::min({rgb.r, rgb.g, rgb.b});
    const float chroma = maxc - minc;
    const float h = fract(hueSector(rgb, maxc, chroma) / 6.0f);
    const float s = maxc > 0.0f ? chroma / maxc : 0.0f;
    return {h, s, maxc};
}

glm::vec3 hsvToRgb(const glm::vec3& hsv) {
    const float chroma = hsv.z * hsv.y;
    return fromChroma(hsv.x, chroma, hsv.z - chroma);
}

glm::vec3 rgbToHsl(const glm::vec3& rgb) {
    const float maxc = std::max({rgb.r, rgb.g, rgb.b});
    const float minc = std::min({rgb.r, rgb.g, rgb.b});
    const float chroma = maxc - minc;
    const float l = 0.5f * (maxc + minc);
    const float h = fract(hueSector(rgb, maxc, chroma) / 6.0f);
    const float denom = 1.0f - std::fabs(2.0f * l - 1.0f);
    const float s = (chroma > 0.0f && denom > 0.0f) ? chroma / denom : 0.0f;
    return {h, s, l};
}

glm::vec3 hslToRgb(const glm::vec3& hsl) {
    const float chroma = (1.0f - std::fabs(2.0f * hsl.z - 1.0f)) * hsl.y;
    return fromChroma(hsl.x, chroma, hsl.z - 0.5f * chroma);
}

glm::vec3 rgbToOklab(const glm::vec3& c) {
    const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
    const float l_ = signedCbrt(l);
    const float m_ = signedCbrt(m);
    const float s_ = signedCbrt(s);
    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

glm::vec3 oklabToRgb(const glm::vec3& lab) {
    const float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
    const float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
    const float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

glm::vec3 oklabToOklch(const glm::vec3& lab) {
    const float chroma = std::sqrt(lab.y * lab.y + lab.z * lab.z);
    float h = 0.0f;
    if (std::fabs(lab.y) > 1e-8f || std::fabs(lab.z) > 1e-8f) {
        h = fract(std::atan2(lab.z, lab.y) / kTwoPi);
    }
    return {lab.x, chroma, h};
}

glm::vec3 oklchToOklab(const glm::vec3& lch) {
    const float angle = lch.z * kTwoPi;
    return {lch.x, lch.y * std::cos(angle), lch.y * std::sin(angle)};
}

glm::vec3 srgbToLinear(const glm::vec3& srgb) {
    return {srgbToLinear1(srgb.r), srgbToLinear1(srgb.g), srgbToLinear1(srgb.b)};
}

glm::vec3 linearToSrgb(const glm::vec3& linear) {
    return {linearToSrgb1(linear.r), linearToSrgb1(linear.g), linearToSrgb1(linear.b)};
}

float luminance(const glm::vec3& rgb) {
    return 0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b;
}

// ---- manipulation ---------------------------------------------------------------------------------

glm::vec3 hueShift(const glm::vec3& rgb, float turns) {
    glm::vec3 lch = oklabToOklch(rgbToOklab(rgb));
    lch.z = fract(lch.z + turns);
    return clampPositive(oklabToRgb(oklchToOklab(lch)));
}

glm::vec3 hueShiftHsv(const glm::vec3& rgb, float turns) {
    glm::vec3 hsv = rgbToHsv(rgb);
    hsv.x = fract(hsv.x + turns);
    return clampPositive(hsvToRgb(hsv));
}

glm::vec3 saturate(const glm::vec3& rgb, float factor) {
    glm::vec3 lab = rgbToOklab(rgb);
    lab.y *= factor;
    lab.z *= factor;
    return clampPositive(oklabToRgb(lab));
}

glm::vec3 lighten(const glm::vec3& rgb, float amount) {
    glm::vec3 lab = rgbToOklab(rgb);
    lab.x += amount;
    return clampPositive(oklabToRgb(lab));
}

glm::vec3 contrast(const glm::vec3& rgb, float factor, float pivot) {
    return clampPositive((rgb - glm::vec3(pivot)) * factor + glm::vec3(pivot));
}

glm::vec3 mixOklab(const glm::vec3& a, const glm::vec3& b, float t) {
    const glm::vec3 la = rgbToOklab(a);
    const glm::vec3 lb = rgbToOklab(b);
    return clampPositive(oklabToRgb(la * (1.0f - t) + lb * t));
}

// ---- palettes and ramps ---------------------------------------------------------------------------

glm::vec3 CosinePalette::sample(float t) const {
    const glm::vec3 phase = (c * t + d) * kTwoPi;
    return a + b * glm::vec3(std::cos(phase.x), std::cos(phase.y), std::cos(phase.z));
}

glm::vec3 Ramp::sample(float t, float offset) const {
    const int count = std::clamp(stopCount, 0, static_cast<int>(stops.size()));
    if (count == 0) {
        return glm::vec3(0.0f);
    }
    if (count == 1) {
        return stops[0].color;
    }
    t += offset;
    t = cyclic ? fract(t) : std::clamp(t, 0.0f, 1.0f);

    const auto blend = [this](const glm::vec3& x, const glm::vec3& y, float u) {
        return perceptual ? mixOklab(x, y, u) : x * (1.0f - u) + y * u;
    };

    const Stop& first = stops[0];
    const Stop& last = stops[static_cast<std::size_t>(count - 1)];
    if (t <= first.position || t >= last.position) {
        if (!cyclic) {
            return t <= first.position ? first.color : last.color;
        }
        // Across the wrap: last stop → (1) ≡ (0) → first stop.
        const float span = (1.0f - last.position) + first.position;
        const float local = t >= last.position ? t - last.position : t + (1.0f - last.position);
        // A zero-length wrap (stops at exactly 0 and 1) resolves to the first stop.
        const float u = span > 0.0f ? std::clamp(local / span, 0.0f, 1.0f) : 1.0f;
        return blend(last.color, first.color, u);
    }
    for (int i = 0; i + 1 < count; ++i) {
        const Stop& s0 = stops[static_cast<std::size_t>(i)];
        const Stop& s1 = stops[static_cast<std::size_t>(i + 1)];
        if (t >= s0.position && t <= s1.position) {
            const float span = s1.position - s0.position;
            const float u = span > 0.0f ? (t - s0.position) / span : 0.0f;
            return blend(s0.color, s1.color, u);
        }
    }
    return last.color; // unreachable for ascending stops
}

} // namespace avgen::color
