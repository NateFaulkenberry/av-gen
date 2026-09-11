#include "entity/placement.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <vector>
#include <cmath>

namespace avgen::entity {
namespace {

struct Frame {
    glm::vec3 eye{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float tanHalfY = 0.36f;
    float tanHalfX = 0.64f;
    glm::mat4 viewProjection{1.0f};
    float aspect = 1.0f;
};

Frame frameOf(const scene::Camera& camera, float aspect) {
    Frame f;
    f.eye = camera.position;
    f.forward = glm::normalize(camera.target - camera.position);
    f.right = glm::normalize(glm::cross(f.forward, camera.up));
    f.up = glm::cross(f.right, f.forward);
    f.tanHalfY = std::tan(camera.effectiveFovY() * 0.5f);
    f.tanHalfX = f.tanHalfY * aspect;
    f.viewProjection = camera.projection(aspect) * camera.view();
    f.aspect = aspect;
    return f;
}

glm::vec3 project(const Frame& f, glm::vec3 p) {
    const glm::vec4 clip = f.viewProjection * glm::vec4(p, 1.0f);
    if (clip.w <= 0.0f) {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.w);
}

float screenRadiusOf(const Frame& f, glm::vec3 p, float radius) {
    const float distance = glm::length(p - f.eye);
    if (distance <= radius) {
        return 10.0f;
    }
    return std::atan(radius / distance) / std::atan(f.tanHalfY);
}

// Saturating reward: 0 at 0, approaching 1, half at `half`. Used everywhere a term should stop
// paying once it is comfortable rather than dominating the sum.
float soften(float x, float half) {
    const float v = std::max(x, 0.0f);
    return v / (v + std::max(half, 1e-3f));
}

} // namespace

std::string PlacementRejects::summary() const {
    return fmt::format(
        "off screen {}, too small {}, too large {}, no headroom {}, inside a hero {}, crowds a hero {}, "
        "not skylined {}, bad ground {}, outside the world {}",
        offScreen, tooSmall, tooLarge, noHeadroom, insideHero, crowdsHero, notSkylined, badGround,
        outOfWorld);
}

glm::vec3 rayThrough(const scene::Camera& camera, float aspect, glm::vec2 ndc) {
    const Frame f = frameOf(camera, aspect);
    return glm::normalize(f.forward + f.right * (ndc.x * f.tanHalfX) + f.up * (ndc.y * f.tanHalfY));
}

PlacementResult findPlacement(const PlacementBrief& brief) {
    PlacementResult best;
    if (brief.camera == nullptr || brief.map == nullptr || brief.field == nullptr) {
        return best;
    }
    const Frame frame = frameOf(*brief.camera, brief.aspect);
    const glm::vec2 worldMin = brief.map->min();
    const glm::vec2 worldMax = brief.map->max();

    // Where the heroes already are in the frame, so a candidate can be kept off them. Computed
    // once: this is the same for every sample.
    struct FramedHero {
        glm::vec2 ndc{0.0f};
        float radius = 0.0f;
        bool framed = false;
    };
    std::vector<FramedHero> framed;
    framed.reserve(brief.heroes.size());
    for (const world::HeroPoint& hero : brief.heroes) {
        // The middle of the hero's body, which is what a viewer reads it as occupying.
        const glm::vec3 centre = hero.position + glm::vec3(0.0f, hero.height * 0.5f, 0.0f);
        const glm::vec3 ndc = project(frame, centre);
        FramedHero f;
        f.framed = ndc.z > 0.0f;
        f.ndc = glm::vec2(ndc.x, ndc.y);
        f.radius = screenRadiusOf(frame, centre, std::max(hero.radius, hero.height * 0.5f));
        framed.push_back(f);
    }

    Rng rng(brief.seed);
    for (int i = 0; i < std::max(brief.samples, 1); ++i) {
        const glm::vec2 ndc = brief.screenTarget +
                              glm::vec2(rng.range(-brief.screenSpread, brief.screenSpread),
                                        rng.range(-brief.screenSpread, brief.screenSpread));
        if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) {
            ++best.rejects.offScreen;
            continue;
        }
        const float distance = rng.range(brief.minDistance, brief.maxDistance);
        const glm::vec3 p = frame.eye + rayThrough(*brief.camera, brief.aspect, ndc) * distance;
        const glm::vec2 flat(p.x, p.z);
        if (flat.x < worldMin.x || flat.x > worldMax.x || flat.y < worldMin.y || flat.y > worldMax.y) {
            ++best.rejects.outOfWorld;
            continue;
        }

        const float screenRadius = screenRadiusOf(frame, p, brief.radius);
        if (screenRadius < brief.minScreenRadius) {
            ++best.rejects.tooSmall;
            continue;
        }
        if (screenRadius > brief.maxScreenRadius) {
            ++best.rejects.tooLarge;
            continue;
        }
        // The whole disc has to be inside the frame, not only its centre.
        if (std::abs(ndc.x) + screenRadius / brief.aspect > 0.9f || std::abs(ndc.y) + screenRadius > 0.9f) {
            ++best.rejects.offScreen;
            continue;
        }

        const world::Sample ground = brief.map->sample(flat, 0.5f);
        const float canopy = brief.field->canopyHeight(flat);
        const float underside = p.y - brief.halfHeight;
        const float headroom = underside - (ground.height + canopy);
        if (headroom < brief.clearanceBelow) {
            ++best.rejects.noHeadroom;
            continue;
        }
        if (ground.slope > brief.maxGroundSlope || ground.submerged || canopy > 6.0f) {
            ++best.rejects.badGround;
            continue;
        }

        // Not inside a hero, with the thing's own radius as its personal space -- the same capsule
        // test a directed camera is kept out of them by.
        world::ClearanceField wide = *brief.field;
        wide.cameraRadius = brief.radius;
        if (wide.heroPenetration(p) > 0.0f) {
            ++best.rejects.insideHero;
            continue;
        }

        // Not on top of one in the frame either. Two subjects overlapping read as one confused
        // subject, and no amount of depth separation fixes it in a still frame.
        bool crowds = false;
        float nearestSeparation = 4.0f;
        for (const FramedHero& hero : framed) {
            if (!hero.framed) {
                continue;
            }
            const glm::vec2 delta((ndc.x - hero.ndc.x) * brief.aspect, ndc.y - hero.ndc.y);
            const float gap = glm::length(delta) - (hero.radius + screenRadius);
            if (gap < 0.0f) {
                crowds = true;
                break;
            }
            nearestSeparation = std::min(nearestSeparation, gap);
        }
        if (crowds) {
            ++best.rejects.crowdsHero;
            continue;
        }

        if (brief.requireSkyline) {
            bool clear = true;
            constexpr int kSteps = 48;
            for (int k = 1; k < kSteps && clear; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(kSteps);
                const glm::vec3 s = frame.eye + (p - frame.eye) * t;
                const glm::vec2 sp(s.x, s.z);
                clear = s.y > brief.map->height(sp) + 1.0f + brief.field->canopyHeight(sp);
            }
            if (!clear) {
                ++best.rejects.notSkylined;
                continue;
            }
            // And nothing behind it either, out to the edge of the world: that is what makes it a
            // silhouette rather than an object in front of a hillside.
            for (int k = 1; k <= kSteps && clear; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(kSteps);
                const glm::vec3 s = frame.eye + (p - frame.eye) * (1.0f + t * 3.0f);
                const glm::vec2 sp(s.x, s.z);
                if (sp.x < worldMin.x || sp.x > worldMax.x || sp.y < worldMin.y || sp.y > worldMax.y) {
                    break;
                }
                clear = s.y > brief.map->height(sp) + brief.field->canopyHeight(sp);
            }
            if (!clear) {
                ++best.rejects.notSkylined;
                continue;
            }
        }

        // Every hard constraint is met; rank what is left. The weights say what a good placement
        // is: mostly "where the frame wanted it", then "clear of the other subject", then "high
        // enough over open ground for the beam to be a beam".
        const float wantedSize = 0.5f * (brief.minScreenRadius + brief.maxScreenRadius);
        const float sizeError = std::abs(screenRadius - wantedSize) / std::max(wantedSize, 1e-3f);
        const float score = 3.0f * (1.0f - soften(glm::length(ndc - brief.screenTarget), 0.35f)) +
                            2.0f * soften(nearestSeparation, 0.18f) +
                            1.5f * soften(headroom - brief.clearanceBelow, 25.0f) +
                            1.0f * (1.0f - soften(ground.slope, 0.25f)) +
                            1.0f * (1.0f - soften(canopy, 4.0f)) +
                            0.8f * (1.0f - std::min(sizeError, 1.0f));
        if (!best.found || score > best.score) {
            const PlacementRejects rejects = best.rejects;
            best.found = true;
            best.position = p;
            best.score = score;
            best.screenRadius = screenRadius;
            best.ndc = ndc;
            best.distance = distance;
            best.groundBelow = ground.height;
            best.canopyBelow = canopy;
            best.headroom = headroom;
            best.rejects = rejects;
        }
    }
    return best;
}

} // namespace avgen::entity
