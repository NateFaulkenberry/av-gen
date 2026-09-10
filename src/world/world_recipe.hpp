#pragma once

// World recipes (ADR-060, milestone 1 of the cinematic upgrade).
//
// A recipe is a world's artistic intent with no objects in it. It says a world should be dense in
// the foreground, sparse in the background, mostly flora with some fungi, lit almost entirely by
// its own bioluminescence, and left a third empty. It does not say where the ferns go: that is the
// composer's job, and the point of separating them is that the same recipe can be composed against
// different asset libraries and different terrain and still read as the same world.
//
// Every weight is 0..1 and every one has a default, so a recipe naming three fields is legal and
// means "and the rest as usual". That matters more than it sounds: a recipe is meant to be edited
// by someone deciding how a world should feel, and a format that demands forty numbers before it
// will load is a format nobody edits.
//
// This deliberately overlaps `app::WorldDirector` (ADR-041) rather than replacing it. The director
// is eighteen words that move live parameters on an existing world; a recipe is what a world is
// composed from before it exists. They meet at `ArtDirection` below, which is the vocabulary both
// can speak.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

// Where the world puts its material. `negativeSpace` is not the absence of the others -- it is a
// positive instruction to leave regions empty, which is the difference between a composition and a
// uniform scatter, and the one number most likely to be left out by mistake.
struct CompositionWeights {
    float foreground = 0.5f;
    float midground = 0.5f;
    float background = 0.5f;
    float negativeSpace = 0.3f;
    float focalStrength = 0.5f;   // how much the focal region outranks everything around it
};

// What lives here. A weight of 0 means the category is absent, not merely rare.
struct EcologyWeights {
    float flora = 0.6f;
    float fungi = 0.2f;
    float rock = 0.3f;
    float crystal = 0.0f;
    float creature = 0.0f;
    float structure = 0.0f;
};

// The air, and what is suspended in it.
struct AtmosphereWeights {
    float fog = 0.35f;
    float spores = 0.25f;      // drifting particulate at human scale
    float floating = 0.1f;     // larger suspended elements
    float depthHaze = 0.5f;    // how strongly distance desaturates
};

// Where the light comes from. These are proportions of the world's illumination, not intensities:
// a world lit 1.0 by bioluminescence and 0.1 by a moon is a different world from the reverse even
// if both end up at the same exposure.
struct LightingWeights {
    float key = 0.5f;             // a moon, a sun, a distant source
    float bioluminescence = 0.3f; // the world lighting itself
    float volumetric = 0.4f;
    float bounce = 0.2f;
};

// The vocabulary a recipe and the World Director share. Palette entries are colour names resolved
// by the art-direction profile, kept as strings so a recipe stays legible and so two worlds can
// name the same colour without agreeing on its exact value.
struct ArtDirection {
    std::string name;
    // A named art-direction profile (ADR-070): "glowmere", "emberwaste", "palefen". Empty means the
    // recipe supplies its own look. A profile is a starting point rather than a cage -- anything the
    // recipe states explicitly wins over what the profile provides.
    std::string profile;
    std::vector<std::string> palette;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float organicMotion = 0.7f;   // how much motion reads as growth rather than machinery
    float chaos = 0.2f;
};

// ---- the palette ------------------------------------------------------------------------------
//
// A palette is written as names because a recipe is meant to be read and edited by a person, and
// because two worlds can name the same colour without agreeing on its exact value. An unrecognised
// name resolves to nothing rather than to grey: a typo that silently becomes a neutral is a typo
// nobody finds. `#rrggbb` is accepted too, for the world that needs a colour this vocabulary has
// no word for.
[[nodiscard]] std::optional<glm::vec3> paletteColor(std::string_view name);

// A palette is read *by position*, darkest first. That is the one convention this file imposes and
// it is what makes a five-word list a usable art direction rather than five colours in a bag: the
// composer has to know which member is the air and which is the rare bright thing, and asking a
// recipe to spell out five roles by name would be a worse format than asking it to put them in
// order. Short palettes fill the remaining roles by falling back inwards rather than to grey.
struct PaletteRoles {
    glm::vec3 shadow{0.06f, 0.07f, 0.13f};    // the air, the distance, ground out of the light
    glm::vec3 secondary{0.32f, 0.18f, 0.62f}; // the second light
    glm::vec3 primary{0.14f, 0.62f, 0.72f};   // most of what the world's own light is
    glm::vec3 foliage{0.16f, 0.48f, 0.34f};   // what living matter is made of
    glm::vec3 accent{0.95f, 0.58f, 0.24f};    // the rare one, used on almost nothing
};
[[nodiscard]] PaletteRoles paletteRoles(const ArtDirection& art);

// A palette colour used as a multiplier: renormalised so its largest component is 1, then mixed
// from white by `amount`. A tint multiplies an asset's own base colour, so feeding a dark saturated
// colour in directly does not tint the asset -- it turns the lights off.
[[nodiscard]] glm::vec3 tintTowards(glm::vec3 base, glm::vec3 target, float amount);

struct WorldRecipe {
    std::string world;                 // the world's name; required
    std::string description;
    std::uint32_t seed = 1;            // the whole composition is a function of this
    float extent = 400.0f;             // metres across, so weights can mean densities

    CompositionWeights composition;
    EcologyWeights ecology;
    AtmosphereWeights atmosphere;
    LightingWeights lighting;
    ArtDirection art;

    // The asset library this world is composed from, as written; resolved against the recipe's
    // own directory. Empty means "whatever library the caller supplies".
    std::filesystem::path assetLibrary;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<WorldRecipe> fromJson(const nlohmann::json& j);
    [[nodiscard]] static Result<WorldRecipe> loadFile(const std::filesystem::path& path);
    // Every weight, by the name it is written under. Lets the composer and the UI iterate a recipe
    // without a switch over every field, and keeps the JSON and the struct from drifting apart.
    [[nodiscard]] std::vector<std::pair<std::string, float>> weights() const;
};

} // namespace avgen::world
