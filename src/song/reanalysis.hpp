#pragma once

// **Re-analysis: what happens to a person's work when they press Analyze again** (ADR-247, the
// brief's section 18).
//
// The brief states the case exactly: a person analyzes a song, gets twelve sections, changes Verse 2
// to Ocean Ambience, changes its shot, moves a boundary, and presses Analyze. "The system must not
// casually destroy their work."
//
// The equation it asks for --
//
//     analysis result + user overrides = current authored song structure
//
// -- is implemented literally. A fresh detection is turned into a fresh timeline and reconciled
// against the authored one, field by field.
//
// ## The policy, stated once
//
// 1. **A fresh detection with no sections changes nothing at all**, and reports nothing. Refusing to
//    act on empty information is better than emptying somebody's timeline because the detector had a
//    bad day, and it is ADR-215's rule kept.
//
// 2. **Frozen sections keep their whole span and all their fields.** A section is frozen when it was
//    authored from nothing, or when either of its boundaries was moved. Fresh sections are trimmed
//    around a frozen one; a fresh section a frozen one completely covers is dropped.
//
//    Pinning *one* boundary freezing the *whole* span is a deliberate conservatism. A section is a
//    span and half of one cannot be carved around. It is stated here rather than discovered.
//
// 3. **Everything else is replaced -- except the fields a person set.** A section whose type, label
//    or treatment was chosen hands those fields to the fresh section it overlaps most, and the fresh
//    boundaries win. This is where this model goes past ADR-215: retyping a passage is not a claim
//    about where the passage starts, so a retyped section gets better boundaries *and* keeps its type.
//
// 4. **At most one current section hands its fields to any one fresh section**, resolved greedily by
//    overlap, largest first. Without that rule, two edited sections that a fresher, coarser detection
//    merges into one would both write to it and the second would silently win.
//
// ## And a way to not do any of it
//
// `ReanalysisPolicy::Replace` throws the authored timeline away. It exists because "start again" is a
// legitimate thing to want and doing it by hand is worse. It is never the default, and
// `previewReanalysis` gives a UI the report *before* anything is touched, so the explicit
// confirmation the brief asks for can say how much is about to go.

#include "song/section_timeline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::song {

enum class ReanalysisPolicy : std::uint8_t {
    // Keep what a person decided; replace what the analyzer guessed. The only policy anything should
    // reach for without being told to.
    Merge,
    // Discard the authored timeline entirely and take the fresh one. Destructive, explicit, and
    // reported: `ReanalysisReport::discarded` counts what it cost.
    Replace,
};
[[nodiscard]] const char* reanalysisPolicyName(ReanalysisPolicy p);

// What a re-analysis did, in terms a person can be shown.
//
// This exists because "we kept your edits" is a claim, and a claim about data loss should be reported
// rather than assumed: if a re-run silently dropped a refined chorus, nothing in a UI would say so.
struct ReanalysisReport {
    int sectionsReplaced = 0; // detected sections the analyzer has now guessed again
    int spansFrozen = 0;      // sections whose boundaries were kept exactly
    int fieldsCarried = 0;    // individual edited fields moved onto a fresh section
    int sectionsCarried = 0;  // fresh sections that received at least one carried field
    int freshTrimmed = 0;     // fresh sections shortened to make room for a frozen one
    int freshDropped = 0;     // fresh sections a frozen one covered completely
    int discarded = 0;        // edited or authored sections lost -- only ever non-zero under Replace
    // "1:02.40-1:31.80 Ocean Ambience (type, shot)" -- one line per thing kept, in clock time.
    std::vector<std::string> kept;

    [[nodiscard]] bool changedAnything() const;
    [[nodiscard]] std::string summary() const;
};

// Folds a fresh timeline into an authored one. `current` is left untouched and the report is empty
// when `fresh` has no sections.
ReanalysisReport reanalyze(SectionTimeline& current, const SectionTimeline& fresh,
                           ReanalysisPolicy policy = ReanalysisPolicy::Merge);

// The same, on a copy, so a UI can say what is about to happen before it happens. Identical result to
// the call it predicts -- it *is* that call, run against a copy -- which is the only way a preview
// and an action cannot come to disagree.
[[nodiscard]] ReanalysisReport previewReanalysis(const SectionTimeline& current,
                                                 const SectionTimeline& fresh,
                                                 ReanalysisPolicy policy = ReanalysisPolicy::Merge);

} // namespace avgen::song
