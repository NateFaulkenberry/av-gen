#include "entity/airborne.hpp"

#include "entity/navigation.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

void Airborne::reset() {
    phase_ = Phase::Grounded;
    position_ = glm::vec3(0.0f);
    velocity_ = glm::vec3(0.0f);
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
    const float gravity = std::max(settings.gravity, 0.1f);
    const float apex = std::max(settings.apex, 0.05f);

    // The one piece of arithmetic here. Rise time comes from the apex, `v0 = sqrt(2 g h)`; the fall
    // is however long the ground takes to arrive, which is why the horizontal speed is set from the
    // *rise* doubled rather than from a solved total. Landing lower than take-off therefore lands
    // slightly long, and landing higher slightly short, which is what a real hop does too.
    const float rise = std::sqrt(2.0f * apex / gravity);
    const float airTime = std::max(rise * 2.0f, 0.05f);
    const glm::vec2 horizontal = distance > 1e-4f ? flat / distance * (distance / airTime) : glm::vec2(0.0f);

    position_ = from;
    velocity_ = glm::vec3(horizontal.x, std::sqrt(2.0f * gravity * apex), horizontal.y);
    elapsed_ = 0.0;
    landedFor_ = 0.0;
    phase_ = Phase::Rising;
    return true;
}

bool Airborne::update(const Navigator& nav, double dt, glm::vec3& out, const JumpSettings& settings) {
    if (phase_ == Phase::Grounded) {
        return false;
    }
    const auto step = static_cast<float>(dt);
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

    velocity_.y -= std::max(settings.gravity, 0.1f) * step;
    position_ += velocity_ * step;
    phase_ = velocity_.y > 0.0f ? Phase::Rising : Phase::Falling;

    const float ground = nav.groundHeight(glm::vec2(position_.x, position_.z));
    // Touchdown only on the way down. Rising through the ground happens when a hop starts inside a
    // bank, and treating that as a landing would end the jump on the frame it began.
    const bool landed = velocity_.y <= 0.0f && position_.y <= ground;
    // The guard, not a control: an arc that never finds ground -- launched off the edge of the
    // world, or into a hole the terrain does not close -- must not fall for ever.
    const bool expired = elapsed_ >= static_cast<double>(settings.maxSeconds);
    if (landed || expired) {
        position_.y = ground;
        velocity_ = glm::vec3(0.0f);
        landedFor_ = 0.0;
        phase_ = Phase::Landing;
    }
    out = position_;
    return true;
}

} // namespace avgen::entity
