#pragma once

// The world composer (ADR-061, milestone 2 of the cinematic upgrade).
//
// A recipe says a world should be dense underfoot, sparse on the ridge, mostly flora, a third
// empty. A library says which assets exist and what each is for. The composer turns the two into
// `world::ScatterLayer`s -- the type the ecology already places -- plus a plan describing the
// composition it intended.
//
// It emits the existing type on purpose. `world::Ecology` already knows about biome densities,
// slope and altitude limits, water avoidance, clustering, proximity between layers, view distance,
// screen-radius culling and instance ceilings. A second placer beside it would fork all of that,
// and the fork would be the copy without the water-avoidance fixes. So the composer's whole output
// is layers the existing system consumes, and it can be dropped into any scene that already has a
// terrain node.
//
// What it adds that ecology has no opinion about is *hierarchy*: which assets belong at the
// viewer's feet, which belong on a ridge four hundred metres away, which single thing the shot is
// about, and where nothing at all should grow.

#include "assets/asset_library.hpp"
#include "core/error.hpp"
#include "world/ecology.hpp"
#include "world/world_recipe.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace avgen::world {

// The three distances a composition is built from, plus the one region that outranks them.
enum class DepthBand : std::uint8_t { Foreground, Midground, Background };
[[nodiscard]] const char* depthBandName(DepthBand band);

// Where the composition wants nothing, or wants one thing. Both are positive instructions: a
// composer that only ever adds material produces a uniform scatter, which is the failure this
// exists to avoid.
struct FocalRegion {
    glm::vec2 center{0.0f};   // world XZ
    float radius = 20.0f;
    float strength = 1.0f;    // how far this outranks the ordinary hierarchy
    std::string assetId;      // what occupies it, resolved from the library
};

struct VoidRegion {
    glm::vec2 center{0.0f};
    float radius = 30.0f;
    float softness = 12.0f;   // metres over which density returns to normal
};

// What the composer decided, kept beside the layers so a caller can explain the result, draw it in
// a debug view, or test it without rendering anything.
struct CompositionPlan {
    std::vector<FocalRegion> focal;
    std::vector<VoidRegion> voids;
    // Layer name -> the band it was assigned to, so a test can assert that a fern did not end up
    // on the ridge line.
    std::vector<std::pair<std::string, DepthBand>> bands;
    float coveredFraction = 0.0f;   // of `extent`, before void regions are subtracted
    float emptyFraction = 0.0f;     // what the voids actually take back
    [[nodiscard]] DepthBand bandOf(const std::string& layer) const;
};

struct ComposedWorld {
    std::vector<ScatterLayer> layers;
    CompositionPlan plan;
};

// Composition is a pure function of (recipe, library). Same inputs, same world, every time --
// which is what lets a composed scene be a deterministic render target like any other.
[[nodiscard]] Result<ComposedWorld> composeWorld(const WorldRecipe& recipe,
                                                 const assets::AssetLibrary& library);

// Which band an asset belongs to, given its height and tags. Exposed because it is the single
// judgement call in the composer and it deserves to be tested directly rather than inferred from
// the layers that come out.
[[nodiscard]] DepthBand bandForAsset(const assets::AssetDescriptor& asset);

} // namespace avgen::world
