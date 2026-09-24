#pragma once

// The Director's structured diagnostics (spec §17, §48).
//
// Every finding the Director makes -- a plan that does not parse, a subject it cannot name, a time it
// cannot place, a capability a character lacks -- is one `Issue`. It must be useful to a person
// reading a panel and to a model deciding what to try next, so each carries a closed `code` (the
// thing a program switches on), a sentence (the thing a person reads), where in the plan it is, why,
// whether it can be recovered from, and concrete suggestions. Never a bare "no".
//
// The code list is closed and its names are stable strings, because an LLM will be told them and
// tests will assert them; renaming one is a schema change.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::directing {

enum class Severity : std::uint8_t { Info, Warning, Error };
[[nodiscard]] const char* severityName(Severity severity);
[[nodiscard]] std::optional<Severity> severityFromName(std::string_view name);

enum class IssueCode : std::uint8_t {
    // ---- the document ----
    SchemaInvalid,            // a field has the wrong type or a required field is missing
    SchemaUnknownField,       // a field this version does not define (warning: it is ignored)
    SchemaVersionUnsupported, // written by a newer build
    DuplicateKey,             // two items in one plan share a key
    // ---- references ----
    UnknownSubject,           // no subject by that name
    AmbiguousReference,       // several subjects answer to that name
    UndeclaredSubject,        // an item names an alias the plan's subject table does not declare
    // ---- time ----
    MalformedTime,            // the text is not a time this parser understands
    UnresolvableTime,         // a well-formed time the song cannot place (no such section, no beat grid)
    AmbiguousTime,            // "the chorus" in a song with several
    TimeOutOfRange,           // before 0 or past the end of the piece
    // ---- capability ----
    CapabilityUnavailable,    // the subject cannot do what the plan asks
    Unsupported,              // the Director cannot compile this yet (named, never silently dropped)
    // ---- feasibility and conflicts ----
    SpatialInfeasible,        // geometry says no: a jump that cannot clear its obstacle
    TimingConflict,           // overlaps something that is already there, or another item
    CameraConflict,           // something with higher precedence may take the frame
    NonDeterministic,         // a rendered result would depend on live, unrecorded behaviour
    UnknownEvent,             // a cue waits on a plan event nothing emits
    HandEdited,               // content this plan made was since edited by hand; a revision will not overwrite it
    Blocked,                  // cannot happen because something it depends on cannot
};
inline constexpr IssueCode kLastIssueCode = IssueCode::Blocked;
[[nodiscard]] const char* issueCodeName(IssueCode code);
[[nodiscard]] std::optional<IssueCode> issueCodeFromName(std::string_view name);

struct Issue {
    Severity severity = Severity::Error;
    IssueCode code = IssueCode::SchemaInvalid;
    std::string subject;   // what it is about ("rook", "chorus 2"), may be empty
    std::string location;  // where in the plan: a JSON pointer ("/shots/0/start")
    std::string item;      // the plan item's key it concerns, when it concerns one
    std::string message;   // one sentence for a person
    std::string cause;     // why, when it is not obvious from the message
    bool recoverable = true; // can a revised plan get past it (false: e.g. a newer schema)
    nlohmann::json details = nlohmann::json::object(); // machine-readable specifics (candidates, available)
    std::vector<std::string> suggestions;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static std::optional<Issue> fromJson(const nlohmann::json& j);
    friend bool operator==(const Issue&, const Issue&) = default;
};

[[nodiscard]] bool hasErrors(const std::vector<Issue>& issues);

} // namespace avgen::directing
