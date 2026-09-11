#include "ui/world_probe.hpp"

#include "entity/placement.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::ui {
namespace {

// The scene's terrain, or nullptr. The first one: a composition with two terrains is not a thing
// the world builder makes, and picking the first is at least predictable.
scene::CompositionNode* terrainOf(scene::Composition& composition) {
    for (const auto& node : composition.nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            return node.get();
        }
    }
    return nullptr;
}

// How far the terrain node's own transform lifts the map. `TerrainQuery` answers in the map's frame
// -- it is a join over a `WorldMap`, which knows nothing about where the node carrying it was
// placed -- so the one thing this file has to add back is the node's world y.
float liftOf(scene::Composition& composition, const scene::CompositionNode* terrain) {
    return terrain != nullptr ? composition.nodeWorldTransform(*terrain).position.y : 0.0f;
}

bool rayBox(const ViewRay& ray, const glm::vec3& lo, const glm::vec3& hi, float maxDistance, float& t) {
    float tMin = 0.0f;
    float tMax = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        const float d = ray.direction[axis];
        if (std::abs(d) < 1e-8f) {
            if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis]) {
                return false;
            }
            continue;
        }
        float t0 = (lo[axis] - ray.origin[axis]) / d;
        float t1 = (hi[axis] - ray.origin[axis]) / d;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) {
            return false;
        }
    }
    t = tMin;
    return true;
}

} // namespace

ViewRay rayThroughNdc(const scene::Camera& camera, float aspect, glm::vec2 ndc) {
    ViewRay ray;
    ray.origin = camera.position;
    ray.direction = entity::rayThrough(camera, aspect, ndc);
    return ray;
}

Projected projectPoint(const scene::Camera& camera, float aspect, glm::vec3 world) {
    Projected out;
    const glm::mat4 view = camera.view();
    const glm::vec4 eye = view * glm::vec4(world, 1.0f);
    // In this convention the camera looks down -z in view space, so anything the camera can see has
    // a negative z. A point at or behind the eye has no honest screen position at all, and
    // projecting it anyway produces a mirrored one that is indistinguishable from a real answer.
    out.depth = -eye.z;
    out.inFront = out.depth > camera.nearPlane;
    const glm::vec4 clip = camera.projection(aspect) * eye;
    if (std::abs(clip.w) > 1e-6f) {
        out.ndc = glm::vec2(clip.x / clip.w, clip.y / clip.w);
    }
    return out;
}

GroundSample sampleGroundAt(const world::TerrainQuery& query, float lift, glm::vec2 xz) {
    GroundSample out;
    out.valid = true;
    if (!query.valid()) {
        // No terrain. The ground is y = 0, and a brush has to work in an empty scene because an
        // empty scene is where somebody finds out what the brush does.
        out.position = glm::vec3(xz.x, lift, xz.y);
        return out;
    }
    // One call, one set of noise evaluations. Asking for height, then slope, then water separately
    // costs three; `TerrainPoint` is the shape that exists so callers do not.
    const world::TerrainPoint p = query.at(xz);
    out.position = glm::vec3(xz.x, p.height + lift, xz.y);
    out.normal = p.normal;
    out.slopeDegrees = glm::degrees(std::acos(std::clamp(p.normal.y, -1.0f, 1.0f)));
    out.submerged = p.water;
    out.waterDepth = p.waterDepth;
    out.canopyHeight = p.canopy;
    out.insideWorld = query.inBounds(xz);
    out.reject = p.reject;
    out.walkable = p.walkable;
    out.hasTerrain = true;
    return out;
}

GroundSample sampleGroundAt(scene::Composition& composition, glm::vec2 xz) {
    return sampleGroundAt(composition.terrainQuery(), liftOf(composition, terrainOf(composition)), xz);
}

std::optional<GroundSample> nearestPlaceable(scene::Composition& composition, glm::vec2 xz, float radius,
                                             float searchRadius) {
    const world::TerrainQuery query = composition.terrainQuery();
    if (!query.valid()) {
        return std::nullopt;
    }
    const std::optional<glm::vec2> found = query.nearestValidPoint(xz, radius, searchRadius);
    if (!found) {
        return std::nullopt;
    }
    return sampleGroundAt(query, liftOf(composition, terrainOf(composition)), *found);
}

GroundSample sampleGroundAlong(scene::Composition& composition, const ViewRay& ray, float maxDistance) {
    GroundSample out;
    scene::CompositionNode* terrain = terrainOf(composition);
    if (terrain == nullptr) {
        // No terrain: the ground is y = 0. A brush has to work in an empty scene, because an empty
        // scene is where somebody finds out what the brush does.
        if (ray.direction.y >= -1e-5f) {
            return out;
        }
        const float t = -ray.origin.y / ray.direction.y;
        if (t < 0.0f || t > maxDistance) {
            return out;
        }
        out.valid = true;
        out.distance = t;
        out.position = ray.origin + ray.direction * t;
        return out;
    }

    const world::WorldMap& map = terrain->worldMap;
    const world::TerrainQuery query = composition.terrainQuery();
    const float lift = liftOf(composition, terrain);
    const auto heightAt = [&](glm::vec2 xz) { return query.heightAt(xz) + lift; };

    // A fixed-step march with a bisection at the crossing. The step is a fraction of the terrain's
    // cell size rather than a fraction of the distance: a coarse step steps over a ridge and lands
    // the cursor on the valley behind it, which reads as the ghost jumping about rather than as an
    // undersampled march. Two metres holds for everything the world builder generates and costs
    // about a thousand closed-form height evaluations over a kilometre -- microseconds, on the CPU,
    // once a frame.
    const float step = std::max(1.0f, std::min(map.size.x, map.size.y) / 256.0f);
    const float reach = std::min(maxDistance, glm::length(map.size) * 2.0f + 500.0f);

    float previousT = 0.0f;
    float previousGap = ray.origin.y - heightAt(glm::vec2(ray.origin.x, ray.origin.z));
    if (previousGap < 0.0f) {
        // The eye is already underground. Marching from here finds the far wall of the hill, which
        // is not what the cursor is pointing at; the honest answer is that there is no surface in
        // front of the camera.
        return out;
    }
    for (float t = step; t <= reach; t += step) {
        const glm::vec3 p = ray.origin + ray.direction * t;
        const float gap = p.y - heightAt(glm::vec2(p.x, p.z));
        if (gap <= 0.0f) {
            // Bisect between the last point above the surface and this one below it. Twenty halvings
            // of a two-metre bracket is a micrometre, so the loop is bounded by precision and not by
            // a tolerance somebody has to tune.
            float lo = previousT;
            float hi = t;
            for (int i = 0; i < 20; ++i) {
                const float mid = 0.5f * (lo + hi);
                const glm::vec3 q = ray.origin + ray.direction * mid;
                if (q.y - heightAt(glm::vec2(q.x, q.z)) > 0.0f) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            const glm::vec3 q = ray.origin + ray.direction * hi;
            out = sampleGroundAt(query, lift, glm::vec2(q.x, q.z));
            out.distance = hi;
            return out;
        }
        previousT = t;
        previousGap = gap;
    }
    return out;
}

ObjectHit pickNodeAlong(scene::Composition& composition, const ViewRay& ray, float maxDistance) {
    ObjectHit best;
    float bestT = maxDistance;
    for (const auto& node : composition.nodes()) {
        if (!node || node->kind == scene::NodeKind::Terrain) {
            continue; // terrain is ground, not an object; sampleGroundAlong answers for it
        }
        const scene::WorldBounds bounds = composition.nodeBounds(node->name);
        if (!bounds.valid) {
            continue;
        }
        float t = 0.0f;
        if (rayBox(ray, bounds.min, bounds.max, maxDistance, t) && t < bestT) {
            bestT = t;
            best.hit = true;
            best.node = node->name;
            best.distance = t;
            best.position = ray.origin + ray.direction * t;
        }
    }
    return best;
}

std::vector<std::string> nodesInScreenRect(scene::Composition& composition, const scene::Camera& camera,
                                           float aspect, glm::vec2 ndcMin, glm::vec2 ndcMax) {
    std::vector<std::string> out;
    const glm::vec2 lo = glm::min(ndcMin, ndcMax);
    const glm::vec2 hi = glm::max(ndcMin, ndcMax);
    for (const auto& node : composition.nodes()) {
        if (!node || node->kind == scene::NodeKind::Terrain) {
            continue;
        }
        const scene::WorldBounds bounds = composition.nodeBounds(node->name);
        if (!bounds.valid) {
            continue;
        }
        // The box's eight corners, not its centre. A tree whose trunk is outside the drag box and
        // whose canopy fills it is a tree the artist meant to catch, and a centre test misses every
        // large object a box is drawn *inside* of.
        glm::vec2 screenLo(0.0f);
        glm::vec2 screenHi(0.0f);
        bool any = false;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? bounds.max.x : bounds.min.x,
                              (corner & 2) ? bounds.max.y : bounds.min.y,
                              (corner & 4) ? bounds.max.z : bounds.min.z);
            const Projected projected = projectPoint(camera, aspect, p);
            if (!projected.inFront) {
                continue; // a corner behind the eye has no screen position; the others still do
            }
            screenLo = any ? glm::min(screenLo, projected.ndc) : projected.ndc;
            screenHi = any ? glm::max(screenHi, projected.ndc) : projected.ndc;
            any = true;
        }
        if (!any) {
            continue;
        }
        const bool overlaps = screenHi.x >= lo.x && screenLo.x <= hi.x && screenHi.y >= lo.y &&
                              screenLo.y <= hi.y;
        if (overlaps) {
            out.push_back(node->name);
        }
    }
    return out;
}

} // namespace avgen::ui
