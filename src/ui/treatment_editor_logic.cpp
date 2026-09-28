#include "ui/treatment_editor_logic.hpp"

#include <algorithm>
#include <string>

namespace avgen::ui {

namespace {

// The dials, in the inspector's order: what the section is about and how tightly, what size its shots
// are, how they move and how often they cut, and how all of that travels across the section. The keys
// are the project file's; the labels say what the cut does.
constexpr TreatmentDial kDials[] = {
    {"focus", "about",
     "Who the section is about: one hero, the group, the world, or no preference (the director chooses).",
     TreatmentDialKind::Focus},
    {"focusStrength", "holds its subject",
     "How tightly the shots hold who the section is about. 0: a suggestion. 1: never leave them.",
     TreatmentDialKind::Amount, &song::ShotIntent::focusStrength},
    {"framing", "shot sizes",
     "The range of shot sizes that suit the section, tightest to widest. The director picks inside it.",
     TreatmentDialKind::Framing},
    {"movement", "camera movement",
     "How much the camera travels. 0: locked off. 1: always moving.", TreatmentDialKind::Amount,
     &song::ShotIntent::movement},
    {"energy", "energy", "How hard the section's shots push, overall.", TreatmentDialKind::Amount,
     &song::ShotIntent::energy},
    {"variation", "variety between shots",
     "How different each shot is from the one before. 0: every shot alike. 1: every shot different.",
     TreatmentDialKind::Amount, &song::ShotIntent::variation},
    {"cutFrequency", "cut rate",
     "How often it cuts. 0: one shot for the whole section. 1: as fast as the beat allows.\n"
     "The Auto-director panel's shortest and longest shot set the range this moves in.",
     TreatmentDialKind::Amount, &song::ShotIntent::cutFrequency},
    {"visualDensity", "how full the frame is",
     "How much is in frame. 0: one thing. 1: a full frame.", TreatmentDialKind::Amount,
     &song::ShotIntent::visualDensity},
    {"cameras", "cameras",
     "How many different cameras the section's coverage uses: the fewest and the most\n"
     "(most 0 = as many as the scene has).",
     TreatmentDialKind::Cameras},
    {"arc", "across the section",
     "How movement, energy, variety and the cut rate travel across the section: steady; rising to\n"
     "their values at its end (a build); falling from them (a release); held still (a pause); or\n"
     "landing at its start and settling (a drop).",
     TreatmentDialKind::Arc},
};

bool isCustom(const song::ShotLanguage& language, std::string_view id) {
    const std::span<const song::ShotIntent> customs = language.customIntents();
    return std::any_of(customs.begin(), customs.end(), [id](const song::ShotIntent& i) { return i.id == id; });
}

} // namespace

std::span<const TreatmentDial> treatmentDials() {
    return kDials;
}

const char* focusLabel(song::SubjectFocus focus) {
    switch (focus) {
    case song::SubjectFocus::Hero: return "one hero";
    case song::SubjectFocus::Ensemble: return "the group";
    case song::SubjectFocus::Environment: return "the world";
    case song::SubjectFocus::Mixed: return "no preference";
    }
    return "no preference";
}

const char* framingLabel(song::Framing framing) {
    switch (framing) {
    case song::Framing::ExtremeClose: return "extreme close";
    case song::Framing::Close: return "close";
    case song::Framing::Medium: return "medium";
    case song::Framing::Wide: return "wide";
    case song::Framing::VeryWide: return "very wide";
    }
    return "medium";
}

const char* arcLabel(song::Arc arc) {
    switch (arc) {
    case song::Arc::Steady: return "steady";
    case song::Arc::Rising: return "rising (a build)";
    case song::Arc::Falling: return "falling (a release)";
    case song::Arc::Suspended: return "held (a pause)";
    case song::Arc::Burst: return "burst, then settling (a drop)";
    }
    return "steady";
}

bool treatmentEditable(const song::ShotLanguage& language, std::string_view id) {
    return isCustom(language, id);
}

std::size_t treatmentUsers(const song::SectionTimeline& timeline, const song::ShotLanguage& language,
                           std::string_view id) {
    return static_cast<std::size_t>(std::count_if(
        timeline.sections.begin(), timeline.sections.end(),
        [&](const song::Section& s) { return language.intentIdFor(s) == id; }));
}

Result<void> editTreatment(song::ShotLanguage& language, const song::ShotIntent& edited) {
    if (!isCustom(language, edited.id)) {
        if (song::builtInShotIntent(edited.id) != nullptr) {
            return fail("'{}' is a built-in treatment: edit a copy of it instead", edited.id);
        }
        return fail("no treatment '{}' in this project to edit", edited.id);
    }
    // `defineIntent` validates first and replaces only when the definition is sound, so a refused edit
    // leaves the project's treatment exactly as it was.
    return language.defineIntent(edited);
}

Result<song::ShotIntentId> copyTreatmentForSection(song::ShotLanguage& language, song::SectionTimeline& timeline,
                                                   std::size_t index) {
    if (index >= timeline.sections.size()) {
        return fail("no section {} to give a treatment to", index);
    }
    song::ShotIntent copy = language.intentFor(timeline.sections[index]);
    const std::string base = copy.id + "_custom";
    std::string id = base;
    for (int n = 2; language.hasIntent(id); ++n) {
        id = base + "_" + std::to_string(n);
    }
    copy.id = id;
    copy.name = copy.name.empty() ? id : copy.name + " (custom)";
    copy.builtIn = false;
    if (auto ok = language.defineIntent(copy); !ok) {
        return fail("{}", ok.error().message);
    }
    if (!song::setSectionShotIntent(timeline, index, id, language)) {
        return fail("section {} would not take the treatment '{}'", index, id);
    }
    return id;
}

} // namespace avgen::ui
