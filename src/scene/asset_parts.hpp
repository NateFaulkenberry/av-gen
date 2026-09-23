#pragma once

// Turning an imported glTF asset into instanced draws, one per sub-material (ADR-044).
//
// A library asset arrives as a `SceneAsset` with ids local to itself. Two things have to happen
// before it can be placed many times in a composition: its mesh and texture ids have to be rebased
// onto the scene's, which is what the `offset*` functions do, and its surfaces have to be grouped
// by material, so a mushroom with a cap material and a stem material becomes two instanced draws
// rather than one draw per placement.
//
// Extracted from `composition.cpp`'s anonymous namespace during the QA pass of 2026-09-23, for the
// reason the particle serialiser was: it is cohesive, it names one job, and it was unreachable
// from a test where it sat. The behaviour is unchanged and the comments are the originals.

#include "assets/asset_registry.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace avgen::scene {

// ---- rebasing an asset's ids onto the scene's --------------------------------------------------

// Shifts every mesh and texture id an entity names by the offsets the scene assigned this asset.
void offsetEntityIds(Entity& e, MeshId meshOffset, TextureId textureOffset);

// The same for the texture references inside a material.
void offsetMaterialTextures(Material& m, TextureId textureOffset);

// ---- grouping an asset's surfaces by material ---------------------------------------------------

// One instanced draw's worth of an asset: the surfaces sharing a material, merged into one mesh.
struct AssetPart {
    std::shared_ptr<const MeshData> mesh;
    Material material;
    bool hasMaterial = false;
    double area = 0.0;
    std::size_t firstEntity = 0; // for a stable order when two parts have the same area
    // The name the asset gave this material, when it gave one. A label, never a grouping key:
    // grouping stays by value so two materials that shade identically keep sharing a draw. What
    // the name is for is addressing -- "parts/Blue/emissiveGain" instead of an index that is an
    // internal ordering by surface area and changes the day someone edits the model.
    std::string name;
};

// Groups an asset's surfaces into parts that shade identically. Grouping is **by material value**,
// not by material name, so two materials that produce the same pixels keep sharing a draw.
[[nodiscard]] std::vector<AssetPart> assetMaterialParts(const assets::SceneAsset& asset);

// The asset's bounds across every part, as (min, max); (0, 0) when no part carries a mesh.
[[nodiscard]] std::pair<glm::vec3, glm::vec3> partsBounds(const std::vector<AssetPart>& parts);

// A triangle budget authored for the whole asset, shared out between its parts in proportion to
// how many triangles each one has. Giving every part the whole budget would multiply it by the
// part count, which is how a "10k triangle" tree becomes 20k the moment it grows a second material.
[[nodiscard]] int partBudget(const std::vector<AssetPart>& parts, std::size_t index, int assetBudget);

} // namespace avgen::scene
