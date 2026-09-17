#include "world/camera_clearance.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

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

namespace {

// Where the ground is, for a sightline. The same `max(height, waterSurface)` `minimumHeight` uses:
// a camera cannot see a hero through a lake any more than it can sit in one.
float surfaceAt(const WorldMap& map, glm::vec2 p) {
    const Sample s = map.sample(p, 0.5f);
    return std::max(s.height, s.waterSurface);
}

// The radius that divides a disc into two equal areas, r/sqrt(2). See the header: it is what makes
// three samples across a silhouette stand for three roughly equal thirds of it.
const float kEqualAreaRadius = 1.0f / std::numbers::sqrt2_v<float>;

} // namespace

Sightline heroSightline(const ClearanceField& field, glm::vec3 eye, const SubjectCapsule& subject,
                        float stepMetres) {
    Sightline out;
    if (field.map == nullptr) {
        return out;
    }
    const float step = std::max(stepMetres, 0.05f);
    const glm::vec3 centre = subject.position + glm::vec3(0.0f, subject.height * 0.5f, 0.0f);
    const glm::vec3 toSubject = centre - eye;
    const float span = glm::length(glm::vec2(toSubject.x, toSubject.z));
    if (span < 1e-3f) {
        return out;   // directly overhead: there is no horizontal silhouette to sample across
    }
    // Across the silhouette, not across the world: perpendicular to the eye's bearing, horizontally.
    const glm::vec2 right = glm::vec2(-toSubject.z, toSubject.x) / span;

    int rays = 0;
    int blocked = 0;
    int throughCanopy = 0;
    for (const float h : {0.2f, 0.5f, 0.8f}) {
        for (const float lateral : {-kEqualAreaRadius, 0.0f, kEqualAreaRadius}) {
            const glm::vec3 aim = subject.position
                                + glm::vec3(0.0f, subject.height * h, 0.0f)
                                + glm::vec3(right.x, 0.0f, right.y) * (lateral * subject.radius);
            const glm::vec3 ray = aim - eye;
            const float length = glm::length(ray);
            ++rays;
            // The subject's own body is not an obstruction between the camera and the subject.
            const float reach = length - subject.radius;
            if (reach <= step) {
                continue;   // already inside it; there is no sightline left to obstruct
            }
            const int steps = std::max(static_cast<int>(reach / step), 1);
            bool rayBlocked = false;
            float canopyRun = 0.0f;
            bool inCanopy = false;
            for (int i = 1; i <= steps; ++i) {
                const float s = (static_cast<float>(i) / static_cast<float>(steps)) * (reach / length);
                const glm::vec3 p = eye + ray * s;
                const glm::vec2 xz(p.x, p.z);
                const float ground = surfaceAt(*field.map, xz);
                const float lever = std::max(1.0f - s, 1e-3f);

                if (p.y < ground + field.cameraRadius) {
                    // `cameraRadius` and not zero, and the margin is not decoration. The lever gives
                    // the lift that puts the ray *exactly on* the obstruction, and a sightline
                    // tangent to a hillside is a sightline with a hillside across the bottom of it:
                    // the first version cleared 8 of 9 rays and left the ninth grazing, which is the
                    // arithmetic being right and the answer being wrong. The margin reused is the
                    // field's own, which exists for the same reason one step up -- "a lens sitting
                    // exactly on a surface still shows it filling the frame".
                    rayBlocked = rayBlocked || p.y < ground;
                    out.requiredLift =
                        std::max(out.requiredLift, (ground + field.cameraRadius - p.y) / lever);
                }
                // Another hero's capsule. Raw geometry with no `cameraRadius` padding: this asks
                // what an eye can *see* past, not where an eye may *sit*.
                for (const HeroPoint& hero : field.heroes) {
                    if (hero.name == subject.name) {
                        continue;
                    }
                    const float above = p.y - hero.position.y;
                    if (above < 0.0f || above > hero.height) {
                        continue;
                    }
                    const glm::vec2 away = xz - glm::vec2(hero.position.x, hero.position.z);
                    const float planar = glm::length(away);
                    if (planar >= hero.radius) {
                        continue;
                    }
                    rayBlocked = true;
                    out.blocker = hero.name;
                    // Two ways out, and the cheaper one wins -- which is a minimality criterion
                    // rather than a preference. Over the top: the eye must raise the ray above
                    // `hero.position.y + hero.height`. Around the side: it must push the ray out to
                    // `hero.radius`. Both through the same (1 - s) lever.
                    const float over =
                        ((hero.position.y + hero.height + field.cameraRadius) - p.y) / lever;
                    const float around = (hero.radius + field.cameraRadius - planar) / lever;
                    if (over <= around) {
                        out.requiredLift = std::max(out.requiredLift, over);
                    } else {
                        const glm::vec2 dir = planar > 1e-4f ? away / planar : glm::vec2(1.0f, 0.0f);
                        if (around > glm::length(out.requiredPush)) {
                            out.requiredPush = dir * around;
                        }
                    }
                }
                // The canopy, measured and not acted on.
                const float canopyTop = ground + field.canopyHeight(xz);
                const bool insideCanopy = p.y > ground && p.y < canopyTop;
                if (insideCanopy) {
                    canopyRun += length / static_cast<float>(steps);
                }
                inCanopy = inCanopy || insideCanopy;
            }
            blocked += rayBlocked ? 1 : 0;
            throughCanopy += inCanopy ? 1 : 0;
            out.canopyMetres = std::max(out.canopyMetres, canopyRun);
        }
    }
    if (rays == 0) {
        return out;
    }
    out.visible = 1.0f - static_cast<float>(blocked) / static_cast<float>(rays);
    out.throughCanopy = static_cast<float>(throughCanopy) / static_cast<float>(rays);
    if (out.clear()) {
        // A clear view needs no correction, whatever a partially-blocked ray asked for along the way.
        out.requiredLift = 0.0f;
        out.requiredPush = glm::vec2(0.0f);
        out.blocker.clear();
    }
    return out;
}

SightlineResult clearSightlines(const ClearanceField& field, std::vector<glm::vec3>& path,
                                std::span<const SightlineTarget> targets, float maxCorrectionRatio,
                                int smoothingPasses) {
    SightlineResult out;
    if (path.empty() || targets.size() != path.size()) {
        return out;
    }
    // The y each key ended at, so the smoothing below can be told never to go under it -- the same
    // device `clearPath` uses, and needed here for the same reason.
    std::vector<float> floors(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        floors[i] = path[i].y;
    }
    bool anyApplied = false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!targets[i].holds) {
            continue;
        }
        ++out.examined;
        const Sightline sight = heroSightline(field, path[i], targets[i].subject);
        if (sight.clear()) {
            continue;
        }
        ++out.obstructed;
        const float distance = glm::length(targets[i].subject.position - path[i]);
        const float budget = std::max(distance, 1.0f) * std::max(maxCorrectionRatio, 0.0f);
        const float push = glm::length(sight.requiredPush);
        const float correction = std::max(sight.requiredLift, push);
        if (correction > budget) {
            ++out.refused;
            out.worstRefused = std::max(out.worstRefused, correction);
            continue;
        }
        ++out.corrected;
        out.largestApplied = std::max(out.largestApplied, correction);
        anyApplied = true;
        path[i].y += sight.requiredLift;
        path[i].x += sight.requiredPush.x;
        path[i].z += sight.requiredPush.y;
        // Re-assert the clearance floor at wherever the correction left the key. A lift can only
        // help it; a lateral push can land the eye over lower ground or inside a hero, and a key
        // that sees its subject from inside a hillside is not an improvement.
        const ClearanceAdjustment again = clearPoint(field, path[i]);
        path[i] = again.position;
        floors[i] = path[i].y;
    }
    if (!anyApplied) {
        return out;   // nothing moved; leave the path byte for byte as `clearPath` left it
    }
    // Only ever raises. Raising the eye raises every point on a ray to a fixed target -- that is the
    // same (1 - s) lever the correction was derived from -- so a pass that only raises cannot
    // re-block a sightline it cleared.
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
    return out;
}

} // namespace avgen::world
