#include "world/world_composer.hpp"

#include "core/noise.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

// The composer speaks the biome vocabulary the shipped worlds already use -- forest, meadow,
// marsh, rim, scree -- rather than inventing one, so its layers drop into an existing terrain
// without translation. The names appear in biomesFor() below.

// A small deterministic hash, so every placement decision is a function of the recipe's seed and
// nothing else. Not a PRNG stream: streams make the result depend on the *order* decisions are
// made in, which means adding a layer silently moves everything after it.
float hash01(std::uint32_t seed, std::uint32_t salt) {
    std::uint32_t h = seed * 0x9E3779B1u ^ (salt + 0x85EBCA6Bu);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Which categories a recipe's ecology weights address, so the two stay in step by construction.
float ecologyWeightFor(const EcologyWeights& w, assets::AssetCategory c) {
    switch (c) {
    case assets::AssetCategory::Flora:
    case assets::AssetCategory::Organic:
        return w.flora;
    case assets::AssetCategory::Fungi:
        return w.fungi;
    case assets::AssetCategory::Rock:
    case assets::AssetCategory::Terrain:
        return w.rock;
    case assets::AssetCategory::Crystal:
        return w.crystal;
    case assets::AssetCategory::Creature:
        return w.creature;
    case assets::AssetCategory::Structure:
    case assets::AssetCategory::Architectural:
        return w.structure;
    default:
        return 0.0f;
    }
}

float bandWeight(const CompositionWeights& c, DepthBand band) {
    switch (band) {
    case DepthBand::Foreground:
        return c.foreground;
    case DepthBand::Midground:
        return c.midground;
    case DepthBand::Background:
        return c.background;
    }
    return 0.0f;
}

// How far a band is allowed to be seen from, and how small on screen it may get before it is
// dropped. A foreground fern that survives to four hundred metres costs a fortune and contributes
// a pixel; a ridge silhouette culled at ninety metres leaves a hole in the horizon.
struct BandTuning {
    float viewDistance;
    float minScreenRadius;
    float clusterScale;
    float clustering;
    bool castsShadow;
    int maxInstances;
};

BandTuning tuningFor(DepthBand band) {
    switch (band) {
    case DepthBand::Foreground:
        return {90.0f, 1.4f, 11.0f, 0.55f, false, 120000};
    case DepthBand::Midground:
        return {220.0f, 2.4f, 22.0f, 0.45f, true, 40000};
    case DepthBand::Background:
        return {620.0f, 1.0f, 48.0f, 0.60f, true, 6000};
    }
    return {200.0f, 2.0f, 20.0f, 0.5f, true, 20000};
}

// Where a category prefers to grow. A fern in scree and a boulder in a marsh are both wrong, and
// getting this from the category means a new asset inherits sensible ground without being told.
std::vector<BiomeDensity> biomesFor(assets::AssetCategory c, float density) {
    switch (c) {
    case assets::AssetCategory::Flora:
    case assets::AssetCategory::Organic:
        return {{"forest", density}, {"meadow", density * 0.55f}, {"marsh", density * 0.4f}};
    case assets::AssetCategory::Fungi:
        return {{"marsh", density}, {"forest", density * 0.5f}};
    case assets::AssetCategory::Rock:
        return {{"scree", density}, {"rim", density * 0.7f}, {"forest", density * 0.25f}};
    case assets::AssetCategory::Crystal:
        return {{"scree", density}, {"rim", density * 0.5f}};
    case assets::AssetCategory::Creature:
        return {{"forest", density}, {"marsh", density * 0.6f}};
    case assets::AssetCategory::Structure:
    case assets::AssetCategory::Architectural:
        return {{"rim", density}, {"meadow", density * 0.4f}};
    default:
        return {{"forest", density}};
    }
}
} // namespace

const char* depthBandName(DepthBand band) {
    switch (band) {
    case DepthBand::Foreground:
        return "foreground";
    case DepthBand::Midground:
        return "midground";
    case DepthBand::Background:
        return "background";
    }
    return "midground";
}

DepthBand CompositionPlan::bandOf(const std::string& layer) const {
    for (const auto& [name, band] : bands) {
        if (name == layer) {
            return band;
        }
    }
    return DepthBand::Midground;
}

DepthBand bandForAsset(const assets::AssetDescriptor& asset) {
    // An explicit tag always wins: it is somebody stating an intention, and guessing over the top
    // of a stated intention is how authored worlds get quietly rearranged.
    if (asset.hasTag("foreground")) {
        return DepthBand::Foreground;
    }
    if (asset.hasTag("background")) {
        return DepthBand::Background;
    }
    if (asset.hasTag("midground")) {
        return DepthBand::Midground;
    }
    // Otherwise height decides, because height is what actually determines whether a thing reads at
    // distance. Under a metre and a half is underfoot; over six metres is a silhouette.
    const float h = asset.effectiveHeight();
    if (h < 1.5f) {
        return DepthBand::Foreground;
    }
    if (h > 6.0f) {
        return DepthBand::Background;
    }
    return DepthBand::Midground;
}

Result<ComposedWorld> composeWorld(const WorldRecipe& recipe, const assets::AssetLibrary& library) {
    if (auto ok = recipe.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    if (library.size() == 0) {
        return fail("world '{}': the asset library is empty, so there is nothing to compose",
                    recipe.world);
    }

    ComposedWorld out;

    // ---- the focal subject -------------------------------------------------------------------
    // One thing the shot is about. Chosen as the most important asset the library offers, which is
    // what visualImportance is for; ties break on the earlier entry so the choice is stable.
    const assets::AssetDescriptor* hero = nullptr;
    for (const auto& a : library.assets()) {
        if (ecologyWeightFor(recipe.ecology, a.category) <= 0.0f) {
            continue;
        }
        if (hero == nullptr || a.visualImportance > hero->visualImportance) {
            hero = &a;
        }
    }
    if (hero != nullptr && recipe.composition.focalStrength > 0.0f) {
        FocalRegion f;
        // Off-centre, deterministically. A subject in the middle of the world is the composition
        // nobody chose.
        const float angle = hash01(recipe.seed, 11u) * 6.2831853f;
        const float dist = lerp(0.12f, 0.34f, hash01(recipe.seed, 12u)) * recipe.extent;
        f.center = glm::vec2(std::cos(angle) * dist, std::sin(angle) * dist);
        f.radius = lerp(0.04f, 0.09f, hash01(recipe.seed, 13u)) * recipe.extent;
        f.strength = recipe.composition.focalStrength;
        f.assetId = hero->id;
        out.plan.focal.push_back(f);
    }

    // ---- negative space ----------------------------------------------------------------------
    // Voids are placed away from the focal subject: an empty region on top of the one thing worth
    // looking at is not negative space, it is a hole.
    const int voidCount = static_cast<int>(std::round(recipe.composition.negativeSpace * 6.0f));
    for (int i = 0; i < voidCount; ++i) {
        const auto salt = static_cast<std::uint32_t>(40 + i * 7);
        VoidRegion v;
        const float angle = hash01(recipe.seed, salt) * 6.2831853f;
        const float dist = lerp(0.18f, 0.46f, hash01(recipe.seed, salt + 1u)) * recipe.extent;
        v.center = glm::vec2(std::cos(angle) * dist, std::sin(angle) * dist);
        v.radius = lerp(0.05f, 0.13f, hash01(recipe.seed, salt + 2u)) * recipe.extent;
        v.softness = v.radius * 0.45f;
        bool clashes = false;
        for (const auto& f : out.plan.focal) {
            if (glm::length(v.center - f.center) < (v.radius + f.radius) * 0.9f) {
                clashes = true;
                break;
            }
        }
        if (!clashes) {
            out.plan.voids.push_back(v);
        }
    }

    // ---- the layers --------------------------------------------------------------------------
    float covered = 0.0f;
    for (const auto& asset : library.assets()) {
        const float ecoWeight = ecologyWeightFor(recipe.ecology, asset.category);
        if (ecoWeight <= 0.0f) {
            continue; // the recipe says this category is absent, not rare
        }
        const DepthBand band = bandForAsset(asset);
        const float bandW = bandWeight(recipe.composition, band);
        if (bandW <= 0.0f) {
            continue;
        }
        // Density: what the asset wants, scaled by how much the recipe wants its kind and its
        // distance. An asset with no authored density gets one from its size, because a thing that
        // is two metres across cannot be as numerous as a thing that is twenty centimetres across.
        const float footprint = std::max(asset.naturalSize.x * asset.naturalSize.z, 0.01f);
        const float implied = std::clamp(0.25f / footprint, 0.0005f, 0.9f);
        const float base = asset.preferredDensity > 0.0f ? asset.preferredDensity : implied;
        const float density = base * ecoWeight * bandW;
        if (density <= 0.0f) {
            continue;
        }

        const BandTuning tune = tuningFor(band);
        ScatterLayer layer;
        layer.name = asset.id;
        layer.asset = library.resolve(asset).generic_string();
        layer.densities = biomesFor(asset.category, density);
        layer.height = asset.effectiveHeight();
        layer.minScale = std::max(1.0f - asset.variation.scale, 0.05f);
        layer.maxScale = 1.0f + asset.variation.scale;
        layer.randomYaw = asset.variation.yaw;
        layer.alignToGround = asset.variation.lean;
        layer.hueRandom = asset.variation.hue;
        layer.emissiveRandom = asset.variation.emissive;
        layer.tint = asset.material.tint;
        layer.clusterScale = tune.clusterScale;
        layer.clustering = tune.clustering;
        layer.viewDistance = tune.viewDistance;
        layer.minScreenRadius = tune.minScreenRadius;
        layer.castsShadow = tune.castsShadow && asset.visualImportance > 0.25f;
        layer.maxInstances = tune.maxInstances;
        layer.meshBudget = asset.triangles > 0 ? std::max(asset.triangles / 2, 24) : 0;

        // Emission comes from the material profile and is scaled by how much of the world's light
        // the recipe says the world itself provides.
        if (asset.material.emissive > 0.0f) {
            layer.emissiveColor = asset.material.tint;
            layer.emissiveIntensity = asset.material.emissive * recipe.lighting.bioluminescence * 3.0f;
            layer.emissiveSparsity = std::clamp(1.0f - asset.material.emissive, 0.0f, 0.85f);
        }

        // ---- ecology relations ---------------------------------------------------------------
        // The rule that makes a world rather than a scatter: small shade-tolerant things grow near
        // large ones. Expressed with the relation ecology already has, pointing at the tallest
        // layer of a taller band.
        if (band == DepthBand::Foreground && asset.category == assets::AssetCategory::Fungi) {
            const assets::AssetDescriptor* host = nullptr;
            for (const auto& other : library.assets()) {
                if (&other == &asset || ecologyWeightFor(recipe.ecology, other.category) <= 0.0f) {
                    continue;
                }
                if (bandForAsset(other) == DepthBand::Foreground) {
                    continue;
                }
                if (host == nullptr || other.effectiveHeight() > host->effectiveHeight()) {
                    host = &other;
                }
            }
            if (host != nullptr) {
                ScatterProximity p;
                p.layer = host->id;
                p.minDistance = 0.4f;
                p.maxDistance = std::max(host->effectiveHeight() * 0.8f, 3.0f);
                p.fade = 1.2f;
                p.strength = 0.7f;
                layer.proximity = p;
            }
        }

        covered += density;
        out.plan.bands.emplace_back(layer.name, band);
        out.layers.push_back(std::move(layer));
    }

    if (out.layers.empty()) {
        return fail("world '{}': the recipe's ecology weights match nothing in the library",
                    recipe.world);
    }

    // Reported rather than enforced: what the composition covers and what it deliberately gives
    // back. A caller that wants a sparser world lowers the weights; the composer does not overrule.
    const float area = recipe.extent * recipe.extent;
    out.plan.coveredFraction = std::min(covered, 1.0f);
    float emptied = 0.0f;
    for (const auto& v : out.plan.voids) {
        emptied += 3.14159265f * v.radius * v.radius;
    }
    out.plan.emptyFraction = area > 0.0f ? std::min(emptied / area, 1.0f) : 0.0f;
    return out;
}

} // namespace avgen::world
