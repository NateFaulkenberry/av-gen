#pragma once

// The behaviour trace and the "why" report (Phase D §40, §41, §64, §65).
//
// **§41 is "not optional"**, and this is the answer to it that needs no editor: for any character,
// a block of text that says what it is doing, about what, why that won, what it is attending to,
// what it intends, how far it has to go and how the body is moving. The same text an overlay
// prints, a test asserts on and a person pastes into a bug report.
//
// **§64 is "one of the strongest regression tests for Phase D"**: simulate a scene for N seconds and
// produce a line per decision. `BehaviorTraceRecorder` collects those lines from every deciding
// character's `DecisionDebug::history` -- the record the decider keeps anyway -- so tracing costs
// nothing when nobody records, and "run it twice and compare" is a string comparison.
//
// Read-only over the world. Nothing here changes what any character does (§65: "allow traces to be
// disabled in production" -- they are off by not constructing a recorder).

#include "entity/behavior.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace avgen::entity {

class EntityWorld;

// One line: "  12.42  scout  roam -> investigate mushroom-1  0.82 (runner-up roam 0.47) ...".
[[nodiscard]] std::string formatTraceEntry(std::string_view entity, const DecisionTraceEntry& e);

class BehaviorTraceRecorder {
public:
    // Call after each world step. Appends every decision recorded since the last call, in entity
    // order, so two runs of the same simulation produce the same lines in the same order.
    void sample(const EntityWorld& world);
    [[nodiscard]] const std::vector<std::string>& lines() const { return lines_; }
    [[nodiscard]] std::string text() const;
    // Decisions dropped because a character's bounded history rolled over between two samples.
    // Zero whenever `sample` is called every step, which is how it is meant to be used.
    [[nodiscard]] std::size_t missed() const { return missed_; }
    void clear();

private:
    std::vector<std::string> lines_;
    std::vector<std::size_t> seen_; // per entity: `historyTotal` already recorded
    std::size_t missed_ = 0;
};

// The §41 report for entity `index`. Empty for an entity that does not decide.
[[nodiscard]] std::string explainCharacter(const EntityWorld& world, std::size_t index);

} // namespace avgen::entity
