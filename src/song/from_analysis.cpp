#include "song/from_analysis.hpp"

namespace avgen::song {

SectionTypeId sectionTypeForFunction(analysis::SectionFunction function) {
    // No `default:` on purpose. Adding a function to the analyzer is then a compile error here --
    // at the site of the decision -- rather than a silent `phrase` that nobody notices until a film
    // is unaccountably bland for thirty seconds.
    switch (function) {
    case analysis::SectionFunction::Intro:
        return "intro";
    case analysis::SectionFunction::Verse:
        return "verse";
    case analysis::SectionFunction::PreChorus:
        return "pre_chorus";
    case analysis::SectionFunction::Build:
        return "build";
    case analysis::SectionFunction::Chorus:
        return "chorus";
    case analysis::SectionFunction::Drop:
        return "drop";
    case analysis::SectionFunction::Break:
        return "break";
    case analysis::SectionFunction::Bridge:
        return "bridge";
    case analysis::SectionFunction::Instrumental:
        return "instrumental";
    case analysis::SectionFunction::Breakdown:
        return "breakdown";
    case analysis::SectionFunction::FinalChorus:
        // Not a type of its own. "Final" is a property of *position*, not of the music, and the cue
        // sheet derives it (`SectionCue::finalOfKind`). A `final_chorus` type would mean a piece
        // with two choruses and a piece with five needed different vocabularies, and it would leave
        // the last Ocean Ambience with no way to be the last one.
        return "chorus";
    case analysis::SectionFunction::Outro:
        return "outro";
    case analysis::SectionFunction::Other:
        // The load-bearing line. The detector is allowed to answer "I do not know" and a film is
        // not, so "I do not know" becomes "an ordinary passage" here -- once, where it can be read.
        return neutralSectionTypeId();
    }
    return neutralSectionTypeId();
}

SectionTimeline timelineFromStructure(const analysis::SongStructure& structure,
                                      const ShotLanguage& language) {
    SectionTimeline out;
    out.durationSeconds = structure.durationSeconds;
    out.sections.reserve(structure.sections.size());
    for (const analysis::SongSection& s : structure.sections) {
        Section made;
        made.type = sectionTypeForFunction(s.function);
        if (!language.hasType(made.type)) {
            // A project whose custom definitions removed a built-in out from under the mapping still
            // gets a timeline, rather than sections naming a type nothing can resolve.
            made.type = neutralSectionTypeId();
        }
        // The detector's own label is deliberately NOT copied into `Section::label`. A label a
        // person did not type is not a person's name for the passage, and copying it would mark
        // every section as renamed and freeze the whole timeline against the next re-analysis.
        made.startSeconds = s.startSeconds;
        made.endSeconds = s.endSeconds;
        made.labelConfidence = s.labelConfidence;
        made.startConfidence = s.startConfidence;
        made.endConfidence = s.endConfidence;
        made.energy = s.energy;
        made.density = s.density;
        made.repetitionGroup = s.repetitionGroup;
        made.authored = false;
        made.edited = SectionField::None;
        // No shot-intent override: every section resolves through its type's default, so a person
        // has a complete first-pass treatment for the whole piece the moment analysis finishes.
        out.sections.push_back(std::move(made));
    }
    out.renumber();
    return out;
}

ReanalysisReport applyAnalysis(SectionTimeline& current, const analysis::SongStructure& fresh,
                               const ShotLanguage& language, ReanalysisPolicy policy) {
    return reanalyze(current, timelineFromStructure(fresh, language), policy);
}

} // namespace avgen::song
