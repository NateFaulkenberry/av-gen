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
#include <glm/glm.hpp>
#include <string>
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
    std::vector<std::string> palette;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float organicMotion = 0.7f;   // how much motion reads as growth rather than machinery
    float chaos = 0.2f;
};

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
