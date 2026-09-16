#include "song/section_cue.hpp"

#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"

#include <algorithm>

namespace avgen::song {

float SectionCue::progressAt(double seconds) const {
    const double span = durationSeconds();
    if (!(span > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp((seconds - startSeconds) / span, 0.0, 1.0));
}

ShotIntent SectionCue::intentAt(double seconds) const {
    return intent.atProgress(progressAt(seconds));
}

std::vector<SectionCue> cueSheet(const SectionTimeline& timeline, const ShotLanguage& language) {
    std::vector<SectionCue> out;
    out.reserve(timeline.sections.size());
    for (std::size_t i = 0; i < timeline.sections.size(); ++i) {
        const Section& s = timeline.sections[i];
        SectionCue cue;
        cue.index = static_cast<int>(i);
        cue.startSeconds = s.startSeconds;
        cue.endSeconds = s.endSeconds;
        cue.intent = language.intentFor(s);
        cue.energy = s.energy;
        cue.density = s.density;
        cue.occurrence = s.occurrence;
        // The one thing a lone section cannot know. Derived from position, so it works for a custom
        // type nobody anticipated -- which is precisely what a `FinalChorus` enumerator could not.
        cue.finalOfKind = timeline.lastOfType(s.type) == i;
        cue.displayName = language.displayName(s);
        out.push_back(std::move(cue));
    }
    return out;
}

const SectionCue* cueAt(std::span<const SectionCue> cues, double seconds) {
    for (const SectionCue& c : cues) {
        if (c.contains(seconds)) {
            return &c;
        }
    }
    // The last cue owns its own end, so a query exactly at the piece's duration answers.
    if (!cues.empty() && seconds >= cues.back().endSeconds &&
        seconds - cues.back().endSeconds < 1e-9) {
        return &cues.back();
    }
    return nullptr;
}

} // namespace avgen::song
