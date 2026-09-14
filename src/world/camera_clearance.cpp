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

glm::vec2 ClearanceField::heroPushOut(glm::vec3 p) const {
    glm::vec2 best(0.0f);
    float deepest = 0.0f;
    for (const HeroPoint& h : heroes) {
        const float above = p.y - h.position.y;
        if (above < -h.radius || above > h.height + h.radius) {
            continue;
        }
        const glm::vec2 away = glm::vec2(p.x, p.z) - glm::vec2(h.position.x, h.position.z);
        const float planar = glm::length(away);
        const float clearance = h.radius + cameraRadius;
        const float inside = clearance - planar;
        if (inside <= 0.0f || inside <= deepest) {
            continue;
        }
        deepest = inside;
        // On the axis there is no direction; +X, deterministically. See the header.
        best = planar > 1e-4f ? (away / planar) * inside : glm::vec2(inside, 0.0f);
    }
    return best;
}

ClearanceAdjustment clearPoint(const ClearanceField& field, glm::vec3 p) {
    ClearanceAdjustment out;
    out.position = p;

    // Heroes first, and sideways. This used to lift, which is right for a cut-based film and exactly
    // wrong for a continuous take: the camera was carried *over* the subject it was circling.
    const glm::vec2 push = field.heroPushOut(out.position);
    if (glm::dot(push, push) > 0.0f) {
        out.insideHero = true;
        out.pushed = glm::length(push);
        out.position.x += push.x;
        out.position.z += push.y;
    }

    // Then the floor, at wherever the push left it.
    const float floorY = field.minimumHeight(glm::vec2(out.position.x, out.position.z));
    if (out.position.y < floorY) {
        out.lifted = floorY - out.position.y;
        out.position.y = floorY;
    }

    // One more hero check: a push out of one hero can land inside another, and the lift can raise a
    // point into a hero's capsule that it had been under.
    const glm::vec2 again = field.heroPushOut(out.position);
    if (glm::dot(again, again) > 0.0f) {
        out.insideHero = true;
        out.pushed += glm::length(again);
        out.position.x += again.x;
        out.position.z += again.y;
    }
    return out;
}

std::size_t clearPath(const ClearanceField& field, std::vector<glm::vec3>& path, int smoothingPasses) {
    if (path.empty()) {
        return 0;
    }
    std::vector<float> floors(path.size(), 0.0f);
    // The lateral *offset* each point needed, not its corrected position. Smoothing the offset is
    // what keeps a point that was never pushed exactly where it was authored -- including the two
    // endpoints, which a positional average pulls inward because their own neighbour is themselves.
    // The first version smoothed positions and moved the far end of a forty-metre dolly by 0.9 m
    // when the only thing in the way was at the middle.
    std::vector<glm::vec2> offsets(path.size(), glm::vec2(0.0f));
    std::size_t moved = 0;
    bool anyPushed = false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const ClearanceAdjustment adjusted = clearPoint(field, path[i]);
        floors[i] = adjusted.position.y;
        offsets[i] = glm::vec2(adjusted.position.x - path[i].x, adjusted.position.z - path[i].z);
        if (adjusted.lifted > 1e-4f || adjusted.pushed > 1e-4f) {
            ++moved;
        }
        anyPushed = anyPushed || adjusted.pushed > 1e-4f;
        path[i] = adjusted.position;
    }
    if (moved == 0) {
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

        // The lateral half. It cannot use the vertical trick -- "never below the floor" has no
        // meaning when neighbouring points were pushed in different directions -- so it smooths
        // freely and then re-asserts clearance, which is the only way to be sure the average did not
        // put a point back inside the hero it was moved out of. Skipped entirely when nothing was
        // pushed, so a path that only cleared terrain is bit-identical to what it was before lateral
        // clearance existed.
        if (!anyPushed) {
            continue;
        }
        std::vector<glm::vec2> smoothedOffsets(path.size());
        for (std::size_t i = 0; i < path.size(); ++i) {
            const std::size_t a = i == 0 ? i : i - 1;
            const std::size_t b = i + 1 < path.size() ? i + 1 : i;
            smoothedOffsets[i] = (offsets[a] + offsets[i] * 2.0f + offsets[b]) * 0.25f;
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            // Back to the authored position, then the smoothed offset, then re-assert -- the only
            // way to be sure the average did not put a point back inside what it was moved out of.
            path[i].x += smoothedOffsets[i].x - offsets[i].x;
            path[i].z += smoothedOffsets[i].y - offsets[i].y;
            const glm::vec2 again = field.heroPushOut(path[i]);
            path[i].x += again.x;
            path[i].z += again.y;
            offsets[i] = smoothedOffsets[i] + again;
        }
    }
    return moved;
}

} // namespace avgen::world
