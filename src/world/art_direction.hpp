#pragma once

// Art-direction profiles (ADR-070).
//
// A recipe says how much of a world there is. A profile says what it *looks* like. They are
// separate because the same recipe -- dense foreground, sparse ridge, a third empty -- should be
// able to produce a bioluminescent night valley or a bleached ash plain, and because the second
// question has a good answer already written down for at least one world.
//
// The Glowmere profile is that answer, read out of the painterly scene rather than invented (see
// docs/glowmere-world-builder-audit.md). Two of its decisions carry most of the look and both are
// encoded here as data rather than as prose:
//
//   * an emission ladder spanning about 200:1 in which most biomass emits essentially nothing, and
//     which has a deliberate *gap* rather than being a smooth ramp -- the gap is what makes bright
//     things read as a different kind of thing instead of the top of a gradient;
//   * one accent colour, warm, that is reserved for the hero and appears nowhere else, so the eye
//     finds the hero from anywhere in frame.
//
// Glowmere is one profile and must never become the only one. Every profile below is a peer, a
// recipe may name any of them, and a recipe that writes its own palette overrules whichever it
// named. If this file ever contains one entry, something has gone wrong with the argument for
// having it at all.

#include "core/error.hpp"
#include "scene/light_rig.hpp"
#include "world/world_recipe.hpp"

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// The rungs of a luminance hierarchy, in the engine's emissive-intensity units.
//
// These are absolute rather than fractions of a maximum on purpose: the ratio *between* rungs is
// the art direction, and normalising them would let a later "brightness" control quietly flatten
// the ladder into the ramp it was written to avoid.
struct EmissionLadder {
    float inert = 0.0f;          // emits nothing at all. Several layers should be here.
    float silhouette = 0.035f;   // the canopy. Fills the frame, so it must not glow.
    float groundCover = 0.0615f; // grass, ferns, fronds: present, not luminous
    float noticeable = 0.295f;   // the brightest ordinary vegetation
    float special = 3.94f;       // the first rung across the gap
    float rare = 4.43f;
    float beacon = 6.15f;
    float brightest = 6.89f;
    // How far `special` must sit above `noticeable` for the hierarchy to read as two kinds of
    // thing. Checked rather than assumed, because a profile edited toward "a bit more glow
    // everywhere" closes this gap without anybody noticing they have removed the effect.
    [[nodiscard]] float gap() const { return noticeable > 0.0f ? special / noticeable : 0.0f; }
    [[nodiscard]] Result<void> validate() const;
};

// The air. Field for field, what the scene's environment already takes.
struct AtmosphereProfile {
    glm::vec3 fogColor{0.10f, 0.18f, 0.32f};
    float fogHeight = 4.0f;
    float fogHeightFalloff = 0.10f;
    glm::vec3 skyZenith{0.008f, 0.016f, 0.048f};
    glm::vec3 skyHorizon{0.04f, 0.08f, 0.17f};
    glm::vec3 skyGround{0.0012f, 0.0021f, 0.0058f};
    float skyHaze = 0.24f;
    float volumeDensity = 0.006f;
    float volumeScattering = 0.5f;
    float volumeAbsorption = 0.4f;
    // Nearly isotropic. Pushing this toward 1 turns the mist into god rays, which is a different
    // and much more familiar look.
    float volumeAnisotropy = 0.12f;
    float volumeNoise = 0.45f;
    float volumeNoiseScale = 0.02f;
    int volumeSteps = 12;
    float volumeMaxDistance = 320.0f;
    // The painterly hemisphere. These two decide how dark a stylized world is, and the engine's
    // defaults are roughly four times Glowmere's -- so a profile that omits them gets a washed-out
    // world whatever else it says.
    glm::vec3 styledSkyAmbient{0.10f, 0.16f, 0.21f};
    glm::vec3 styledGroundAmbient{0.008f, 0.011f, 0.024f};
};

// The rig, as ratios rather than as lights. What matters is that the key rakes and the ambient
// stays out of its way; where exactly the moon sits is a scene's business.
struct LightingProfile {
    float keyIntensity = 4.5f;
    float ambientIntensity = 0.6f;   // 7.5:1 against the key. Raising this to "see more" is fatal.
    glm::vec3 ambientColor{0.38f, 0.50f, 0.64f};
    glm::vec3 keyColor{0.42f, 0.62f, 1.0f};
    float keyElevationDegrees = 22.0f;   // low, so form comes from the rake
    float keyAzimuthDegrees = 64.0f;
    float fillIntensity = 0.35f;
    glm::vec3 fillColor{0.38f, 0.58f, 0.80f};
    [[nodiscard]] float keyToAmbient() const {
        return ambientIntensity > 0.0f ? keyIntensity / ambientIntensity : 0.0f;
    }
};

// What the image does after it is lit. Bloom restraint belongs to the art direction because it is
// what decides whether the ladder's top rungs read as luminous or as smeared.
struct PostProfile {
    int tonemap = 1;               // AgX
    float chromaRetention = 0.6f;
    bool bloomEnabled = true;
    float bloomIntensity = 0.18f;
    float bloomThreshold = 1.0f;   // only the ladder's top rungs cross it
    float bloomKnee = 0.5f;
    float bloomRadius = 1.15f;
    int bloomLevels = 6;
    float bloomEmissionWeight = 0.75f;
    float antialias = 0.75f;
};

// A complete visual language.
struct ArtDirectionProfile {
    std::string name;
    std::string description;
    PaletteRoles palette;
    // The reserved colour. Used by the hero and by nothing else when `reserveAccent` is set, which
    // is the whole mechanism: one warm light in a cool world is found by the eye from anywhere.
    glm::vec3 heroAccent{1.0f, 0.47f, 0.15f};
    bool reserveAccent = true;
    EmissionLadder emission;
    AtmosphereProfile atmosphere;
    LightingProfile lighting;
    PostProfile post;
    // Whether this world's surfaces use the painterly shading mode.
    bool stylized = true;
    // Faint luminous mottling on the ground itself.
    float groundGlow = 0.08f;
    glm::vec3 groundGlowColor{0.08f, 1.0f, 0.68f};
    float groundGlowCoverage = 0.30f;

    [[nodiscard]] Result<void> validate() const;
};

// The rig a profile describes, as a `scene::LightRig` ready to install.
//
// Expressed as a rig rather than as loose parameters because the ratio between the key and the
// ambient *is* the art direction -- Glowmere's 7.5:1 is what makes it a night -- and a rig is the
// one structure in this engine that keeps a key and an ambient together where they can be read
// against each other.
[[nodiscard]] scene::LightRig rigFor(const ArtDirectionProfile& profile);

// The built-in profiles, in a stable order. Every one is a peer; none is a default the others are
// variations of.
[[nodiscard]] const std::vector<ArtDirectionProfile>& artProfiles();
[[nodiscard]] const ArtDirectionProfile* findArtProfile(std::string_view name);
[[nodiscard]] std::vector<std::string> artProfileNames();

// The profile a recipe asks for, with the recipe's own overrides applied on top.
//
// Precedence, and the reason for it: a named profile supplies everything, then anything the recipe
// states explicitly wins. A recipe that writes its own palette gets its own palette -- naming a
// profile is a starting point, not a cage. An unknown name is an error rather than a silent
// fallback, because a typo that quietly returns Glowmere is a typo that ships.
[[nodiscard]] Result<ArtDirectionProfile> resolveArtDirection(const ArtDirection& art);

} // namespace avgen::world
