#pragma once

// What the Auto-director knows about a song (ADR-249).
//
// **This file is the seam.** Everything the director learns from the song data model arrives
// through `SongPlan`, and `SongPlan` is built in exactly one place -- the two functions at the
// bottom of this header. Nothing downstream of here has ever heard of a Verse, a Chorus, an
// "Ocean Ambience", or a section type of any kind.
//
// ## Why a separate type at all
//
// The song-shot-language brief's single hardest requirement is that the director must not depend on
// musical labels: the system thinks
//
//     Chorus -> Dynamic Hero Coverage -> Auto Director -> available cameras -> camera decisions
//
// and never `Chorus = Camera 3`. A requirement like that is not enforceable by intention. It is
// enforceable by *not giving the director the label in a form it can branch on*, which is what this
// file does: a `ShotIntentProfile` is six numbers, a camera count and an opaque string id, and the
// string is written to logs and panels and is never compared against anything.
//
// The test that holds it is `every intent id can be scrambled without changing a single camera
// decision`. That test cannot pass if anybody adds `if (intent.id == "hero performance")`, which is
// the entire point of preferring a structural guarantee to a documented one.
//
// ## Who owns what
//
//   * The **song data model** owns `Section`, `SectionType` and `ShotIntent` -- the vocabulary, the
//     built-in library, the custom types a user makes, the analyzer's assignment of them, and the
//     re-analysis policy that keeps a person's edits. None of that is here.
//   * The **director** owns what a shot intent *means to a camera*: how wide, how much movement,
//     how much variation, how often to cut, how many cameras. That is `ShotIntentProfile`.
//
// `ShotIntentProfile` is deliberately not a subset of the shot intent: it is the *projection* of one
// onto the axes a camera can act on. A shot intent may carry a description, a category, an icon and
// a colour; a camera cannot do anything with any of them.

#include "analysis/structure.hpp"
#include "core/error.hpp"

#include "song/section_cue.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

// How much freedom the director has inside an authored section (the brief's section 10).
//
// Three, and the distinction is about *what the section fixes* rather than about how adventurous
// the result looks:
//
//   * `Locked` -- the section is one shot on one camera at the intent's nominal framing. The
//     authored decision is honoured as closely as a director can honour it. This is the setting
//     that makes Song Mode able to behave like an authored shot list when somebody wants that.
//   * `Guided` -- the intent is respected and the *execution* is the director's: which camera, how
//     many cuts, where within the intent's framing range each shot sits.
//   * `Expressive` -- the intent is a strong suggestion, and the director additionally varies
//     coverage with the section's own measured energy and density. A loud section gets more cameras
//     and shorter holds than a quiet one carrying the same intent.
//
// Ordered from least to most freedom, and that order is load-bearing: `>=` comparisons against
// `Guided` are how the director asks "am I allowed to choose".
enum class Autonomy : std::uint8_t { Locked, Guided, Expressive };
[[nodiscard]] const char* autonomyName(Autonomy a);
[[nodiscard]] std::optional<Autonomy> autonomyFromName(std::string_view name);
[[nodiscard]] std::span<const Autonomy> allAutonomies();

// A shot intent, as a camera can act on it.
//
// Every axis is 0..1 and every axis is a *preference*, not an instruction. The director reads them
// together: `distance` alone does not choose a shot kind, and `movement` alone does not choose a
// curve -- the pair does, which is what keeps this a vocabulary of intent rather than a compressed
// spelling of "Camera 2, 50 mm, 8 seconds".
struct ShotIntentProfile {
    // What the shot intent is called, for a log line and a panel row. **Opaque.** The director
    // never compares it, parses it, or switches on it. If it were removed the film would be
    // identical and only the log would be poorer, which is the test that it is genuinely inert.
    std::string id;

    // Whose shot this is: 0 is the environment, 1 is the hero. Feeds the cast rotation's weighting
    // and the spotlight emphasis, and is the axis "Atmospheric Establishing" and "Hero Performance"
    // most differ on.
    float heroEmphasis = 0.5f;
    // How far away the subject is seen from: 0 is intimate, 1 is the widest this world offers.
    // Expressed here rather than in metres or millimetres because the director already frames in
    // subject radii, and because a lens is a property of a camera (ADR-245) rather than of a wish.
    float distance = 0.5f;
    // How much the camera travels through the shot: 0 is locked off, 1 is constantly moving.
    float movement = 0.5f;
    // How much the director should *differ* from one shot to the next inside this section: 0 is one
    // setup held, 1 is keep finding new ones. Distinct from `cutRate`, which is how often; this is
    // how much changes when it does.
    float variation = 0.5f;
    // How often to cut: 0 is the longest hold the settings allow, 1 is the shortest.
    float cutRate = 0.5f;

    // How many distinct cameras the intent wants used across the section. 1 is "stay on one
    // viewpoint"; more is coverage. A *request*, not a guarantee -- a world with two eligible
    // cameras cannot honour a request for five, and the director says so in its log rather than
    // inventing a camera.
    int cameras = 1;

    // Refuses a profile that cannot mean anything: an axis outside 0..1, a camera count below 1 or
    // absurdly high, an empty id. Refused rather than clamped, for ADR-225's reason: the only route
    // to one is a hand edit or a file from a build that meant something else by the key.
    [[nodiscard]] Result<void> validate() const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<ShotIntentProfile> fromJson(const nlohmann::json& doc);

    friend bool operator==(const ShotIntentProfile&, const ShotIntentProfile&) = default;
};

// One section of the piece, in the director's terms.
//
// Note what is *not* here: no `SectionFunction`, no section type, no musical label of any kind.
// `label` is a display string and is used for exactly two things -- a log line and a panel row.
struct SongPlanSection {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::string label; // display only; see the note above
    ShotIntentProfile intent;

    // ---- the music, as generic quantities (the brief's section 13) -----------------------------
    //
    // The brief is explicit that music-aware behaviour must not become "a giant hard-coded
    // collection of musical rules", and these three are the whole of the alternative: a section is
    // loud or quiet, busy or sparse, and arrived at hard or softly. Everything a shot intent wants
    // to do with the music, it does with these.
    //
    // All measured, not assigned. `energy` and `density` come straight from the analysis (they are
    // `SongSection::energy` and `::density`, already normalised against the track's own range);
    // `transition` is the size of the energy step across this section's opening boundary, which is
    // a measurement of the boundary rather than a claim about what kind of boundary it is.
    float energy = 0.5f;
    float density = 0.5f;
    float transition = 0.0f;

    Autonomy autonomy = Autonomy::Guided;

    // Which time round this material is. 0 the first time, 1 the second, and so on -- carried from
    // the analysis's repetition grouping, or from the plan's own count of identical intents when
    // there is no grouping.
    //
    // **This is what makes two passes over the same section different.** Verse 1 and Verse 2 carry
    // the same intent and differ in `occurrence`, the director folds `occurrence` into its
    // decision hash, and the two passes get different cameras, different framings and different cut
    // points from one bake -- with no per-frame randomness anywhere and therefore no cost to
    // ADR-091's scrub determinism. See ADR-249.
    int occurrence = 0;

    [[nodiscard]] double durationSeconds() const { return endSeconds - startSeconds; }

    friend bool operator==(const SongPlanSection&, const SongPlanSection&) = default;
};

// The whole song, as the director sees it. Ordered and non-overlapping; gaps are allowed, because a
// person may legitimately author coverage for part of a piece and leave the rest alone.
struct SongPlan {
    std::string name;
    std::vector<SongPlanSection> sections;

    [[nodiscard]] bool empty() const { return sections.empty(); }
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] const SongPlanSection* sectionAt(double seconds) const;

    // Refuses a plan that cannot be directed: a section that ends before it starts, two sections
    // that overlap, sections out of order, or a profile `validate()` refuses.
    [[nodiscard]] Result<void> validate() const;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<SongPlan> fromJson(const nlohmann::json& doc);

    friend bool operator==(const SongPlan&, const SongPlan&) = default;
};

// ================================================================================================
// THE SEAM
// ================================================================================================
//
// Two ways a plan comes into existence, and both of them are here so that there is exactly one file
// to change when the song data model publishes its vocabulary.
//
// **The merge happened, and the note above was one type wrong.** It predicted
//
//     Result<SongPlan> songPlanFrom(std::span<const song::Section>);
//
// and the published vocabulary does not hand a `song::Section` to the director at all. What it
// publishes is `song::SectionCue`: the section's span, its resolved `ShotIntent` by value, its
// measured energy and density, and its occurrence -- with the section TYPE deliberately absent, and
// a test that fails the build if `section_type.hpp` ever becomes reachable from here.
//
// That is a better seam than the one predicted, and it is the seam that exists. The adapter reads a
// cue rather than a section, and nothing else in the director moved -- which is the property this
// file was built to buy, and it held across a shape neither side chose alone.

// The authored plan, from JSON. This is what a project carries and what the demo ships: a plan
// somebody wrote down, with the intent ids and profiles they chose.
[[nodiscard]] Result<SongPlan> songPlanFromJson(const nlohmann::json& doc);

// **The adapter.** A cue sheet from the song data model becomes a plan the director can execute.
//
// This is the only function in the application that reads both vocabularies, and it is deliberately
// the whole of the coupling between them: `song::` knows nothing about cameras, `app::SongPlan`
// knows nothing about section types, and this projects one onto the other.
//
// `ShotIntent` carries more than a director can act on -- a description, a name, a built-in flag, a
// framing RANGE, a camera range, an arc. `ShotIntentProfile` is five axes and a count. The mapping
// is a projection and loses information on purpose; what it must not lose is the *ordering* of the
// axes, because that is what the director actually consumes.
[[nodiscard]] Result<SongPlan> songPlanFromCues(std::span<const song::SectionCue> cues);

// A plan derived from an analyzed structure **without reading a single label**.
//
// A stand-in, and stated as one: the song data model's job is to assign each section a type and
// give each type a default shot intent, and when that lands this function is superseded by the
// `songPlanFrom` above. Until then it is what makes the beginner path in the brief's section 11 --
// import, analyze, Auto-director: Song, play -- produce something rather than nothing.
//
// It is also a genuine architectural artefact rather than only a placeholder, and worth keeping as
// a test even after it is superseded: it reads `startSeconds`, `endSeconds`, `energy`, `density`
// and `repetitionGroup`, and it reads the display name *only* to fill in `label`. It never looks at
// `SectionFunction`. So it demonstrates that this engine can direct a piece whose sections nobody
// has named -- an ambient track the detector honestly answered `Other` for, which ADR-206 says is
// the correct answer and not a degenerate one.
//
// The projection, stated once so it can be argued with:
//
//   heroEmphasis  rises with energy      -- a loud passage is about its subject
//   distance      falls with energy      -- ...and gets closer to it
//   movement      rises with density     -- a busy passage moves
//   variation     rises with density     -- ...and changes more when it does
//   cutRate       rises with both        -- and cuts more often
//   cameras       1, or 2 above a threshold of energy, or 3 at the top
//
// Every one of those is a defensible default and none of them is a rule. The moment a person
// assigns a section type, that type's shot intent replaces the lot.
[[nodiscard]] SongPlan songPlanFromMeasurements(const analysis::SongStructure& structure);

} // namespace avgen::app
