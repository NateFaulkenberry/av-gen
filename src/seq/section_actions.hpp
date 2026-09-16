#pragma once

// The other half of ADR-216's seam: turning a fired section event into an action a character does.
//
// `section_performance.hpp` had a hole in it -- `SectionPerformanceTable`, "the entire remaining job" --
// and filling that hole alone would not have moved anything on screen. The generated events reach
// `Engine::firedEvents()` and stop there, deliberately: `engine.hpp` says an `EntityAction` "belongs
// to the action system, and the engine inventing an interpretation would be the second event system
// this design exists to avoid". It was right, and the consequence was that nothing in the shipping
// application read a fired event at all. Only a test did.
//
// So the seam needed two pieces, not one:
//
//   1. a table an author can actually fill (`SectionPerformanceSet`, below), and
//   2. one place that knows how a `{subject, verb, argument}` becomes an `entity::ActionDesc`
//      (`actionFromEvent`, below).
//
// This file is piece two, and it is the only place that mapping exists. The engine calls it and
// applies the result; it does not decide what a verb means, and neither does `section_performance.hpp`.
//
// ## Why the table is authored rather than built in
//
// `section_performance.hpp` warns that it "must not grow an opinion about the vocabulary of
// behaviours, or it becomes the thing it is a seam for". A built-in table saying *a Drop means the
// visitor hovers* would do exactly that -- it would put one scene's cast into a generic seam, which
// is the mistake ADR-245 spent a day retiring from the camera side.
//
// So the default still declines everything, and an authored `SectionPerformanceSet` is what fills it.
// "An empty generated sequence is an honest report that the Director is not wired up" stays true for
// a project nobody has authored, and becomes false the moment somebody does.

#include "core/error.hpp"
#include "entity/action.hpp"
#include "seq/section_performance.hpp"
#include "signals/musical_events.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::seq {

// One authored row: "when a section of this kind begins, this subject does this".
struct SectionPerformanceEntry {
    signals::MusicalSection kind = signals::MusicalSection::Intro;
    SectionPerformance direction;
    [[nodiscard]] bool operator==(const SectionPerformanceEntry&) const = default;
};

// The authored table. A vector rather than a map because it round-trips as an array and because the
// order a person wrote the rows in is the order they should read back.
struct SectionPerformanceSet {
    std::vector<SectionPerformanceEntry> entries;

    // The first row matching `kind`, or nothing. First rather than last so an earlier row wins,
    // which is the rule a reader assumes from a list.
    [[nodiscard]] const SectionPerformance* find(signals::MusicalSection kind) const;
    [[nodiscard]] bool empty() const { return entries.empty(); }
    [[nodiscard]] bool operator==(const SectionPerformanceSet&) const = default;
};

// Every verb must name an `entity::ActionKind`, and every subject must be non-empty: a row that
// names neither is a row that would generate an event nothing could apply.
[[nodiscard]] Result<void> validate(const SectionPerformanceSet& set);

// ADR-225: a setting the application does not keep is not a setting.
[[nodiscard]] nlohmann::json toJson(const SectionPerformanceSet& set);
[[nodiscard]] Result<SectionPerformanceSet> sectionPerformanceSetFromJson(const nlohmann::json& doc);

// The authored set as the table `generatePerformanceEvents` takes. Declines exactly what the set does
// not mention, so an empty set behaves identically to `defaultSectionPerformanceTable()`.
[[nodiscard]] SectionPerformanceTable tableFrom(SectionPerformanceSet set);

// ---- applying a firing ---------------------------------------------------------------------------

// What the action system should be asked to do, and of whom.
struct DirectedAction {
    std::string entity;
    entity::ActionDesc action;
};

// `{value, argument}` from a generated event becomes an `ActionDesc`.
//
// The verb is an `entity::ActionKind` name and nothing else -- `move`, `face`, `pose`, `wait`,
// `interact`, `equip`, `unequip`, `set` -- because inventing a second verb vocabulary here is the
// duplication both halves of this seam exist to refuse. An unknown verb is an error carrying the
// list, not a silent no-op: a section that was supposed to make something happen and made nothing
// happen is the failure mode this repository keeps writing ADRs about.
//
// `duration` is seconds and `argument` is the target's name; which of the two a verb uses is the
// verb's business, and a verb that uses neither ignores both.
[[nodiscard]] Result<DirectedAction> actionFromEvent(std::string_view subject, std::string_view verb,
                                                     std::string_view argument);

} // namespace avgen::seq
