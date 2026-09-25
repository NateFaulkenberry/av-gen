#pragma once

// Musical and clock time for the Director (spec §13, ADR-755).
//
// A person says "1:30", "90s", "bar 64 beat 3", "the second chorus", "end of the bridge". The LLM is
// not allowed to turn those into seconds (spec §1.4): it writes a `TimeRef` -- or the text itself --
// and the Director resolves it against the piece. So a TimeRef is the *request*, kept in the plan as
// written, and `resolveTime` is the only arithmetic.
//
// Resolution reads a `MusicalContext`, a plain value the host builds from the sequence's section
// timeline, the analysed beat grid and the tempo. The module never reads the engine (ADR-750), and a
// test can hand it any song it likes.
//
// **Sections are counted as runs.** A section timeline may split one musical passage into several
// consecutive entries of the same type -- Glowmere's first chorus is three entries, 89.1-111.3 s, so
// its shots can differ. "The second chorus" means the second time the chorus *comes round*, not the
// second entry, so consecutive entries of one type are one occurrence (ADR-755).

#include "directing/issue.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::seq {
struct Sequence;
}

namespace avgen::directing {

struct TimeRef {
    // Event (ADR-767): when something happened in the film -- a world event, observed by watching a
    // play of the project from zero (`Plan::observation`). What makes a plan event-driven.
    enum class Kind : std::uint8_t { Seconds, Bar, Section, Event };
    enum class Anchor : std::uint8_t { Start, End };

    Kind kind = Kind::Seconds;
    double seconds = 0.0;     // Seconds
    int bar = 0;              // Bar: 1-based
    int beat = 1;             // Bar: 1-based beat within the bar
    std::string section;      // Section: the type id, normalised ("chorus", "middle_eight")
    // Section: 1-based occurrence; 0 = not said (ambiguous when the song has several); -1 = the last.
    int occurrence = 0;
    Anchor anchor = Anchor::Start; // Section: its start or its end
    std::string event;             // Event: the world event's name ("abduction/beam", "goal.arrived")
    std::string eventSubject;      // Event: who raised it (an entity), or empty for anyone
    double offsetSeconds = 0.0;    // added after resolution ("chorus 2 + 1.5s")
    std::string text;              // as the request wrote it, for display; never parsed again

    [[nodiscard]] static TimeRef at(double seconds) {
        TimeRef t;
        t.seconds = seconds;
        return t;
    }
    [[nodiscard]] nlohmann::json toJson() const;
    // Fails with a SCHEMA_INVALID issue naming `location` when the document is not a TimeRef.
    [[nodiscard]] static std::optional<TimeRef> fromJson(const nlohmann::json& j, std::string_view location,
                                                         std::vector<Issue>& issues);
    // A canonical reading: "01:30.000", "bar 64 beat 3", "start of chorus 2 + 1.500s".
    [[nodiscard]] std::string describe() const;
    friend bool operator==(const TimeRef&, const TimeRef&) = default;
};

// Parses what a person or a model wrote. Accepts:
//   "1:30", "01:30.5", "1:02:03"          clock (m:ss, h:mm:ss)
//   "90", "90s", "90.5 sec", "90 seconds" seconds
//   "bar 64", "bar 64 beat 3"             1-based bar and beat
//   "chorus 2", "second chorus", "the last verse", "start of the bridge", "end of chorus 2",
//   "middle eight", "post-chorus"         a section, by type and occurrence
//   any of the above followed by "+ 1.5s" / "- 0.5s"
// A MALFORMED_TIME issue on anything else; never a guess.
[[nodiscard]] std::optional<TimeRef> parseTime(std::string_view text, std::vector<Issue>& issues,
                                               std::string_view location = {});

// One passage of the piece, with consecutive same-type entries already merged.
struct SectionRun {
    std::string type;  // "chorus"
    std::string label; // a person's name for it, when they gave one
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int occurrence = 1; // 1-based among runs of this type
};

// One thing that happened in a watched play (ADR-767): a world event, when, and who raised it.
struct ObservedEvent {
    std::string name;
    std::string subject; // the entity that raised it, or empty
    double seconds = 0.0;
    friend bool operator==(const ObservedEvent&, const ObservedEvent&) = default;
};

struct MusicalContext {
    std::vector<double> beatTimes; // ascending; the analysed grid
    int beatsPerBar = 4;
    double tempoBpm = 0.0;          // for a constant-tempo fallback when there is no grid
    double firstBeatSeconds = 0.0;  // ...and its origin
    std::vector<SectionRun> sections;
    double durationSeconds = 0.0;   // 0 = unknown; no upper bound is checked
    // Where the sections came from: "sectionTimeline" (the person's), "structure" (the analyser's), or
    // "" (none). Reported on every section resolution, so a plan never silently uses the detector's
    // guess when the person has authored their own.
    std::string sectionSource;
    // ADR-767: the events a watched play of the project raised, when a plan carries an observation.
    // `observedUntil` < 0: nothing was watched, so an Event time cannot be placed.
    std::vector<ObservedEvent> observed;
    double observedUntil = -1.0;
};

// Builds the context from a sequence: the authored section timeline when it has one, otherwise the
// analyser's structure (function names), merged into runs. The beat grid and tempo come from the host
// (the engine's analysis track), because a `seq::Sequence` carries beat *markers* only when somebody
// asked for them.
[[nodiscard]] MusicalContext musicalContextFrom(const seq::Sequence& sequence, std::span<const double> beatTimes,
                                                double tempoBpm, double durationSeconds);

struct TimeResolution {
    std::optional<double> seconds;
    std::vector<Issue> issues; // errors when unresolved; warnings (e.g. a constant-tempo bar) when resolved
    std::string explanation;   // how a section time was placed, for the diff: "chorus 2 runs 118.6-132.5s"
};

[[nodiscard]] TimeResolution resolveTime(const TimeRef& ref, const MusicalContext& context,
                                         std::string_view location = {});

// "90" -> "90", "Middle-Eight" -> "middle_eight": the one normalisation the parser and the context
// share, so the two can never disagree about a type's spelling.
[[nodiscard]] std::string normaliseSectionType(std::string_view text);

} // namespace avgen::directing
