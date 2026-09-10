#pragma once

// The semantic asset library (ADR-060, milestone 1 of the cinematic upgrade).
//
// `assets::AssetRegistry` answers "give me the mesh at this path". It is a file cache and it knows
// nothing about what an asset is *for*. That is fine for a scene that names every object it wants,
// and useless for a composer that has to decide what belongs in a foreground and what belongs on a
// ridge line four hundred metres away.
//
// This file adds the layer in between: what an asset is, what it is for, how big it wants to be,
// how densely it wants to grow, and how it should answer the music. It reads `assets/manifest.json`
// -- the file that already exists and already carries a source, a licence and a category per pack --
// rather than introducing a second manifest format beside it. Every field beyond `name` and `file`
// is optional and has a defensible default, so the manifests already in the repository load
// unchanged and can be enriched one entry at a time.
//
// The library holds descriptions, not meshes. Nothing here loads geometry, touches the GPU or costs
// anything per frame; it is consulted when a world is composed and then not again.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::assets {

// What kind of thing this is. The category is the coarse bucket a composer reasons in ("I need
// something massive on that ridge"); `archetype` below is the specific noun ("twisted_pine").
enum class AssetCategory : std::uint8_t {
    Flora, Fungi, Rock, Crystal, Creature, Structure, Particle, Atmosphere, Terrain, Water,
    Floating, Architectural, Organic, Unknown,
};
[[nodiscard]] const char* assetCategoryName(AssetCategory c);
[[nodiscard]] std::optional<AssetCategory> assetCategoryFromName(std::string_view name);
[[nodiscard]] std::vector<AssetCategory> allAssetCategories();

// How a material behaves, in the terms a composer and an art-direction profile can both use. These
// are intentions, not shader parameters: a scene turns them into whatever its material programs
// need. Kept small on purpose -- a full material description belongs in the material program.
struct MaterialProfile {
    float emissive = 0.0f;      // 0 inert, 1 a primary light source in its own right
    float translucency = 0.0f;  // 0 opaque, 1 reads lit from behind
    float roughness = 0.8f;
    float metallic = 0.0f;
    glm::vec3 tint{1.0f};       // the colour family this asset belongs to
};

// What the asset does when the music does something. A composer uses this to decide which layers
// carry a drop and which stay still; without it, everything would have to react equally, which is
// the failure mode the art direction is written against.
struct AudioResponseProfile {
    float energy = 0.0f;    // responds to overall level (slow)
    float impact = 0.0f;    // responds to onsets and drops (fast)
    float sway = 0.0f;      // responds to the beat clock as motion rather than brightness
    float bloom = 0.0f;     // how much its emission moves with the music
};

// How much this asset may differ from itself. Structured variation reads as a species; unstructured
// variation reads as noise, which is why hue and scale are separate numbers rather than one
// "randomness" knob.
struct VariationProfile {
    float scale = 0.15f;     // +/- fraction of preferredScale
    float hue = 0.0f;        // radians of hue rotation available to an instance
    float emissive = 0.0f;   // fraction of specimens that differ in brightness
    float yaw = 1.0f;        // 0..1 of a full turn
    float lean = 0.0f;       // how far an instance may tilt from upright
};

// One entry in the library.
struct AssetDescriptor {
    std::string id;                      // unique within the library; defaults to `name`
    std::string name;
    std::filesystem::path file;          // as written in the manifest, relative to it
    AssetCategory category = AssetCategory::Unknown;
    std::string archetype;               // the specific noun: "twisted_pine", "shelf_fungus"
    std::vector<std::string> tags;       // foreground, hero, bioluminescent, delicate, rare, ...
    glm::vec3 naturalSize{1.0f};         // the mesh's own bounds, from the manifest
    int triangles = 0;

    // Artistic role. `visualImportance` is what a composer sorts by when it decides what may
    // occupy a focal region and what may not; it is the single most useful number here.
    float visualImportance = 0.5f;       // 0 texture, 1 the thing the shot is about
    // 0 means "whatever size the mesh already is", which has to be the default: with a default of
    // 1 the fallback to naturalSize could never fire, and every unauthored asset silently became
    // one metre tall. A test caught that.
    float preferredScale = 0.0f;         // metres of height it wants to be; 0 = use naturalSize
    float preferredDensity = 0.0f;       // instances per square metre where it is welcome

    MaterialProfile material;
    AudioResponseProfile audioResponse;
    VariationProfile variation;

    [[nodiscard]] bool hasTag(std::string_view tag) const;
    // Height in metres an instance of this asset wants, falling back to its own mesh bounds.
    [[nodiscard]] float effectiveHeight() const;
};

// A query over the library. Every field is optional; an empty query matches everything, which makes
// it usable as "give me the whole library" without a second entry point.
struct AssetQuery {
    std::optional<AssetCategory> category;
    std::vector<std::string> anyTags;   // matches an asset carrying at least one of these
    std::vector<std::string> allTags;   // matches an asset carrying every one of these
    std::optional<float> minImportance;
    std::optional<float> maxImportance;
    std::optional<float> minHeight;
    std::optional<float> maxHeight;
};

// The library itself: a flat list of descriptors plus the provenance the manifest carries, which
// travels with them because "where did this come from and may we ship it" is a question that has to
// survive being loaded into memory.
class AssetLibrary {
public:
    [[nodiscard]] static Result<AssetLibrary> loadFile(const std::filesystem::path& manifest);
    [[nodiscard]] static Result<AssetLibrary> fromJson(const nlohmann::json& j,
                                                       const std::filesystem::path& baseDirectory);

    [[nodiscard]] const std::string& source() const { return source_; }
    [[nodiscard]] const std::string& license() const { return license_; }
    [[nodiscard]] const std::filesystem::path& baseDirectory() const { return baseDirectory_; }
    [[nodiscard]] const std::vector<AssetDescriptor>& assets() const { return assets_; }
    [[nodiscard]] std::size_t size() const { return assets_.size(); }

    [[nodiscard]] const AssetDescriptor* find(std::string_view id) const;
    // Matches in library order, which is manifest order: deterministic, so a composed world is
    // reproducible from the same manifest.
    [[nodiscard]] std::vector<const AssetDescriptor*> select(const AssetQuery& query) const;
    // The resolved path to an asset's file, for handing to the AssetRegistry.
    [[nodiscard]] std::filesystem::path resolve(const AssetDescriptor& asset) const;

    [[nodiscard]] nlohmann::json toJson() const;

private:
    std::string source_;
    std::string license_;
    std::filesystem::path baseDirectory_;
    std::vector<AssetDescriptor> assets_;
};

} // namespace avgen::assets
