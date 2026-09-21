#include "entity/behavior_trace.hpp"

#include "entity/entity.hpp"

#include <fmt/format.h>

#include <cmath>

namespace avgen::entity {

std::string formatTraceEntry(std::string_view entity, const DecisionTraceEntry& e) {
    std::string line = fmt::format("{:8.2f}  {:<10}  {} -> {}", e.time, entity,
                                   e.previous.empty() ? "(start)" : e.previous, e.option);
    if (!e.subject.empty()) {
        line += fmt::format(" [{}]", e.subject);
    }
    line += fmt::format("  intent={}  utility={:.3f}", intentTypeName(e.intent), e.score);
    if (!e.runnerUp.empty()) {
        line += fmt::format("  runner-up={} {:.3f}", e.runnerUp, e.runnerUpScore);
    }
    if (!e.previousOutcome.empty()) {
        line += fmt::format("  previous-plan={}", e.previousOutcome);
    }
    if (!e.factors.empty()) {
        line += fmt::format("  factors=({})", e.factors);
    }
    if (!e.attention.empty()) {
        line += fmt::format("  attending={}", e.attention);
    }
    return line;
}

void BehaviorTraceRecorder::sample(const EntityWorld& world) {
    const auto& entities = world.entities();
    if (seen_.size() != entities.size()) {
        seen_.assign(entities.size(), 0);
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        DecisionDebug debug;
        bool found = false;
        for (const auto& behavior : entities[i]->behaviors()) {
            if (behavior->decisionDebug(debug)) {
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }
        if (debug.historyTotal < seen_[i]) {
            seen_[i] = 0; // the world was reset (a seek): start this character's record again
        }
        const std::size_t fresh = debug.historyTotal - seen_[i];
        const std::size_t available = debug.history.size();
        if (fresh > available) {
            missed_ += fresh - available;
        }
        const std::size_t take = std::min(fresh, available);
        for (std::size_t k = available - take; k < available; ++k) {
            lines_.push_back(formatTraceEntry(entities[i]->name(), debug.history[k]));
        }
        seen_[i] = debug.historyTotal;
    }
}

std::string BehaviorTraceRecorder::text() const {
    std::string out;
    for (const std::string& l : lines_) {
        out += l;
        out += '\n';
    }
    return out;
}

void BehaviorTraceRecorder::clear() {
    lines_.clear();
    seen_.clear();
    missed_ = 0;
}

std::string explainCharacter(const EntityWorld& world, std::size_t index) {
    if (index >= world.entities().size()) {
        return {};
    }
    const Entity& e = *world.entities()[index];
    DecisionDebug d;
    NavDebug nav;
    bool decides = false;
    bool navigates = false;
    for (const auto& behavior : e.behaviors()) {
        decides = decides || behavior->decisionDebug(d);
        navigates = navigates || behavior->navDebug(nav);
    }
    if (!decides) {
        return {};
    }
    const EntityState& s = e.state();
    std::string out = fmt::format("Character {}\n", e.name());
    out += fmt::format("Current behavior: {}\n", d.chosen.empty() ? "(none)" : d.chosen);
    out += fmt::format("Why: {}\n", d.subject.empty() ? "(nothing in particular)" : d.subject);

    float utility = 0.0f;
    std::string_view runnerUp;
    float runnerUpScore = 0.0f;
    for (const ScoredOption& o : d.options) {
        if (o.chosen) {
            utility = o.score;
        }
    }
    for (const ScoredOption& o : d.options) {
        if (!o.chosen && o.score > runnerUpScore) {
            runnerUpScore = o.score;
            runnerUp = o.name;
        }
    }
    out += fmt::format("Why selected: utility = {:.3f}", utility);
    if (!runnerUp.empty()) {
        out += fmt::format(" (best alternative: {} {:.3f})", runnerUp, runnerUpScore);
    }
    out += "\n";
    out += fmt::format("Factors: {}\n", d.factors.empty() ? "(none reported)" : d.factors);
    if (d.aware) {
        if (d.attentionSubject.empty()) {
            out += "Attention: (nothing)\n";
        } else {
            out += fmt::format("Attention: {} (reason: {}, score {:.2f})\n", d.attentionSubject,
                               d.attentionReason, d.attentionScore);
        }
    }
    out += fmt::format("Current intent: {}", intentTypeName(s.intent.type));
    if (s.intent.hasTarget) {
        out += fmt::format(" -> ({:.1f}, {:.1f}, {:.1f})", s.intent.targetPosition.x,
                           s.intent.targetPosition.y, s.intent.targetPosition.z);
    }
    out += "\n";
    if (navigates && !nav.route.empty()) {
        // Remaining length: from the body to the leg it is walking, then along the rest.
        const glm::vec3 p = s.position();
        glm::vec2 from(p.x, p.z);
        float length = 0.0f;
        for (std::size_t i = std::min(nav.leg, nav.route.size()); i < nav.route.size(); ++i) {
            length += glm::length(nav.route[i] - from);
            from = nav.route[i];
        }
        out += fmt::format("Navigation: {:.1f} m path, {} waypoints left\n", length,
                           nav.route.size() - std::min(nav.leg, nav.route.size()));
    } else {
        out += "Navigation: (no route)\n";
    }
    out += fmt::format("Motion: {}  speed {:.2f} m/s (intended {:.2f})\n",
                       activityName(e.locomotion().activity), s.groundSpeed(), s.speed);
    out += fmt::format("Motion provider: {}\n",
                       e.desc().proceduralMotion
                           ? fmt::format("motion chain, provider #{}", e.motionChainResult().provider)
                           : std::string("clip (animation player)"));
    if (s.intent.stoppingDistance > 0.0f) {
        out += fmt::format("Current target: {:.1f} m\n", s.intent.stoppingDistance);
    }
    out += fmt::format("Plan: {}", d.planActive ? "running" : "idle");
    if (!d.lastOutcome.empty()) {
        out += fmt::format(" (last plan: {})", d.lastOutcome);
    }
    out += "\n";
    if (!runnerUp.empty()) {
        out += fmt::format("Next transition: {} (needs +{:.3f} over the margin)\n", runnerUp,
                           std::max(0.0f, utility - runnerUpScore));
    }
    return out;
}

} // namespace avgen::entity
