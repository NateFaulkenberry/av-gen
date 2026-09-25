#include "entity/airborne.hpp"

#include "entity/navigation.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

void Airborne::reset() {
    phase_ = Phase::Grounded;
    arc_ = JumpArc{};
    position_ = glm::vec3(0.0f);
    elapsed_ = 0.0;
    landedFor_ = 0.0;
}

Activity Airborne::activity() const {
    switch (phase_) {
    case Phase::Rising: return Activity::Jump;
    case Phase::Falling: return Activity::Fall;
    case Phase::Landing: return Activity::Land;
    case Phase::Grounded: break;
    }
    return Activity::Idle;
}

// ---- the arc (ADR-822) --------------------------------------------------------------------------

glm::vec3 JumpArc::at(float t) const {
    const float s = duration > 0.0f ? t / duration : 0.0f;
    const float d = to.y - from.y;
    return {from.x + (to.x - from.x) * s, from.y + d * s + curvature * s * (1.0f - s), from.z + (to.z - from.z) * s};
}

glm::vec3 JumpArc::velocityAt(float t) const {
    if (duration <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float s = t / duration;
    const float d = to.y - from.y;
    return {(to.x - from.x) / duration, (d + curvature * (1.0f - 2.0f * s)) / duration, (to.z - from.z) / duration};
}

float JumpArc::apexTime() const {
    if (curvature <= 0.0f) {
        return 0.0f;
    }
    return duration * (to.y - from.y + curvature) / (2.0f * curvature);
}

float JumpArc::horizontalSpeed() const {
    return duration > 0.0f ? glm::length(glm::vec2(to.x - from.x, to.z - from.z)) / duration : 0.0f;
}

std::optional<JumpArc> planJump(glm::vec3 from, glm::vec3 to, float apex, float gravity) {
    const float d = to.y - from.y;
    if (gravity <= 0.0f || apex <= 0.0f || apex < d || glm::length(glm::vec2(to.x - from.x, to.z - from.z)) < 1e-4f) {
        return std::nullopt;
    }
    // h = (d + K)^2 / (4K), solved for K on the branch where the peak lies between the two ends:
    // K = 2h - d + 2 sqrt(h (h - d)).
    const float k = 2.0f * apex - d + 2.0f * std::sqrt(std::max(apex * (apex - d), 0.0f));
    JumpArc arc;
    arc.from = from;
    arc.to = to;
    arc.gravity = gravity;
    arc.apex = apex;
    arc.curvature = k;
    arc.duration = std::sqrt(2.0f * k / gravity);
    return arc;
}

std::optional<float> minimumApex(glm::vec3 from, glm::vec3 to, glm::vec3 centre, float radius, float top,
                                 float clearance) {
    const glm::vec2 a(from.x, from.z);
    const glm::vec2 path(to.x - from.x, to.z - from.z);
    const float length = glm::length(path);
    if (length < 1e-4f) {
        return std::nullopt;
    }
    const glm::vec2 dir = path / length;
    const glm::vec2 rel = glm::vec2(centre.x, centre.z) - a;
    const float along = glm::dot(rel, dir);
    const float across = std::abs(dir.x * rel.y - dir.y * rel.x);
    if (across >= radius) {
        return std::nullopt; // the path passes beside it
    }
    // The stretch of path over the footprint, as fractions of the way across.
    const float half = std::sqrt(radius * radius - across * across);
    const float s0 = (along - half) / length;
    const float s1 = (along + half) / length;
    if (s1 <= 0.0f || s0 >= 1.0f) {
        return std::nullopt; // behind the take-off or past the landing
    }
    const float d = to.y - from.y;
    const float need = top + clearance - from.y;
    // y(s) - y0 = d s + K s (1 - s) >= need at both ends of the stretch (clamped inside the jump; a
    // footprint over the take-off or the landing point cannot be cleared by any apex).
    float k = 0.0f;
    for (const float raw : {s0, s1}) {
        const float s = std::clamp(raw, 1e-3f, 1.0f - 1e-3f);
        k = std::max(k, (need - d * s) / (s * (1.0f - s)));
    }
    k = std::max(k, 1e-4f);
    // The apex that curvature implies, never below the landing.
    return std::max((d + k) * (d + k) / (4.0f * k), std::max(d, 0.0f));
}

ArcCheck checkArc(const JumpArc& arc, const std::function<float(float x, float z)>& groundAt, float sampleSeconds) {
    ArcCheck out;
    out.landingGround = groundAt(arc.to.x, arc.to.z);
    out.landingError = arc.to.y - out.landingGround;
    // The first and last few centimetres of height are the take-off and the landing, where the body
    // is on the ground by definition; everything between must be above it.
    const float step = std::max(sampleSeconds, 1e-3f);
    for (float t = step; t < arc.duration - step * 0.5f; t += step) {
        const glm::vec3 p = arc.at(t);
        if (p.y < groundAt(p.x, p.z) - 0.02f) {
            out.clear = false;
            out.firstContact = t;
            break;
        }
    }
    return out;
}

// ---- the autonomous hop -------------------------------------------------------------------------

bool Airborne::launch(glm::vec3 from, glm::vec3 to, const JumpSettings& settings) {
    if (phase_ != Phase::Grounded) {
        return false; // already committed; a hop is not interruptible by another hop
    }
    const glm::vec2 flat(to.x - from.x, to.z - from.z);
    const float distance = glm::length(flat);
    // Too far to clear, or too near to be worth leaving the ground for. The lower bound matters as
    // much as the upper: a body that hops a quarter of a metre reads as a stumble.
    if (distance > settings.maxDistance || distance < 0.25f) {
        return false;
    }
    // ADR-822: the shared arc, landing exactly on `to`. A bank higher than the body's own apex is
    // reached by rising just above it rather than by an arc that cannot get there.
    const float apex = std::max(std::max(settings.apex, 0.05f), (to.y - from.y) + 0.05f);
    const std::optional<JumpArc> arc = planJump(from, to, apex, std::max(settings.gravity, 0.1f));
    if (!arc) {
        return false;
    }
    arc_ = *arc;
    position_ = from;
    elapsed_ = 0.0;
    landedFor_ = 0.0;
    phase_ = Phase::Rising;
    return true;
}

bool Airborne::update(const Navigator& nav, double dt, glm::vec3& out, const JumpSettings& settings) {
    if (phase_ == Phase::Grounded) {
        return false;
    }
    elapsed_ += dt;

    if (phase_ == Phase::Landing) {
        // Standing again, but not yet walking. The body is held on the ground so the landing clip
        // plays against a surface rather than against wherever the arc happened to stop.
        landedFor_ += dt;
        position_.y = nav.groundHeight(glm::vec2(position_.x, position_.z));
        out = position_;
        if (landedFor_ >= static_cast<double>(settings.landSeconds)) {
            phase_ = Phase::Grounded;
            return false;
        }
        return true;
    }

    // Closed form: the body is where the arc says at this many seconds, whatever steps got it here.
    const auto t = static_cast<float>(elapsed_);
    position_ = arc_.at(t);
    const bool descending = t >= arc_.apexTime();
    phase_ = descending ? Phase::Falling : Phase::Rising;

    const float ground = nav.groundHeight(glm::vec2(position_.x, position_.z));
    // Touchdown only on the way down. Rising through the ground happens when a hop starts inside a
    // bank, and treating that as a landing would end the jump on the frame it began.
    const bool landed = descending && position_.y <= ground;
    // The guard, not a control: an arc that never finds ground -- launched off the edge of the
    // world, or into a hole the terrain does not close -- must not fall for ever.
    const bool expired = elapsed_ >= static_cast<double>(settings.maxSeconds);
    if (landed || expired) {
        position_.y = ground;
        landedFor_ = 0.0;
        phase_ = Phase::Landing;
    }
    out = position_;
    return true;
}

} // namespace avgen::entity
