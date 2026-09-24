#include "directing/performance.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace avgen::directing {
namespace {

constexpr float kStopMargin = 1.0f;  // metres short of a target's radius a *_to stops
constexpr float kPassMargin = 1.5f;  // metres beside a target's radius a *_past passes
constexpr double kLeadSeconds = 2.0; // the approach to a first target, when the plan does not say
constexpr double kDefaultBeat = 1.0; // run/walk/hold/look_at, when the plan does not say
constexpr float kRunFraction = 0.95f;

bool isRun(std::string_view a) { return a == "run" || a == "run_to" || a == "run_past"; }
bool isTo(std::string_view a) { return a == "run_to" || a == "walk_to"; }
bool isPast(std::string_view a) { return a == "run_past" || a == "walk_past"; }

glm::vec3 flatDir(glm::vec3 from, glm::vec3 to, glm::vec3 fallback) {
    glm::vec3 d(to.x - from.x, 0.0f, to.z - from.z);
    const float n = glm::length(d);
    return n > 1e-4f ? d / n : fallback;
}

float yawDegrees(glm::vec3 d) { return std::atan2(d.x, d.z) * 180.0f / std::numbers::pi_v<float>; }

} // namespace

bool beatCompilable(std::string_view action) {
    static constexpr std::string_view kSet[] = {"run_to", "walk_to", "run_past", "walk_past",
                                                "run",    "walk",    "hold",     "look_at"};
    return std::find(std::begin(kSet), std::end(kSet), action) != std::end(kSet);
}

std::optional<double> performanceStart(const Plan& plan, std::size_t index, const PlanTimes& times) {
    const PlanPerformance& p = plan.performances[index];
    if (!p.beats.empty() && p.beats.front().at) {
        return times.at(fmt::format("/performances/{}/beats/0/at", index));
    }
    for (std::size_t i = 0; i < plan.shots.size(); ++i) {
        if (plan.shots[i].subject == p.subject) {
            return times.at(fmt::format("/shots/{}/start", i));
        }
    }
    return std::nullopt;
}

CompiledPerformance compilePerformance(const Plan& plan, std::size_t index, const SceneFacts& facts,
                                       const PlanTimes& times) {
    const PlanPerformance& perf = plan.performances[index];
    const Subject* subject = plan.subject(perf.subject);
    const CharacterCard* card = facts.capabilities.character(subject->id);
    const CharacterMark* mark = facts.character(subject->id);
    const glm::vec3 anchor = mark != nullptr ? mark->anchor : glm::vec3(0.0f);
    const auto speedOf = [&](std::string_view action) {
        return isRun(action) ? std::max(card->runSpeed * kRunFraction, 0.5f) : std::max(card->walkSpeed, 0.3f);
    };
    const auto placeOf = [&](const std::string& alias) -> const Place* {
        const Subject* s = plan.subject(alias);
        return s == nullptr ? nullptr : facts.place(s->id);
    };

    CompiledPerformance out;
    out.actor.id = subject->id;
    out.actor.node = mark != nullptr && mark->node != subject->id ? mark->node : std::string();
    double t = *performanceStart(plan, index, times);
    out.from = t;

    // ---- the start mark: authored facts only (ADR-758) -------------------------------------------
    glm::vec3 p = anchor;
    glm::vec3 dir(0.0f, 0.0f, 1.0f);
    if (!perf.beats.empty()) {
        const PerformanceBeat& first = perf.beats.front();
        if (const Place* target = placeOf(first.target); target != nullptr && (isTo(first.action) || isPast(first.action))) {
            dir = flatDir(anchor, target->position, dir);
            const float speed = speedOf(first.action);
            const double lead = first.seconds.value_or(kLeadSeconds);
            if (isTo(first.action)) {
                const glm::vec3 stop = target->position - (dir * (target->radius + kStopMargin));
                p = stop - (dir * static_cast<float>(speed * lead));
            } else {
                // Behind the pass point on a line parallel to anchor->target, offset so the target
                // is on the performer's left: the tangent from this mark is that same line.
                const glm::vec3 right(dir.z, 0.0f, -dir.x);
                const glm::vec3 pass = target->position - (right * (target->radius + kPassMargin));
                p = pass - (dir * static_cast<float>(speed * lead));
            }
        }
    }
    p.y = anchor.y; // the `ground` behaviour decides the height; the plan decides the ground path
    const auto key = [&](double at, glm::vec3 where, std::optional<float> yaw = std::nullopt) {
        seq::ActorKey k;
        k.timeSeconds = at;
        k.position = glm::vec3(where.x, anchor.y, where.z);
        if (yaw) {
            k.rotationDegrees = glm::vec3(0.0f, *yaw, 0.0f);
        }
        k.interp = params::KeyInterp::Linear;
        out.actor.keys.push_back(k);
    };
    key(t, p);

    for (std::size_t b = 0; b < perf.beats.size(); ++b) {
        const PerformanceBeat& beat = perf.beats[b];
        // A later `at` holds until then; an earlier one is the validator's to report.
        if (b > 0 && beat.at) {
            if (const auto at = times.at(fmt::format("/performances/{}/beats/{}/at", index, b)); at && *at > t) {
                t = *at;
                key(t, p);
            }
        }
        const double start = t;
        const float speed = speedOf(beat.action);
        double eventTime = t;
        if (isTo(beat.action) || isPast(beat.action)) {
            const Place* target = placeOf(beat.target);
            dir = flatDir(p, target->position, dir);
            if (isTo(beat.action)) {
                const glm::vec3 stop = target->position - (dir * (target->radius + kStopMargin));
                t += glm::length(glm::vec2(stop.x - p.x, stop.z - p.z)) / speed;
                p = stop;
                key(t, p);
                eventTime = t;
                out.summary.push_back(fmt::format("{} {} to {:.1f} m short of {} ({:.1f} m/s)", subject->id,
                                                  isRun(beat.action) ? "runs" : "walks", target->radius + kStopMargin,
                                                  target->id, speed));
            } else {
                // The tangent: the straight line from here that passes the target at exactly the
                // clearance, keeping it on the performer's LEFT. Its closest approach IS the pass
                // point, which is where the event fires. (Aiming at the centre and then stepping
                // sideways bends the path, and the true closest approach falls after the event.)
                const float clearance = target->radius + kPassMargin;
                const glm::vec2 d(target->position.x - p.x, target->position.z - p.z);
                const float dist = glm::length(d);
                glm::vec3 pass = p;
                if (dist > clearance) {
                    const glm::vec3 toward = flatDir(p, target->position, dir);
                    const glm::vec3 right(toward.z, 0.0f, -toward.x);
                    const float sinT = clearance / dist;
                    const float cosT = std::sqrt(1.0f - sinT * sinT);
                    dir = glm::normalize((toward * cosT) + (right * sinT));
                    pass = p + (dir * std::sqrt((dist * dist) - (clearance * clearance)));
                }
                t += glm::length(glm::vec2(pass.x - p.x, pass.z - p.z)) / speed;
                p = pass;
                key(t, p);
                eventTime = t; // closest approach
                const glm::vec3 beyond = pass + (dir * (target->radius + kStopMargin + speed * static_cast<float>(kDefaultBeat)));
                t += glm::length(glm::vec2(beyond.x - p.x, beyond.z - p.z)) / speed;
                p = beyond;
                key(t, p);
                out.summary.push_back(fmt::format("{} {} past {}, keeping it {:.1f} m off on the left ({:.1f} m/s)",
                                                  subject->id, isRun(beat.action) ? "runs" : "walks", target->id,
                                                  target->radius + kPassMargin, speed));
            }
        } else if (beat.action == "run" || beat.action == "walk") {
            const double seconds = beat.seconds.value_or(kDefaultBeat);
            p += dir * static_cast<float>(speed * seconds);
            t += seconds;
            key(t, p);
            eventTime = t;
            out.summary.push_back(fmt::format("{} {} on for {:.2f} s", subject->id, beat.action == "run" ? "runs" : "walks", seconds));
        } else if (beat.action == "hold") {
            t += beat.seconds.value_or(kDefaultBeat);
            key(t, p);
            eventTime = t;
            out.summary.push_back(fmt::format("{} holds for {:.2f} s", subject->id, t - start));
        } else if (beat.action == "look_at") {
            const Place* target = placeOf(beat.target);
            const float yaw = target != nullptr ? yawDegrees(flatDir(p, target->position, dir)) : yawDegrees(dir);
            // A pair of rotation keys: the heading is explicit between them, and the body faces its
            // travel everywhere else (seq::performerFor).
            out.actor.keys.back().rotationDegrees = glm::vec3(0.0f, yaw, 0.0f);
            t += beat.seconds.value_or(kDefaultBeat);
            key(t, p, yaw);
            eventTime = start;
            out.summary.push_back(fmt::format("{} looks at {} for {:.2f} s", subject->id,
                                              target != nullptr ? target->id : std::string("ahead"), t - start));
        }
        if (!beat.emits.empty()) {
            out.events.emplace_back(beat.emits, eventTime);
        }
    }
    out.to = t;
    return out;
}

} // namespace avgen::directing
