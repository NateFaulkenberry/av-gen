#include "entity/obstacles.hpp"

#include "assets/asset_library.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace avgen::entity {
namespace {

std::string lowered(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

spatial::ObstacleType fromCategory(assets::AssetCategory category) {
    switch (category) {
    case assets::AssetCategory::Rock:
    case assets::AssetCategory::Crystal:
        return spatial::ObstacleType::Rock;
    case assets::AssetCategory::Structure:
    case assets::AssetCategory::Architectural:
        return spatial::ObstacleType::Structure;
    case assets::AssetCategory::Creature:
        return spatial::ObstacleType::Creature;
    case assets::AssetCategory::Flora:
    case assets::AssetCategory::Fungi:
    case assets::AssetCategory::Organic:
    case assets::AssetCategory::Floating:
    case assets::AssetCategory::Particle:
    case assets::AssetCategory::Atmosphere:
    case assets::AssetCategory::Terrain:
    case assets::AssetCategory::Water:
    case assets::AssetCategory::Unknown:
        break;
    }
    return spatial::ObstacleType::Vegetation;
}

} // namespace

bool NavigationObstacles::occupied(glm::vec2 p, float radius) const {
    return field_ != nullptr && field_->isOccupied(p.x, p.y, radius);
}

float NavigationObstacles::penetration(glm::vec2 p, float radius) const {
    if (field_ == nullptr) {
        return -std::max(radius, 0.0f);
    }
    // The real answer rather than the interface's coarse default, which can only say "blocked" and
    // leaves a steering behaviour with nothing to steer by. `clearance` is a signed distance from
    // the disc's rim to the nearest solid; penetration is its negation.
    //
    // The filter is the plan-view one on purpose: `occupied` and `penetration` take a radius and
    // nothing else, so they cannot know how tall the mover is or what it can step over. A walker
    // that has an opinion about those asks `spatial::ObstacleField` directly with its own filter --
    // which `entity::Navigator` does. This is the shared, conservative answer.
    spatial::ObstacleFilter filter;
    filter.bodyRadius = std::max(radius, 0.0f);
    filter.stepOver = 0.0f;
    filter.headHeight = 0.0f;
    return -field_->clearance(p, filter, std::max(radius, 1.0f) * 4.0f);
}

spatial::ObstacleType classifyAsset(std::string_view category, std::string_view assetPath) {
    // The composer's own word first. It came from the asset library, which read the manifest, and
    // second-guessing it from a filename would make two classifications of the same thing that can
    // disagree -- the failure mode this project keeps rediscovering.
    if (!category.empty()) {
        if (const std::optional<assets::AssetCategory> c = assets::assetCategoryFromName(category)) {
            const spatial::ObstacleType type = fromCategory(*c);
            // Flora is the library's largest bucket and holds both a blade of grass and a tree.
            // The path still decides between them; everything else the category settles.
            if (type != spatial::ObstacleType::Vegetation) {
                return type;
            }
        }
    }
    // Glowmere's scene file predates categories and carries none, so the filename is all there is.
    // A keyword table rather than a clever heuristic: it is inspectable, it is wrong in ways an
    // author can see, and adding a library means adding a line.
    const std::string path = lowered(assetPath);
    if (contains(path, "rock") || contains(path, "boulder") || contains(path, "stone") ||
        contains(path, "cliff") || contains(path, "crystal")) {
        return spatial::ObstacleType::Rock;
    }
    if (contains(path, "tree") || contains(path, "pine") || contains(path, "trunk") ||
        contains(path, "log") || contains(path, "stump") || contains(path, "palm")) {
        return spatial::ObstacleType::Trunk;
    }
    if (contains(path, "wall") || contains(path, "pillar") || contains(path, "column") ||
        contains(path, "ruin") || contains(path, "tower") || contains(path, "building") ||
        contains(path, "fence") || contains(path, "monument")) {
        return spatial::ObstacleType::Structure;
    }
    return spatial::ObstacleType::Vegetation;
}

spatial::ObstacleType classifyScatterLayer(const world::ScatterLayer& layer) {
    const spatial::ObstacleType byAsset = classifyAsset(layer.category, layer.asset);
    if (byAsset != spatial::ObstacleType::Vegetation) {
        return byAsset;
    }
    // A layer may be named for what it is when the asset is not. "canopy" over a file called
    // CommonTree is already a tree; "deadwood" over DeadTree is too. This catches the case where
    // an author reuses a generic mesh for a species the name identifies.
    const std::string name = lowered(layer.name);
    if (contains(name, "tree") || contains(name, "canopy") || contains(name, "pine") ||
        contains(name, "deadwood") || contains(name, "timber")) {
        return spatial::ObstacleType::Trunk;
    }
    if (contains(name, "rock") || contains(name, "boulder") || contains(name, "scree")) {
        return spatial::ObstacleType::Rock;
    }
    return spatial::ObstacleType::Vegetation;
}

spatial::Traversal traversalClass(spatial::ObstacleType type, float height,
                                  const ObstaclePolicy& policy) {
    // A creature is the one identity that changes the answer. Everything else is decided by how
    // tall the solid is, because that is what getting past it actually depends on -- a
    // one-metre stump and a one-metre boulder take the same vault.
    if (type == spatial::ObstacleType::Creature) {
        return spatial::Traversal::Blocking;
    }
    if (height <= policy.stepOverHeight) {
        return spatial::Traversal::StepOver;
    }
    if (height <= policy.jumpOverHeight) {
        return spatial::Traversal::Jumpable;
    }
    return spatial::Traversal::Blocking;
}

float instanceHeight(const world::ScatterLayer& layer, float instanceScale, float assetHeight) {
    // `layer.height` is what the world wants the thing to be; the asset's own height is what it is.
    // The composition normalises the mesh by the ratio, so the world height of an instance is the
    // layer's height times the per-instance scale -- and only when the layer declined to normalise
    // does the asset's own size matter.
    const float base = layer.height > 0.0f ? layer.height : std::max(assetHeight, 0.0f);
    return base * std::max(instanceScale, 0.0f);
}

namespace {

// Whether a solid of this class and height is in a walker's way at all.
bool blocksAt(spatial::ObstacleType type, float height, const ObstaclePolicy& policy) {
    switch (type) {
    case spatial::ObstacleType::Rock: return height >= policy.rockMinHeight;
    case spatial::ObstacleType::Trunk: return height >= policy.treeMinHeight;
    case spatial::ObstacleType::Structure: return height >= policy.structureMinHeight;
    case spatial::ObstacleType::Creature: return true;
    case spatial::ObstacleType::Vegetation:
    case spatial::ObstacleType::Custom: return height >= policy.vegetationMinHeight;
    }
    return false;
}

// The radius of the part of this thing a walker actually collides with.
float solidRadius(spatial::ObstacleType type, float height, float footprintAspect,
                  const ObstaclePolicy& policy) {
    const float full = footprintAspect > 0.0f
                           ? height * footprintAspect * policy.footprintScale
                           : 0.0f;
    if (type == spatial::ObstacleType::Trunk || height >= policy.trunkHeight) {
        // Tall things are stems. Taking the bounding radius here is what would make a forest a
        // wall: a fourteen-metre tree bounds at about five metres and blocks at about half of one.
        const float trunk = std::clamp(height * policy.trunkFraction, policy.minTrunkRadius,
                                       policy.maxTrunkRadius);
        return full > 0.0f ? std::min(trunk, full) : trunk;
    }
    // Squat things are solid all the way out. With no bounds to go on, fall back to something
    // proportionate rather than to nothing: a boulder with a zero radius is not an obstacle.
    return full > 0.0f ? std::min(full, policy.maxRadius) : std::max(height * 0.45f, 0.25f);
}

} // namespace

std::size_t obstaclesFromScatter(const world::ScatterLayer& layer, const spatial::PointCloud& cloud,
                                 float footprintAspect, float assetHeight,
                                 const ObstaclePolicy& policy, spatial::ObstacleField& out) {
    const std::size_t count = cloud.count();
    if (count == 0) {
        return 0;
    }
    // The author's word, where there is one (ADR-196). A layer declared `passable` is scenery
    // whatever it is called and however tall it grew, and saying so costs the same one comparison
    // the height early-out costs.
    if (layer.navigation == world::ScatterNavigation::Passable) {
        return 0;
    }
    const bool declaredSolid = layer.navigation == world::ScatterNavigation::Blocks;
    const spatial::ObstacleType type = classifyScatterLayer(layer);
    // Cheapest possible rejection of the layers that produce nothing: 120,000 grass instances must
    // not cost 120,000 iterations to decide they are grass. The tallest instance a layer can grow
    // is its authored height at its largest scale, so one comparison settles the whole layer.
    const float tallest = instanceHeight(layer, std::max(layer.maxScale, layer.minScale), assetHeight);
    if (!declaredSolid && !blocksAt(type, tallest, policy)) {
        return 0;
    }
    const std::span<const glm::vec3> positions = cloud.positions();
    const std::span<const glm::vec3> scales = cloud.scales();
    std::size_t added = 0;
    out.reserve(out.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 p = positions[i];
        // Scatter writes a uniform scale, but reading y for the height and x for the footprint
        // keeps this correct if it ever stops being uniform.
        const float scaleY = i < scales.size() ? scales[i].y : 1.0f;
        const float scaleXZ = i < scales.size() ? scales[i].x : 1.0f;
        const float height = instanceHeight(layer, scaleY, assetHeight);
        if (!declaredSolid && !blocksAt(type, height, policy)) {
            continue; // this specimen is small enough to walk through even though its species is not
        }
        spatial::NavigationObstacle o;
        o.center = glm::vec2(p.x, p.z);
        o.radius = std::min(solidRadius(type, height, footprintAspect, policy) *
                                std::max(scaleXZ / std::max(scaleY, 1e-3f), 1e-3f),
                            policy.maxRadius);
        // `sink` pushed the mesh into the ground; the solid starts at the surface either way.
        o.base = p.y + layer.sink;
        o.height = height;
        o.type = type;
        // A declared solid is `Blocking` and nothing derives its way out of that: the author has
        // said the thing is in the way, and the heuristic that classified it has already been
        // overruled once. Everything else is classed by what it is and how tall it grew.
        o.traversal = declaredSolid ? spatial::Traversal::Blocking
                                    : traversalClass(type, height, policy);
        out.add(o);
        ++added;
    }
    return added;
}

std::size_t obstaclesFromHeroes(std::span<const world::HeroPoint> heroes, spatial::ObstacleField& out,
                                float radiusScale) {
    std::size_t added = 0;
    for (const world::HeroPoint& hero : heroes) {
        if (hero.radius <= 0.0f || hero.height <= 0.0f) {
            continue;
        }
        spatial::NavigationObstacle o;
        o.center = glm::vec2(hero.position.x, hero.position.z);
        // A hero's radius is sized for a camera to frame it, not for a body to touch it; using it
        // whole puts a sixteen-metre exclusion around a tree a character is meant to walk up to.
        o.radius = std::max(hero.radius * std::max(radiusScale, 0.05f), 0.5f);
        o.base = hero.position.y;
        o.height = hero.height;
        o.type = spatial::ObstacleType::Structure;
        // A hero is a monument, an elder tree or an arch, and none of them is vaulted. Blocking
        // outright rather than through `traversalClass`, which would call a half-metre hero a kerb.
        o.traversal = spatial::Traversal::Blocking;
        out.add(o);
        ++added;
    }
    return added;
}

} // namespace avgen::entity
