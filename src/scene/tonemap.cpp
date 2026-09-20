#include "scene/tonemap.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

glm::vec3 clamp01(glm::vec3 c) { return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)); }

glm::vec3 acesFitted(glm::vec3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

glm::vec3 agxContrast(glm::vec3 x) {
    const glm::vec3 x2 = x * x;
    const glm::vec3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 +
           0.1191f * x - 0.00232f;
}

// The exact inverse of the sRGB OETF below. ADR-372: `pow(v, 2.2)` here is NOT that inverse and
// cost up to 9 code values of 255, worst in the toe. The two functions are a pair and neither may
// be changed without the other.
glm::vec3 srgbToLinearExact(glm::vec3 c) {
    const glm::vec3 lo = c / 12.92f;
    const glm::vec3 hi = glm::pow((c + 0.055f) / 1.055f, glm::vec3(2.4f));
    return {c.x <= 0.04045f ? lo.x : hi.x, c.y <= 0.04045f ? lo.y : hi.y,
            c.z <= 0.04045f ? lo.z : hi.z};
}

float linearToSrgb1(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

glm::vec3 linearToSrgb(glm::vec3 c) {
    return {linearToSrgb1(c.x), linearToSrgb1(c.y), linearToSrgb1(c.z)};
}

glm::vec3 agx(glm::vec3 val) {
    // WGSL's mat3x3 constructor takes COLUMNS and so does glm::mat3, so these transcribe directly.
    // Getting that backwards is a transpose that looks like a colour-science disagreement.
    const glm::mat3 inset(glm::vec3(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f),
                          glm::vec3(0.0784335999999992f, 0.878468636469772f, 0.0784336f),
                          glm::vec3(0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f));
    const glm::mat3 outset(glm::vec3(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f),
                           glm::vec3(-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f),
                           glm::vec3(-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f));
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    glm::vec3 v = inset * val;
    v = glm::clamp(glm::log2(glm::max(v, glm::vec3(1e-10f))), glm::vec3(minEv), glm::vec3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    v = outset * v;
    // AgX's output is sRGB-ENCODED; the caller re-encodes. This undoes exactly that encode.
    return srgbToLinearExact(clamp01(v));
}

glm::vec3 reinhardExtended(glm::vec3 c) {
    const float white = 4.0f;
    const float l = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    const float lm = l * (1.0f + l / (white * white)) / (1.0f + l);
    return clamp01(c * (lm / std::max(l, 1e-5f)));
}

glm::vec3 pbrNeutral(glm::vec3 color) {
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;
    const float x = std::min(color.r, std::min(color.g, color.b));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color -= glm::vec3(offset);
    const float peak = std::max(color.r, std::max(color.g, color.b));
    if (peak < startCompression) {
        return clamp01(color);
    }
    const float d = 1.0f - startCompression;
    const float newPeak = 1.0f - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return clamp01(glm::mix(color, glm::vec3(newPeak), g));
}

float smoothstep1(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// `hash12` in the shader. See the header: this is the one thing that cannot agree with the GPU,
// because `sin` of a large argument is not specified to the same precision and the 43758.5 multiply
// turns the last bits into a different random number.
float hash12(glm::vec2 p) {
    const float h = glm::dot(p, glm::vec2(127.1f, 311.7f));
    const float s = std::sin(h) * 43758.5453123f;
    return s - std::floor(s);
}

} // namespace

glm::vec3 tonemapOperator(TonemapOperator op, glm::vec3 hdr) {
    switch (op) {
    case TonemapOperator::AgX: return agx(hdr);
    case TonemapOperator::Reinhard: return reinhardExtended(hdr);
    case TonemapOperator::PbrNeutral: return pbrNeutral(hdr);
    case TonemapOperator::Clamp: return clamp01(hdr);
    case TonemapOperator::AcesFitted: break;
    }
    return acesFitted(hdr);
}

glm::vec3 retainChroma(glm::vec3 hdr, glm::vec3 mapped, float amount) {
    const float peakHdr = std::max(hdr.r, std::max(hdr.g, hdr.b));
    const float peakMapped = std::max(mapped.r, std::max(mapped.g, mapped.b));
    if (amount <= 0.0f || peakHdr < 1e-5f || peakMapped < 1e-5f) {
        return mapped;
    }
    const glm::vec3 hue = hdr * (peakMapped / peakHdr);
    const float overWhite = smoothstep1(0.8f, 3.0f, peakHdr);
    return clamp01(glm::mix(mapped, hue, amount * overWhite));
}

glm::vec3 tonemapPixel(const TonemapInputs& in, glm::vec3 hdr, glm::vec2 uv, glm::vec2 size) {
    const glm::vec3 exposed = hdr * in.exposure;
    glm::vec3 mapped = tonemapOperator(in.op, exposed);
    mapped = retainChroma(exposed, mapped, in.chromaRetention);
    if (in.vignette > 0.0f) {
        const glm::vec2 d2 = (uv - glm::vec2(0.5f)) * glm::vec2(1.0f, size.y / std::max(size.x, 1.0f)) * 2.0f;
        mapped *= 1.0f - in.vignette * smoothstep1(0.35f, 1.25f, glm::length(d2));
    }
    if (in.grain > 0.0f) {
        const float n = hash12(uv * size + glm::vec2(in.seed * 17.0f, in.seed * 3.0f)) - 0.5f;
        mapped = clamp01(mapped + n * in.grain * 0.12f);
    }
    return linearToSrgb(mapped);
}

std::uint8_t quantise8(float encoded) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

std::uint8_t srgbByte(float displayLinear) { return quantise8(linearToSrgb1(displayLinear)); }

void tonemapImage(const TonemapInputs& in, std::uint32_t width, std::uint32_t height,
                  std::span<const glm::vec3> hdr, std::span<std::uint8_t> rgba) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || hdr.size() < pixels || rgba.size() < pixels * 4) {
        return;
    }
    const glm::vec2 size(static_cast<float>(width), static_cast<float>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            // The shader's uv at the centre of this texel. `fs_main` samples rather than loads
            // (ADR-137), and at a matched size a bilinear footprint collapses to weight 1 on the
            // texel under the sample point -- so a 1:1 CPU pass is the same picture, which is the
            // only case an offline renderer produces.
            const glm::vec2 uv((static_cast<float>(x) + 0.5f) / size.x,
                               (static_cast<float>(y) + 0.5f) / size.y);
            const glm::vec3 encoded = tonemapPixel(in, hdr[i], uv, size);
            rgba[i * 4 + 0] = quantise8(encoded.r);
            rgba[i * 4 + 1] = quantise8(encoded.g);
            rgba[i * 4 + 2] = quantise8(encoded.b);
            rgba[i * 4 + 3] = 255;
        }
    }
}

} // namespace avgen::scene
