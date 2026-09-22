#pragma once

// Phase C §46: automated search evaluation. Replay a known motion as a stream of requests, let the
// matcher choose, and compare what it chose with what actually happened.
//
// "Given known motion sequence, simulate queries and compare the selected continuation to the known
// ground truth." The ground truth is a clip whose motion is known frame by frame. At each step the
// request is exactly what that clip was doing: its body velocity, its facing and its turn rate. The
// matcher answers from a database the clip may or may not be in. The seven measures §46 names are
// then read off the chosen samples against the clip's own:
//
//   continuity         fraction of steps that carried on rather than switching
//   trajectory error   mean distance between the chosen sample's future positions and the clip's
//   velocity error     mean |chosen root velocity - clip root velocity|
//   pose error         mean joint distance, body-relative, chosen pose against the clip's pose
//   contact mismatch   fraction of (step, foot) pairs whose planted flags disagree
//   phase mismatch     mean circular phase difference, in cycles (0..0.5)
//   transition rate    switches per second
//
// With the clip left in the database, every measure should be near its floor: that is the sanity
// arm, the evaluation's own control. With it left out, the numbers say how well the rest of the
// corpus can stand in for it, which is the question.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <string>

namespace avgen::entity {

struct MotionEvaluation {
    std::uint32_t steps = 0;
    float continuity = 0.0f;
    float trajectoryError = 0.0f;
    float velocityError = 0.0f;
    float poseError = 0.0f;
    float contactMismatch = 0.0f;
    float phaseMismatch = 0.0f;
    float transitionsPerSecond = 0.0f;
    std::string clip;
    [[nodiscard]] std::string report() const;
};

struct MotionEvaluationOptions {
    float rate = 60.0f;           // simulation steps per second
    float seconds = 0.0f;         // 0 = the whole clip
    MatchSettings settings;       // the matcher under evaluation
    float worldScale = 1.0f;
};

// Evaluate matching `groundTruth` (a clip of `pack`, by index) against `db`, whose clips are
// `clips`. The pack supplies the ground truth's skeleton, contacts and phase.
[[nodiscard]] MotionEvaluation evaluateMatcher(const scene::MotionPack& pack, std::size_t groundTruth,
                                               const scene::MotionDatabase& db,
                                               const std::vector<scene::AnimationClip>& clips,
                                               const MotionEvaluationOptions& options);

} // namespace avgen::entity
