#include "seq/song_structure.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::seq {
namespace {
using nlohmann::json;
using analysis::SectionFunction;
using analysis::SectionOrigin;
using analysis::SongSection;
using analysis::SongStructure;

// A boundary is a beat time and two beats are never this close, so anything inside it is the same
// instant expressed twice rather than two instants.
constexpr double kEpsilon = 1e-9;

[[nodiscard]] double readNumber(const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

[[nodiscard]] std::string clockOf(double seconds) {
    const auto minutes = static_cast<int>(std::floor(std::max(seconds, 0.0) / 60.0));
    return fmt::format("{}:{:05.2f}", minutes, std::max(seconds, 0.0) - minutes * 60.0);
}

// Re-establishes the gapless invariant after an edit, with one rule: **a person's boundary wins.**
// Where a detected section and a kept one disagree about where they meet, the detected one moves.
void seal(std::vector<SongSection>& sections) {
    std::stable_sort(sections.begin(), sections.end(),
                     [](const SongSection& a, const SongSection& b) {
                         return a.startSeconds < b.startSeconds;
                     });
    std::erase_if(sections, [](const SongSection& s) { return s.endSeconds - s.startSeconds <= kEpsilon; });
    for (std::size_t i = 1; i < sections.size(); ++i) {
        if (std::fabs(sections[i].startSeconds - sections[i - 1].endSeconds) <= kEpsilon) {
            // Exactly equal already, or equal to within a representation wobble: make them the same
            // value so `validate()`'s comparison is not deciding anything at 1e-10.
            sections[i].startSeconds = sections[i - 1].endSeconds;
            continue;
        }
        if (sections[i].origin == SectionOrigin::Detected) {
            sections[i].startSeconds = sections[i - 1].endSeconds;
        } else {
            sections[i - 1].endSeconds = sections[i].startSeconds;
        }
    }
    std::erase_if(sections, [](const SongSection& s) { return s.endSeconds - s.startSeconds <= kEpsilon; });
}

void markRefined(SongSection& s) {
    if (s.origin == SectionOrigin::Detected) {
        s.origin = SectionOrigin::Refined;
    }
}
} // namespace

// ---- persistence -------------------------------------------------------------------------------

json songStructureToJson(const SongStructure& structure) {
    json sections = json::array();
    for (const SongSection& s : structure.sections) {
        json e{{"function", analysis::sectionFunctionName(s.function)},
               {"start", s.startSeconds},
               {"end", s.endSeconds},
               {"origin", analysis::sectionOriginName(s.origin)}};
        if (!s.label.empty()) {
            e["label"] = s.label;
        }
        // The confidences and the measurements are written for `Detected` sections only. For a
        // refined or authored one they are not a smaller claim, they are not a claim at all
        // (`confidenceIsMeaningful`), and writing them would be inviting the next reader to show
        // a number that means nothing.
        if (s.origin == SectionOrigin::Detected) {
            e["labelConfidence"] = s.labelConfidence;
            e["startConfidence"] = s.startConfidence;
            e["endConfidence"] = s.endConfidence;
        }
        if (s.repetitionGroup >= 0) {
            e["group"] = s.repetitionGroup;
            e["occurrence"] = s.occurrence;
        }
        e["energy"] = s.energy;
        e["density"] = s.density;
        sections.push_back(std::move(e));
    }
    // `beatTimes` and `novelty` are absent on purpose -- derived, large, and stale the moment the
    // audio changes. See the header.
    return json{{"duration", structure.durationSeconds},
                {"tempoBpm", structure.tempoBpm},
                {"tempoConfidence", structure.tempoConfidence},
                {"sections", std::move(sections)}};
}

Result<SongStructure> songStructureFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("song structure must be an object");
    }
    SongStructure out;
    out.durationSeconds = readNumber(j, "duration", 0.0);
    out.tempoBpm = static_cast<float>(readNumber(j, "tempoBpm", 0.0));
    out.tempoConfidence = static_cast<float>(readNumber(j, "tempoConfidence", 0.0));
    const auto sections = j.find("sections");
    if (sections == j.end()) {
        return out;
    }
    if (!sections->is_array()) {
        return fail("song structure: 'sections' must be an array");
    }
    for (const auto& e : *sections) {
        if (!e.is_object()) {
            return fail("song structure: every section must be an object");
        }
        SongSection s;
        const std::string function = e.value("function", std::string("other"));
        const auto parsed = analysis::sectionFunctionFromName(function);
        if (!parsed) {
            return fail("song structure: unknown section function '{}'", function);
        }
        s.function = *parsed;
        const std::string origin = e.value("origin", std::string("detected"));
        const auto parsedOrigin = analysis::sectionOriginFromName(origin);
        if (!parsedOrigin) {
            return fail("song structure: unknown section origin '{}'", origin);
        }
        s.origin = *parsedOrigin;
        s.label = e.value("label", std::string());
        s.startSeconds = readNumber(e, "start", 0.0);
        s.endSeconds = readNumber(e, "end", 0.0);
        s.labelConfidence = static_cast<float>(readNumber(e, "labelConfidence", 0.0));
        s.startConfidence = static_cast<float>(readNumber(e, "startConfidence", 0.0));
        s.endConfidence = static_cast<float>(readNumber(e, "endConfidence", 0.0));
        s.repetitionGroup = e.value("group", -1);
        s.occurrence = e.value("occurrence", 0);
        s.energy = static_cast<float>(readNumber(e, "energy", 0.0));
        s.density = static_cast<float>(readNumber(e, "density", 0.0));
        out.sections.push_back(std::move(s));
    }
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    if (out.durationSeconds <= 0.0 && !out.sections.empty()) {
        out.durationSeconds = out.sections.back().endSeconds;
    }
    return out;
}

// ---- the re-analysis policy ---------------------------------------------------------------------

std::string ReanalysisReport::summary() const {
    if (kept() == 0) {
        return fmt::format("{} section(s) detected", detectedReplaced);
    }
    return fmt::format("{} section(s) detected; kept {} you had edited ({} refined, {} authored)",
                       detectedReplaced, kept(), refinedKept, authoredKept);
}

ReanalysisReport reanalyse(SongStructure& current, const SongStructure& fresh) {
    ReanalysisReport report;
    if (fresh.sections.empty()) {
        // Nothing was learned, so nothing is replaced. Emptying a person's structure because the
        // detector had a bad day is precisely the data loss this whole file exists to prevent.
        return report;
    }

    std::vector<SongSection> kept;
    for (const SongSection& s : current.sections) {
        if (s.origin == SectionOrigin::Detected) {
            continue;
        }
        kept.push_back(s);
        (s.origin == SectionOrigin::Refined ? report.refinedKept : report.authoredKept) += 1;
        report.keptSpans.push_back(fmt::format("{}-{} {} ({})", clockOf(s.startSeconds),
                                               clockOf(s.endSeconds), sectionDisplayName(s),
                                               analysis::sectionOriginName(s.origin)));
    }

    std::vector<SongSection> merged = fresh.sections;
    for (const SongSection& protectedSection : kept) {
        std::vector<SongSection> next;
        next.reserve(merged.size() + 2);
        for (const SongSection& d : merged) {
            const bool disjoint = d.endSeconds <= protectedSection.startSeconds + kEpsilon ||
                                  d.startSeconds >= protectedSection.endSeconds - kEpsilon;
            if (disjoint) {
                next.push_back(d);
                continue;
            }
            // The part of the detected section before the kept one, and the part after it. Either
            // may be empty, which is how a detected section wholly inside a kept one disappears.
            if (d.startSeconds < protectedSection.startSeconds - kEpsilon) {
                SongSection left = d;
                left.endSeconds = protectedSection.startSeconds;
                // Its end is now a person's boundary rather than the one the detector found, so the
                // detector's confidence in it no longer describes anything.
                left.endConfidence = 0.0f;
                next.push_back(std::move(left));
                report.detectedTrimmed += 1;
            }
            if (d.endSeconds > protectedSection.endSeconds + kEpsilon) {
                SongSection right = d;
                right.startSeconds = protectedSection.endSeconds;
                right.startConfidence = 0.0f;
                next.push_back(std::move(right));
                report.detectedTrimmed += 1;
            }
        }
        next.push_back(protectedSection);
        merged = std::move(next);
    }
    seal(merged);

    current.sections = std::move(merged);
    current.durationSeconds = fresh.durationSeconds;
    current.tempoBpm = fresh.tempoBpm;
    current.tempoConfidence = fresh.tempoConfidence;
    current.beatTimes = fresh.beatTimes;
    current.novelty = fresh.novelty;
    // Counted off the result rather than accumulated along the way: a fresh section cut by two kept
    // ones would otherwise be counted as lost twice, and a count that can be wrong is worse than no
    // count at all in a report whose whole job is to be believed.
    for (const SongSection& s : current.sections) {
        if (s.origin == SectionOrigin::Detected) {
            report.detectedReplaced += 1;
        }
    }
    return report;
}

// ---- editing -----------------------------------------------------------------------------------

std::optional<double> moveBoundary(SongStructure& structure, std::size_t index, double newTime,
                                   double minSectionSeconds) {
    if (index == 0 || index >= structure.sections.size()) {
        return std::nullopt;
    }
    SongSection& before = structure.sections[index - 1];
    SongSection& after = structure.sections[index];
    const double low = before.startSeconds + minSectionSeconds;
    const double high = after.endSeconds - minSectionSeconds;
    if (!(high > low)) {
        return std::nullopt; // the two sections together are too short to hold a boundary
    }
    const double time = std::clamp(newTime, low, high);
    // Assigned, not computed: whatever the caller decided (a beat's own time, when snapping is on)
    // is stored bit for bit on both sides, so the two sections meet at one value rather than two
    // that differ in the last place.
    before.endSeconds = time;
    after.startSeconds = time;
    markRefined(before);
    markRefined(after);
    return time;
}

bool setSectionFunction(SongStructure& structure, std::size_t index, SectionFunction function) {
    if (index >= structure.sections.size()) {
        return false;
    }
    SongSection& s = structure.sections[index];
    if (s.function == function) {
        return false;
    }
    s.function = function;
    markRefined(s);
    return true;
}

bool setSectionLabel(SongStructure& structure, std::size_t index, std::string label) {
    if (index >= structure.sections.size()) {
        return false;
    }
    SongSection& s = structure.sections[index];
    if (s.label == label) {
        return false;
    }
    s.label = std::move(label);
    markRefined(s);
    return true;
}

std::optional<std::size_t> splitSection(SongStructure& structure, double seconds,
                                        double minSectionSeconds) {
    for (std::size_t i = 0; i < structure.sections.size(); ++i) {
        SongSection& s = structure.sections[i];
        if (seconds <= s.startSeconds + minSectionSeconds ||
            seconds >= s.endSeconds - minSectionSeconds) {
            continue;
        }
        SongSection right = s;
        right.startSeconds = seconds;
        right.origin = SectionOrigin::Authored;
        right.label.clear();
        // A new section is not a repeat of anything and carries no claim about itself. The function
        // is inherited because it is the most useful starting point, not because it is known --
        // which is exactly what `Authored` records.
        right.repetitionGroup = -1;
        right.occurrence = 0;
        right.labelConfidence = 0.0f;
        right.startConfidence = 0.0f;
        right.endConfidence = 0.0f;
        s.endSeconds = seconds;
        s.endConfidence = 0.0f;
        markRefined(s);
        structure.sections.insert(structure.sections.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                  std::move(right));
        return i + 1;
    }
    return std::nullopt;
}

bool removeSection(SongStructure& structure, std::size_t index) {
    if (index >= structure.sections.size() || structure.sections.size() <= 1) {
        return false;
    }
    if (index == 0) {
        structure.sections[1].startSeconds = structure.sections[0].startSeconds;
        markRefined(structure.sections[1]);
    } else {
        structure.sections[index - 1].endSeconds = structure.sections[index].endSeconds;
        markRefined(structure.sections[index - 1]);
    }
    structure.sections.erase(structure.sections.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

// ---- reading -----------------------------------------------------------------------------------

bool confidenceIsMeaningful(const SongSection& section) {
    return section.origin == SectionOrigin::Detected;
}

std::string sectionDisplayName(const SongSection& section) {
    return section.label.empty() ? analysis::sectionFunctionName(section.function) : section.label;
}

// ---- the two vocabularies -----------------------------------------------------------------------

signals::MusicalSection sectionKindFor(SectionFunction function) {
    using F = SectionFunction;
    using S = signals::MusicalSection;
    switch (function) {
    case F::Intro:
        return S::Intro;
    case F::Verse:
        return S::Verse;
    case F::PreChorus:
        return S::PreChorus;
    case F::Build:
        return S::Build;
    case F::Chorus:
        return S::Chorus;
    case F::Drop:
        return S::Drop;
    case F::Break:
        return S::Break;
    case F::Bridge:
        return S::Bridge;
    case F::Instrumental:
        return S::Instrumental;
    case F::Breakdown:
        return S::Breakdown;
    case F::FinalChorus:
        return S::FinalChorus;
    case F::Outro:
        return S::Outro;
    case F::Other:
        // The load-bearing line of the whole mapping. `Other` is the detector declining to claim
        // anything, and the director's vocabulary has no way to decline -- a shot has to be some
        // shot. `Phrase` is "an ordinary passage", which is the honest image of "I do not know":
        // the camera travels through the world and the film does not pretend a chorus arrived.
        return S::Phrase;
    }
    return S::Phrase;
}

std::vector<signals::MusicalSection> sectionKindsFor(const SongStructure& structure,
                                                    double finalFraction) {
    std::vector<signals::MusicalSection> kinds;
    kinds.reserve(structure.sections.size());
    for (const SongSection& s : structure.sections) {
        kinds.push_back(sectionKindFor(s.function));
    }
    const double total = structure.durationSeconds > 0.0
                             ? structure.durationSeconds
                             : (structure.sections.empty() ? 0.0 : structure.sections.back().endSeconds);
    if (!(total > 0.0)) {
        return kinds;
    }
    // The last of each payoff family, if it lands late enough to be *the* one. Late-ness is the only
    // thing a single section cannot know about itself, which is the entire reason this pass exists
    // rather than being folded into `sectionKindFor`.
    const double threshold = total * finalFraction;
    const auto promoteLast = [&](signals::MusicalSection from, signals::MusicalSection to) {
        for (std::size_t i = kinds.size(); i-- > 0;) {
            if (kinds[i] != from) {
                continue;
            }
            if (structure.sections[i].startSeconds >= threshold) {
                kinds[i] = to;
            }
            return; // only the last one; an earlier drop is not the final drop whatever its time
        }
    };
    promoteLast(signals::MusicalSection::Drop, signals::MusicalSection::FinalDrop);
    promoteLast(signals::MusicalSection::Chorus, signals::MusicalSection::FinalChorus);
    promoteLast(signals::MusicalSection::Build, signals::MusicalSection::FinalBuild);
    return kinds;
}

signals::MusicalStructure toMusicalStructure(const SongStructure& structure, double finalFraction) {
    signals::MusicalStructure out;
    const std::vector<signals::MusicalSection> kinds = sectionKindsFor(structure, finalFraction);
    out.sections.reserve(structure.sections.size());
    for (std::size_t i = 0; i < structure.sections.size(); ++i) {
        const SongSection& s = structure.sections[i];
        out.sections.push_back(signals::StructureSection{
            .kind = kinds[i],
            .startSeconds = s.startSeconds,
            .durationSeconds = s.durationSeconds(),
            // `energy` and not a blend with `density`: both are measured, but intensity means "how
            // much of the thing is happening" and that is what energy is. Combining two real
            // numbers into a third with no definition is how a metric gets invented.
            .intensity = std::clamp(s.energy, 0.0f, 1.0f)});
    }
    return out;
}

} // namespace avgen::seq
