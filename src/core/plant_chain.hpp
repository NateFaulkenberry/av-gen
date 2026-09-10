#pragma once

// Tier 1 vegetation: a plant as a short chain of control points, integrated (ADR-056).
//
// Tier 0 (ADR-055) is a transfer function: the tip's offset is an instantaneous, analytically
// filtered function of the wind field. It is nearly free and it is right about *frequency* -- a
// tree ignores gusts, grass follows them -- but it has no state, so it can never overshoot, never
// ring, and never answer anything that is not the wind field. Tier 1 is the same plant with a
// state vector: a handful of points with position, velocity, a rest length that is enforced rather
// than sprung, an angular spring toward straightness, damping, and gravity. It costs about a
// microsecond per plant per frame and is spent only on the plants a viewer can actually resolve.
//
// Two properties make Tier 1 usable as a level of detail rather than as a second look:
//
//  * The chain is dimensionless. The root is the origin, the rest pose is the unit vector +Y, and
//    every position is in units of the plant's own height -- so the tip's horizontal offset IS the
//    2D `bend` vector that shaders/wind.wgsl already consumes, and the downstream profile, soft
//    ceiling and length-preserving drop are byte-identical between the two tiers.
//  * It is calibrated against Tier 0. `chainCalibration` measures, once, what the chain's static
//    tip deflection and fundamental frequency are for a unit joint spring; the drive and the spring
//    are then scaled so that the chain's DC gain equals Tier 0's `steadyGain` and its resonance
//    equals the species' `omega0`. A promoted plant therefore settles to exactly the lean its
//    unpromoted neighbours have, and differs from them only in the transient -- which is the whole
//    point.

#include "core/wind.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace avgen::wind {

// ---- localised disturbance (P5) ----------------------------------------------------------------

// A push through the vegetation: a body moving through it, something landing, a downdraught. It
// decays with distance from `position` and with time since it was raised, and it is deliberately a
// small bounded set -- a disturbance costs every *active* plant a distance test, so an unbounded
// list would turn the one cost that scales with the active set into one that does not.
struct Disturbance {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f}; // the way it pushes; normalised on insertion (zero = radially out)
    float radius = 1.0f;       // metres; beyond this it does nothing at all
    float strength = 0.0f;     // metres per second squared at the centre, at age 0
    float duration = 0.5f;     // seconds until it is spent
    float age = 0.0f;          // seconds since it was raised

    [[nodiscard]] bool spent() const { return age >= duration || strength <= 0.0f || radius <= 0.0f; }
};

inline constexpr int kMaxDisturbances = 16;

// How a body moving through the vegetation raises one. The wake is a *persistent* slot rather than
// an impulse per frame: a camera walking through a meadow is a continuous presence, and pushing a
// new impulse every frame would evict every other disturbance in sixteen frames.
using CameraWake = CameraWakeParams; // the authored form is the whole model; see core/wind.hpp

// The live set. Cheap to copy; one of these is shared by every simulated layer.
class DisturbanceField {
public:
    // Adds one, normalising `direction`. When the set is full the weakest live entry is replaced,
    // and if every live entry is stronger the new one is dropped: the cost stays bounded either way.
    void add(const Disturbance& d);
    void clear();
    // Ages every entry and retires the spent ones. Also decays the camera wake when the body stops.
    void advance(float dt);
    // Points the camera wake at a body moving at `velocity` (metres per second). Call once a frame.
    void trackBody(const glm::vec3& position, const glm::vec3& velocity, const CameraWake& cfg);

    [[nodiscard]] std::span<const Disturbance> items() const { return {items_.data(), count_}; }
    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }

    // The acceleration a plant rooted at `p` feels, in metres per second squared. Spatial falloff
    // is (1 - (d/r)^2)^2 -- smooth at the rim, so a plant never snaps as the radius passes it --
    // and the temporal one is (1 - age/duration)^2.
    [[nodiscard]] glm::vec3 accelerationAt(const glm::vec3& p) const;
    // Whether anything reaches `p` at all: the wake test for a sleeping plant, which must be
    // cheaper than the acceleration and must never miss a disturbance the acceleration would find.
    [[nodiscard]] bool reaches(const glm::vec3& p) const;

private:
    std::array<Disturbance, kMaxDisturbances> items_{};
    std::size_t count_ = 0;
};

// ---- the chain ---------------------------------------------------------------------------------

// Four movable points and a pinned root. Fewer than three cannot show a whip (the tip needs to lag
// the middle, which needs to lag the base); more buys shape a 0.4 m grass clump forty metres from
// the camera cannot display, and every point is paid for on every substep.
inline constexpr int kChainPoints = 4;

// What the chain's geometry does, measured rather than asserted. All three are pure functions of
// the point count, found once by exact linear algebra on the (tiny) stiffness matrix.
struct ChainCalibration {
    float staticTip = 1.0f;   // tip offset under unit uniform drive with a unit joint spring
    float frequency = 1.0f;   // fundamental angular frequency with a unit joint spring (rad/s)
    float stiffRatio = 1.0f;  // stiffest mode / fundamental: what the substep has to survive
    float buckling = 1.0f;    // downward acceleration, per unit joint spring, that topples it
};
[[nodiscard]] const ChainCalibration& chainCalibration(int points = kChainPoints);

// Everything a chain needs from the species and the frame, resolved once per draw. `steadyGain`,
// `gustGain`, `flutterGain`, `flutterOmega` and `bendLimit` are Tier 0's own numbers
// (wind::MotionResponse), which is how the two tiers are made to agree at DC.
struct ChainParams {
    float omega0 = 4.0f;      // rad/s, the species' resonance: sqrt(stiffness/mass)
    float zeta = 0.3f;        // damping ratio
    float height = 1.0f;      // metres; converts world accelerations into the chain's units
    float gravity = 9.81f;    // metres per second squared
    float gravitySag = 1.0f;  // how much of it is admitted (see the note in plant_chain.cpp)
    float maxLean = 0.9f;     // hard cap on |tip| in plant heights; the stalk cannot lie down flat
};

// The species resolved into the handful of numbers a substep multiplies by. Everything expensive
// -- a four by four assemble, an inverse and two power iterations -- happens here, once per draw,
// never once per plant: a scatter layer is one species and ten thousand specimens.
struct ChainTuning {
    float kBend = 1.0f;      // joint spring
    float drive = 0.0f;      // acceleration per unit of Tier 0 bend
    float damp = 0.0f;       // velocity damping, 2 zeta omega
    float gravity = 0.0f;    // the downward acceleration actually admitted, chain units
    float substep = 1.0f / 240.0f;
    float omega = 0.0f;      // the resonance the chain will actually ring at
    float staticGain = 1.0f; // tip lean per unit drive acceleration, gravity included
};

// `dt` is the frame's delta: the substep is chosen from the *stiffest* mode of the chain, not from
// the species' resonance, because that is the one an explicit integrator has to survive.
[[nodiscard]] ChainTuning tuneChain(const ChainParams& p, float dt);

// The integrator's state for one plant, 96 bytes. Deliberately a plain aggregate with no pointers:
// a few hundred of these are walked in order every frame and they should be one cache stream.
struct PlantChain {
    std::array<glm::vec3, kChainPoints> pos{};
    std::array<glm::vec3, kChainPoints> vel{};
    float quiet = 0.0f;   // seconds it has been still enough to sleep
    bool asleep = false;

    // The rest pose: straight up, at rest.
    void reset();
    // The static shape that puts the tip at `bend` (plant heights, world XZ), with `bendVel` as the
    // tip's velocity. This is the Tier 0 pose expressed as a chain, and it is what a promotion
    // initialises from: at the instant of the switch the chain's tip equals Tier 0's bend exactly,
    // so nothing moves. See ADR-056 on why continuity is a property rather than a fade.
    void setFromBend(const glm::vec2& bend, const glm::vec2& bendVel);
    // The tip's horizontal offset in plant heights: the `bend` the vertex shader consumes.
    [[nodiscard]] glm::vec2 bend() const { return {pos[kChainPoints - 1].x, pos[kChainPoints - 1].z}; }
    // Sum of |v| over the points, plant heights per second. The sleep test.
    [[nodiscard]] float speed() const;
};
static_assert(sizeof(PlantChain) == 8 * sizeof(glm::vec3) + 8);

// One step. `dt` is the frame's delta; the chain is substepped internally at `t.substep`, so a stiff
// species stays stable at any frame rate, and the count is capped so a hitch cannot turn into a
// spiral. `driveBend` is the tip offset Tier 0 would ask for right now (plant heights) -- the chain
// is driven *toward* it rather than set to it, and that difference is the whole of Tier 1.
// `worldAccel` is any extra acceleration in metres per second squared (disturbances).
void stepChain(PlantChain& chain, const ChainTuning& t, const glm::vec2& driveBend,
               const glm::vec3& worldAccel, float height, float maxLean, float dt);

// A soft plant does not need a stiff plant's time step, so the step is derived per species rather
// than fixed: the accuracy target is this many substeps per period of the fundamental, and the
// stability target is the stiffest mode (see tuneChain). A fourteen-metre tree ends up at one
// substep a frame and a grass blade at four, which is the right way round.
inline constexpr float kChainStepsPerPeriod = 20.0f;
inline constexpr int kMaxChainSubsteps = 8;

} // namespace avgen::wind
