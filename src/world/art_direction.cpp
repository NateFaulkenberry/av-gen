#include "world/art_direction.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

// The minimum ratio between the brightest ordinary vegetation and the first bright rung. Glowmere's
// is 13.3x. Below about four the two stop reading as different kinds of thing and the ladder is a
// ramp -- which is the failure this whole structure exists to prevent, so it is checked.
constexpr float kMinimumLadderGap = 4.0f;

ArtDirectionProfile glowmere() {
    ArtDirectionProfile p;
    p.name = "glowmere";
    p.description = "A cool alien night valley lit by its own flora, with one warm hero.";
    // Read out of examples/world/glowmere-stylized.scene.json rather than invented.
    p.palette.shadow = glm::vec3(0.008f, 0.016f, 0.048f);     // the night sky's zenith
    p.palette.secondary = glm::vec3(0.3085f, 0.1208f, 1.0f);  // the flowers' violet
    p.palette.primary = glm::vec3(0.06f, 0.82f, 1.0f);        // the beacons' cyan
    p.palette.foliage = glm::vec3(0.02f, 1.0f, 0.58f);        // the ferns' green-cyan
    p.palette.accent = glm::vec3(1.0f, 0.47f, 0.15f);         // the elder's filaments
    p.heroAccent = glm::vec3(1.0f, 0.47f, 0.15f);
    p.reserveAccent = true;
    // The ladder, verbatim from the scene's thirteen scatter layers.
    p.emission = EmissionLadder{};
    p.atmosphere = AtmosphereProfile{};
    p.lighting = LightingProfile{};
    p.post = PostProfile{};
    p.stylized = true;
    return p;
}

// A second profile that shares none of Glowmere's assumptions, so the vocabulary is exercised
// rather than merely parameterised around one world. A world lit by an actual sun, where the bright
// things are bright because they are *hot*, and where almost nothing emits at all.
ArtDirectionProfile emberwaste() {
    ArtDirectionProfile p;
    p.name = "emberwaste";
    p.description = "A hot, dry, high-contrast plain under a low sun. Ash, rust and a hard sky.";
    p.palette.shadow = glm::vec3(0.055f, 0.042f, 0.038f);
    p.palette.secondary = glm::vec3(0.56f, 0.18f, 0.07f);
    p.palette.primary = glm::vec3(0.90f, 0.56f, 0.18f);
    p.palette.foliage = glm::vec3(0.30f, 0.26f, 0.16f);
    p.palette.accent = glm::vec3(0.25f, 0.70f, 0.95f);   // cool, because the world is warm
    p.heroAccent = glm::vec3(0.25f, 0.70f, 0.95f);
    p.reserveAccent = true;
    // Almost nothing glows: this world's light comes from its sky. The ladder still has its gap,
    // because the gap is the grammar rather than a Glowmere-specific number.
    p.emission.inert = 0.0f;
    p.emission.silhouette = 0.0f;
    p.emission.groundCover = 0.0f;
    p.emission.noticeable = 0.05f;
    p.emission.special = 0.9f;
    p.emission.rare = 1.6f;
    p.emission.beacon = 2.4f;
    p.emission.brightest = 3.2f;
    p.atmosphere.fogColor = glm::vec3(0.32f, 0.24f, 0.18f);
    p.atmosphere.fogHeight = 9.0f;
    p.atmosphere.fogHeightFalloff = 0.06f;
    p.atmosphere.skyZenith = glm::vec3(0.10f, 0.13f, 0.22f);
    p.atmosphere.skyHorizon = glm::vec3(0.62f, 0.38f, 0.20f);
    p.atmosphere.skyGround = glm::vec3(0.10f, 0.075f, 0.055f);
    p.atmosphere.skyHaze = 0.55f;
    p.atmosphere.volumeDensity = 0.011f;
    p.atmosphere.volumeAnisotropy = 0.62f;   // a real sun, so the air does have a direction
    p.atmosphere.volumeNoise = 0.8f;
    p.atmosphere.styledSkyAmbient = glm::vec3(0.42f, 0.36f, 0.30f);
    p.atmosphere.styledGroundAmbient = glm::vec3(0.14f, 0.10f, 0.07f);
    p.lighting.keyIntensity = 9.0f;
    p.lighting.ambientIntensity = 1.1f;
    p.lighting.ambientColor = glm::vec3(0.55f, 0.44f, 0.36f);
    p.lighting.keyColor = glm::vec3(1.0f, 0.78f, 0.52f);
    p.lighting.keyElevationDegrees = 9.0f;
    p.lighting.fillIntensity = 0.20f;
    p.lighting.fillColor = glm::vec3(0.42f, 0.46f, 0.58f);
    p.post.bloomIntensity = 0.10f;
    p.post.bloomThreshold = 1.6f;
    p.post.chromaRetention = 0.35f;
    p.stylized = true;
    p.groundGlow = 0.0f;
    p.groundGlowCoverage = 0.0f;
    return p;
}

// A third, to make the point that the profile is not a two-way switch: overcast, desaturated, very
// low contrast, nothing luminous. The hardest kind of world to make interesting, and a useful
// counter-example precisely because none of Glowmere's tricks apply to it.
ArtDirectionProfile palefen() {
    ArtDirectionProfile p;
    p.name = "palefen";
    p.description = "Flat overcast light over standing water. Desaturated, quiet, almost no glow.";
    p.palette.shadow = glm::vec3(0.085f, 0.095f, 0.105f);
    p.palette.secondary = glm::vec3(0.26f, 0.32f, 0.34f);
    p.palette.primary = glm::vec3(0.46f, 0.52f, 0.53f);
    p.palette.foliage = glm::vec3(0.24f, 0.30f, 0.24f);
    p.palette.accent = glm::vec3(0.78f, 0.36f, 0.42f);
    p.heroAccent = glm::vec3(0.78f, 0.36f, 0.42f);
    p.reserveAccent = true;
    p.emission.inert = 0.0f;
    p.emission.silhouette = 0.0f;
    p.emission.groundCover = 0.0f;
    p.emission.noticeable = 0.02f;
    p.emission.special = 0.35f;
    p.emission.rare = 0.6f;
    p.emission.beacon = 0.9f;
    p.emission.brightest = 1.2f;
    p.atmosphere.fogColor = glm::vec3(0.42f, 0.45f, 0.47f);
    p.atmosphere.fogHeight = 3.0f;
    p.atmosphere.fogHeightFalloff = 0.16f;
    p.atmosphere.skyZenith = glm::vec3(0.30f, 0.33f, 0.36f);
    p.atmosphere.skyHorizon = glm::vec3(0.50f, 0.52f, 0.54f);
    p.atmosphere.skyGround = glm::vec3(0.16f, 0.17f, 0.17f);
    p.atmosphere.skyHaze = 0.85f;
    p.atmosphere.volumeDensity = 0.014f;
    p.atmosphere.volumeAnisotropy = 0.05f;
    p.atmosphere.volumeNoise = 0.25f;
    p.atmosphere.styledSkyAmbient = glm::vec3(0.52f, 0.55f, 0.58f);
    p.atmosphere.styledGroundAmbient = glm::vec3(0.20f, 0.21f, 0.20f);
    p.lighting.keyIntensity = 1.6f;
    p.lighting.ambientIntensity = 1.3f;   // overcast: the sky *is* the light
    p.lighting.ambientColor = glm::vec3(0.52f, 0.55f, 0.58f);
    p.lighting.keyColor = glm::vec3(0.80f, 0.83f, 0.86f);
    p.lighting.keyElevationDegrees = 46.0f;
    p.lighting.fillIntensity = 0.5f;
    p.post.bloomIntensity = 0.06f;
    p.post.bloomThreshold = 1.2f;
    p.post.chromaRetention = 0.25f;
    p.stylized = true;
    p.groundGlow = 0.0f;
    p.groundGlowCoverage = 0.0f;
    return p;
}
} // namespace

Result<void> EmissionLadder::validate() const {
    const float rungs[] = {inert, silhouette, groundCover, noticeable, special, rare, beacon, brightest};
    for (const float r : rungs) {
        if (!(r >= 0.0f) || !std::isfinite(r)) {
            return fail("emission ladder: every rung must be finite and not negative");
        }
    }
    for (std::size_t i = 1; i < std::size(rungs); ++i) {
        if (rungs[i] < rungs[i - 1]) {
            return fail("emission ladder: rung {} ({}) is dimmer than the one below it ({})", i,
                        rungs[i], rungs[i - 1]);
        }
    }
    // The gap, checked rather than assumed. A ladder edited toward "a bit more glow everywhere"
    // closes it without anyone noticing that the hierarchy has become a gradient.
    if (noticeable > 0.0f && gap() < kMinimumLadderGap) {
        return fail("emission ladder: the step from ordinary vegetation ({}) to the first bright "
                    "rung ({}) is only {:.1f}x; below {:.0f}x they stop reading as different kinds "
                    "of thing and the hierarchy becomes a ramp",
                    noticeable, special, gap(), kMinimumLadderGap);
    }
    return {};
}

Result<void> ArtDirectionProfile::validate() const {
    if (name.empty()) {
        return fail("an art-direction profile needs a name");
    }
    if (auto ok = emission.validate(); !ok) {
        return fail("art profile '{}': {}", name, ok.error().message);
    }
    if (!(lighting.ambientIntensity >= 0.0f) || !(lighting.keyIntensity >= 0.0f)) {
        return fail("art profile '{}': light intensities must not be negative", name);
    }
    if (!(post.bloomThreshold > 0.0f)) {
        return fail("art profile '{}': a bloom threshold of zero blooms everything, which is the "
                    "opposite of selective", name);
    }
    if (atmosphere.volumeSteps < 1) {
        return fail("art profile '{}': volumeSteps must be at least 1", name);
    }
    return {};
}

scene::LightRig rigFor(const ArtDirectionProfile& profile) {
    scene::LightRig rig;
    rig.name = profile.name;
    rig.description = profile.description;
    rig.keyIntensity = profile.lighting.keyIntensity;
    rig.ambientIntensity = profile.lighting.ambientIntensity;
    rig.ambientColor = profile.lighting.ambientColor;
    rig.ambientTemperature = 8200.0f;

    scene::RigLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.role = scene::PunctualLight::Role::Key;
    key.azimuthDegrees = profile.lighting.keyAzimuthDegrees;
    key.elevationDegrees = profile.lighting.keyElevationDegrees;
    key.intensity = 1.0f;   // relative to the rig's keyIntensity
    key.color = profile.lighting.keyColor;
    key.temperature = 7800.0f;
    key.castsShadow = true;
    key.shadowStrength = 0.85f;
    key.softness = 1.0f;
    key.volumetricStrength = 0.12f;
    // Fixed to the world, not to the camera. A key that follows the camera cannot rake across a
    // landscape -- every shot gets the same relationship to the light, which is exactly the flat
    // look the low elevation is there to avoid.
    key.followCamera = false;
    rig.lights.push_back(key);

    scene::RigLight fill;
    fill.name = "fill";
    fill.type = scene::PunctualLight::Type::Directional;
    fill.role = scene::PunctualLight::Role::Fill;
    fill.azimuthDegrees = profile.lighting.keyAzimuthDegrees + 172.0f;
    fill.elevationDegrees = profile.lighting.keyElevationDegrees + 12.0f;
    fill.intensity = profile.lighting.keyIntensity > 0.0f
                         ? profile.lighting.fillIntensity / profile.lighting.keyIntensity
                         : 0.0f;
    fill.color = profile.lighting.fillColor;
    fill.temperature = 9500.0f;
    fill.castsShadow = false;
    fill.followCamera = false;
    rig.lights.push_back(fill);
    return rig;
}

const std::vector<ArtDirectionProfile>& artProfiles() {
    static const std::vector<ArtDirectionProfile> profiles = {glowmere(), emberwaste(), palefen()};
    return profiles;
}

const ArtDirectionProfile* findArtProfile(std::string_view name) {
    for (const auto& p : artProfiles()) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<std::string> artProfileNames() {
    std::vector<std::string> names;
    names.reserve(artProfiles().size());
    for (const auto& p : artProfiles()) {
        names.push_back(p.name);
    }
    return names;
}

Result<ArtDirectionProfile> resolveArtDirection(const ArtDirection& art) {
    ArtDirectionProfile out;
    if (!art.profile.empty()) {
        const ArtDirectionProfile* named = findArtProfile(art.profile);
        if (named == nullptr) {
            // Not a fallback. A typo that quietly returns Glowmere is a typo that ships, and the
            // whole point of profiles is that a world can be something other than Glowmere.
            std::string known;
            for (const auto& n : artProfileNames()) {
                known += known.empty() ? n : ", " + n;
            }
            return fail("unknown art profile '{}'; known profiles are {}", art.profile, known);
        }
        out = *named;
    } else {
        out.name = art.name.empty() ? std::string("custom") : art.name;
    }

    // The recipe's own words win over the profile it named. Naming a profile is a starting point.
    if (!art.name.empty()) {
        out.name = art.name;
    }
    if (!art.palette.empty()) {
        out.palette = paletteRoles(art);
        // A recipe that states its own palette states its own accent with it, or the reserved
        // colour would still be the profile's and would no longer be absent from the world.
        out.heroAccent = out.palette.accent;
    }
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

} // namespace avgen::world
