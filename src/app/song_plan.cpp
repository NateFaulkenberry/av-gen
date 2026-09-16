#include "app/song_plan.hpp"

#include "seq/song_structure.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace avgen::app {
namespace {

constexpr std::array<Autonomy, 3> kAutonomies{Autonomy::Locked, Autonomy::Guided,
                                              Autonomy::Expressive};

// One place, so a panel offering the choices, a `--director` argument parsing one and a project
// reading one back cannot come to disagree about how they are spelled.
constexpr std::array<std::string_view, 3> kAutonomyNames{"locked", "guided", "expressive"};

// Reads an axis, refusing rather than clamping (ADR-225's rule and this file's own). The default is
// used when the key is absent, which is what lets a plan written before an axis existed read as
// "the author did not say" rather than as zero.
Result<float> axis(const nlohmann::json& doc, const char* key, float fallback) {
    const auto it = doc.find(key);
    if (it == doc.end()) {
        return fallback;
    }
    if (!it->is_number()) {
        return fail("song plan: '{}' must be a number 0..1", key);
    }
    const auto v = it->get<float>();
    if (!(v >= 0.0f) || !(v <= 1.0f)) {
        return fail("song plan: '{}' is {}, and every intent axis is 0..1", key, v);
    }
    return v;
}

} // namespace

const char* autonomyName(Autonomy a) {
    const auto i = static_cast<std::size_t>(a);
    return i < kAutonomyNames.size() ? kAutonomyNames[i].data() : "guided";
}

std::optional<Autonomy> autonomyFromName(std::string_view name) {
    for (std::size_t i = 0; i < kAutonomyNames.size(); ++i) {
        if (kAutonomyNames[i] == name) {
            return static_cast<Autonomy>(i);
        }
    }
    return std::nullopt;
}

std::span<const Autonomy> allAutonomies() { return kAutonomies; }

// ---- ShotIntentProfile ---------------------------------------------------------------------------

Result<void> ShotIntentProfile::validate() const {
    if (id.empty()) {
        return fail("a shot intent needs a name: it is what a log line and a panel row say");
    }
    const auto check = [](const char* what, float v) -> Result<void> {
        if (!(v >= 0.0f) || !(v <= 1.0f)) {
            return fail("shot intent '{}' is {}, and every intent axis is 0..1", what, v);
        }
        return {};
    };
    if (auto ok = check("heroEmphasis", heroEmphasis); !ok) {
        return ok;
    }
    if (auto ok = check("distance", distance); !ok) {
        return ok;
    }
    if (auto ok = check("movement", movement); !ok) {
        return ok;
    }
    if (auto ok = check("variation", variation); !ok) {
        return ok;
    }
    if (auto ok = check("cutRate", cutRate); !ok) {
        return ok;
    }
    // An upper bound as well as a lower one, because a request for four hundred cameras is not an
    // ambitious intent, it is a typo, and honouring it silently would produce a shot per frame.
    if (cameras < 1 || cameras > 16) {
        return fail("shot intent '{}' asks for {} camera(s); 1..16", id, cameras);
    }
    return {};
}

nlohmann::json ShotIntentProfile::toJson() const {
    return nlohmann::json{{"id", id},
                          {"hero", heroEmphasis},
                          {"distance", distance},
                          {"movement", movement},
                          {"variation", variation},
                          {"cutRate", cutRate},
                          {"cameras", cameras}};
}

Result<ShotIntentProfile> ShotIntentProfile::fromJson(const nlohmann::json& doc) {
    if (!doc.is_object()) {
        return fail("a shot intent must be a JSON object");
    }
    ShotIntentProfile out;
    out.id = doc.value("id", std::string());
    const std::array<std::pair<const char*, float*>, 5> axes{{{"hero", &out.heroEmphasis},
                                                              {"distance", &out.distance},
                                                              {"movement", &out.movement},
                                                              {"variation", &out.variation},
                                                              {"cutRate", &out.cutRate}}};
    for (const auto& [key, field] : axes) {
        auto v = axis(doc, key, *field);
        if (!v) {
            return std::unexpected(v.error());
        }
        *field = *v;
    }
    out.cameras = doc.value("cameras", out.cameras);
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ---- SongPlan ------------------------------------------------------------------------------------

double SongPlan::durationSeconds() const {
    double end = 0.0;
    for (const SongPlanSection& s : sections) {
        end = std::max(end, s.endSeconds);
    }
    return end;
}

const SongPlanSection* SongPlan::sectionAt(double seconds) const {
    for (const SongPlanSection& s : sections) {
        if (seconds >= s.startSeconds && seconds < s.endSeconds) {
            return &s;
        }
    }
    return nullptr;
}

Result<void> SongPlan::validate() const {
    double previousEnd = -1.0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const SongPlanSection& s = sections[i];
        if (!(s.endSeconds > s.startSeconds)) {
            return fail("song plan section {} ('{}') runs from {:.3f}s to {:.3f}s", i + 1, s.label,
                        s.startSeconds, s.endSeconds);
        }
        if (s.startSeconds < previousEnd) {
            return fail("song plan section {} ('{}') starts at {:.3f}s, inside the one before it, "
                        "which ends at {:.3f}s",
                        i + 1, s.label, s.startSeconds, previousEnd);
        }
        if (auto ok = s.intent.validate(); !ok) {
            return std::unexpected(ok.error());
        }
        previousEnd = s.endSeconds;
    }
    return {};
}

nlohmann::json SongPlan::toJson() const {
    nlohmann::json out = nlohmann::json::object();
    if (!name.empty()) {
        out["name"] = name;
    }
    nlohmann::json list = nlohmann::json::array();
    for (const SongPlanSection& s : sections) {
        nlohmann::json j = nlohmann::json::object();
        j["start"] = s.startSeconds;
        j["end"] = s.endSeconds;
        if (!s.label.empty()) {
            j["label"] = s.label;
        }
        j["intent"] = s.intent.toJson();
        j["energy"] = s.energy;
        j["density"] = s.density;
        j["transition"] = s.transition;
        j["autonomy"] = autonomyName(s.autonomy);
        j["occurrence"] = s.occurrence;
        list.push_back(std::move(j));
    }
    out["sections"] = std::move(list);
    return out;
}

Result<SongPlan> SongPlan::fromJson(const nlohmann::json& doc) {
    if (!doc.is_object()) {
        return fail("a song plan must be a JSON object");
    }
    SongPlan out;
    out.name = doc.value("name", std::string());
    const auto sections = doc.find("sections");
    if (sections == doc.end()) {
        return out; // an empty plan is a real plan: it means Song Mode has nothing to direct yet
    }
    if (!sections->is_array()) {
        return fail("song plan: 'sections' must be an array");
    }
    for (const auto& j : *sections) {
        if (!j.is_object()) {
            return fail("song plan: every section must be a JSON object");
        }
        SongPlanSection s;
        s.startSeconds = j.value("start", 0.0);
        s.endSeconds = j.value("end", 0.0);
        s.label = j.value("label", std::string());
        const auto intent = j.find("intent");
        if (intent == j.end()) {
            return fail("song plan: section '{}' has no shot intent, and a section with no intent "
                        "is a section the director has nothing to execute",
                        s.label);
        }
        auto parsed = ShotIntentProfile::fromJson(*intent);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        s.intent = std::move(*parsed);
        s.energy = j.value("energy", s.energy);
        s.density = j.value("density", s.density);
        s.transition = j.value("transition", s.transition);
        if (const auto autonomy = j.find("autonomy"); autonomy != j.end()) {
            if (!autonomy->is_string()) {
                return fail("song plan: 'autonomy' must be a string");
            }
            const auto a = autonomyFromName(autonomy->get<std::string>());
            if (!a) {
                return fail("song plan: '{}' is not an autonomy; expected locked, guided or "
                            "expressive",
                            autonomy->get<std::string>());
            }
            s.autonomy = *a;
        }
        s.occurrence = j.value("occurrence", s.occurrence);
        out.sections.push_back(std::move(s));
    }
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ================================================================================================
// THE SEAM
// ================================================================================================

Result<SongPlan> songPlanFromJson(const nlohmann::json& doc) { return SongPlan::fromJson(doc); }

SongPlan songPlanFromMeasurements(const analysis::SongStructure& structure) {
    SongPlan plan;
    plan.name = "measured";
    plan.sections.reserve(structure.sections.size());

    // Occurrence, from the repetition grouping where the detector found one. A section the grouping
    // could not place (`repetitionGroup == -1`) is counted in its own bucket by *duration decile*,
    // which is a crude but honest stand-in: two sections of about the same length in a piece with no
    // detected repetition are the closest thing to "the same material again" that the measurements
    // can support. Nothing here reads a label to decide it.
    std::map<int, int> seenByGroup;
    std::map<int, int> seenByLength;

    for (std::size_t i = 0; i < structure.sections.size(); ++i) {
        const analysis::SongSection& in = structure.sections[i];
        SongPlanSection out;
        out.startSeconds = in.startSeconds;
        out.endSeconds = in.endSeconds;
        // The *only* use of a name anywhere in this function, and it goes straight into a display
        // field. Nothing downstream branches on it; see the header.
        out.label = seq::sectionDisplayName(in);
        out.energy = std::clamp(in.energy, 0.0f, 1.0f);
        out.density = std::clamp(in.density, 0.0f, 1.0f);
        // How hard this boundary lands: the size of the energy step across it. A measurement of the
        // boundary, not a claim about what kind of boundary it is -- the director wants to know
        // "did something change here", and that is a number the analysis already carries.
        out.transition = i == 0 ? 0.0f
                                : std::clamp(std::abs(in.energy - structure.sections[i - 1].energy),
                                             0.0f, 1.0f);
        out.autonomy = Autonomy::Guided;

        if (in.repetitionGroup >= 0) {
            out.occurrence = in.occurrence >= 0 ? in.occurrence : seenByGroup[in.repetitionGroup];
            ++seenByGroup[in.repetitionGroup];
        } else {
            const int bucket = static_cast<int>(in.durationSeconds() / 4.0);
            out.occurrence = seenByLength[bucket]++;
        }

        const float e = out.energy;
        const float d = out.density;
        ShotIntentProfile intent;
        // The id says what it is, so a log line reads sensibly, and says where it came from, so
        // nobody mistakes it for somebody's authored intent. Opaque to the director either way.
        intent.id = fmt::format("measured e{:.0f} d{:.0f}", e * 10.0f, d * 10.0f);
        intent.heroEmphasis = std::clamp(0.25f + 0.60f * e, 0.0f, 1.0f);
        intent.distance = std::clamp(0.80f - 0.55f * e, 0.0f, 1.0f);
        intent.movement = std::clamp(0.20f + 0.65f * d, 0.0f, 1.0f);
        intent.variation = std::clamp(0.15f + 0.70f * d, 0.0f, 1.0f);
        intent.cutRate = std::clamp(0.10f + 0.45f * e + 0.40f * d, 0.0f, 1.0f);
        intent.cameras = e > 0.75f ? 3 : (e > 0.45f ? 2 : 1);
        out.intent = std::move(intent);

        plan.sections.push_back(std::move(out));
    }
    return plan;
}

} // namespace avgen::app
