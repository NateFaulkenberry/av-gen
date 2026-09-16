#pragma once

// **The one file that knows both the analyzer and the shot language** (ADR-247).
//
// Everything else in `song/` compiles without `analysis/` in the include path, and everything in
// `analysis/` compiles without `song/`. That is not tidiness: it is the brief's section 15 asking for
// no circular dependencies and for the analyzer not to acquire downstream concerns. The detector's
// job is to report what the audio contains, and it does that job identically whether or not anything
// is ever cut to it.
//
// The translation is **one-way**, for exactly ADR-215's reason. Analysis produces an authored
// timeline; an authored timeline never produces an analysis. Going back would mean inventing a claim
// about the music out of a decision about the film -- and a section a person typed "Ocean Ambience"
// into has no musical function to invent.
//
// ## What the mapping does and does not preserve
//
// `analysis::SectionFunction::FinalChorus` maps to the plain `chorus` type, not to a type of its own.
// The "final" part is not a property of the music -- it is a property of *position*, and the cue
// sheet derives it (`SectionCue::finalOfKind`). Making it a type would mean a piece with two choruses
// and a piece with five needed different vocabularies, and it would leave the last Ocean Ambience
// with no way to be the last one.
//
// `SectionFunction::Other` maps to `phrase`. That is the load-bearing line of the whole table: the
// detector is allowed to answer "I do not know" and a film is not, so "I do not know" becomes "an
// ordinary passage" here, once, where it can be read -- rather than in fifteen places downstream
// where a `default:` would quietly produce the same thing.

#include "analysis/structure.hpp"
#include "song/reanalysis.hpp"
#include "song/section_timeline.hpp"
#include "song/section_type.hpp"
#include "song/shot_language.hpp"

namespace avgen::song {

// The type a detected function becomes. Total: every `SectionFunction` has an answer, and the switch
// has no `default:`, so adding one to the analyzer is a compile error here rather than a silent
// `phrase`.
[[nodiscard]] SectionTypeId sectionTypeForFunction(analysis::SectionFunction function);

// A fresh authored timeline from a detector's report.
//
// Every section comes out `Detected` with no fields edited, carrying its measurements and its
// confidences, with no shot-intent override -- so each one resolves through its type's default and
// the person immediately has a complete first-pass treatment for the whole piece (the brief's
// section 7). The labels the detector produced are **not** copied into `Section::label`: a label a
// person did not type is not a person's name for the passage, and copying it would mark every
// section as renamed and freeze the lot against the next re-analysis.
//
// `language` is consulted only to check that each mapped type exists, so a project whose custom
// definitions shadow a built-in gets its own vocabulary here too.
[[nodiscard]] SectionTimeline timelineFromStructure(const analysis::SongStructure& structure,
                                                    const ShotLanguage& language);

// **"Press Analyze", in one call** -- the brief's section 7, and the only entry point anything
// outside this namespace should need for it.
//
// Generation and re-analysis are the same operation with a different starting point, which is why
// they are one function rather than two a caller has to choose between: choosing wrongly is how a
// first analysis ends up going through the merge path and a re-analysis ends up going through the
// replacing one. When `current` is empty, the fresh detection simply becomes the timeline; when it
// is not, ADR-247's per-field policy runs and the report says what it kept.
//
// The detection itself is *not* performed here. It is an offline job (ADR-216) and this is a pure
// fold over its result: no clock, no audio, no allocation of anything large.
ReanalysisReport applyAnalysis(SectionTimeline& current, const analysis::SongStructure& fresh,
                               const ShotLanguage& language,
                               ReanalysisPolicy policy = ReanalysisPolicy::Merge);

} // namespace avgen::song
