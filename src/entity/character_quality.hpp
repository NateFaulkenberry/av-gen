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
// **ADR-910 adds the patterns a viewer reads as a mechanism rather than a creature**, which the
// GV3 character audit measured by hand from a cast trace and the owner named in so many words:
// long stretches standing still, walking to a point and walking straight back, turning on the spot,
// turns tighter than the body is long, and standing on -- or facing up -- a hillside. They are the
// regression gate for ADR-907 to 909: each of those decisions claims to move one of these numbers,
// and a claim about behaviour that no number can contradict is the shape of defect this repository
// keeps shipping.
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
#include <deque>
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

    // ---- ADR-910: the patterns ----------------------------------------------------------------
    // A body is standing still below this measured horizontal speed, in m/s: the GV3 audit's
    // "still", so a number from this file and a number from that report mean the same thing.
    float stillSpeed = 0.10f;
    // A still stretch at least this long is a stop. Shorter is a hesitation inside a walk -- a body
    // decelerating through a corner dips under `stillSpeed` for a frame or two -- and counting it
    // would make every sharp turn a "stop" with a heading in and a heading out.
    double stopSeconds = 0.25;
    // A body turning while it goes slower than this (m/s) is turning on the spot: the audit's
    // "yaw turned below 0.3 m/s".
    float pivotSpeed = 0.30f;
    // Walking out of a stop more than this many degrees from the way the body walked into it is a
    // reversal -- the owner's "walk to a point, turn round, walk back".
    float reversalDegrees = 150.0f;
    // The headings into and out of a stop are taken over this much straight-line travel, so the
    // last metre of an arrival curve or the first of a departure one does not decide them.
    float headingMetres = 1.5f;
    // A stop within this many metres of the stop before the last one is an A->B->A revisit.
    float revisitRadius = 4.0f;
    // A turn-radius sample needs the body going faster than this (m/s) on both ends of the step and
    // turning faster than `turnRateMin` (rad/s). Below either, speed over turn rate divides noise.
    float turnSpeedMin = 0.5f;
    float turnRateMin = 0.17453293f; // 10 degrees a second
    // Ground steeper than this (degrees) under a standing body is steep ground, and a body standing
    // on it facing within `uphillDegrees` of straight uphill is facing into the hill -- the owner's
    // "horses facing directly into hills", in the audit's own numbers.
    float steepDegrees = 12.0f;
    float uphillDegrees = 45.0f;
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
    // ADR-910. The facing (forward is (sin yaw, 0, cos yaw), the engine's one convention) and the
    // surface the body is standing on, when something grounded it this step.
    float yaw = 0.0f;
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool hasGround = false;
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
    // ADR-910: how the body turns.
    double yawDegrees = 0.0;            // every degree the facing turned, whatever the body did
    double pivotYawDegrees = 0.0;       // ...of which while slower than `pivotSpeed`: on the spot
    double pivotYawFraction = 0.0;
    std::uint32_t turnSamples = 0;      // steps that measured a turn radius
    double turnRadiusMedian = 0.0;      // metres of speed / turn rate over those steps; 0 with none
    double turnRadiusP10 = 0.0;         // the tight end: one turn in ten is at least this tight
};

struct CharacterBehaviourMetrics {
    double stuckSeconds = 0.0;
    double idleFraction = 0.0;
    std::uint32_t activityChanges = 0;
    double activityChangesPerMinute = 0.0;
    std::uint32_t oscillations = 0;
    double directorSeconds = 0.0;
    double airborneSeconds = 0.0;
    // ADR-910: standing, and where the body goes after it has stood.
    double stillFraction = 0.0;         // of stepped seconds, measured speed under `stillSpeed`
    double longestStillSeconds = 0.0;
    std::uint32_t stops = 0;            // still stretches of at least `stopSeconds`
    std::uint32_t measuredStops = 0;    // ...with a heading in and a heading out to compare
    std::uint32_t reversals = 0;        // ...walked out of more than `reversalDegrees` from the way in
    std::uint32_t turnsOver90 = 0;      // ...walked out of more than 90 degrees from it
    std::uint32_t revisits = 0;         // stops within `revisitRadius` of the stop before the last
};

// ADR-910: the ground under a body that is standing on it.
struct CharacterGroundMetrics {
    double stillSeconds = 0.0;          // standing, on a surface it reported
    double slopeMeanDegrees = 0.0;      // time-weighted over those seconds
    double slopeMaxDegrees = 0.0;
    double steepSeconds = 0.0;          // ...on ground steeper than `steepDegrees`
    double facingUphillSeconds = 0.0;   // ...and facing within `uphillDegrees` of straight uphill
};

struct CharacterQuality {
    std::string name;
    std::uint64_t frames = 0;
    double seconds = 0.0;
    CharacterMotionMetrics motion;
    CharacterBehaviourMetrics behaviour;
    CharacterGroundMetrics ground;
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

        // ---- ADR-910 ----
        float lastYaw = 0.0f;
        bool hasYaw = false;
        double lastSpeed = 0.0;        // measured, the previous step
        double stillRun = 0.0;         // seconds of the current still stretch
        double stillTotal = 0.0;
        bool inStop = false;           // the current still stretch has reached `stopSeconds`
        // The most recent stretch of moving positions, just long enough to reach `headingMetres`
        // back from the newest: the heading into a stop is read off it when the stop begins.
        std::deque<glm::vec2> trail;
        glm::vec2 headingIn{0.0f};     // the way the current (or last) stop was walked into
        bool hasHeadingIn = false;
        bool pendingOut = false;       // a stop ended and the way out of it is not measured yet
        glm::vec2 stopExit{0.0f};
        std::vector<glm::vec2> stopPlaces; // the last two stops' places, for A->B->A
        std::vector<float> radii;
        double slopeSecondsWeighted = 0.0;
    };

    // The way out of the last stop, measured from where the body left it to `here`, compared with
    // the way in. Called once the body has gone `headingMetres`, or earlier -- with what it has --
    // when it stops again first.
    void closeStop(Track& t, glm::vec2 here) const;

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
