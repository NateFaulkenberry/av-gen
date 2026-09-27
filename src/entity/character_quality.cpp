#include "entity/character_quality.hpp"

#include "entity/action.hpp"
#include "entity/entity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::entity {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegrees = 180.0 / kPi;

bool travelling(Activity activity) { return activity == Activity::Walk || activity == Activity::Run; }

// How far a slip ratio is from "the feet are where the ground is", symmetric in over- and
// under-speed: 2.0 (moonwalking) and 0.5 (treading water) are equally wrong, which a plain
// `|slip - 1|` would call 1.0 and 0.5.
double slipDistance(double slip) { return std::abs(std::log(std::max(slip, 1e-6))); }

// The smallest angle between two yaws, in radians, 0..pi.
double yawGap(float a, float b) {
    double d = std::fmod(static_cast<double>(b) - static_cast<double>(a) + kPi, 2.0 * kPi);
    if (d < 0.0) {
        d += 2.0 * kPi;
    }
    return std::abs(d - kPi);
}

// The angle between two directions in the plane, in degrees, 0..180.
double degreesBetween(glm::vec2 a, glm::vec2 b) {
    const double la = glm::length(a);
    const double lb = glm::length(b);
    if (la <= 1e-9 || lb <= 1e-9) {
        return 0.0;
    }
    const double c = std::clamp(static_cast<double>(glm::dot(a, b)) / (la * lb), -1.0, 1.0);
    return std::acos(c) * kDegrees;
}

// A value at fraction `q` of a sorted list, by linear interpolation between neighbours -- the same
// rule the audit's `measure.py` uses, so a median here and a median there are the same median.
double quantile(std::vector<float> values, double q) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double k = (static_cast<double>(values.size()) - 1.0) * q;
    const auto lo = static_cast<std::size_t>(std::floor(k));
    const auto hi = static_cast<std::size_t>(std::ceil(k));
    if (lo == hi) {
        return values[lo];
    }
    return values[lo] + (values[hi] - values[lo]) * (k - static_cast<double>(lo));
}

// Metres of straight-line travel below which a heading is not a heading: a body that shuffled 20 cm
// out of a stop and stopped again has no "way out" worth comparing, and the audit drops it too.
constexpr float kHeadingFloor = 0.3f;

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
        // ADR-910. The facing the animation layer is handed, and the surface `ground` last read under
        // the body -- the one its feet and its lean are resolved against, not a fresh query of the
        // terrain at a point, which could disagree with what the body is drawn standing on.
        s.yaw = e.locomotion().yaw;
        s.hasGround = e.state().hasGroundPlane;
        s.groundNormal = e.state().groundNormal;
        scratch_.push_back(std::move(s));
    }
    record(std::span<const CharacterSample>(scratch_), time, dt);
}

void CharacterQualityRecorder::closeStop(Track& t, glm::vec2 here) const {
    t.pendingOut = false;
    const glm::vec2 out = here - t.stopExit;
    if (!t.hasHeadingIn || glm::length(out) < kHeadingFloor) {
        return; // walked in from nowhere measurable, or out to nowhere measurable
    }
    CharacterBehaviourMetrics& b = t.out.behaviour;
    ++b.measuredStops;
    const double turned = degreesBetween(t.headingIn, out);
    if (turned > 90.0) {
        ++b.turnsOver90;
    }
    if (turned > static_cast<double>(thresholds_.reversalDegrees)) {
        ++b.reversals;
        // ADR-933: a reversal after a reversal is pacing; anything walked on out of breaks the run.
        if (t.pacingRun == 0) {
            t.pacingFrom = t.pendingStopAt;
        }
        ++t.pacingRun;
        if (t.pacingRun > b.longestPacing) {
            b.longestPacing = t.pacingRun;
            b.longestPacingFrom = t.pacingFrom;
            b.longestPacingSeconds = t.pendingStopAt - t.pacingFrom;
        }
    } else {
        t.pacingRun = 0;
    }
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
        CharacterGroundMetrics& g = t.out.ground;
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
            t.lastYaw = s.yaw;
            t.hasYaw = true;
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

        // ---- ADR-910: turning -------------------------------------------------------------------
        //
        // Measured off the yaw the body is drawn with and the ground it actually covered, so a mover
        // that says it is walking a curve and a body that pivots on the spot cannot be mistaken for
        // each other: the pivot turns while going nowhere, and that is the whole of what it is.
        {
            const double turned = t.hasYaw ? yawGap(t.lastYaw, s.yaw) : 0.0;
            m.yawDegrees += turned * kDegrees;
            if (speed < static_cast<double>(thresholds_.pivotSpeed)) {
                m.pivotYawDegrees += turned * kDegrees;
            }
            // Radius on both ends of the step, so a body just setting off or just stopping -- whose
            // speed over the step is half of what it was at one end -- is not a tight turn.
            const double rate = turned / dt;
            if (speed > static_cast<double>(thresholds_.turnSpeedMin) &&
                t.lastSpeed > static_cast<double>(thresholds_.turnSpeedMin) &&
                rate > static_cast<double>(thresholds_.turnRateMin)) {
                t.radii.push_back(static_cast<float>(speed / rate));
            }
            t.lastYaw = s.yaw;
            t.hasYaw = true;
            t.lastSpeed = speed;
        }

        // ---- ADR-910: standing, stops, and where the body goes out of them -----------------------
        const bool still = speed < static_cast<double>(thresholds_.stillSpeed);
        if (still) {
            t.stillRun += dt;
            t.stillTotal += dt;
            b.longestStillSeconds = std::max(b.longestStillSeconds, t.stillRun);
            if (!t.inStop && t.stillRun >= thresholds_.stopSeconds) {
                t.inStop = true;
                ++b.stops;
                // A stop that begins while the way out of the last one is still being measured ends
                // that measurement with whatever travel it has.
                if (t.pendingOut) {
                    closeStop(t, here);
                }
                t.stopBegan = time - t.stillRun; // the stop began when the body stood, not when it counted

                // The way in: straight-line travel from the oldest point of the trail to here.
                t.hasHeadingIn = false;
                if (!t.trail.empty()) {
                    const glm::vec2 in = here - t.trail.front();
                    if (glm::length(in) >= kHeadingFloor) {
                        t.headingIn = in;
                        t.hasHeadingIn = true;
                    }
                }
                t.trail.clear();
                // A->B->A: this stop against the one before the last.
                if (t.stopPlaces.size() >= 2 &&
                    glm::length(here - t.stopPlaces[t.stopPlaces.size() - 2]) < thresholds_.revisitRadius) {
                    ++b.revisits;
                }
                t.stopPlaces.push_back(here);
                if (t.stopPlaces.size() > 2) {
                    t.stopPlaces.erase(t.stopPlaces.begin());
                }
            }
            // The ground under a standing body. Only when something grounded it this step: a body
            // with no `ground` behaviour, or one a beam is holding, reports no surface, and
            // inventing flat ground for it would report "no slope" about a body on a cliff.
            if (s.hasGround) {
                const double up = std::clamp(static_cast<double>(s.groundNormal.y) /
                                                 std::max(1e-9, static_cast<double>(glm::length(s.groundNormal))),
                                             -1.0, 1.0);
                const double slope = std::acos(up) * kDegrees;
                g.stillSeconds += dt;
                t.slopeSecondsWeighted += slope * dt;
                g.slopeMaxDegrees = std::max(g.slopeMaxDegrees, slope);
                if (slope > static_cast<double>(thresholds_.steepDegrees)) {
                    g.steepSeconds += dt;
                    // Uphill is against the normal's lean: a surface rising toward +x has a normal
                    // leaning toward -x.
                    const glm::vec2 uphill(-s.groundNormal.x, -s.groundNormal.z);
                    const glm::vec2 facing(std::sin(s.yaw), std::cos(s.yaw));
                    if (degreesBetween(facing, uphill) < static_cast<double>(thresholds_.uphillDegrees)) {
                        g.facingUphillSeconds += dt;
                    }
                }
            }
        } else {
            if (t.inStop) {
                // Leaving a stop: the way out is measured from where it stood.
                t.inStop = false;
                t.pendingOut = true;
                t.pendingStopAt = t.stopBegan;
                t.stopExit = t.stopPlaces.empty() ? here : t.stopPlaces.back();
            }
            t.stillRun = 0.0;
            if (t.pendingOut && glm::length(here - t.stopExit) >= thresholds_.headingMetres) {
                closeStop(t, here);
            }
            // The trail keeps the shortest recent stretch that still reaches `headingMetres` back
            // from here, so the way into the next stop is read over that much travel and no more.
            t.trail.push_back(here);
            while (t.trail.size() > 2 && glm::length(here - t.trail[1]) >= thresholds_.headingMetres) {
                t.trail.pop_front();
            }
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
            q.behaviour.stillFraction = t.stillTotal / q.seconds;
        }
        if (q.motion.movingFrames > 0) {
            q.motion.slipOutOfBandFraction =
                static_cast<double>(q.motion.slipOutOfBand) / static_cast<double>(q.motion.movingFrames);
        }
        if (q.motion.yawDegrees > 0.0) {
            q.motion.pivotYawFraction = q.motion.pivotYawDegrees / q.motion.yawDegrees;
        }
        q.motion.turnSamples = static_cast<std::uint32_t>(t.radii.size());
        q.motion.turnRadiusMedian = quantile(t.radii, 0.5);
        q.motion.turnRadiusP10 = quantile(t.radii, 0.1);
        if (q.ground.stillSeconds > 0.0) {
            q.ground.slopeMeanDegrees = t.slopeSecondsWeighted / q.ground.stillSeconds;
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
        {"stillSpeed", th.stillSpeed},
        {"stopSeconds", th.stopSeconds},
        {"pivotSpeed", th.pivotSpeed},
        {"reversalDegrees", th.reversalDegrees},
        {"headingMetres", th.headingMetres},
        {"revisitRadius", th.revisitRadius},
        {"turnSpeedMin", th.turnSpeedMin},
        {"turnRateMinDegrees", static_cast<double>(th.turnRateMin) * kDegrees},
        {"steepDegrees", th.steepDegrees},
        {"uphillDegrees", th.uphillDegrees},
    };
    json list = json::array();
    for (const CharacterQuality& q : report.characters) {
        const CharacterMotionMetrics& m = q.motion;
        const CharacterBehaviourMetrics& b = q.behaviour;
        const CharacterGroundMetrics& g = q.ground;
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
            {"turning", {{"yawDegrees", m.yawDegrees}, {"pivotYawDegrees", m.pivotYawDegrees},
                         {"pivotYawFraction", m.pivotYawFraction}, {"turnSamples", m.turnSamples},
                         {"turnRadiusMedian", m.turnRadiusMedian}, {"turnRadiusP10", m.turnRadiusP10}}},
        };
        e["behaviour"] = {
            {"stuckSeconds", b.stuckSeconds},
            {"idleFraction", b.idleFraction},
            {"activityChanges", b.activityChanges},
            {"activityChangesPerMinute", b.activityChangesPerMinute},
            {"oscillations", b.oscillations},
            {"directorSeconds", b.directorSeconds},
            {"airborneSeconds", b.airborneSeconds},
            {"stillFraction", b.stillFraction},
            {"longestStillSeconds", b.longestStillSeconds},
            {"stops", {{"count", b.stops}, {"measured", b.measuredStops}, {"reversals", b.reversals},
                       {"turnsOver90", b.turnsOver90}, {"revisits", b.revisits},
                       {"pacing", {{"longest", b.longestPacing}, {"seconds", b.longestPacingSeconds},
                                   {"from", b.longestPacingFrom}}}}},
        };
        e["ground"] = {
            {"stillSeconds", g.stillSeconds},
            {"slopeMeanDegrees", g.slopeMeanDegrees},
            {"slopeMaxDegrees", g.slopeMaxDegrees},
            {"steepSeconds", g.steepSeconds},
            {"facingUphillSeconds", g.facingUphillSeconds},
        };
        list.push_back(std::move(e));
    }
    out["entities"] = std::move(list);
    return out;
}

} // namespace avgen::entity
