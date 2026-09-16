#include "song/section_timeline.hpp"

#include "song/shot_language.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace avgen::song {
namespace {
using nlohmann::json;

// Two boundaries are never this close; anything inside it is the same instant expressed twice. The
// same figure `seq/song_structure.cpp` uses, and for the same reason.
constexpr double kEpsilon = 1e-9;
// What `validate()` will tolerate between the end of one section and the start of the next. Wider
// than kEpsilon because it is judging arithmetic that has been through a JSON round trip, and it is
// the same 1e-6 `analysis::SongStructure::validate` uses.
constexpr double kSealTolerance = 1e-6;

constexpr std::array kFieldBits{SectionField::Type, SectionField::Label, SectionField::ShotIntent,
                                SectionField::Start, SectionField::End};
constexpr std::array kFieldNames{"type", "label", "shot-intent", "start", "end"};
static_assert(kFieldBits.size() == kFieldNames.size());

constexpr std::array kOrigins{SectionOrigin::Detected, SectionOrigin::Refined,
                              SectionOrigin::Authored};
constexpr std::array kOriginNames{"detected", "refined", "authored"};
static_assert(kOrigins.size() == kOriginNames.size());

[[nodiscard]] float readFloat(const json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

[[nodiscard]] double readDouble(const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

[[nodiscard]] std::string readString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Restores the gapless invariant after an edit that could have disturbed it, with one rule: **a
// person's boundary wins.** Where a detected section and a frozen one disagree about where they
// meet, the detected one moves.
void seal(std::vector<Section>& sections) {
    std::stable_sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) {
        return a.startSeconds < b.startSeconds;
    });
    std::erase_if(sections,
                  [](const Section& s) { return s.endSeconds - s.startSeconds <= kEpsilon; });
    for (std::size_t i = 1; i < sections.size(); ++i) {
        const double gap = sections[i].startSeconds - sections[i - 1].endSeconds;
        if (std::fabs(gap) <= kEpsilon) {
            // Equal already, or equal to within a representation wobble: make them literally the
            // same value so `validate()` is not deciding anything at 1e-10.
            sections[i].startSeconds = sections[i - 1].endSeconds;
            continue;
        }
        if (sections[i].spanIsFrozen() && !sections[i - 1].spanIsFrozen()) {
            sections[i - 1].endSeconds = sections[i].startSeconds;
        } else if (sections[i - 1].spanIsFrozen() && !sections[i].spanIsFrozen()) {
            sections[i].startSeconds = sections[i - 1].endSeconds;
        } else {
            // Neither or both: the earlier one's end is the shared value, which keeps the operation
            // deterministic rather than dependent on which was touched last.
            sections[i].startSeconds = sections[i - 1].endSeconds;
        }
    }
    std::erase_if(sections,
                  [](const Section& s) { return s.endSeconds - s.startSeconds <= kEpsilon; });
}
} // namespace

// ---- provenance ---------------------------------------------------------------------------------

std::vector<std::string> sectionFieldNames(SectionField f) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < kFieldBits.size(); ++i) {
        if (any(f & kFieldBits[i])) {
            out.emplace_back(kFieldNames[i]);
        }
    }
    return out;
}

Result<SectionField> sectionFieldsFromNames(std::span<const std::string> names) {
    SectionField out = SectionField::None;
    for (const std::string& name : names) {
        bool found = false;
        for (std::size_t i = 0; i < kFieldNames.size(); ++i) {
            if (name == kFieldNames[i]) {
                out |= kFieldBits[i];
                found = true;
                break;
            }
        }
        if (!found) {
            return fail("unknown section field '{}'", name);
        }
    }
    return out;
}

const char* sectionOriginName(SectionOrigin o) {
    const auto i = static_cast<std::size_t>(o);
    return i < kOriginNames.size() ? kOriginNames[i] : "detected";
}

std::optional<SectionOrigin> sectionOriginFromName(std::string_view name) {
    for (std::size_t i = 0; i < kOriginNames.size(); ++i) {
        if (name == kOriginNames[i]) {
            return kOrigins[i];
        }
    }
    return std::nullopt;
}

SectionOrigin Section::origin() const {
    if (authored) {
        return SectionOrigin::Authored;
    }
    return any(edited) ? SectionOrigin::Refined : SectionOrigin::Detected;
}

bool labelConfidenceIsMeaningful(const Section& s) {
    // The detector's label confidence is a claim about the *function* it chose, which is what became
    // the type. Retyping and renaming both retire the claim.
    return !s.authored && !s.isEdited(SectionField::Type) && !s.isEdited(SectionField::Label);
}

bool boundaryConfidenceIsMeaningful(const Section& s) {
    return !s.authored && !s.isEdited(SectionField::Boundaries);
}

// ---- the timeline -------------------------------------------------------------------------------

Result<void> SectionTimeline::validate() const {
    if (sections.empty()) {
        return {}; // a timeline with nothing in it is empty, not invalid
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const Section& s = sections[i];
        if (s.type.empty()) {
            return fail("section timeline: section {} has no type", i);
        }
        if (!(s.endSeconds > s.startSeconds)) {
            return fail("section timeline: section {} ('{}') ends at {} and starts at {}", i, s.type,
                        s.endSeconds, s.startSeconds);
        }
        if (i > 0 && std::fabs(s.startSeconds - sections[i - 1].endSeconds) > kSealTolerance) {
            // Gapless *and* non-overlapping in one check: they are the same property. The start of
            // one section is the end of the last, and daylight between them is a stretch of the
            // piece belonging to nothing.
            return fail("section timeline: section {} starts at {} but section {} ended at {}", i,
                        s.startSeconds, i - 1, sections[i - 1].endSeconds);
        }
    }
    return {};
}

const Section* SectionTimeline::at(double seconds) const {
    const auto index = indexAt(seconds);
    return index ? &sections[*index] : nullptr;
}

std::optional<std::size_t> SectionTimeline::indexAt(double seconds) const {
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (seconds >= sections[i].startSeconds && seconds < sections[i].endSeconds) {
            return i;
        }
    }
    // The last section owns its own end, so a query exactly at the piece's duration answers.
    if (!sections.empty() && std::fabs(seconds - sections.back().endSeconds) < kEpsilon) {
        return sections.size() - 1;
    }
    return std::nullopt;
}

int SectionTimeline::countOfType(std::string_view type) const {
    return static_cast<int>(
        std::count_if(sections.begin(), sections.end(),
                      [type](const Section& s) { return s.type == type; }));
}

std::optional<std::size_t> SectionTimeline::lastOfType(std::string_view type) const {
    for (std::size_t i = sections.size(); i > 0; --i) {
        if (sections[i - 1].type == type) {
            return i - 1;
        }
    }
    return std::nullopt;
}

void SectionTimeline::renumber() {
    std::vector<std::pair<std::string, int>> seen;
    for (Section& s : sections) {
        auto it = std::find_if(seen.begin(), seen.end(),
                               [&s](const auto& e) { return e.first == s.type; });
        if (it == seen.end()) {
            s.occurrence = 0;
            seen.emplace_back(s.type, 1);
        } else {
            s.occurrence = it->second;
            ++it->second;
        }
    }
}

// ---- persistence --------------------------------------------------------------------------------

json sectionTimelineToJson(const SectionTimeline& timeline) {
    json sections = json::array();
    for (const Section& s : timeline.sections) {
        json e{{"type", s.type}, {"start", s.startSeconds}, {"end", s.endSeconds}};
        if (!s.label.empty()) {
            e["label"] = s.label;
        }
        if (s.shotIntent) {
            e["shotIntent"] = *s.shotIntent;
        }
        if (s.authored) {
            e["authored"] = true;
        }
        if (any(s.edited)) {
            e["edited"] = sectionFieldNames(s.edited);
        }
        // The confidences are written only while they still mean something (ADR-215). On an edited
        // section they are not a smaller claim, they are not a claim at all, and persisting them
        // would invite the next reader to display a number about nothing. The in-memory values are
        // left alone -- deleting the detector's record would be a second kind of data loss.
        if (labelConfidenceIsMeaningful(s)) {
            e["labelConfidence"] = s.labelConfidence;
        }
        if (boundaryConfidenceIsMeaningful(s)) {
            e["startConfidence"] = s.startConfidence;
            e["endConfidence"] = s.endConfidence;
        }
        if (s.repetitionGroup >= 0) {
            e["group"] = s.repetitionGroup;
        }
        e["energy"] = s.energy;
        e["density"] = s.density;
        // `occurrence` is deliberately absent: it is derived from the order of the timeline, and a
        // stored derivation is a copy that can go stale. `renumber()` rebuilds it on load.
        sections.push_back(std::move(e));
    }
    return json{{"duration", timeline.durationSeconds}, {"sections", std::move(sections)}};
}

Result<SectionTimeline> sectionTimelineFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a section timeline must be an object");
    }
    SectionTimeline out;
    out.durationSeconds = readDouble(j, "duration", 0.0);
    const auto sections = j.find("sections");
    if (sections == j.end()) {
        return out; // a timeline with no sections is empty, not malformed
    }
    if (!sections->is_array()) {
        return fail("section timeline: 'sections' must be an array");
    }
    for (const json& e : *sections) {
        if (!e.is_object()) {
            return fail("section timeline: every section must be an object");
        }
        Section s;
        s.type = readString(e, "type");
        if (s.type.empty()) {
            return fail("section timeline: a section has no type");
        }
        s.label = readString(e, "label");
        s.startSeconds = readDouble(e, "start", 0.0);
        s.endSeconds = readDouble(e, "end", 0.0);
        if (const auto it = e.find("shotIntent"); it != e.end() && it->is_string()) {
            s.shotIntent = it->get<std::string>();
        }
        s.authored = e.contains("authored") && e["authored"].is_boolean() &&
                     e["authored"].get<bool>();
        if (const auto it = e.find("edited"); it != e.end()) {
            if (!it->is_array()) {
                return fail("section timeline: 'edited' must be an array of field names");
            }
            std::vector<std::string> names;
            for (const json& n : *it) {
                if (!n.is_string()) {
                    return fail("section timeline: 'edited' must contain only strings");
                }
                names.push_back(n.get<std::string>());
            }
            auto parsed = sectionFieldsFromNames(names);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            s.edited = *parsed;
        }
        s.labelConfidence = readFloat(e, "labelConfidence", 0.0f);
        s.startConfidence = readFloat(e, "startConfidence", 0.0f);
        s.endConfidence = readFloat(e, "endConfidence", 0.0f);
        s.energy = readFloat(e, "energy", 0.0f);
        s.density = readFloat(e, "density", 0.0f);
        if (const auto it = e.find("group"); it != e.end() && it->is_number_integer()) {
            s.repetitionGroup = it->get<int>();
        }
        out.sections.push_back(std::move(s));
    }
    out.renumber();
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ---- editing ------------------------------------------------------------------------------------

std::optional<double> moveBoundary(SectionTimeline& timeline, std::size_t index, double newTime,
                                   double minSectionSeconds) {
    if (index == 0 || index >= timeline.sections.size()) {
        return std::nullopt;
    }
    Section& before = timeline.sections[index - 1];
    Section& after = timeline.sections[index];
    const double low = before.startSeconds + minSectionSeconds;
    const double high = after.endSeconds - minSectionSeconds;
    if (low > high) {
        return std::nullopt; // the two together are too short to have a boundary anywhere
    }
    // Clamped into the legal window, and otherwise stored **exactly** as given. A boundary a person
    // dropped on a beat keeps that beat's own value, bit for bit, because nothing here computes a
    // nearby one.
    const double time = std::clamp(newTime, low, high);
    before.endSeconds = time;
    after.startSeconds = time;
    // Both sections changed, and both say so. The two halves of one boundary are two fields.
    before.edited |= SectionField::End;
    after.edited |= SectionField::Start;
    return time;
}

std::optional<std::size_t> splitSection(SectionTimeline& timeline, double seconds,
                                        double minSectionSeconds) {
    const auto index = timeline.indexAt(seconds);
    if (!index) {
        return std::nullopt;
    }
    Section& original = timeline.sections[*index];
    if (seconds - original.startSeconds < minSectionSeconds ||
        original.endSeconds - seconds < minSectionSeconds) {
        return std::nullopt;
    }
    Section later = original;
    later.startSeconds = seconds;
    later.authored = true; // it did not exist before
    // Its label would be a duplicate of the earlier half's, so it is dropped and composed from the
    // type instead -- which is how a split Verse becomes "Verse" and "Verse 2" rather than two
    // sections with the same name.
    later.label.clear();
    // The detector's confidences were claims about a span that no longer exists.
    later.labelConfidence = 0.0f;
    later.startConfidence = 0.0f;
    later.endConfidence = original.endConfidence;
    // Half of a repeated unit is not that unit.
    later.repetitionGroup = -1;
    // Carry the decisions, not the boundary marks: the new section's boundaries are its own.
    later.edited = original.edited & (SectionField::Type | SectionField::ShotIntent);

    original.endSeconds = seconds;
    original.edited |= SectionField::End;

    timeline.sections.insert(timeline.sections.begin() + static_cast<std::ptrdiff_t>(*index) + 1,
                             std::move(later));
    timeline.renumber();
    return *index + 1;
}

bool mergeSectionWithPrevious(SectionTimeline& timeline, std::size_t index) {
    if (index == 0 || index >= timeline.sections.size()) {
        return false;
    }
    Section& survivor = timeline.sections[index - 1];
    const Section& absorbed = timeline.sections[index];
    survivor.endSeconds = absorbed.endSeconds;
    // The boundary now held is the absorbed section's old end, so its record is the one that
    // describes it. Carried rather than discarded, so a later un-merge has something to show.
    survivor.endConfidence = absorbed.endConfidence;
    survivor.edited |= SectionField::End;
    timeline.sections.erase(timeline.sections.begin() + static_cast<std::ptrdiff_t>(index));
    timeline.renumber();
    return true;
}

bool removeSection(SectionTimeline& timeline, std::size_t index) {
    if (index >= timeline.sections.size() || timeline.sections.size() <= 1) {
        // A timeline of nothing is not a timeline. Clearing is a separate, explicit act.
        return false;
    }
    const Section& going = timeline.sections[index];
    if (index > 0) {
        Section& before = timeline.sections[index - 1];
        before.endSeconds = going.endSeconds;
        before.endConfidence = going.endConfidence;
        before.edited |= SectionField::End;
    } else {
        Section& after = timeline.sections[index + 1];
        after.startSeconds = going.startSeconds;
        after.startConfidence = going.startConfidence;
        after.edited |= SectionField::Start;
    }
    timeline.sections.erase(timeline.sections.begin() + static_cast<std::ptrdiff_t>(index));
    timeline.renumber();
    return true;
}

std::optional<std::size_t> insertSection(SectionTimeline& timeline, SectionTypeId type,
                                         double start, double end, const ShotLanguage& language,
                                         double minSectionSeconds) {
    if (!language.hasType(type)) {
        return std::nullopt;
    }
    if (!(end - start >= minSectionSeconds) || start < 0.0) {
        return std::nullopt;
    }
    if (timeline.durationSeconds > 0.0 && end > timeline.durationSeconds + kEpsilon) {
        return std::nullopt;
    }

    std::vector<Section> rebuilt;
    rebuilt.reserve(timeline.sections.size() + 2);
    for (const Section& s : timeline.sections) {
        const bool coveredEntirely = s.startSeconds >= start - kEpsilon &&
                                     s.endSeconds <= end + kEpsilon;
        if (coveredEntirely) {
            continue; // the new section takes all of it
        }
        const bool disjoint = s.endSeconds <= start + kEpsilon || s.startSeconds >= end - kEpsilon;
        if (disjoint) {
            rebuilt.push_back(s);
            continue;
        }
        if (s.startSeconds < start && s.endSeconds > end) {
            // The new section lands inside this one, which becomes two. The right-hand piece keeps
            // everything the left one has; it is still detected material and a re-analysis is free
            // to replace both.
            Section left = s;
            left.endSeconds = start;
            Section right = s;
            right.startSeconds = end;
            right.label.clear();
            rebuilt.push_back(std::move(left));
            rebuilt.push_back(std::move(right));
            continue;
        }
        Section trimmed = s;
        if (trimmed.startSeconds < start) {
            trimmed.endSeconds = start;
        } else {
            trimmed.startSeconds = end;
        }
        rebuilt.push_back(std::move(trimmed));
    }

    Section made;
    made.type = std::move(type);
    made.startSeconds = start;
    made.endSeconds = end;
    made.authored = true;
    rebuilt.push_back(std::move(made));

    seal(rebuilt);
    timeline.sections = std::move(rebuilt);
    timeline.renumber();
    const auto index = timeline.indexAt(start);
    return index;
}

bool setSectionType(SectionTimeline& timeline, std::size_t index, SectionTypeId type,
                    const ShotLanguage& language) {
    if (index >= timeline.sections.size()) {
        return false;
    }
    if (!language.hasType(type)) {
        // A section naming a type nobody defined resolves to a treatment nobody chose, with no error
        // anywhere. That looks exactly like the feature not working, so it is refused here instead.
        return false;
    }
    Section& s = timeline.sections[index];
    s.type = std::move(type);
    s.edited |= SectionField::Type;
    // `shotIntent` is deliberately untouched. An un-overridden section stores no intent at all, so
    // it resolves through whatever type it now has -- which is "changing the type updates the
    // default" with no rule to implement, and "unless I chose one" for free.
    timeline.renumber();
    return true;
}

bool setSectionLabel(SectionTimeline& timeline, std::size_t index, std::string label) {
    if (index >= timeline.sections.size()) {
        return false;
    }
    Section& s = timeline.sections[index];
    const bool clearing = label.empty();
    s.label = std::move(label);
    if (clearing) {
        // Renaming back to nothing un-marks the field, so the section is detected again and a
        // re-analysis may relabel it. That is what "undo my rename" has to mean.
        s.edited &= ~SectionField::Label;
    } else {
        s.edited |= SectionField::Label;
    }
    return true;
}

bool setSectionShotIntent(SectionTimeline& timeline, std::size_t index, ShotIntentId intent,
                          const ShotLanguage& language) {
    if (index >= timeline.sections.size() || !language.hasIntent(intent)) {
        return false;
    }
    Section& s = timeline.sections[index];
    s.shotIntent = std::move(intent);
    s.edited |= SectionField::ShotIntent;
    return true;
}

bool clearSectionShotIntent(SectionTimeline& timeline, std::size_t index) {
    if (index >= timeline.sections.size()) {
        return false;
    }
    Section& s = timeline.sections[index];
    s.shotIntent.reset();
    s.edited &= ~SectionField::ShotIntent;
    return true;
}

} // namespace avgen::song
