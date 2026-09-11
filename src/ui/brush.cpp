#include "ui/brush.hpp"

#include "core/log.hpp"
#include "world/terrain_query.hpp"

#include <fmt/format.h>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::ui {
namespace {

// Higher wins when several apply: what the artist most needs told.
int severity(PlacementIssue issue) {
    switch (issue) {
    case PlacementIssue::None:
        return 0;
    case PlacementIssue::Collides:
        return 1;
    case PlacementIssue::TooSteep:
        return 2;
    case PlacementIssue::InWater:
        return 3;
    case PlacementIssue::OutsideWorld:
        return 4;
    case PlacementIssue::NoSurface:
        return 5;
    case PlacementIssue::NoAsset:
        return 6;
    }
    return 0;
}

} // namespace

const char* placementIssueName(PlacementIssue issue) {
    switch (issue) {
    case PlacementIssue::None:
        return "ok";
    case PlacementIssue::Collides:
        return "collides";
    case PlacementIssue::TooSteep:
        return "too steep";
    case PlacementIssue::InWater:
        return "in water";
    case PlacementIssue::OutsideWorld:
        return "outside the world";
    case PlacementIssue::NoSurface:
        return "no surface";
    case PlacementIssue::NoAsset:
        return "nothing armed";
    }
    return "ok";
}

BrushPreview planBrush(scene::Composition& composition, const app::PlacementSettings& settings,
                       const BrushAsset& asset, const GroundSample& cursor, std::uint32_t seed) {
    BrushPreview preview;
    preview.mode = settings.mode;
    preview.ground = cursor;
    const bool erasing = settings.mode == app::PlacementMode::Eraser;
    preview.brushRadius = (settings.mode == app::PlacementMode::Brush ||
                           settings.mode == app::PlacementMode::Eraser ||
                           settings.mode == app::PlacementMode::Replace)
                              ? settings.brushRadius
                              : (settings.mode == app::PlacementMode::Cluster ? settings.clusterRadius : 0.0f);

    // Eraser and Replace both remove what the brush covers. Done before the placement plan so that
    // Replace's ghosts are tested against a world with the removed objects already gone -- a
    // replace brush that reported every instance as colliding with what it was about to delete
    // would be technically correct and completely useless.
    std::vector<std::string> doomed;
    if (erasing || settings.mode == app::PlacementMode::Replace) {
        const float reach = std::max(settings.brushRadius, 0.01f);
        for (const auto& node : composition.nodes()) {
            if (!node || node->kind == scene::NodeKind::Terrain || node->kind == scene::NodeKind::Group) {
                continue;
            }
            const scene::WorldBounds bounds = composition.nodeBounds(node->name);
            if (!bounds.valid) {
                continue;
            }
            const glm::vec3 centre = bounds.centre();
            const float d = glm::length(glm::vec2(centre.x - cursor.position.x, centre.z - cursor.position.z));
            if (d <= reach) {
                doomed.push_back(node->name);
            }
        }
        preview.erasing = doomed;
    }

    if (erasing) {
        preview.armed = true; // the eraser needs no asset
        preview.assetName = "eraser";
        preview.validCount = doomed.size();
        if (doomed.empty()) {
            preview.reason = "nothing to erase under the brush";
        } else {
            preview.reason = fmt::format("{} object{} under the brush", doomed.size(),
                                         doomed.size() == 1 ? "" : "s");
        }
        return preview;
    }

    if (!asset.ready()) {
        preview.issue = PlacementIssue::NoAsset;
        preview.reason = "choose an asset to place";
        return preview;
    }
    // Armed *before* the surface is checked, and this order is the whole point. Reporting "no
    // asset" for a brush that is holding a fern and pointing at the sky is a message that sends the
    // artist to the palette to fix something that is not broken; what they need told is that there
    // is no ground where they are pointing. A preview that is wrong about *which* thing is wrong is
    // worse than one that says nothing.
    preview.armed = true;
    preview.assetId = asset.descriptor->id;
    preview.assetName = asset.descriptor->name.empty() ? asset.descriptor->id : asset.descriptor->name;

    if (!cursor.valid) {
        preview.issue = PlacementIssue::NoSurface;
        preview.reason = "no ground under the cursor -- that is the sky, or past the far edge";
        return preview;
    }

    const std::uint32_t strokeSeed = settings.seed != 0 ? settings.seed : seed;
    const std::vector<app::Placement> plan =
        app::planPlacements(settings, cursor.position, cursor.normal, strokeSeed);
    const float base = app::normalisingScale(*asset.descriptor);
    const glm::vec3 natural = asset.descriptor->naturalSize;
    // The footprint is the mesh's own horizontal extent, scaled the way the instance is. Half the
    // larger of x and z, because a circle is what a brush draws and the honest radius of a box is
    // its larger half-extent -- an under-reported footprint is a collision test that passes and a
    // tree that comes out inside a rock.
    const float naturalRadius = 0.5f * std::max(natural.x, natural.z);

    // Everything already in the world that a new instance could hit. Gathered once for the whole
    // plan: a brush of forty plants against a scene of three hundred nodes is twelve thousand
    // distance tests, which is nothing, but re-deriving the bounds forty times is not.
    struct Obstacle {
        std::string name;
        glm::vec3 centre{0.0f};
        float radius = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
    };
    std::vector<Obstacle> obstacles;
    if (settings.avoidCollisions) {
        for (const auto& node : composition.nodes()) {
            if (!node || node->kind == scene::NodeKind::Terrain || node->kind == scene::NodeKind::Group) {
                continue;
            }
            if (std::find(doomed.begin(), doomed.end(), node->name) != doomed.end()) {
                continue; // Replace is about to take it away
            }
            const scene::WorldBounds bounds = composition.nodeBounds(node->name);
            if (!bounds.valid) {
                continue;
            }
            const glm::vec3 size = bounds.size();
            obstacles.push_back(Obstacle{node->name, bounds.centre(),
                                         0.5f * std::max(size.x, size.z), bounds.max.y, bounds.min.y});
        }
    }

    // One query for the whole stroke. It is pointers and floats, but building it walks the node list
    // to find the terrain, and a scatter has dozens of instances.
    const world::TerrainQuery query = composition.terrainQuery();
    float lift = 0.0f;
    for (const auto& node : composition.nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            lift = composition.nodeWorldTransform(*node).position.y;
            break;
        }
    }

    const glm::vec3 upright(0.0f, 1.0f, 0.0f);
    for (const app::Placement& p : plan) {
        GhostInstance ghost;
        ghost.scale = p.scale;
        ghost.footprintRadius = std::max(naturalRadius * base * p.scale, 0.05f);
        ghost.height = std::max(asset.descriptor->effectiveHeight() * p.scale, 0.01f);

        // Every instance is dropped onto the ground under *itself*. `planPlacements` lays a brush
        // out on the tangent plane at the cursor, which is right for the pattern and wrong for the
        // altitude: over ten metres of hillside the far edge of the disc is metres off the ground.
        // Sampling per instance is what makes a stroke follow the terrain.
        GroundSample under = sampleGroundAt(query, lift, glm::vec2(p.position.x, p.position.z));
        // Snap to somewhere the world would accept, when asked. `nearestValidPoint` is a
        // deterministic outward spiral, so a stroke that snaps is the same stroke twice.
        if (settings.snapToValid && query.valid()) {
            const bool refused = !under.insideWorld || (settings.avoidWater && under.submerged) ||
                                 under.slopeDegrees > settings.maxSlopeDegrees;
            if (refused) {
                if (const auto moved = query.nearestValidPoint(glm::vec2(p.position.x, p.position.z),
                                                               ghost.footprintRadius,
                                                               settings.snapSearchRadius)) {
                    under = sampleGroundAt(query, lift, *moved);
                }
            }
        }
        ghost.groundNormal = under.valid ? under.normal : p.normal;
        ghost.slopeDegrees = under.slopeDegrees;
        const glm::vec3 surface = under.valid ? under.normal : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 position = under.valid ? under.position : p.position;
        position -= surface * settings.sink;
        ghost.position = position;

        glm::quat rotation = glm::angleAxis(p.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        if (settings.alignToNormal && glm::dot(upright, surface) < 0.9999f) {
            rotation = glm::rotation(upright, surface) * rotation;
        }
        ghost.rotation = rotation;

        // ---- validity, in the order the artist would ask ----
        if (!under.valid) {
            ghost.issue = PlacementIssue::NoSurface;
        } else if (!under.insideWorld) {
            ghost.issue = PlacementIssue::OutsideWorld;
        } else if (settings.avoidWater && under.submerged) {
            ghost.issue = PlacementIssue::InWater;
        } else if (under.slopeDegrees > settings.maxSlopeDegrees) {
            ghost.issue = PlacementIssue::TooSteep;
        } else if (query.isOccupied(glm::vec2(position.x, position.z), ghost.footprintRadius)) {
            // A hero, a registered obstacle, or the world's edge -- the things the world itself
            // calls solid (ADR-090). Deliberately *not* the canopy: it is statistical and cannot say
            // whether a particular disc contains a trunk, so folding it in here would turn "is
            // something standing here" into "does something grow nearby".
            ghost.issue = PlacementIssue::Collides;
            ghost.blockedBy = query.hasObstacles() ? "something solid" : "a hero or the world's edge";
        } else {
            for (const Obstacle& obstacle : obstacles) {
                const float gap = glm::length(glm::vec2(obstacle.centre.x - position.x,
                                                        obstacle.centre.z - position.z));
                const float wanted = obstacle.radius + ghost.footprintRadius + settings.collisionPadding;
                if (gap >= wanted) {
                    continue;
                }
                // Overlapping footprints are not automatically a collision: a fern under a tree's
                // canopy overlaps it from above and is exactly the composition somebody wanted. It
                // is a collision when the two also share height.
                const float mine = position.y + ghost.height;
                if (mine < obstacle.bottom || position.y > obstacle.top) {
                    continue;
                }
                ghost.issue = PlacementIssue::Collides;
                ghost.blockedBy = obstacle.name;
                break;
            }
        }
        // Instances also have to keep out of each other's way, or a brush cheerfully stacks its own
        // plan on itself wherever the dart throwing and the terrain conspired.
        if (ghost.valid() && settings.avoidCollisions) {
            for (const GhostInstance& other : preview.instances) {
                if (!other.valid()) {
                    continue;
                }
                const float gap = glm::length(glm::vec2(other.position.x - position.x,
                                                        other.position.z - position.z));
                if (gap < (other.footprintRadius + ghost.footprintRadius) * 0.5f) {
                    ghost.issue = PlacementIssue::Collides;
                    ghost.blockedBy = "another instance of this stroke";
                    break;
                }
            }
        }

        if (ghost.valid()) {
            ++preview.validCount;
        } else {
            ++preview.blockedCount;
            if (severity(ghost.issue) > severity(preview.issue)) {
                preview.issue = ghost.issue;
            }
        }
        preview.instances.push_back(std::move(ghost));
    }

    // The reason, with its numbers. "Too steep" without the angle is a warning nobody can act on.
    if (preview.blockedCount > 0) {
        const GhostInstance* worst = nullptr;
        for (const GhostInstance& ghost : preview.instances) {
            if (ghost.issue == preview.issue) {
                worst = &ghost;
                break;
            }
        }
        switch (preview.issue) {
        case PlacementIssue::TooSteep:
            preview.reason = fmt::format("too steep: {:.0f} degrees, limit {:.0f}",
                                         worst != nullptr ? worst->slopeDegrees : 0.0f,
                                         settings.maxSlopeDegrees);
            break;
        case PlacementIssue::InWater:
            preview.reason = fmt::format("under {:.1f} m of water", cursor.waterDepth);
            break;
        case PlacementIssue::OutsideWorld:
            preview.reason = "outside the world";
            break;
        case PlacementIssue::Collides:
            preview.reason = worst != nullptr && !worst->blockedBy.empty()
                                 ? "blocked by " + worst->blockedBy
                                 : "something is already there";
            break;
        case PlacementIssue::NoSurface:
            preview.reason = "no ground under it";
            break;
        default:
            preview.reason = placementIssueName(preview.issue);
            break;
        }
    }
    return preview;
}

std::vector<scene::CompositionNode> makeNodes(const scene::Composition& composition,
                                              const BrushPreview& preview, const BrushAsset& asset,
                                              const std::string& parent) {
    std::vector<scene::CompositionNode> out;
    if (!asset.ready()) {
        return out;
    }
    const float base = app::normalisingScale(*asset.descriptor);
    std::vector<std::string> taken;
    for (const auto& node : composition.nodes()) {
        if (node) {
            taken.push_back(node->name);
        }
    }
    for (const GhostInstance& ghost : preview.instances) {
        if (!ghost.valid()) {
            continue;
        }
        scene::CompositionNode node;
        node.name = app::uniquePlacementName(asset.descriptor->id, taken);
        taken.push_back(node.name);
        node.kind = scene::NodeKind::Gltf;
        node.asset = asset.file;
        node.parent = parent;
        node.transform.position = ghost.position;
        node.transform.rotation = ghost.rotation;
        node.transform.scale = glm::vec3(base * ghost.scale);
        out.push_back(std::move(node));
    }
    return out;
}

std::string previewSummary(const BrushPreview& preview) {
    if (!preview.armed) {
        return "click selects";
    }
    if (preview.mode == app::PlacementMode::Eraser) {
        return preview.erasing.empty() ? std::string("eraser: nothing under the brush")
                                       : fmt::format("eraser: {} object{}", preview.erasing.size(),
                                                     preview.erasing.size() == 1 ? "" : "s");
    }
    if (preview.instances.empty()) {
        return preview.reason.empty() ? std::string("nothing to place") : preview.reason;
    }
    std::string text = fmt::format("{} x {}", preview.validCount, preview.assetName);
    if (!preview.erasing.empty()) {
        text += fmt::format(", replacing {}", preview.erasing.size());
    }
    if (preview.blockedCount > 0) {
        text += fmt::format("  |  {} blocked ({})", preview.blockedCount, preview.reason);
    }
    return text;
}

} // namespace avgen::ui
