#include "world/world_composer.hpp"

#include "core/log.hpp"

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

// ---- art direction ----------------------------------------------------------------------------
//
// Section 15 of the brief is a luminance hierarchy, and it is a hierarchy rather than a set of
// brightnesses: the background is mostly dark, general vegetation is subtle, special plants are
// moderate, the landmark is strong, and rare accents are extremely bright. A world where every
// glowing thing glows equally is the "particle screensaver" failure wearing a forest costume --
// there is nothing to look at because everything is equally worth looking at.
//
// The library says what kind of thing a species is; the recipe says how brightly this world burns.
// So the tier is chosen from tags and band, and what it produces is a multiplier -- never an
// absolute intensity, which would let a manifest overrule the recipe.
struct EmissionTier {
    float intensity;    // absolute, from the profile's ladder
    float sparsity;     // fraction of specimens that stay dark
    int paletteRole;    // 0 shadow, 1 secondary, 2 primary, 3 foliage, 4 accent
};

// Which rung of the profile's ladder a species stands on. The rung values are the art direction's;
// this only decides *which* rung, from what the library says the species is.
//
// The intensities used to be multipliers invented here, which meant the hierarchy was the
// composer's opinion and a profile could not change its shape. Reading them off a ladder is what
// lets Glowmere's 200:1 spread and a bleached fen's 60:1 spread both be expressible, and it is why
// the gap between `noticeable` and `special` is validated on the profile rather than hoped for
// here.
EmissionTier emissionTierFor(const assets::AssetDescriptor& asset, DepthBand band,
                             const EmissionLadder& ladder) {
    // Rare accents first: they are defined by being rare, so they must not be reachable by any
    // other rule. Almost none of them are lit, and the ones that are are the brightest thing in
    // the world by a wide margin.
    if (asset.hasTag("rare") || asset.hasTag("accent")) {
        return {ladder.brightest, 0.88f, 4};
    }
    if (asset.hasTag("beacon")) {
        return {ladder.beacon, 0.82f, 2};
    }
    if (asset.hasTag("special")) {
        return {ladder.special, 0.55f, 1};
    }
    if (asset.category == assets::AssetCategory::Fungi) {
        return {ladder.rare, 0.45f, 2};
    }
    // Rock and anything else inert stays inert. Several layers emitting exactly nothing is part of
    // the hierarchy rather than an oversight -- it is what the bright things are bright against.
    if (asset.category == assets::AssetCategory::Rock ||
        asset.category == assets::AssetCategory::Crystal) {
        return {ladder.inert, 1.0f, 0};
    }
    switch (band) {
    case DepthBand::Background:
        // Silhouettes. A ridge line that glows is a ridge line that stops being a ridge line.
        return {ladder.silhouette, 0.80f, 2};
    case DepthBand::Midground:
        return {ladder.noticeable, 0.65f, 3};
    case DepthBand::Foreground:
        return {ladder.groundCover, 0.72f, 3};
    }
    return {ladder.groundCover, 0.6f, 3};
}

glm::vec3 roleColor(const PaletteRoles& roles, int role) {
    switch (role) {
    case 0:
        return roles.shadow;
    case 1:
        return roles.secondary;
    case 2:
        return roles.primary;
    case 3:
        return roles.foliage;
    default:
        return roles.accent;
    }
}

// How far a band's material is pulled toward the colour of the air. This is aerial perspective
// applied to the material rather than to the pixel: fog already handles what happens between the
// camera and a distant object, but a ridge of trees whose *own* colour is the same saturated green
// as the fern at the viewer's feet reads as a flat cut-out no matter how much fog is in front of
// it. Depth is a colour relationship before it is a fog integral.
float shadowPullFor(DepthBand band) {
    switch (band) {
    case DepthBand::Background:
        return 0.62f;
    case DepthBand::Midground:
        return 0.30f;
    case DepthBand::Foreground:
        return 0.10f;
    }
    return 0.3f;
}

// The atmosphere the profile describes, then bent by the recipe's weights.
//
// The two do different jobs and both are needed. The profile knows what this *kind* of world's air
// looks like -- a colour, a scattering character, whether the mist has a direction. The recipe knows
// how much of it this particular world wants. Deriving everything from the weights alone is what
// produced a valley in a glass of milk; taking the profile verbatim would make `atmosphere.fog` a
// dead control.
EnvironmentPlan planEnvironment(const WorldRecipe& recipe, const PaletteRoles& roles,
                                const ArtDirectionProfile& profile) {
    const AtmosphereWeights& air = recipe.atmosphere;
    const LightingWeights& light = recipe.lighting;
    const AtmosphereProfile& a = profile.atmosphere;
    EnvironmentPlan env;

    // Character from the profile; quantity from the recipe. A weight of 0.5 means "as the profile
    // intends"; the ends of the range are half and double that.
    const auto scaled = [](float base, float weight) {
        return base * glm::mix(0.35f, 2.0f, glm::clamp(weight, 0.0f, 1.0f));
    };
    env.fogColor = a.fogColor;
    env.fogDensity = scaled(a.fogDensity, air.fog);
    env.fogHeight = a.fogHeight;
    env.fogHeightFalloff = a.fogHeightFalloff;
    env.skyZenith = a.skyZenith;
    env.skyHorizon = a.skyHorizon;
    env.skyGround = a.skyGround;
    env.haze = glm::mix(a.skyHaze * 0.5f, a.skyHaze * 1.6f, air.depthHaze);
    env.volumeDensity = scaled(a.volumeDensity, light.volumetric);
    env.volumeScattering = a.volumeScattering;
    env.volumeAbsorption = a.volumeAbsorption;
    env.volumeAnisotropy = a.volumeAnisotropy;
    env.volumeNoise = glm::mix(a.volumeNoise * 0.4f, a.volumeNoise * 1.8f, air.spores);
    env.volumeNoiseScale = a.volumeNoiseScale;
    env.volumeNoiseSpeed = 0.02f + 0.05f * air.spores;
    env.styledSkyAmbient = a.styledSkyAmbient;
    env.styledGroundAmbient = a.styledGroundAmbient;
    env.volumeEmission = glm::mix(0.0f, 0.22f, light.bioluminescence * air.spores);
    // The rig, as the profile states it, scaled by how much key the recipe wants. The ratio between
    // key and ambient is the profile's and is not something a weight may quietly flatten.
    env.skyIntensity = glm::mix(profile.lighting.ambientIntensity * 0.3f,
                                profile.lighting.ambientIntensity * 1.4f, light.key);
    env.sunColor = profile.lighting.keyColor;
    env.sunIntensity = glm::mix(profile.lighting.keyIntensity * 0.25f,
                                profile.lighting.keyIntensity * 1.5f, light.key);
    env.keyLight = glm::mix(0.35f, 1.25f, light.key);
    env.sunSize = 0.022f;
    env.sunGlow = 0.55f;
    return env;
}

} // namespace

BiomeSet composerBiomes() {
    // Five biomes on the three axes a biome is defined by: altitude, slope and moisture. The ranges
    // overlap deliberately -- BiomeSet::at blends, so hard borders would produce visible seams in
    // the ground colour and in what grows on it.
    const auto make = [](const char* name, Range altitude, Range slope, Range moisture,
                         glm::vec3 ground, glm::vec3 rock) {
        Biome b;
        b.name = name;
        b.rule.altitude = altitude;
        b.rule.slope = slope;
        b.rule.moisture = moisture;
        b.groundColor = ground;
        b.rockColor = rock;
        return b;
    };
    BiomeSet set;
    set.biomes = {
        make("marsh",  {0.00f, 0.28f}, {0.00f, 0.30f}, {0.55f, 1.00f},
             {0.020f, 0.070f, 0.055f}, {0.070f, 0.085f, 0.090f}),
        make("forest", {0.10f, 0.62f}, {0.00f, 0.45f}, {0.25f, 0.85f},
             {0.023f, 0.090f, 0.060f}, {0.080f, 0.090f, 0.100f}),
        make("meadow", {0.15f, 0.55f}, {0.00f, 0.22f}, {0.10f, 0.55f},
             {0.045f, 0.105f, 0.055f}, {0.090f, 0.095f, 0.105f}),
        make("scree",  {0.35f, 1.00f}, {0.35f, 1.00f}, {0.00f, 0.45f},
             {0.075f, 0.080f, 0.090f}, {0.100f, 0.105f, 0.120f}),
        make("rim",    {0.60f, 1.00f}, {0.00f, 0.55f}, {0.00f, 0.40f},
             {0.060f, 0.070f, 0.085f}, {0.095f, 0.100f, 0.115f}),
    };
    return set;
}

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
    // The art direction, resolved once. An unknown profile name is an error rather than a silent
    // fallback to whichever profile happens to be first.
    auto profile = resolveArtDirection(recipe.art);
    if (!profile) {
        return fail("world '{}': {}", recipe.world, profile.error().message);
    }
    out.profile = *profile;
    const PaletteRoles roles = profile->palette;
    out.environment = planEnvironment(recipe, roles, *profile);

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
        // The landmark itself, sized to be unarguable. A tree that is nine metres tall in the
        // population is forty at the focal point: the point of a landmark is that no amount of
        // ordinary vegetation could be mistaken for it, and "somewhat bigger" reads as an
        // accident of the scatter's scale variation rather than as a decision.
        f.landmarkPath = library.resolve(*hero).generic_string();
        f.landmarkHeight = hero->effectiveHeight() * lerp(3.2f, 5.4f, recipe.composition.focalStrength);
        f.landmarkScale = hero->naturalSize.y > 1e-3f ? f.landmarkHeight / hero->naturalSize.y : 1.0f;
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

    // ---- the viewpoint and the corridor --------------------------------------------------------
    // Chosen here rather than by whoever installs the world, because everything below is arranged
    // relative to it. The distance is set by the landmark: about three times its height frames it
    // whole on a normal lens, and tying the stand-off to the subject rather than to the map keeps
    // the framing when a recipe changes the world's size.
    if (!out.plan.focal.empty()) {
        const FocalRegion& subject = out.plan.focal.front();
        const float landmark = subject.landmarkHeight > 0.0f ? subject.landmarkHeight
                                                             : recipe.extent * 0.06f;
        const float back = std::max(landmark * 3.0f, 45.0f);
        // Approached off-axis so the composition is not symmetrical about the frame's centre.
        const float bearing = 6.2831853f * (hash01(recipe.seed, 61u) * 0.25f + 0.1f);
        out.plan.viewpoint = subject.center + glm::vec2(std::cos(bearing), std::sin(bearing)) * back;

        // The lane. Wide enough at the near end that nothing is in the lens and the eye has
        // somewhere to enter the frame; narrowing toward the subject so the subject keeps the
        // material around it that gives it scale. Cleared to just short of the focal radius: a
        // corridor that runs all the way in would strip the ground the landmark stands on.
        // Voids are chosen before the viewpoint exists, so nothing has yet stopped one from landing
        // on top of it. A viewpoint standing in the middle of a fifty-metre empty region renders as
        // a bald hillside: the world is dense everywhere except the one place it is seen from. The
        // corridor is the only emptiness the viewpoint is entitled to.
        const std::size_t before = out.plan.voids.size();
        std::erase_if(out.plan.voids, [&](const VoidRegion& v) {
            return glm::length(v.center - out.plan.viewpoint) < v.radius + v.softness;
        });
        if (out.plan.voids.size() != before) {
            log::debug("world '{}': dropped {} void region(s) that contained the viewpoint",
                       recipe.world, before - out.plan.voids.size());
        }

        const glm::vec2 toSubject = subject.center - out.plan.viewpoint;
        const float length = glm::length(toSubject);
        if (length > 1e-3f) {
            const glm::vec2 dir = toSubject / length;
            // Stops at the focal region's rim rather than inside it. A corridor that runs all the
            // way in strips the ground the landmark stands on, and the landmark loses the material
            // around it that gives it a sense of scale.
            const float reach = std::max(length - subject.radius * 1.15f, 0.0f);
            // Width scales with the recipe's appetite for empty space, but never to nothing: the
            // corridor is mandatory, so `negativeSpace` sets how generous it is and not whether it
            // exists. A world that asks for no negative space still gets a clear line of sight.
            const float nearWidth = std::max(landmark * (0.10f + 0.16f * recipe.composition.negativeSpace), 7.0f);
            const float farWidth = nearWidth * 0.35f;
            const int steps = std::max(static_cast<int>(reach / std::max(nearWidth * 0.7f, 1.0f)), 3);
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                VoidRegion v;
                v.center = out.plan.viewpoint + dir * (reach * t);
                v.radius = lerp(nearWidth, farWidth, t);
                v.softness = v.radius * 0.6f;
                // Only the canopy. The lane exists so nothing large stands between the viewpoint
                // and the subject; the ground cover in it is what gives the lane a floor.
                v.clearsAbove = 1.2f;
                out.plan.corridor.push_back(v);
            }
            out.plan.viewpointClearance = nearWidth;
            out.plan.voids.insert(out.plan.voids.end(), out.plan.corridor.begin(),
                                  out.plan.corridor.end());
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
        // The palette is applied here rather than being left to the assets, which is what section
        // 16 means by a controlled palette: the library is one pack authored in one set of colours,
        // and a world is a decision about colour laid over it. Living things are pulled toward the
        // foliage colour; everything is pulled toward the colour of the air by its distance.
        glm::vec3 tint = asset.material.tint;
        const bool living = asset.category == assets::AssetCategory::Flora ||
                            asset.category == assets::AssetCategory::Fungi ||
                            asset.category == assets::AssetCategory::Organic;
        if (living) {
            tint = tintTowards(tint, roles.foliage, 0.45f * recipe.art.saturation);
        }
        layer.tint = tintTowards(tint, roles.shadow,
                                 shadowPullFor(band) * recipe.atmosphere.depthHaze);
        layer.clusterScale = tune.clusterScale;
        layer.clustering = tune.clustering;
        layer.viewDistance = tune.viewDistance;
        layer.minScreenRadius = tune.minScreenRadius;
        layer.castsShadow = tune.castsShadow && asset.visualImportance > 0.25f;
        layer.maxInstances = tune.maxInstances;
        layer.meshBudget = asset.triangles > 0 ? std::max(asset.triangles / 2, 24) : 0;

        // Emission: the section 15 ladder. The asset's own emissive weight says whether this kind
        // of thing glows at all; the tier says where it sits in the hierarchy; the recipe says how
        // much of this world's light comes from the world. The colour is the palette's, by role,
        // not the asset's -- a library authored in reds does not get to decide that a world lit in
        // cyan has red lights in it.
        if (asset.material.emissive > 0.0f && recipe.lighting.bioluminescence > 0.0f) {
            const EmissionTier tier = emissionTierFor(asset, band, profile->emission);
            // The accent is reserved. A world in which several species wear the hero's colour is a
            // world with no hero colour, and it is the cheapest of all the art-direction rules to
            // lose by accident -- so a scatter layer may only reach the accent role when the
            // profile has released it.
            int role = tier.paletteRole;
            if (role == 4 && profile->reserveAccent) {
                role = 2;
            }
            layer.emissiveColor = roleColor(roles, role);
            // Absolute, from the ladder, scaled by how much of this world's light comes from the
            // world itself and by how strongly the species emits at all. The asset's weight can
            // dim a rung but never promote a species past one.
            layer.emissiveIntensity = tier.intensity *
                                      std::clamp(asset.material.emissive, 0.0f, 1.0f) *
                                      recipe.lighting.bioluminescence;
            layer.emissiveSparsity = std::clamp(tier.sparsity, 0.0f, 0.95f);
            // The hue of a glowing population drifts across the map and over time rather than
            // being one colour repeated, which is what keeps a field of lights reading as biology.
            layer.hueField = 0.06f + 0.10f * recipe.art.chaos;
            layer.hueFieldScale = std::max(recipe.extent * 0.14f, 12.0f);
            layer.chromaDrift = 0.35f * recipe.art.organicMotion;
            layer.chromaDriftScale = std::max(recipe.extent * 0.16f, 18.0f);
            layer.chromaDriftSpeed = 0.02f + 0.05f * recipe.art.organicMotion;
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

    // Emit in band order: background, then midground, then foreground. The ecology requires a
    // layer's proximity host to be placed *before* it, and a host is always in a taller band than
    // its dweller, so ordering by band satisfies that by construction rather than by a topological
    // sort that could still fail. It is also the right order on its own terms -- the big things
    // decide where they are and the small things grow around them, which is what the relation
    // means. Emitting in library order instead produced "proximity layer 'tree_tall' must precede
    // it" the first time a real manifest was composed.
    const auto bandRank = [](DepthBand b) {
        switch (b) {
        case DepthBand::Background:
            return 0;
        case DepthBand::Midground:
            return 1;
        case DepthBand::Foreground:
            return 2;
        }
        return 1;
    };
    std::vector<std::size_t> order(out.layers.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    // Stable, so two layers in the same band keep their manifest order and the world stays a pure
    // function of its inputs.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return bandRank(out.plan.bandOf(out.layers[a].name)) <
               bandRank(out.plan.bandOf(out.layers[b].name));
    });
    std::vector<ScatterLayer> sorted;
    sorted.reserve(out.layers.size());
    for (const std::size_t i : order) {
        sorted.push_back(std::move(out.layers[i]));
    }
    out.layers = std::move(sorted);

    // The negative space, handed to the placer. `plan.voids` already carries both the scattered
    // empty regions and the corridor; this is the same set in the type the ecology consumes.
    out.clearances.reserve(out.plan.voids.size());
    for (const VoidRegion& v : out.plan.voids) {
        ScatterClearance c;
        c.center = v.center;
        c.radius = v.radius;
        c.softness = v.softness;
        c.strength = 1.0f;
        c.minHeight = v.clearsAbove;
        out.clearances.push_back(c);
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
