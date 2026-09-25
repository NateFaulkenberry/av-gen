#pragma once

// A per-character quality analyzer over a simulated run (ADR-826, the Glowmere review build's
// item 9, Phase F8-lite).
//
// The question this answers is "does the running cast LOOK right", asked offline, of numbers, and
// without a window. Every defect the motion stack has shipped so far was found by somebody watching
// a film and saying "that alien just teleported" or "the feet are skating" -- and every one of them
// was already visible in a number the engine computes and nobody compared: the root position from
// one step to the next, the gait's authored stride against the speed the body actually covered, the
// activity the state machine chose frame by frame. This file collects those numbers per entity, per
// frame, and hands them back as individual metrics.
//
// **Individual metrics, never one score.** A combined score is the defect it pretends to measure:
// weight the pops high and a skating cast scores well; weight the slip high and a teleporting one
// does. Each metric below has its own threshold and its own meaning, and the reader decides which
// one matters for the shot in front of them. `toJson` deliberately writes no aggregate key, and the
// test that pins the schema fails if one appears.
//
// **Read-only, and two doors in.** `record(EntityWorld, ...)` reads `Entity::state()`,
// `locomotion()` and `actions()` and changes nothing -- a quality meter that perturbed what it
// measured would be measuring itself. `record(span<CharacterSample>, ...)` takes the same facts as
// plain structs, which is what lets the arithmetic be tested by hand-built cases ("a body that
// wants 2 m/s and covers nothing for a second is stuck for a second") without having to persuade a
// real behaviour to get stuck on cue. The world door is a thin adapter onto the sample door, so
// both are the same code past the first line.
//
// Deterministic in everything but `wallClockMsPerFrame`, which the caller measures and which the
// JSON files under a key that says it is machine-dependent: two runs of the same scene give the same
// motion and behaviour numbers bit for bit (ADR-091), and a regression in one of them is a change in
// the simulation, not noise.

#include "entity/gait.hpp"
#include "entity/locomotion.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace avgen::entity {

class EntityWorld;

// The thresholds, in one place and in the report, so a number in a JSON file can always be read
// against the line it was measured against. Each is argued where it is used, in the .cpp.
struct CharacterQualityThresholds {
    // A step is a root pop when it covers more than `popRunMultiple` times the body's authored run
    // speed in one dt. Three times the fastest the body is ever meant to go is not a fast run; it is
    // a teleport, and no gait choice can make it look like anything else.
    float popRunMultiple = 3.0f;
    // Used when a body authors no run speed at all.
    float fallbackRunSpeed = 8.0f;
    // m/s^2 of measured horizontal velocity change in one step that reads as a hitch.
    float discontinuityAccel = 40.0f;
    // `footSlip` outside [1/band, band] is visible skating or moonwalking.
    float slipBand = 1.5f;
    // A body "intends to move" above this `state().speed`, and "is not moving" below this measured
    // horizontal speed. Together they are stuck.
    float stuckIntentSpeed = 0.3f;
    float stuckMeasuredSpeed = 0.05f;
    // A body counts as moving, for the foot-slip denominator, above this measured speed.
    float movingSpeed = 0.05f;
    // A->B->A inside this many seconds is an oscillation, not two decisions.
    double oscillationWindow = 1.0;
};

// One body at one instant: everything the recorder needs and nothing else.
struct CharacterSample {
    std::string name;
    glm::vec3 position{0.0f};  // world; only x and z are read
    float intendedSpeed = 0.0f; // `state().speed`: what the mover MEANT
    Activity activity = Activity::Idle;
    bool grounded = true;
    bool director = false;      // a Director-authority action is running
    GaitSettings gait;          // the body's authored stride speeds, for slip and the pop limit
};

struct CharacterMotionMetrics {
    double travelMetres = 0.0;
    double maxSpeed = 0.0;              // m/s, largest single-step displacement / dt
    std::uint32_t rootPops = 0;
    double largestPopMetres = 0.0;
    double popThresholdSpeed = 0.0;     // m/s the pop test used for this body
    std::uint32_t velocityDiscontinuities = 0;
    double largestAccel = 0.0;          // m/s^2, over every step, popped or not
    std::uint32_t movingFrames = 0;     // walk/run frames above `movingSpeed`: the slip denominator
    std::uint32_t slipOutOfBand = 0;
    double slipOutOfBandFraction = 0.0;
    double worstSlip = 1.0;             // the ratio furthest from 1, in log terms
};

struct CharacterBehaviourMetrics {
    double stuckSeconds = 0.0;
    double idleFraction = 0.0;
    std::uint32_t activityChanges = 0;
    double activityChangesPerMinute = 0.0;
    std::uint32_t oscillations = 0;
    double directorSeconds = 0.0;
    double airborneSeconds = 0.0;
};

struct CharacterQuality {
    std::string name;
    std::uint64_t frames = 0;
    double seconds = 0.0;
    CharacterMotionMetrics motion;
    CharacterBehaviourMetrics behaviour;
};

struct CharacterQualityReport {
    CharacterQualityThresholds thresholds;
    std::uint64_t frames = 0;
    double seconds = 0.0;
    std::size_t entityCount = 0;
    // Filled by the caller, which is the only thing that owns a wall clock. Machine-dependent.
    std::optional<double> wallClockMsPerFrame;
    std::vector<CharacterQuality> characters; // in first-seen order
};

class CharacterQualityRecorder {
public:
    explicit CharacterQualityRecorder(CharacterQualityThresholds thresholds = {});

    // One simulated frame. `dt` 0 (the application's first frame) or a body's first appearance
    // records a baseline and measures nothing: there is no step to measure.
    void record(const EntityWorld& world, double time, double dt);
    void record(std::span<const CharacterSample> samples, double time, double dt);

    [[nodiscard]] CharacterQualityReport report() const;

private:
    struct Track {
        CharacterQuality out;
        glm::vec2 lastPosition{0.0f};
        glm::vec2 lastVelocity{0.0f};
        bool hasPosition = false;
        bool hasVelocity = false;
        Activity lastActivity = Activity::Idle;
        // The previous activity change, for A->B->A: what it changed FROM and when.
        bool hasChange = false;
        Activity changedFrom = Activity::Idle;
        double changedAt = 0.0;
        std::uint64_t idleFrames = 0;
    };

    CharacterQualityThresholds thresholds_;
    std::vector<Track> tracks_;
    std::unordered_map<std::string, std::size_t> index_;
    std::vector<CharacterSample> scratch_;
    std::uint64_t frames_ = 0;
    double seconds_ = 0.0;
    std::size_t entityCount_ = 0;
};

[[nodiscard]] nlohmann::json toJson(const CharacterQualityReport& report);

} // namespace avgen::entity
