#include "entity/grounding.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

constexpr float kDegrees = 57.2957795f;

// A one-pole coefficient for a time constant, independent of the frame rate. The same shape the
// rest of the behaviour layer uses, so a character grounded at 30 fps and the same character
// grounded at 120 lands in the same place.
float lerpRate(float ms, double dt) {
    if (ms <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - std::exp(-static_cast<float>(dt) / (ms * 0.001f));
}

} // namespace

GroundResult GroundFollower::update(const Navigator& nav, glm::vec2 p, float yaw, float speed,
                                    double dt, const GroundSettings& settings) {
    GroundResult out;
    if (!nav.valid()) {
        // No terrain is a legitimate world. Report the y = 0 plane and stay upright rather than
        // refusing to ground, which would leave a character wherever the scene file put it.
        out.height = 0.0f;
        out.grounded = true;
        height_ = 0.0f;
        pitch_ = 0.0f;
        roll_ = 0.0f;
        primed_ = true;
        return out;
    }

    // Read the ground over the body's own footprint, not under a point. Everything a walker stands
    // on that is smaller than the walker is detail it should ignore: a boot bridges a wrinkle, and a
    // body that tracked one twitches at whatever rate it is travelling. Four points on the footprint
    // circle plus the centre, plus one ahead in the direction of travel so a rise is anticipated
    // rather than climbed into.
    const glm::vec2 heading(std::sin(yaw), std::cos(yaw));
    const glm::vec2 right(heading.y, -heading.x);
    const float radius = std::max(settings.footprint, 0.0f);
    const float centre = nav.groundHeight(p);
    float sum = centre * 2.0f;
    float weight = 2.0f;
    float lowest = centre;
    const auto take = [&](glm::vec2 at, float w) {
        const float h = nav.groundHeight(at);
        sum += h * w;
        weight += w;
        lowest = std::min(lowest, h);
    };
    if (radius > 0.01f) {
        take(p + heading * radius, 1.0f);
        take(p - heading * radius, 1.0f);
        take(p + right * radius, 1.0f);
        take(p - right * radius, 1.0f);
    }
    const float ahead = std::max(speed, 0.0f) * std::max(settings.lookaheadSeconds, 0.0f);
    if (ahead > 0.05f) {
        // The lookahead informs the height but must not widen the band: a body is not standing
        // where it is about to be, and letting it set `highest` would let it climb early.
        const float front = nav.groundHeight(p + heading * ahead);
        sum += front * 1.5f;
        weight += 1.5f;
    }
    // The filtered surface, and never below the point the body's own origin is over. On a straight
    // slope the footprint mean *is* the centre sample, so this follows a hillside exactly; over a
    // dip the mean is higher and the body bridges it; over a bump the centre is higher and the body
    // stands on it. Biasing toward the footprint maximum instead was tried and lifts a body off
    // every slope it climbs, because the high side of a footprint on a hill is just the hill.
    const float mean = sum / weight;
    const float target = std::max(mean, centre);
    // Sampled as wide as the body, so the lean comes from the slope the body spans rather than from
    // whatever the noise is doing at one point of it.
    const glm::vec3 normal = nav.groundNormal(p, std::max(radius, 0.25f));

    if (!primed_) {
        primed_ = true;
        height_ = target;
    } else {
        height_ += (target - height_) * lerpRate(settings.heightSmoothingMs, dt);
    }
    // The band, and it is measured against the footprint rather than against the point under the
    // body's origin. That distinction is the whole difference between a filter and a snap: clamping
    // to the centre sample forces the body to trace every upward wrinkle exactly, which is the
    // surface's own noise arriving by another route -- an earlier version did that and reduced
    // vertical acceleration by six percent.
    //
    // What is actually forbidden: the body may not sit below the lowest ground its own footprint
    // covers, and may not float more than `maxFloat` above the *filtered* surface. Stating the
    // ceiling against the mean rather than against the raw sample is what lets a spike be cut --
    // clamping to the centre sample hands the body every upward wrinkle and reduced vertical
    // acceleration by six percent instead of by half.
    const float floor = lowest - std::max(settings.maxSink, 0.0f);
    const float ceiling = std::max(mean + std::max(settings.maxFloat, 0.0f), floor);
    height_ = std::clamp(height_, floor, ceiling);
    out.height = height_;
    out.grounded = true;

    // Orientation. The surface normal in the body's own frame: its component along the heading is
    // pitch, along the right-hand axis is roll.
    const float align = std::clamp(settings.slopeAlign, 0.0f, 1.0f);
    float wantPitch = 0.0f;
    float wantRoll = 0.0f;
    if (align > 0.0f && normal.y > 1e-3f) {
        const glm::vec2 right(heading.y, -heading.x);
        const glm::vec2 flat(normal.x, normal.z);
        // atan of the normal's tilt resolved onto each axis. Negated on pitch so a normal leaning
        // backwards (the body facing uphill) raises the nose.
        wantPitch = -std::atan2(glm::dot(flat, heading), normal.y) * kDegrees * align;
        wantRoll = std::atan2(glm::dot(flat, right), normal.y) * kDegrees * align;
        const float limit = std::max(settings.maxTilt, 0.0f);
        wantPitch = std::clamp(wantPitch, -limit, limit);
        wantRoll = std::clamp(wantRoll, -limit, limit);
    }
    const float tiltRate = lerpRate(settings.slopeSmoothingMs, dt);
    pitch_ += (wantPitch - pitch_) * tiltRate;
    roll_ += (wantRoll - roll_) * tiltRate;
    out.pitch = pitch_;
    out.roll = roll_;
    return out;
}

} // namespace avgen::entity
