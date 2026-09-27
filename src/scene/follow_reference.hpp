#pragma once

// The subject reference a follow camera reads (ADR-911).
//
// A follow rig used to read its subject's node directly: the eye stood at the node (or, with a lag,
// at where a per-frame trail said the node had been) and the aim looked at the node. The node is
// the body's drawn position, which carries the stride bob -- about 1 Hz, and on GV3 made larger by
// an audio reaction on the bounce -- and every start, stop and turn one to one. The audit measured
// what that does on screen: a lag on the eye alone turns the bob into a 1.1-1.2 degree nod, and a
// rig with no lag translates rigidly, bobbing 24-35 cm with the world bouncing behind a pinned
// subject.
//
// This file is the replacement, and it is deliberately a pure function. The reference at time `t`
// is a weighted sum of the subject's own past: its transform history (HIST, ADR-703) through a
// causal, finite, critically damped kernel
//
//     h(tau) = w^2 tau e^(-w tau),   w = 2 / T,   truncated at tau = 8 / w = 4 T,
//
// whose mean delay is exactly T. There is no integrator, so nothing depends on how the playhead got
// to `t`: a play to `t` and a seek to `t` hold the same history (ADR-700 replays it and carries it
// in every checkpoint), so they compute the same reference. That is the property a spring could not
// have, and the reason this is a kernel and not a spring.
//
// The kernel is sampled on HIST's own grid (1/60 s) and binned: tap k weighs the kernel's integral
// over [k - 1/2, k + 1/2] steps, from the closed-form distribution function
// C(tau) = 1 - e^(-w tau)(1 + w tau). Binned rather than point-sampled so that T -> 0 approaches
// the raw subject continuously (tap 0's weight tends to 1) instead of collapsing to a one-step
// delay, and so the weights need no table: each is two exponentials, computed where it is used.

#include "world/effects/history_bank.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>

namespace avgen::scene {

// The kernel's tap spacing: HIST's grid, so a 60 Hz play and the seek replay put a sample under
// every tap.
inline constexpr double kFollowKernelStep = world::HistoryBank::kGridStep;
// Where the kernel is cut off, in mean delays: 8 / w = 4 T. Past it lies 0.3% of the weight
// (e^-8 (1 + 8)), which the normalisation hands back to the taps that are kept.
inline constexpr double kFollowKernelSupport = 4.0;
// The clearance floor's softness, in metres: the width of the softplus that replaced `max`.
inline constexpr float kFollowFloorSoftness = 0.25f;
// Radius of the footprint the ground is read over for `FollowFilter::ground`, in metres. Wider than
// a body (whose own grounding already bridges wrinkles smaller than its feet) because what the
// camera follows is the hill, not the ground under one step of it. Measured on GV3's walkers: the
// ground under Vane in s19 carries 2.3 cm of relief at walking frequencies, and at 1.5 m the eye
// kept 2.06 cm of it; wider than about 3.5 m and a change of slope is followed late enough to show
// in the framing.
inline constexpr float kFollowGroundRadius = 2.5f;

// What one rig asks of its subject reference. All zero is the raw subject.
struct FollowFilter {
    double horizontalSeconds = 0.0; // T for X and Z
    double verticalSeconds = 0.0;   // T for Y
    double headingSeconds = 0.0;    // T for the yaw `followLocal` turns its offset by
    float lead = 0.0f;              // 0..1: lead * T * (filtered horizontal velocity), X and Z only
    bool ground = false;            // Y relative to the ground under the subject

    [[nodiscard]] bool smooths() const {
        return horizontalSeconds > 0.0 || verticalSeconds > 0.0 || headingSeconds > 0.0;
    }
};

// How far back a kernel of mean delay `seconds` reads, in seconds: its last tap. 0 for 0.
[[nodiscard]] double followKernelReach(double seconds);
// Tap k's weight, un-normalised (the kernel's integral over tap k's bin). Exposed for the tests.
[[nodiscard]] double followKernelWeight(double seconds, std::size_t k);

// The subject's past as a camera evaluated at `now` may read it: HIST's samples strictly before
// `now` that belong to the head's placement, and then `head`, the subject as it stands this frame.
//
// Strictly before, because at the moment a frame's camera is evaluated a play has recorded up to the
// previous frame and a seek has recorded the landing instant itself; reading the head in both cases
// is what makes the two the same input. Held at both ends: before the oldest sample the subject is
// where it was first seen (the head of a film, a body that has not moved), and a query past `now`
// is the head. With no bank or no ring the trail is the head alone.
//
// Of the head's placement (`HistorySample::placement`; amended 2026-09-27), because a body that was
// put somewhere -- a performance on its mark, a staging placement, a body shown again -- did not
// move there, and a camera that filtered across the change glided from where it was to where it
// was put: 21.8 m down a hillside over two seconds, in the defect that found this. The trail stops
// at the newest sample of another placement, so the oldest sample of this one is where the subject
// "was first seen", exactly as at the head of a film. ADR-912 drops the renderer's history at a cut
// by the same rule: whoever knows the picture does not continue says so, and the reader starts
// again.
class SubjectTrail {
public:
    SubjectTrail(const world::HistoryBank* bank, std::size_t ring, double now, const world::HistorySample& head);

    [[nodiscard]] world::HistorySample at(double seconds) const;
    // Whether any history backs the head: false means every read is the head.
    [[nodiscard]] bool hasHistory() const { return count_ > first_; }
    [[nodiscard]] double now() const { return now_; }

private:
    const world::HistoryBank* bank_ = nullptr;
    std::size_t ring_ = 0;
    std::size_t first_ = 0; // the ring's oldest sample of the head's placement
    std::size_t count_ = 0; // how many of the ring's samples are strictly before `now`
    double now_ = 0.0;
    world::HistorySample head_;
};

// The ground a walking subject stands on, for `FollowFilter::ground`. An interface so this file
// knows nothing about terrain, and a test can hand it a plane.
class FollowGround {
public:
    virtual ~FollowGround() = default;
    [[nodiscard]] virtual float heightAt(glm::vec2 xz) const = 0;
};

// The ground averaged over a footprint of `radius` round `xz`: the centre twice and four points on
// the circle, the weighting a body's own grounding uses (entity/grounding.cpp).
[[nodiscard]] float footprintHeight(const FollowGround& ground, glm::vec2 xz, float radius = kFollowGroundRadius);

struct FollowReference {
    glm::vec3 position{0.0f};
    float heading = 0.0f; // radians: the yaw that turns +Z onto the subject's facing, about +Y
};

// The reference at `seconds` (at or before the trail's `now`). `ground` is read only when
// `filter.ground` is set, and a null one is a flat world at y = 0.
//
//   xz = F_h[P.xz] + lead * T_h' * d/dt F_h[P.xz]            T_h' = the kernel's own mean delay
//   y  = F_v[P.y]                                            (filter.ground off)
//   y  = F_v[P.y] + G(F_h[P.xz]) - G(F_v[P.xz])              (filter.ground on)
//   heading = F_psi[yaw], the yaw unwrapped tap to tap
//
// With `ground`, the second and third terms move the filtered height from the ground under the
// vertically-lagged path to the ground under the horizontally-smoothed one: the vertical lag a
// descent builds up, less the horizontal constant's, and nothing of the stride, which is in P.y and
// is filtered. G is the footprint height (`footprintHeight`). The smoothed path and not the led
// one, because the lead overshoots every stop and a slope turns that overshoot into a bounce.
[[nodiscard]] FollowReference followReference(const SubjectTrail& trail, double seconds, const FollowFilter& filter,
                                              const FollowGround* ground);

// The yaw of a rotation's +Z about +Y, in radians; `fallback` when +Z is vertical.
[[nodiscard]] float headingOf(const glm::quat& rotation, float fallback = 0.0f);

// The clearance floor: `floor + w ln(1 + e^((y - floor) / w))`. Never below `floor`, smooth
// everywhere, and within a few millimetres of `y` once `y` is a metre above it.
[[nodiscard]] float softFloor(float y, float floor, float width = kFollowFloorSoftness);

} // namespace avgen::scene
