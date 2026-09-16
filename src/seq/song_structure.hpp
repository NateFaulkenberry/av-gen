#pragma once

// The editable song structure (ADR-215): the layer between the detector and the Sequencer.
//
// ADR-206 built the detector -- `analysis::detectStructure` -- and it answers one question well:
// given audio, where are the sections and what are they probably called. What it deliberately does
// not do is remember anything. Run it twice and you get two answers; move a boundary and the next
// run throws the move away.
//
// This file is the other half, and it is a *model* change rather than a UI one:
//
//  1. **Persistence.** A structure goes into the project, inside the sequence that owns it, so
//     reopening a project does not re-detect and a person's edits survive a restart.
//  2. **A re-analysis policy.** `reanalyze()` replaces what the analyzer guessed and keeps what a
//     person decided, and says which it did. `SectionOrigin` already existed for exactly this; what
//     was missing was something that honoured it.
//  3. **Editing.** Moving a boundary, renaming, retyping, splitting and deleting -- each of which
//     marks what it touched, so the next re-analysis knows to leave it alone.
//  4. **The bridge between the two section vocabularies**, which ADR-206 explicitly left open:
//     "when those land, the two need a stated relationship rather than a silent overlap".
//
// ## Precision
//
// Every boundary here is a `double` second and is **never rounded**. A boundary that came from the
// detector is a beat time, bit for bit; a boundary a person dragged with beat snapping on is also a
// beat time, bit for bit, because snapping assigns the beat's own value rather than computing a
// nearby one. That property is asserted in the tests, and it is the one rounding would break
// silently -- a structure that is 40 ms out everywhere still looks perfectly reasonable in a UI.
//
// ## What is not stored
//
// `SongStructure::beatTimes` and `novelty` are **derived** and are not written to the project, for
// the same reason `Sequence::toJson` refuses to write thousands of Beat markers: they are a copy of
// the analysis, they are large, and a copy of the analysis goes stale the moment somebody changes
// the audio. They come back from the track on load.

#include "analysis/structure.hpp"
#include "core/error.hpp"
#include "signals/musical_events.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace avgen::seq {

// ---- persistence -------------------------------------------------------------------------------

[[nodiscard]] nlohmann::json songStructureToJson(const analysis::SongStructure& structure);
[[nodiscard]] Result<analysis::SongStructure> songStructureFromJson(const nlohmann::json& j);

// ---- the re-analysis policy (the brief's section 17) --------------------------------------------

// What a re-analysis did, in terms a person can be shown. It exists because "we kept your edits" is
// a claim, and a claim about data loss is exactly the kind that should be reported rather than
// assumed: if a re-run silently dropped a refined chorus, nothing in the UI would say so.
struct ReanalysisReport {
    int detectedReplaced = 0;  // sections the analyzer had guessed, and has now guessed again
    int refinedKept = 0;       // boundaries or labels a person moved
    int authoredKept = 0;      // sections a person made from nothing
    int detectedTrimmed = 0;   // fresh sections shortened to make room for a kept one
    std::vector<std::string> keptSpans; // "1:02.40-1:31.80 chorus (refined)"

    [[nodiscard]] int kept() const { return refinedKept + authoredKept; }
    [[nodiscard]] std::string summary() const;
};

// Folds a fresh detection into an existing structure.
//
// The policy, stated once: **`Detected` sections are replaced; `Refined` and `Authored` sections
// are kept exactly, times and all, and the fresh detection is cut around them.** The result is still
// ordered, gapless and covering, because a kept section displaces detected material rather than
// being laid on top of it.
//
// A fresh detection with no sections (the audio changed to something with no beat grid, say) leaves
// `current` completely alone and reports nothing: refusing to act is the right answer when the new
// information is empty, and it is very much better than emptying a person's structure.
ReanalysisReport reanalyze(analysis::SongStructure& current, const analysis::SongStructure& fresh);

// ---- editing -----------------------------------------------------------------------------------
//
// Each of these marks what it touched. A `Detected` section that a person moved or renamed becomes
// `Refined`; a section made from nothing is `Authored`. Moving a boundary marks *both* of the
// sections it separates, because both of them changed -- the conservative answer, and the one that
// cannot lose an edit.

// Boundary `index` separates section `index - 1` from section `index`; valid indices are 1 ..
// sections.size() - 1, so the track's own start and end are not boundaries and cannot be dragged.
// `newTime` is clamped so neither neighbour falls below `minSectionSeconds`, and is otherwise stored
// **exactly** as given. Returns the time actually used, or nothing when the index is not a boundary.
[[nodiscard]] std::optional<double> moveBoundary(analysis::SongStructure& structure,
                                                 std::size_t index, double newTime,
                                                 double minSectionSeconds = 0.25);

bool setSectionFunction(analysis::SongStructure& structure, std::size_t index,
                        analysis::SectionFunction function);
bool setSectionLabel(analysis::SongStructure& structure, std::size_t index, std::string label);

// Splits the section containing `seconds` there. The earlier half is `Refined` (its end moved); the
// later half is `Authored` (it did not exist before). Returns the index of the new later half.
[[nodiscard]] std::optional<std::size_t> splitSection(analysis::SongStructure& structure,
                                                      double seconds,
                                                      double minSectionSeconds = 0.25);

// Removes a section by giving its time to a neighbour, which keeps the structure gapless. Refuses
// on the last remaining section: a structure of nothing is not a structure, it is an empty one, and
// clearing is a different, explicit act.
bool removeSection(analysis::SongStructure& structure, std::size_t index);

// ---- reading -----------------------------------------------------------------------------------

// Whether the confidences on this section mean anything.
//
// ADR-206's rule, honoured literally: a confidence is *the detector's* claim about its own guess.
// The moment a person moves or renames a section it is no longer a guess, and the number stops
// being about anything -- so it is not cleared (destroying the detector's record would be a second
// kind of data loss) and it is not shown. A UI asks this before displaying one.
[[nodiscard]] bool confidenceIsMeaningful(const analysis::SongSection& section);

// The section's name for a person: its label when it has one, otherwise its function's name.
[[nodiscard]] std::string sectionDisplayName(const analysis::SongSection& section);

// ---- the two vocabularies (the relationship ADR-206 left open) ----------------------------------
//
// `analysis::SectionFunction` describes the music and may answer `Other`, which is a real answer.
// `signals::MusicalSection` is the director's vocabulary, where every value has to mean something to
// a shot and there is therefore no "unknown" -- `Phrase`, "an ordinary passage", is what `Other`
// becomes when something has to point a camera.
//
// The mapping is **one-way and total**: analysis -> director, never back. Going back would be
// inventing a musical claim out of a directorial one, and `FinalBuild` / `FinalDrop` / `Phrase` have
// no musical meaning to invent from.
[[nodiscard]] signals::MusicalSection sectionKindFor(analysis::SectionFunction function);

// The same mapping, with the one piece of context a single section cannot carry: *where in the piece
// it is*. The director's vocabulary distinguishes the last build and the last drop from the first,
// because they are different shots; the musical vocabulary does not, because they are the same
// music. A Build/Drop/Chorus that starts after `finalFraction` of the piece and is the last of its
// kind is promoted to the Final variant.
[[nodiscard]] std::vector<signals::MusicalSection> sectionKindsFor(
    const analysis::SongStructure& structure, double finalFraction = 0.55);

// The whole structure in the director's terms, so everything already built on
// `signals::MusicalStructure` -- `app::cinematic`'s shot planner above all -- reads an *edited*
// structure with no changes of its own. `intensity` is the section's measured `energy`, which is the
// one number of the two that has a defensible definition here.
[[nodiscard]] signals::MusicalStructure toMusicalStructure(const analysis::SongStructure& structure,
                                                           double finalFraction = 0.55);

} // namespace avgen::seq
