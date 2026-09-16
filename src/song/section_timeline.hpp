#pragma once

// **The authored section timeline** (ADR-247): what the film is made of, as opposed to what the
// audio was found to contain.
//
// The brief's section 2 is emphatic that these are not one object:
//
//     Song Analysis  -- what the audio appears to contain  (`analysis::SongStructure`, ADR-206)
//     Section        -- what the person says this part is  (this file)
//     Shot Intent    -- how it should be treated           (`song/shot_intent.hpp`)
//     Director       -- what the cameras actually do       (`app/`)
//
// So `analysis::SongStructure` stays exactly what it is: the detector's report, evidence, a thing
// that gets recomputed. A `SectionTimeline` is built from one (`song/from_analysis.hpp`) and is then
// the authored article -- the thing a person edits, the thing that goes in the project, and the thing
// the director is cut from. A re-analysis produces a new report and reconciles it against the
// authored timeline (`song/reanalysis.hpp`); it does not overwrite it.
//
// ## Provenance is per field, not per section
//
// This is the one place this model deliberately goes further than ADR-215, and the brief's section 18
// is why. Its worked example has a person make *three different kinds* of edit -- retype a section,
// change its shot, and move a boundary -- and then press Analyze.
//
// ADR-215's model is per section: touch anything about a section and the whole section freezes,
// boundaries included. It says so, and says it is "over-protective, and it is the deliberate direction
// to err in". That is right for a model that has only boundaries and labels. Here it is too blunt:
// renaming a passage to "Ocean Ambience" is not a claim about where the passage starts, and freezing
// the boundary because of it means a re-analysis can never improve a boundary on any section anybody
// ever renamed.
//
// So `Section::edited` is a mask of which *fields* a person set. Re-analysis replaces every field
// that is not in the mask and keeps every field that is. A retyped section with untouched boundaries
// keeps its type and gets better boundaries, which is what the person actually asked for.
//
// The one place it stays blunt on purpose: pinning **either** boundary pins the section's whole span.
// A section is a span, and half of one is not something a fresh detection can be carved around. That
// is a stated conservatism, and it is tested.
//
// ## Precision
//
// Boundaries are `double` seconds and are **never rounded**, at any point, including through JSON.
// `test_section_timeline.cpp` compares them bit for bit rather than within a tolerance, because a
// tolerance would pass against a serializer that quietly rounded to three decimals -- and a timeline
// that is forty milliseconds out everywhere still looks perfectly reasonable in a UI.

#include "core/error.hpp"
#include "song/section_type.hpp"
#include "song/shot_intent.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::song {

class ShotLanguage;

// ---- provenance ---------------------------------------------------------------------------------

// Which fields of a section a person set for themselves. A bitmask rather than five bools because it
// is passed around, unioned and tested as a unit, and because `edited == SectionField::None` is the
// question asked most often.
enum class SectionField : std::uint32_t {
    None = 0u,
    Type = 1u << 0,
    Label = 1u << 1,
    ShotIntent = 1u << 2,
    Start = 1u << 3,
    End = 1u << 4,
    Boundaries = Start | End,
    All = Type | Label | ShotIntent | Start | End,
};

[[nodiscard]] constexpr SectionField operator|(SectionField a, SectionField b) {
    return static_cast<SectionField>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr SectionField operator&(SectionField a, SectionField b) {
    return static_cast<SectionField>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr SectionField operator~(SectionField a) {
    return static_cast<SectionField>(~static_cast<std::uint32_t>(a) &
                                     static_cast<std::uint32_t>(SectionField::All));
}
constexpr SectionField& operator|=(SectionField& a, SectionField b) { return a = a | b; }
constexpr SectionField& operator&=(SectionField& a, SectionField b) { return a = a & b; }
[[nodiscard]] constexpr bool any(SectionField f) { return static_cast<std::uint32_t>(f) != 0u; }

// The names of the individual bits set in `f`, in declaration order, for serialization and for
// telling a person what a re-analysis is about to keep.
[[nodiscard]] std::vector<std::string> sectionFieldNames(SectionField f);
[[nodiscard]] Result<SectionField> sectionFieldsFromNames(std::span<const std::string> names);

// ADR-215's three-valued provenance, which this model derives rather than stores. Keeping the
// vocabulary identical matters: the same word must mean the same thing in both layers or the UI ends
// up explaining two systems.
enum class SectionOrigin : std::uint8_t {
    Detected, // the analyzer's, untouched; a re-analysis may replace all of it
    Refined,  // the analyzer's, with at least one field a person set; those fields are kept
    Authored, // made by a person from nothing; a re-analysis keeps all of it
};
[[nodiscard]] const char* sectionOriginName(SectionOrigin o);
[[nodiscard]] std::optional<SectionOrigin> sectionOriginFromName(std::string_view name);

// ---- a section ----------------------------------------------------------------------------------

struct Section {
    // Names a type in the `ShotLanguage` -- built-in or the person's own; the model does not know
    // which and nothing here behaves differently for either.
    SectionTypeId type;
    // The person's own name for it. Empty means "compose one from the type and the occurrence", which
    // is what makes `Verse`, `Verse 2`, `Verse 3` free rather than three stored strings.
    std::string label;

    double startSeconds = 0.0;
    double endSeconds = 0.0;

    // Empty means "whatever this type's default treatment is". Storing the *absence* rather than a
    // copy of the default is what makes "change the type and the treatment follows, unless I chose
    // one" fall out of the model instead of needing a rule.
    std::optional<ShotIntentId> shotIntent;

    bool authored = false;                      // created from nothing, rather than detected
    SectionField edited = SectionField::None;   // which fields a person set

    // ---- carried from the analysis, and meaningless once the matching field is edited -----------
    //
    // Confidences are *the detector's claim about its own guess* (ADR-206). The moment a person sets
    // a field it is not a guess any more and the number is not about anything. It is not cleared --
    // deleting the detector's record would be a second kind of data loss -- and it is not shown:
    // `confidenceIsMeaningful` is what a UI asks first.
    float labelConfidence = 0.0f;
    float startConfidence = 0.0f;
    float endConfidence = 0.0f;
    // Measured against the track's own distribution, not absolutely. These survive editing because
    // they are facts about the audio under the span rather than claims about the label.
    float energy = 0.0f;
    float density = 0.0f;
    // Which sections are the same music, from the detector. -1 means it found nothing to group with.
    int repetitionGroup = -1;

    // ---- derived, never stored -------------------------------------------------------------------
    // 0 for the first section of this type in the timeline, 1 for the second, and so on.
    // `SectionTimeline::renumber()` owns it and every mutating operation calls that; it is not
    // serialized, for the same reason the beat markers are not.
    int occurrence = 0;

    [[nodiscard]] double durationSeconds() const { return endSeconds - startSeconds; }
    [[nodiscard]] bool isEdited(SectionField f) const { return any(edited & f); }
    [[nodiscard]] SectionOrigin origin() const;
    // Whether this section's span may be moved by a re-analysis. False once either boundary is
    // pinned, or once the whole section was authored.
    [[nodiscard]] bool spanIsFrozen() const { return authored || isEdited(SectionField::Boundaries); }

    friend bool operator==(const Section&, const Section&) = default;
};

// Whether this section's confidences mean anything, field by field.
[[nodiscard]] bool labelConfidenceIsMeaningful(const Section& s);
[[nodiscard]] bool boundaryConfidenceIsMeaningful(const Section& s);

// ---- the timeline -------------------------------------------------------------------------------

struct SectionTimeline {
    // Ordered, gapless, non-overlapping, and covering 0..durationSeconds. `validate()` is the
    // statement of that and every mutating operation restores it.
    std::vector<Section> sections;
    double durationSeconds = 0.0;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] bool empty() const { return sections.empty(); }

    [[nodiscard]] const Section* at(double seconds) const;
    [[nodiscard]] std::optional<std::size_t> indexAt(double seconds) const;
    [[nodiscard]] int countOfType(std::string_view type) const;
    // The index of the last section of `type`, or nothing. What "the final chorus" is derived from.
    [[nodiscard]] std::optional<std::size_t> lastOfType(std::string_view type) const;

    // Recomputes every `occurrence`. Called by each editing operation; call it yourself only after
    // reaching into `sections` directly.
    void renumber();

    friend bool operator==(const SectionTimeline&, const SectionTimeline&) = default;
};

[[nodiscard]] nlohmann::json sectionTimelineToJson(const SectionTimeline& timeline);
[[nodiscard]] Result<SectionTimeline> sectionTimelineFromJson(const nlohmann::json& j);

// ---- editing (the brief's section 8) ------------------------------------------------------------
//
// Everything the brief lists: move a boundary, split, merge, rename, retype, change the treatment,
// add and remove. Each marks exactly the fields it touched and nothing else, which is what gives
// re-analysis something precise to honour.
//
// Every one of these preserves the timeline's invariants or refuses. None of them silently drops a
// section, and none of them rounds a time.

// Boundary `index` separates section `index - 1` from section `index`; valid indices are
// 1 .. sections.size() - 1, so the piece's own start and end are not boundaries and cannot be
// dragged. `newTime` is clamped so neither neighbour falls under `minSectionSeconds` and is otherwise
// stored **exactly** as given. Marks `End` on the earlier section and `Start` on the later one.
// Returns the time actually used, or nothing when `index` is not a boundary.
[[nodiscard]] std::optional<double> moveBoundary(SectionTimeline& timeline, std::size_t index,
                                                 double newTime, double minSectionSeconds = 0.25);

// Splits the section containing `seconds` there. The earlier half keeps everything it had and is
// marked `End`; the later half is a copy that is marked `Authored` -- it did not exist before, and
// its detector confidences are dropped because they were a claim about a different span. Returns the
// index of the new later half.
[[nodiscard]] std::optional<std::size_t> splitSection(SectionTimeline& timeline, double seconds,
                                                      double minSectionSeconds = 0.25);

// Merges section `index` into section `index - 1`, which keeps the earlier one's type, label and
// treatment and takes the later one's end. Refuses on index 0 and on a timeline of one. The survivor
// is marked `End`, because its end is now somewhere a person put it.
bool mergeSectionWithPrevious(SectionTimeline& timeline, std::size_t index);

// Removes a section by giving its span to a neighbour, which keeps the timeline gapless. Refuses on
// the last remaining section: a timeline of nothing is not a timeline, and clearing is a separate,
// explicit act.
bool removeSection(SectionTimeline& timeline, std::size_t index);

// Inserts a new authored section over `start`..`end`, taking that span from whatever is there.
// Refuses a span that is empty, inverted, outside the piece, or shorter than `minSectionSeconds`.
// Returns the index of the new section.
[[nodiscard]] std::optional<std::size_t> insertSection(SectionTimeline& timeline,
                                                       SectionTypeId type, double start, double end,
                                                       const ShotLanguage& language,
                                                       double minSectionSeconds = 0.25);

// Sets the type. Refuses a type the language does not know -- an unknown type is a section with no
// treatment and no error, which looks exactly like the feature being broken.
//
// The treatment follows the new type **unless the person chose one**, which needs no rule here: an
// un-overridden section stores no intent, so it resolves through whatever type it currently has.
bool setSectionType(SectionTimeline& timeline, std::size_t index, SectionTypeId type,
                    const ShotLanguage& language);

// Sets or clears the person's own name. Clearing (an empty label) un-marks `Label`, so a section
// renamed back to nothing is detected again and a re-analysis may relabel it -- which is what
// "undo my rename" has to mean.
bool setSectionLabel(SectionTimeline& timeline, std::size_t index, std::string label);

// Overrides the treatment. Refuses an intent the language does not know.
bool setSectionShotIntent(SectionTimeline& timeline, std::size_t index, ShotIntentId intent,
                          const ShotLanguage& language);

// Drops the override, so the section goes back to its type's default and a re-analysis may move it.
bool clearSectionShotIntent(SectionTimeline& timeline, std::size_t index);

} // namespace avgen::song
