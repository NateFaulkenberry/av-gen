#include "world/camera_clearance.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

// How much of a biome has to be present before its vegetation counts as an obstacle.
//
// Biome weights blend, so almost everywhere has a trace of almost everything. Treating any non-zero
// weight as "trees grow here" would make the whole map read as forest and lift the camera to the
// canopy over open meadow. A quarter is enough to mean the biome is actually present rather than
// bleeding in from next door.
constexpr float kBiomePresence = 0.25f;
} // namespace

float ClearanceField::canopyHeight(glm::vec2 p) const {
    if (map == nullptr || ecology == nullptr || ecology->layers.empty()) {
        return 0.0f;
    }
    const Sample s = map->sample(p, 0.5f);
    const BiomeWeights w = map->biomes.at(s.altitude, s.slope, s.moisture, p);

    float tallest = 0.0f;
    for (const ScatterLayer& layer : ecology->layers) {
        if (layer.height <= tallest) {
            continue;   // cannot raise the answer; skip the biome lookup entirely
        }
        // Cleared ground grows nothing, so a corridor or a glade is not an obstacle however tall
        // the layer that would otherwise be there. This is what lets a directed camera fly low down
        // the negative-space corridor the composer cut for exactly that purpose.
        if (clearanceWeight(ecology->clearances, p, layer.height, layer.category) <= 0.05f) {
            continue;
        }
        for (const BiomeDensity& d : layer.densities) {
            if (d.density <= 0.0f) {
                continue;
            }
            for (std::size_t b = 0; b < map->biomes.biomes.size() && static_cast<int>(b) < w.count; ++b) {
                if (map->biomes.biomes[b].name != d.biome) {
                    continue;
                }
                if (w.weights[b] >= kBiomePresence) {
                    tallest = layer.height;
                }
                break;
            }
            if (tallest >= layer.height) {
                break;
            }
        }
    }
    return tallest;
}

float ClearanceField::minimumHeight(glm::vec2 p) const {
    if (map == nullptr) {
        return 0.0f;
    }
    const Sample s = map->sample(p, 0.5f);
    // Water is a surface a camera should stay above too, and it is above the ground by definition.
    const float ground = std::max(s.height, s.waterSurface);
    const float canopy = canopyHeight(p);
    // Either clear of the bare ground, or clear of whatever grows on it -- whichever is higher.
    return ground + std::max(groundClearance + cameraRadius, canopy * canopyClearance + cameraRadius);
}

float ClearanceField::heroPenetration(glm::vec3 p) const {
    float deepest = 0.0f;
    for (const HeroPoint& h : heroes) {
        // A capsule rather than a sphere: a hero's bounding sphere on a twenty-metre subject is
        // enormous and would push the camera out of shots that are meant to be close, while its
        // actual obstruction is a trunk of roughly `radius` running up its height.
        const float above = p.y - h.position.y;
        if (above < -h.radius || above > h.height + h.radius) {
            continue;
        }
        const float planar = glm::length(glm::vec2(p.x, p.z) - glm::vec2(h.position.x, h.position.z));
        const float clearance = h.radius + cameraRadius;
        deepest = std::max(deepest, clearance - planar);
    }
    return std::max(deepest, 0.0f);
}

ClearanceAdjustment clearPoint(const ClearanceField& field, glm::vec3 p) {
    ClearanceAdjustment out;
    out.position = p;

    const float floorY = field.minimumHeight(glm::vec2(p.x, p.z));
    if (p.y < floorY) {
        out.lifted = floorY - p.y;
        out.position.y = floorY;
    }

    // Heroes last, and also by lifting. A hero is a thing the shot is *about*, so being inside one
    // usually means the camera has come too close on its way past rather than that it wants to be
    // somewhere else; rising over it keeps the subject in frame, where sliding around it does not.
    const float inside = field.heroPenetration(out.position);
    if (inside > 0.0f) {
        out.insideHero = true;
        out.position.y += inside;
        out.lifted += inside;
    }
    return out;
}

std::size_t clearPath(const ClearanceField& field, std::vector<glm::vec3>& path, int smoothingPasses) {
    if (path.empty()) {
        return 0;
    }
    std::vector<float> floors(path.size(), 0.0f);
    std::size_t raised = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const ClearanceAdjustment adjusted = clearPoint(field, path[i]);
        floors[i] = adjusted.position.y;
        if (adjusted.lifted > 1e-4f) {
            ++raised;
        }
        path[i] = adjusted.position;
    }
    if (raised == 0) {
        return 0;   // nothing was in the way; leave the authored path byte for byte
    }

    // Smooth, but never below the floor each point needed. A plain average would drag a corrected
    // point back down into the thing it was lifted out of, which is the obvious way to write this
    // and is wrong: the smoothing exists to remove the kink, not the clearance.
    for (int pass = 0; pass < std::max(smoothingPasses, 0); ++pass) {
        std::vector<float> smoothed(path.size());
        for (std::size_t i = 0; i < path.size(); ++i) {
            const std::size_t a = i == 0 ? i : i - 1;
            const std::size_t b = i + 1 < path.size() ? i + 1 : i;
            smoothed[i] = (path[a].y + path[i].y * 2.0f + path[b].y) * 0.25f;
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            path[i].y = std::max(smoothed[i], floors[i]);
        }
    }
    return raised;
}

} // namespace avgen::world
