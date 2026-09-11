#include "entity/navigation.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::entity {
namespace {

constexpr float kTwoPi = 6.283185307179586f;

} // namespace

const char* navRejectName(NavReject reason) {
    switch (reason) {
    case NavReject::None: return "navigable";
    case NavReject::OutOfBounds: return "outside the world";
    case NavReject::TooSteep: return "too steep";
    case NavReject::Submerged: return "under water";
    case NavReject::NoHeadroom: return "no headroom";
    case NavReject::InsideHero: return "inside a hero";
    case NavReject::Step: return "step too high";
    }
    return "unknown";
}

Navigator::Navigator(const world::WorldMap* map, world::ClearanceField field, NavSettings settings)
    : map_(map), field_(field), settings_(settings) {}

float Navigator::groundHeight(glm::vec2 p) const {
    return map_ != nullptr ? map_->height(p) : 0.0f;
}

float Navigator::canopyHeight(glm::vec2 p) const {
    return field_.canopyHeight(p);
}

NavSample Navigator::sample(glm::vec2 p) const {
    NavSample out;
    if (map_ == nullptr) {
        // No terrain: the y = 0 plane, and everything on it is walkable. A scene with no ground is
        // a legitimate scene, and refusing to navigate it would make this class useless there.
        out.navigable = true;
        return out;
    }
    const glm::vec2 lo = map_->min() + settings_.boundaryMargin;
    const glm::vec2 hi = map_->max() - settings_.boundaryMargin;
    if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y) {
        out.reject = NavReject::OutOfBounds;
        return out;
    }
    const world::Sample s = map_->sample(p, 0.5f);
    out.ground = s.height;
    out.slope = s.slope;
    out.canopy = field_.canopyHeight(p);
    if (s.slope > settings_.maxSlope) {
        out.reject = NavReject::TooSteep;
        return out;
    }
    // `waterSurface` is -infinity on dry ground, so this comparison is a no-op there. A walker
    // needs dry land, not merely a bed above the water table: the margin is the difference
    // between standing on a bank and standing ankle-deep in the river.
    if (std::isfinite(s.waterSurface) && s.height < s.waterSurface + settings_.waterMargin) {
        out.reject = NavReject::Submerged;
        return out;
    }
    if (out.canopy > 0.0f && out.canopy < settings_.headroom) {
        // Something grows here and it is shorter than the walker: undergrowth, not a canopy. The
        // tall-canopy case is the walkable one -- a forest floor is walkable, a bramble is not.
        out.reject = NavReject::NoHeadroom;
        return out;
    }
    const glm::vec3 stand(p.x, s.height + settings_.heroMargin, p.y);
    if (field_.heroPenetration(stand) > 0.0f) {
        out.reject = NavReject::InsideHero;
        return out;
    }
    out.navigable = true;
    return out;
}

bool Navigator::pickDestination(Rng& rng, glm::vec2 from, float minRadius, float maxRadius,
                                glm::vec2& out, int attempts) const {
    const float lo = std::max(0.0f, std::min(minRadius, maxRadius));
    const float hi = std::max(lo, maxRadius);
    for (int i = 0; i < std::max(attempts, 1); ++i) {
        const float angle = rng.range(0.0f, kTwoPi);
        // sqrt of the radius fraction, so points are uniform over the annulus rather than bunched
        // at its inner edge -- a wanderer that always chose the near ring would pace, not wander.
        const float t = rng.nextFloat();
        const float radius = std::sqrt(lo * lo + t * (hi * hi - lo * lo));
        const glm::vec2 candidate = from + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        if (sample(candidate).navigable) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool Navigator::pathClear(glm::vec2 a, glm::vec2 b, float spacing) const {
    const float distance = glm::length(b - a);
    const float step = std::max(spacing, 0.25f);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / step)));
    float previousGround = sample(a).ground;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const NavSample s = sample(a + (b - a) * t);
        if (!s.navigable) {
            return false;
        }
        if (std::abs(s.ground - previousGround) > settings_.stepHeight) {
            return false;
        }
        previousGround = s.ground;
    }
    return true;
}

glm::vec2 Navigator::steer(glm::vec2 from, glm::vec2 to, float lookahead) const {
    const glm::vec2 delta = to - from;
    const float distance = glm::length(delta);
    if (distance < 1e-4f) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 direction = delta / distance;
    const float reach = std::min(std::max(lookahead, 0.5f), distance);
    if (pathClear(from, from + direction * reach)) {
        return direction;
    }
    // A fan either side, widening. The first clear deviation wins, and the sides alternate so a
    // walker does not develop a preference for turning one way into every obstacle it meets.
    constexpr std::array<float, 5> kFan{0.35f, 0.7f, 1.05f, 1.4f, 1.9f};
    for (const float spread : kFan) {
        for (const float sign : {-1.0f, 1.0f}) {
            const float angle = spread * sign;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec2 candidate(direction.x * c - direction.y * s, direction.x * s + direction.y * c);
            if (pathClear(from, from + candidate * reach)) {
                return candidate;
            }
        }
    }
    return glm::vec2(0.0f);
}

} // namespace avgen::entity
