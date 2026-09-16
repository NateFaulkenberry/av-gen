#pragma once

// **Shot intent: what a passage should feel like, said without naming a camera** (ADR-247).
//
// This is the leaf of the song-aware shot language and it deliberately includes almost nothing. It
// knows nothing about sections, nothing about music, nothing about the analyzer, and above all
// nothing about cameras. That is the entire point of it existing as its own header: the Auto-director
// includes *this* and gets a complete description of what to do, without acquiring the ability to ask
// what a chorus is.
//
// ## Why not just use `app::ShotKind`
//
// `app::ShotKind` (`app/cinematic.hpp`) is the *literal* vocabulary -- Establish, Orbit, Flyby -- and
// every value of it names a camera move. It answers "what does the camera do". A shot intent answers
// the question one level up: "what is this passage *for*", from which a director may pick any of
// several moves, on any of several cameras, and pick differently on the next pass. `Hero Performance`
// is an intent; `Orbit at 12 metres for 8 seconds` is what a director decided this time.
//
// The distinction is load-bearing rather than decorative. If `Chorus` meant `Camera 2` there would be
// nothing for a director to do and no way for the same song to be cut twice. The brief's own phrasing:
// a shot intent "should NOT simply mean Camera 2, Position X/Y/Z, Lens 50mm, Duration 8 seconds".
//
// ## The dials, and why these and not more
//
// Eight numbers and three enums. Each one earns its place by being something a director can act on
// *without* a second table telling it what the value means:
//
//   focus + focusStrength  -- who the passage is about, and how tightly to hold to them
//   framing                -- the range of shot sizes that suit it
//   movement               -- how much the camera should be travelling
//   energy                 -- how hard the treatment should push overall
//   variation              -- how different successive shots should be from each other
//   cutFrequency           -- one held shot, or many
//   visualDensity          -- how much should be in frame
//   cameras                -- how many distinct cameras the coverage wants
//   arc                    -- how all of the above should travel across the section
//
// `arc` is the one that stops this becoming a rule engine. The brief asks for "Build -> progressively
// increase movement", "Riser -> movement accumulates toward the boundary", "Pause -> hold or suspend"
// -- three musical rules. They are one generic parameter instead: the intent says `Rising`, and
// `atProgress()` applies it. A director reading `Rising` never learns that risers exist, and a custom
// section type called "Ocean Swell" gets the same behaviour for free. That is the difference between
// a vocabulary and a hard-coded collection of musical special cases.
//
// ## Identity
//
// A `ShotIntentId` is a stable lowercase snake_case string, not an enum, because a user may define
// their own and because a project file saved today must still name the same intent in a year. The
// built-in ids are frozen: `tests/unit/test_shot_intent.cpp` pins every one of them against a literal
// list, so renaming one is a test failure rather than a silent project-file break.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace avgen::song {

// A stable identifier. Lowercase ASCII letters, digits and underscore; see `validateId`.
//
// `ShotIntentId` and `SectionTypeId` are both `std::string` and are therefore interchangeable to the
// compiler. That is a real hazard and it is answered where it can actually be caught: `ShotLanguage`
// validates every id against the registry it belongs to at define time and at resolve time, so an
// intent id used as a type id is an error with a message rather than a silent lookup miss.
using ShotIntentId = std::string;

// Whether an id is well-formed. Ids go in project files and in URLs-in-waiting; they are restricted
// so that a user-invented name cannot produce a file that will not round-trip.
[[nodiscard]] Result<void> validateId(std::string_view id);

// "Ocean Ambience" -> "ocean_ambience". Idempotent on an already-valid id. Returns nothing when the
// name has no id-safe characters in it at all, which is a real input ("???") and not a crash.
[[nodiscard]] std::optional<std::string> makeId(std::string_view displayName);

// ---- the semantic dials -------------------------------------------------------------------------

// Who the passage is about. Categorical, because "hero or environment" is a decision rather than a
// blend -- the *strength* of it is the continuous part, and that is `focusStrength`.
enum class SubjectFocus : std::uint8_t {
    Hero,        // one subject, and the shot is about them
    Ensemble,    // several subjects, and the shot is about the group
    Environment, // the world is the subject
    Mixed,       // no strong preference; the director may choose
};
[[nodiscard]] const char* subjectFocusName(SubjectFocus f);
[[nodiscard]] std::optional<SubjectFocus> subjectFocusFromName(std::string_view name);
[[nodiscard]] std::span<const SubjectFocus> allSubjectFocuses();

// Shot size, ordered tight to wide. The order is part of the contract: `FramingRange` compares these,
// and a director choosing "one step wider" relies on the enumerators being adjacent.
enum class Framing : std::uint8_t {
    ExtremeClose,
    Close,
    Medium,
    Wide,
    VeryWide,
};
[[nodiscard]] const char* framingName(Framing f);
[[nodiscard]] std::optional<Framing> framingFromName(std::string_view name);
[[nodiscard]] std::span<const Framing> allFramings();

// The band of shot sizes an intent suits. A *range* rather than a single size, because "Chorus is
// wide" is exactly the kind of semantic rule the brief forbids: an intent says which sizes read
// correctly and the director picks one.
struct FramingRange {
    Framing tightest = Framing::Medium;
    Framing widest = Framing::Wide;

    [[nodiscard]] bool contains(Framing f) const { return f >= tightest && f <= widest; }
    [[nodiscard]] Framing clamp(Framing f) const;
    // The middle of the band, rounding wider. Convenience for a director that wants one answer.
    [[nodiscard]] Framing middle() const;
    friend bool operator==(const FramingRange&, const FramingRange&) = default;
};

// How many distinct cameras the coverage wants. `most <= 0` means "as many as are available", which
// is what `Large-Scale Dynamic Coverage` actually means and is not expressible as a number.
struct CameraCount {
    int fewest = 1;
    int most = 1;

    [[nodiscard]] bool unbounded() const { return most <= 0; }
    // `available` cameras, narrowed to what this intent asks for. Never returns less than 1 and never
    // more than `available`, so a director with one camera still gets a usable answer from an intent
    // that wanted four.
    [[nodiscard]] int resolve(int available) const;
    friend bool operator==(const CameraCount&, const CameraCount&) = default;
};

// How an intent's dials travel across the span of its section.
//
// The four non-Steady shapes are the whole of this system's answer to "music-aware camera behaviour".
// They are stated as shapes rather than as musical causes precisely so that nothing downstream has to
// know what caused them.
enum class Arc : std::uint8_t {
    Steady,    // the dials mean what they say, for the whole section
    Rising,    // they arrive at their stated values at the section's end (a build, a riser, a swell)
    Falling,   // they hold their stated values at the start and decay (an outro, a release)
    Suspended, // held, and movement and cutting are pinned down (a pause, a stop)
    Burst,     // the stated values land at the section's start and settle back (a drop, an impact)
};
[[nodiscard]] const char* arcName(Arc a);
[[nodiscard]] std::optional<Arc> arcFromName(std::string_view name);
[[nodiscard]] std::span<const Arc> allArcs();

// ---- the intent ---------------------------------------------------------------------------------

// A named cinematic treatment.
//
// Every float is 0..1 and is a *preference*, not a command. A director is free to ignore any of them;
// what it must not do is go looking for the section type behind them.
struct ShotIntent {
    ShotIntentId id;
    std::string name;        // for people: "Hero Performance"
    std::string description; // one sentence, shown in a picker

    SubjectFocus focus = SubjectFocus::Mixed;
    float focusStrength = 0.5f; // 0: the subject is a suggestion. 1: never leave them.
    FramingRange framing{};
    float movement = 0.5f;       // 0: locked off. 1: always travelling.
    float energy = 0.5f;         // how hard the treatment pushes overall
    float variation = 0.5f;      // 0: every shot alike. 1: every shot different.
    float cutFrequency = 0.5f;   // 0: one shot for the section. 1: as fast as the cadence allows.
    float visualDensity = 0.5f;  // 0: one thing in frame. 1: a full frame.
    CameraCount cameras{};
    Arc arc = Arc::Steady;

    // Built-ins are not written to a project file: they are code, and writing them would mean a
    // project could resurrect a definition the engine has since improved. Custom intents are written.
    bool builtIn = false;

    // Ranges checked, ids checked, `framing.tightest <= framing.widest`, `cameras.fewest >= 1`.
    // Out-of-range values are **refused, not clamped** -- ADR-225's third rule, and the reason is
    // that a clamped 1.7 looks exactly like an authored 1.0 forever afterwards.
    [[nodiscard]] Result<void> validate() const;

    // The intent as it applies `progress` of the way through its section, with `arc` applied.
    //
    // `progress` is clamped to 0..1. Only `movement`, `energy`, `variation` and `cutFrequency` move:
    // framing, focus, density and camera count describe what the passage *is* and do not travel.
    // Steady returns an exact copy, which is what makes this safe to call unconditionally.
    //
    // A director may use this or ignore it. It exists so that "movement accumulates toward the
    // boundary" has one implementation instead of one per director mode.
    [[nodiscard]] ShotIntent atProgress(float progress) const;

    friend bool operator==(const ShotIntent&, const ShotIntent&) = default;
};

[[nodiscard]] nlohmann::json shotIntentToJson(const ShotIntent& intent);
[[nodiscard]] Result<ShotIntent> shotIntentFromJson(const nlohmann::json& j);

// ---- the built-in library -----------------------------------------------------------------------
//
// The brief's table of default treatments, plus the handful the table implies but does not name. Each
// is a starting point and every one of them is overridable per section.

// Every built-in intent, in a fixed order. One list, so a picker and a test cannot disagree about how
// many there are.
[[nodiscard]] std::span<const ShotIntent> builtInShotIntents();

// The built-in with this id, or nothing. Does **not** see custom intents; use `ShotLanguage` for that.
[[nodiscard]] const ShotIntent* builtInShotIntent(std::string_view id);

// The id every fallback lands on: a neutral, medium, moderately-covered treatment that is wrong for
// nothing in particular. Named rather than inlined so the fallback is greppable.
[[nodiscard]] ShotIntentId neutralShotIntentId();

} // namespace avgen::song
