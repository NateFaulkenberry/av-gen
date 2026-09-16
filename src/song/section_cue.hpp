#pragma once

// **What the director is handed** (ADR-247), and deliberately all of it.
//
// The brief's section 15 asks for one thing above all the others: *avoid making the Auto Director
// depend on musical labels like "Chorus"*. That is easy to agree to and hard to keep, because the
// section object is right there and switching on its type is always the shortest path to a result.
//
// So the director is not given the section object. It is given this, a projection with **no section
// type in it at all**. There is no `SectionTypeId` field, no `SectionType` pointer and no enum of
// musical kinds, so "the director must not know what a chorus is" is a property of the type rather
// than a promise somebody has to keep. A director written against this file cannot special-case the
// structural vocabulary, and therefore treats `ocean_ambience` exactly as it treats `chorus` -- which
// is the entire architectural claim of this feature, made structurally instead of by assertion.
//
// `displayName` is the one concession, and it is for overlays, logs and debug text. Switching on it
// is the bug this file exists to prevent; it is a person's arbitrary string and on a custom type it
// is whatever they typed.
//
// The two useful things a lone section cannot know are computed here, from the timeline:
//
//   * `occurrence` -- which time round this is
//   * `finalOfKind` -- whether this is the last time this material appears
//
// `finalOfKind` is how "the final chorus is a different shot from the first" survives without a
// `FinalChorus` enumerator. `signals::MusicalSection` needed one (and a `FinalBuild`, and a
// `FinalDrop`) because its vocabulary is closed; a boolean computed from position works for a custom
// type nobody anticipated, which a fourth enumerator never could.

#include "song/shot_intent.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace avgen::song {

class ShotLanguage;
struct SectionTimeline;

// One section, in the only terms a director needs.
struct SectionCue {
    int index = 0; // position in the timeline
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    // The resolved treatment, **by value**.
    //
    // A pointer would have been cheaper and is a use-after-free waiting to happen: it would point
    // into `ShotLanguage`'s own vector of custom intents, and defining one more intent reallocates
    // that vector and dangles every cue already handed out. A cue sheet is tens of entries of about
    // a hundred bytes; the copy is not worth a lifetime rule that somebody has to remember.
    //
    // It also means a cue is self-contained: a director may hold one across a frame, a job boundary
    // or an edit to the language, which is exactly the sort of thing a director does.
    ShotIntent intent;

    // Measured from the audio under this span, 0..1 against the track's own range. The brief's
    // "generic parameters -- section energy, beat density -- that shot intents consume", carried
    // here rather than re-derived, because analysis is an offline job and a director runs live.
    float energy = 0.0f;
    float density = 0.0f;

    int occurrence = 0;        // 0 the first time this type appears, 1 the second, ...
    bool finalOfKind = false;  // the last section of this type in the piece
    std::string displayName;   // FOR DISPLAY AND LOGS. Never switch on this.

    [[nodiscard]] double durationSeconds() const { return endSeconds - startSeconds; }
    [[nodiscard]] bool contains(double seconds) const {
        return seconds >= startSeconds && seconds < endSeconds;
    }
    // How far through this section `seconds` is, clamped to 0..1. A zero-length section reads 0.
    [[nodiscard]] float progressAt(double seconds) const;
    // The treatment with its arc applied at `seconds`. The one call a music-aware director needs:
    // a build's movement climbs, a drop's settles, a pause stays put, and none of those words appear
    // in the director.
    [[nodiscard]] ShotIntent intentAt(double seconds) const;
};

// Projects a timeline into cues. One cue per section, in order.
//
// Pure: no clock, no randomness, no I/O. The same timeline and language give the same sheet on every
// machine and every run, which is what lets a render reproduce a preview.
[[nodiscard]] std::vector<SectionCue> cueSheet(const SectionTimeline& timeline,
                                               const ShotLanguage& language);

// The cue covering `seconds`, or null. Linear; a cue sheet is tens of entries, not thousands, and a
// binary search here would be a micro-optimisation over a list that fits in a cache line or two.
[[nodiscard]] const SectionCue* cueAt(std::span<const SectionCue> cues, double seconds);

} // namespace avgen::song
