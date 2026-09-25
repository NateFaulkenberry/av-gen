#include "entity/character_quality.hpp"

#include "entity/action.hpp"
#include "entity/entity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::entity {

namespace {

bool travelling(Activity activity) { return activity == Activity::Walk || activity == Activity::Run; }

// How far a slip ratio is from "the feet are where the ground is", symmetric in over- and
// under-speed: 2.0 (moonwalking) and 0.5 (treading water) are equally wrong, which a plain
// `|slip - 1|` would call 1.0 and 0.5.
double slipDistance(double slip) { return std::abs(std::log(std::max(slip, 1e-6))); }

} // namespace

CharacterQualityRecorder::CharacterQualityRecorder(CharacterQualityThresholds thresholds)
    : thresholds_(thresholds) {}

void CharacterQualityRecorder::record(const EntityWorld& world, double time, double dt) {
    scratch_.clear();
    scratch_.reserve(world.entities().size());
    for (const auto& owned : world.entities()) {
        const Entity& e = *owned;
        CharacterSample s;
        s.name = e.name();
        // `locomotion().position`, not `state().position()`: it is the root the animation layer is
        // handed (the navigated place plus any root motion), which is the thing a viewer sees pop.
        s.position = e.locomotion().position;
        s.intendedSpeed = e.state().speed;
        s.activity = e.locomotion().activity;
        s.grounded = e.locomotion().grounded;
        s.director = e.actions().running() && e.actions().authority() == Authority::Director;
        s.gait = e.desc().gait;
        scratch_.push_back(std::move(s));
    }
    record(std::span<const CharacterSample>(scratch_), time, dt);
}

void CharacterQualityRecorder::record(std::span<const CharacterSample> samples, double time, double dt) {
    ++frames_;
    const bool step = dt > 0.0;
    if (step) {
        seconds_ += dt;
    }
    entityCount_ = std::max(entityCount_, samples.size());

    for (const CharacterSample& s : samples) {
        auto [it, inserted] = index_.try_emplace(s.name, tracks_.size());
        if (inserted) {
            Track fresh;
            fresh.out.name = s.name;
            tracks_.push_back(std::move(fresh));
        }
        Track& t = tracks_[it->second];
        CharacterMotionMetrics& m = t.out.motion;
        CharacterBehaviourMetrics& b = t.out.behaviour;
        const glm::vec2 here{s.position.x, s.position.z};

        const float runSpeed = s.gait.runSpeed > 0.0f ? s.gait.runSpeed : thresholds_.fallbackRunSpeed;
        m.popThresholdSpeed = static_cast<double>(thresholds_.popRunMultiple * runSpeed);

        ++t.out.frames;
        if (s.activity == Activity::Idle) {
            ++t.idleFrames;
        }

        // Activity churn is counted on every frame, stepped or not: a change is a change whatever
        // the clock did. An A->B->A inside the window is ONE oscillation, counted on the return.
        if (t.out.frames > 1 && s.activity != t.lastActivity) {
            ++b.activityChanges;
            if (t.hasChange && s.activity == t.changedFrom && time - t.changedAt <= thresholds_.oscillationWindow) {
                ++b.oscillations;
            }
            t.hasChange = true;
            t.changedFrom = t.lastActivity;
            t.changedAt = time;
        }
        t.lastActivity = s.activity;

        // A body's first sample, and any frame without a step, is a baseline: there is no
        // displacement to divide by a dt, and dividing by the first frame's zero is ADR-521's
        // infinity. The velocity memory is dropped too, so the next step is not compared against a
        // velocity from before the gap.
        if (!step || !t.hasPosition) {
            t.lastPosition = here;
            t.hasPosition = true;
            t.hasVelocity = false;
            continue;
        }

        t.out.seconds += dt;
        const glm::vec2 delta = here - t.lastPosition;
        const double metres = static_cast<double>(glm::length(delta));
        const glm::vec2 velocity = delta / static_cast<float>(dt);
        const double speed = metres / dt;

        // Travel includes a pop's distance. Deliberate: the metric is "how far did the root go",
        // and the pop count beside it says how much of that was a teleport.
        m.travelMetres += metres;
        m.maxSpeed = std::max(m.maxSpeed, speed);
        if (speed > m.popThresholdSpeed) {
            ++m.rootPops;
            m.largestPopMetres = std::max(m.largestPopMetres, metres);
        }

        // 40 m/s^2 is about four g horizontally -- well beyond anything a gait's authored `accel`
        // and `decel` (6 and 8 m/s^2 by default) allow, so a step above it is a velocity the body
        // could not have produced by moving: a snap, a re-target, a pop's entry or exit. A pop
        // therefore also shows up here twice (the jump and the stop), which is correct; the two
        // counts answer different questions.
        if (t.hasVelocity) {
            const double accel = static_cast<double>(glm::length(velocity - t.lastVelocity)) / dt;
            m.largestAccel = std::max(m.largestAccel, accel);
            if (accel > static_cast<double>(thresholds_.discontinuityAccel)) {
                ++m.velocityDiscontinuities;
            }
        }
        t.lastVelocity = velocity;
        t.hasVelocity = true;
        t.lastPosition = here;

        // Foot slip over the frames the body is visibly travelling in a stride clip, measured with
        // the body's MEASURED speed: the gait helper's own argument is whatever speed the clip is
        // being matched to, and the question here is whether the ground actually moved at it.
        if (travelling(s.activity) && speed > static_cast<double>(thresholds_.movingSpeed)) {
            ++m.movingFrames;
            const double slip = static_cast<double>(Gait::footSlip(s.gait, s.activity, static_cast<float>(speed)));
            const double band = static_cast<double>(thresholds_.slipBand);
            if (slip > band || slip < 1.0 / band) {
                ++m.slipOutOfBand;
            }
            if (slipDistance(slip) > slipDistance(m.worstSlip)) {
                m.worstSlip = slip;
            }
        }

        if (s.intendedSpeed > thresholds_.stuckIntentSpeed && speed < static_cast<double>(thresholds_.stuckMeasuredSpeed)) {
            b.stuckSeconds += dt;
        }
        if (s.director) {
            b.directorSeconds += dt;
        }
        if (!s.grounded) {
            b.airborneSeconds += dt;
        }
    }
}

CharacterQualityReport CharacterQualityRecorder::report() const {
    CharacterQualityReport r;
    r.thresholds = thresholds_;
    r.frames = frames_;
    r.seconds = seconds_;
    r.entityCount = entityCount_;
    r.characters.reserve(tracks_.size());
    for (const Track& t : tracks_) {
        CharacterQuality q = t.out;
        if (q.frames > 0) {
            q.behaviour.idleFraction = static_cast<double>(t.idleFrames) / static_cast<double>(q.frames);
        }
        if (q.seconds > 0.0) {
            q.behaviour.activityChangesPerMinute = static_cast<double>(q.behaviour.activityChanges) * 60.0 / q.seconds;
        }
        if (q.motion.movingFrames > 0) {
            q.motion.slipOutOfBandFraction =
                static_cast<double>(q.motion.slipOutOfBand) / static_cast<double>(q.motion.movingFrames);
        }
        r.characters.push_back(std::move(q));
    }
    return r;
}

nlohmann::json toJson(const CharacterQualityReport& report) {
    using nlohmann::json;
    const CharacterQualityThresholds& th = report.thresholds;
    json out;
    out["scene"] = {
        {"frames", report.frames},
        {"seconds", report.seconds},
        {"entityCount", report.entityCount},
    };
    // Under its own key so nobody diffs it against another machine's file and calls it a
    // regression: every other number in this document is a function of the scene alone.
    out["machineDependent"] = {
        {"note", "wall-clock cost on the machine that produced this file; not comparable across machines or load"},
        {"wallClockMsPerFrame", report.wallClockMsPerFrame ? json(*report.wallClockMsPerFrame) : json(nullptr)},
    };
    out["thresholds"] = {
        {"popRunMultiple", th.popRunMultiple},
        {"fallbackRunSpeed", th.fallbackRunSpeed},
        {"discontinuityAccel", th.discontinuityAccel},
        {"slipBand", {1.0 / static_cast<double>(th.slipBand), th.slipBand}},
        {"stuckIntentSpeed", th.stuckIntentSpeed},
        {"stuckMeasuredSpeed", th.stuckMeasuredSpeed},
        {"oscillationWindow", th.oscillationWindow},
    };
    json list = json::array();
    for (const CharacterQuality& q : report.characters) {
        const CharacterMotionMetrics& m = q.motion;
        const CharacterBehaviourMetrics& b = q.behaviour;
        json e;
        e["name"] = q.name;
        e["frames"] = q.frames;
        e["seconds"] = q.seconds;
        e["motion"] = {
            {"travelMetres", m.travelMetres},
            {"maxSpeed", m.maxSpeed},
            {"rootPops", {{"count", m.rootPops}, {"largestMetres", m.largestPopMetres},
                          {"thresholdSpeed", m.popThresholdSpeed}}},
            {"velocityDiscontinuities", {{"count", m.velocityDiscontinuities}, {"largestAccel", m.largestAccel}}},
            {"footSlip", {{"movingFrames", m.movingFrames}, {"outOfBandFrames", m.slipOutOfBand},
                          {"outOfBandFraction", m.slipOutOfBandFraction}, {"worst", m.worstSlip}}},
        };
        e["behaviour"] = {
            {"stuckSeconds", b.stuckSeconds},
            {"idleFraction", b.idleFraction},
            {"activityChanges", b.activityChanges},
            {"activityChangesPerMinute", b.activityChangesPerMinute},
            {"oscillations", b.oscillations},
            {"directorSeconds", b.directorSeconds},
            {"airborneSeconds", b.airborneSeconds},
        };
        list.push_back(std::move(e));
    }
    out["entities"] = std::move(list);
    return out;
}

} // namespace avgen::entity
