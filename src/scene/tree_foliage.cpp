#include "scene/tree_foliage.hpp"

#include "core/noise.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::scene {
namespace {

enum Channel : std::uint32_t {
    kLeafAngle = 600,
    kLeafLength = 601,
    kLeafWidth = 602,
    kLeafValue = 603,
    kLeafSide = 604,
};

// sRGB encode. The base colour texture is sampled as sRGB, so a value written linearly comes out
// darker than intended -- which on a leaf mask reads as the whole canopy losing a stop.
std::uint8_t encodeSrgb(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f));
}

struct Leaf {
    glm::vec2 base;
    glm::vec2 axis;   // unit, base -> tip
    float length;
    float halfWidth;
    float value;      // per-leaf brightness, so a spray is not one flat tone
};

// A lanceolate outline: widest a third of the way along, tapering to a point. Returns the half
// width at `t` along the leaf, as a fraction of `halfWidth`.
float leafProfile(float t) {
    if (t <= 0.0f || t >= 1.0f) {
        return 0.0f;
    }
    // sin(pi t) is a lozenge; raising the argument's first half sharpens the tip and fattens the
    // shoulder, which is the difference between a leaf and a grain of rice.
    return std::pow(std::sin(std::pow(t, 0.72f) * glm::pi<float>()), 0.85f);
}

} // namespace

TextureData makeLeafSprayTexture(const LeafSpraySettings& settings) {
    TextureData texture;
    texture.name = "tree.leafspray";
    texture.width = std::max(settings.resolution, 32u);
    texture.height = texture.width;
    texture.format = TextureFormat::Rgba8Unorm;
    texture.data.assign(static_cast<std::size_t>(texture.width) * texture.height * 4, 0);

    // Lay the leaves along a curved spine running up the card. Alternating sides with a little
    // jitter, because strictly alternating reads as a fishbone.
    std::vector<Leaf> leaves;
    const int count = std::max(settings.leaves, 1);
    for (int i = 0; i < count; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        // The spine: a gentle arc from the bottom centre toward the top, bending sideways.
        const glm::vec2 spine(0.5f + settings.spineCurve * (t * t - 0.15f), 0.06f + t * 0.86f);
        const float side = (i % 2 == 0 ? 1.0f : -1.0f) *
                           (0.55f + 0.45f * noise::hashIndex(settings.seed, index, kLeafSide));
        // Leaves point up and out; the ones near the tip point more nearly along the spine.
        const float angle = glm::half_pi<float>() - side * settings.leafSpread * (1.0f - 0.45f * t) +
                            (noise::hashIndex(settings.seed, index, kLeafAngle) - 0.5f) * 0.35f;
        Leaf leaf;
        leaf.base = spine;
        leaf.axis = glm::vec2(std::cos(angle), std::sin(angle));
        leaf.length = settings.leafLength * (0.72f + 0.56f * noise::hashIndex(settings.seed, index, kLeafLength));
        leaf.halfWidth = settings.leafWidth * (0.78f + 0.44f * noise::hashIndex(settings.seed, index, kLeafWidth));
        leaf.value = 0.78f + 0.30f * noise::hashIndex(settings.seed, index, kLeafValue);
        leaves.push_back(leaf);
    }

    for (std::uint32_t py = 0; py < texture.height; ++py) {
        for (std::uint32_t px = 0; px < texture.width; ++px) {
            const glm::vec2 p((static_cast<float>(px) + 0.5f) / static_cast<float>(texture.width),
                              1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(texture.height));
            float alpha = 0.0f;
            float value = 1.0f;
            for (const Leaf& leaf : leaves) {
                const glm::vec2 d = p - leaf.base;
                const float along = glm::dot(d, leaf.axis) / leaf.length;
                if (along <= 0.0f || along >= 1.0f) {
                    continue;
                }
                const glm::vec2 perp(-leaf.axis.y, leaf.axis.x);
                const float across = std::abs(glm::dot(d, perp));
                const float halfW = leaf.halfWidth * leafProfile(along);
                if (halfW <= 0.0f || across > halfW) {
                    continue;
                }
                // A soft edge over roughly one texel, so the cutout does not crawl when the mip
                // chain averages it. The renderer's alpha-coverage preservation handles the rest.
                const float edge = 1.0f - std::clamp((across / halfW - 0.82f) / 0.18f, 0.0f, 1.0f);
                if (edge <= alpha) {
                    continue;
                }
                alpha = std::max(alpha, edge);
                // Darker at the base and along the midrib, lighter at the tip. This is what a
                // viewer sees on looking into the crown, and it is invisible from fifty metres --
                // which is the correct division of labour for a texture.
                const float rib = 1.0f - settings.midribDarkening * (1.0f - std::clamp(across / halfW, 0.0f, 1.0f));
                const float root = 1.0f - settings.baseDarkening * std::pow(1.0f - along, 2.0f);
                const float tip = 1.0f + settings.tipLightening * along * along;
                value = leaf.value * rib * root * tip;
            }
            const std::size_t o = (static_cast<std::size_t>(py) * texture.width + px) * 4;
            const std::uint8_t v = encodeSrgb(value);
            texture.data[o] = v;
            texture.data[o + 1] = v;
            texture.data[o + 2] = v;
            texture.data[o + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
        }
    }
    return texture;
}

float leafSprayCoverage(const LeafSpraySettings& settings, float alphaCutoff) {
    const TextureData texture = makeLeafSprayTexture(settings);
    const auto cut = static_cast<std::uint8_t>(std::lround(std::clamp(alphaCutoff, 0.0f, 1.0f) * 255.0f));
    std::size_t solid = 0;
    const std::size_t texels = static_cast<std::size_t>(texture.width) * texture.height;
    for (std::size_t i = 0; i < texels; ++i) {
        if (texture.data[i * 4 + 3] >= cut) {
            ++solid;
        }
    }
    return texels > 0 ? static_cast<float>(solid) / static_cast<float>(texels) : 1.0f;
}

} // namespace avgen::scene
