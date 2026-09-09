#pragma once

// Colour utilities (ADR-030): conversions between linear RGB, HSV, HSL, OKLab and OKLCH,
// perceptual hue/saturation/lightness manipulation, cosine palettes and gradient ramps.
// Pure functions; `shaders/color.wgsl` implements the same maths for material programs.
// All RGB values are linear (not sRGB-encoded) unless the function says otherwise.

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace avgen::color {

[[nodiscard]] glm::vec3 rgbToHsv(const glm::vec3& rgb);   // h in turns [0,1), s, v
[[nodiscard]] glm::vec3 hsvToRgb(const glm::vec3& hsv);
[[nodiscard]] glm::vec3 rgbToHsl(const glm::vec3& rgb);
[[nodiscard]] glm::vec3 hslToRgb(const glm::vec3& hsl);
[[nodiscard]] glm::vec3 rgbToOklab(const glm::vec3& rgb); // L, a, b
[[nodiscard]] glm::vec3 oklabToRgb(const glm::vec3& lab);
[[nodiscard]] glm::vec3 oklabToOklch(const glm::vec3& lab); // L, C, h (turns)
[[nodiscard]] glm::vec3 oklchToOklab(const glm::vec3& lch);
[[nodiscard]] glm::vec3 srgbToLinear(const glm::vec3& srgb);
[[nodiscard]] glm::vec3 linearToSrgb(const glm::vec3& linear);
[[nodiscard]] float luminance(const glm::vec3& rgb); // Rec. 709 weights

// Manipulation (in OKLCH unless noted; results clamped to >= 0).
[[nodiscard]] glm::vec3 hueShift(const glm::vec3& rgb, float turns);          // OKLCH hue rotation
[[nodiscard]] glm::vec3 hueShiftHsv(const glm::vec3& rgb, float turns);       // HSV hue rotation (legacy look)
[[nodiscard]] glm::vec3 saturate(const glm::vec3& rgb, float factor);         // chroma × factor
[[nodiscard]] glm::vec3 lighten(const glm::vec3& rgb, float amount);          // L += amount
[[nodiscard]] glm::vec3 contrast(const glm::vec3& rgb, float factor, float pivot = 0.5f);
[[nodiscard]] glm::vec3 mixOklab(const glm::vec3& a, const glm::vec3& b, float t); // perceptual mix

// Cosine palette (Quilez): a + b * cos(2π (c t + d)).
struct CosinePalette {
    glm::vec3 a{0.5f}, b{0.5f}, c{1.0f}, d{0.0f, 0.33f, 0.67f};
    [[nodiscard]] glm::vec3 sample(float t) const;
};
// Gradient ramp with up to 8 stops (position in [0,1] ascending, colour), linear or OKLab mixing,
// optional cycling offset (t += offset, wrapped).
struct Ramp {
    struct Stop {
        float position = 0.0f;
        glm::vec3 color{0.0f};
    };
    std::array<Stop, 8> stops{};
    int stopCount = 0;
    bool perceptual = true;
    bool cyclic = false;
    [[nodiscard]] glm::vec3 sample(float t, float offset = 0.0f) const;
};

} // namespace avgen::color
