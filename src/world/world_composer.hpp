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
#include "world/biome.hpp"
#include "world/ecology.hpp"
#include "world/art_direction.hpp"
#include "world/hero.hpp"
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
    // The landmark itself. A focal region that only marks a spot is a note about a composition
    // rather than a composition: the first generated valley had one, and nothing stood in it.
    // `landmarkPath` is resolved against the library so a caller can place the thing without
    // holding the library, and `landmarkHeight` is metres -- deliberately enormous, because the
    // whole job of a landmark is to be unmistakably larger than the population it rises out of.
    std::string landmarkPath;
    float landmarkHeight = 0.0f;   // metres it should stand
    // Uniform scale to apply to the mesh to reach that height. Computed here because only the
    // composer holds both the intended height and the asset's own bounds; a caller that placed the
    // file and guessed would get a forty-metre tree or a two-metre one depending on the pack.
    float landmarkScale = 1.0f;
};

struct VoidRegion {
    glm::vec2 center{0.0f};
    float radius = 30.0f;
    float softness = 12.0f;   // metres over which density returns to normal
    // Only layers at least this tall are removed; 0 empties the region completely. A corridor uses
    // it to take out the canopy and leave the ground growing.
    float clearsAbove = 0.0f;
};

// What the composer decided, kept beside the layers so a caller can explain the result, draw it in
// a debug view, or test it without rendering anything.
//
// Heroes live here rather than in the layers because they are the one part of a world that is not a
// population: each is a single object in a single place with its own importance, and a scatter
// layer is by definition a set of interchangeable instances.
struct CompositionPlan {
    std::vector<FocalRegion> focal;
    std::vector<VoidRegion> voids;
    // Where the composition is composed *from*. A composition is a relationship between a viewpoint
    // and what it looks at, so the composer choosing the material and leaving the viewpoint to
    // whoever installs it means nothing in the world was ever arranged for the place it is seen
    // from. It is also the only way the corridor below can exist: a clear line has to be clear
    // between two known points.
    glm::vec2 viewpoint{0.0f};
    float viewpointClearance = 0.0f;   // metres of nothing around the viewpoint itself
    // The negative-space corridor: an unplanted lane from the viewpoint to the focal subject. The
    // brief makes it mandatory, and it earns that -- it is what stops a dense world from being a
    // wall, it gives the eye a path to the subject, and it keeps whatever is at the viewpoint from
    // standing in the lens. It is reported here as well as being folded into `voids` so a caller
    // can draw it, test it, or explain it.
    std::vector<VoidRegion> corridor;
    // The things worth travelling towards, most important first. A camera director reads this; so
    // does whatever decides which heroes are near enough to react to the music.
    std::vector<HeroPoint> heroes;
    // Layer name -> the band it was assigned to, so a test can assert that a fern did not end up
    // on the ridge line.
    std::vector<std::pair<std::string, DepthBand>> bands;
    float coveredFraction = 0.0f;   // of `extent`, before void regions are subtracted
    float emptyFraction = 0.0f;     // what the voids actually take back
    [[nodiscard]] DepthBand bandOf(const std::string& layer) const;
};

// The air and the light, derived from the recipe's atmosphere, lighting and palette. These are the
// values the scene's existing environment parameters already take -- fog, sky, volumetrics -- so
// this is a plan for parameters that exist rather than a new environment model. It lives beside the
// layers because a world's atmosphere is composed from the same recipe as its ecology, and because
// a generated world whose sky is left at the default is a generated world with a flat navy sky
// above a world that was carefully composed. That was the first one.
struct EnvironmentPlan {
    glm::vec3 skyZenith{0.02f, 0.025f, 0.06f};
    glm::vec3 skyHorizon{0.05f, 0.09f, 0.14f};
    glm::vec3 skyGround{0.012f, 0.014f, 0.025f};
    glm::vec3 sunColor{0.62f, 0.70f, 0.90f};
    glm::vec3 fogColor{0.04f, 0.06f, 0.11f};
    float skyIntensity = 1.0f;
    float sunIntensity = 2.0f;
    float sunSize = 0.03f;
    float sunGlow = 0.35f;
    float haze = 0.30f;
    float keyLight = 1.0f;
    float fogDensity = 0.02f;
    float fogHeight = 0.0f;
    float fogHeightFalloff = 0.05f;
    float volumeDensity = 0.02f;
    float volumeScattering = 0.6f;
    float volumeAbsorption = 0.1f;
    float volumeAnisotropy = 0.35f;
    float volumeEmission = 0.0f;
    float volumeNoise = 0.6f;
    float volumeNoiseScale = 40.0f;
    float volumeNoiseSpeed = 0.03f;
    glm::vec3 styledSkyAmbient{0.10f, 0.16f, 0.21f};
    glm::vec3 styledGroundAmbient{0.008f, 0.011f, 0.024f};
};

struct ComposedWorld {
    std::vector<ScatterLayer> layers;
    // The art direction this world was composed under, resolved from the recipe's profile and its
    // own overrides. Kept so a caller can apply the parts that are not layers -- post-processing
    // restraint, the light rig's ratio, the painterly surface mode -- without resolving it again
    // and risking a different answer.
    ArtDirectionProfile profile;
    // The plan's void regions and corridor as the ecology's own type, so negative space is a thing
    // the placer applies rather than a thing the plan describes.
    std::vector<ScatterClearance> clearances;
    CompositionPlan plan;
    EnvironmentPlan environment;
};

// Composition is a pure function of (recipe, library). Same inputs, same world, every time --
// which is what lets a composed scene be a deterministic render target like any other.
[[nodiscard]] Result<ComposedWorld> composeWorld(const WorldRecipe& recipe,
                                                 const assets::AssetLibrary& library);

// The biome vocabulary the composer's layers are written against: forest, meadow, marsh, rim,
// scree. A generated world's terrain has to define exactly these or every layer references a biome
// that does not exist and the ecology refuses the lot -- which is precisely what happened the first
// time Generate World was run against a fresh terrain. Returned from here so the composer and
// whoever builds the terrain cannot drift apart.
[[nodiscard]] BiomeSet composerBiomes();

// The ground a recipe's world grows on. Lives here rather than in whoever installs the world so the
// composer can *sample* it: the viewpoint, the corridor and the heroes all need to know what grows
// where they are being put, and a composer that decides all of that blind puts its camera on a
// scree slope and its corridor through a marsh. The first generated valley did exactly that -- the
// viewpoint landed on bare rock and the frame was five objects on an empty hillside.
[[nodiscard]] WorldMap terrainFor(const WorldRecipe& recipe);

// Which band an asset belongs to, given its height and tags. Exposed because it is the single
// judgement call in the composer and it deserves to be tested directly rather than inferred from
// the layers that come out.
[[nodiscard]] DepthBand bandForAsset(const assets::AssetDescriptor& asset);

} // namespace avgen::world
