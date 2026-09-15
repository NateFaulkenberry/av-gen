#pragma once

// The seam between the song structure and the Director (ADR-216, the brief's sections 10 and 12).
//
// "Generate initial Director sequence" is one sentence with two halves, and only one of them belongs
// here. The half that belongs here is a **translation**: an edited `analysis::SongStructure` becomes
// `seq::SequenceEvent`s whose triggers are `TriggerKind::Section`. The half that does not is the
// decision about *what a camera should actually do* when a drop lands, which is the Director
// decision layer (`docs/director-poc-plan.md`) and is being built separately.
//
// So this file deliberately contains **no direction**. It contains the shape of the connection and
// one hole -- `SectionDirectionTable` -- and the hole is the entire remaining job. Nothing here
// invents a behaviour, and `defaultSectionDirectionTable()` answers "nothing" for every kind on
// purpose: an empty generated sequence is an honest report that the Director is not wired up, and a
// generated sequence full of guesses would be a second director competing with the real one. That is
// the duplication this project keeps paying for when it is not refused up front.
//
// ## What a generated Director event is, exactly
//
// One `SequenceEvent` per distinct section *name* in the structure, because `TriggerKind::Section`
// matches markers by name and the markers are derived from the structure:
//
//     when.kind   = TriggerKind::Section
//     when.name   = the section's display name -- its label if a person gave it one, otherwise its
//                   function's name. This is the string the marker carries, and it is why renaming a
//                   section is an edit with consequences rather than a decoration.
//     when.repeat = 0 (unlimited): one event fires at *every* occurrence of that name, which is what
//                   makes this a table from section kind to behaviour rather than a shot list.
//     what.kind   = EventActionKind::EntityAction -- the SCHEDULED tier of seq/events.hpp. Correct
//                   by that file's own rule: the *when* is knowable before the piece runs and the
//                   *what* is imperative ("hand this behaviour to the Director"), which is exactly
//                   tier 2. It is not bakeable and must not pretend to be.
//     what.target = the subject the Director should act on (a cast role, an entity name)
//     what.value  = the verb
//     what.argument = the verb's object, where it has one
//
// Nothing is generated for a section kind the table declines, and every decline is reported as a
// warning rather than silently skipped -- "the Director had no answer for `bridge`" is the single
// most useful thing this can say while the table is being filled in.

#include "analysis/structure.hpp"
#include "seq/events.hpp"
#include "signals/musical_events.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace avgen::seq {

// What the Director should be asked to do when a section of a given kind begins.
//
// Every field here is a string the Director layer defines the meaning of. That is on purpose: this
// file must not grow an opinion about the vocabulary of behaviours, or it becomes the thing it is a
// seam for.
struct SectionDirection {
    std::string subject;  // which entity or cast role -- `EventAction::target`
    std::string verb;     // what to ask of it -- `EventAction::value`
    std::string argument; // the verb's own object, where it needs one
    int priority = 0;     // ordering against other events at the same instant
    double delaySeconds = 0.0; // fire this long after the section opens; usually 0
};

// **The thing to fill in.** Given a section kind in the director's vocabulary, what the Director
// should do -- or nothing, which is a real answer and the current one for every kind.
using SectionDirectionTable =
    std::function<std::optional<SectionDirection>(signals::MusicalSection)>;

// Declines everything. Not a stub awaiting a body: the honest answer until a Director decision layer
// exists to answer differently, and it keeps "generate" from fabricating a cut.
[[nodiscard]] SectionDirectionTable defaultSectionDirectionTable();

struct GenerationOptions {
    SectionDirectionTable table = defaultSectionDirectionTable();
    std::string idPrefix = "director";
    // Where in the piece a payoff becomes *the* payoff; passed through to `sectionKindsFor`, so a
    // generated sequence distinguishes the last drop from the first exactly as the shot planner
    // does.
    double finalFraction = 0.55;
};

struct GeneratedDirection {
    std::vector<SequenceEvent> events;
    // One line per section kind the table had no answer for. A person reading "no direction for
    // bridge, break" knows precisely what is missing; an empty event list on its own does not.
    std::vector<std::string> warnings;
};

// The translation. Pure, deterministic, and dependent on nothing that is not in its arguments.
[[nodiscard]] GeneratedDirection generateDirectorEvents(const analysis::SongStructure& structure,
                                                        const GenerationOptions& options = {});

} // namespace avgen::seq
