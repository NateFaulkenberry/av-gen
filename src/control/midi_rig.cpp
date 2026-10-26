#include "control/midi_rig.hpp"

#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <fstream>

namespace avgen::control {

nlohmann::json MidiRig::toJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const MidiBinding& b : mappings) {
        list.push_back(midiBindingToJson(b));
    }
    return {{"format", kFormatName}, {"version", kFormatVersion}, {"mappings", std::move(list)}};
}

Result<MidiRig> MidiRig::fromJson(const nlohmann::json& doc) {
    if (!doc.is_object()) {
        return fail("the MIDI rig must be an object");
    }
    if (doc.value("format", std::string()) != kFormatName) {
        return fail("not a MIDI rig (format '{}')", doc.value("format", std::string()));
    }
    const int version = doc.value("version", 0);
    if (version < 1 || version > kFormatVersion) {
        return fail("MIDI rig version {} is not one this build reads (1..{})", version, kFormatVersion);
    }
    MidiRig rig;
    const auto it = doc.find("mappings");
    if (it == doc.end()) {
        return rig;
    }
    if (!it->is_array()) {
        return fail("the MIDI rig's 'mappings' must be an array");
    }
    for (const auto& e : *it) {
        auto b = midiBindingFromJson(e, MidiScope::Rig, &rig.warnings);
        if (!b) {
            MidiBinding kept;
            kept.scope = MidiScope::Rig;
            kept.enabled = false;
            kept.invalidRaw = e.dump();
            kept.invalidReason = b.error().message;
            if (e.is_object() && e.contains("id") && e["id"].is_string()) {
                kept.id = e["id"].get<std::string>();
            }
            rig.warnings.push_back(fmt::format("MIDI rig: a mapping was set aside: {}", kept.invalidReason));
            rig.mappings.push_back(std::move(kept));
            continue;
        }
        if (b->id.empty()) {
            b->id = newMidiMappingId(); // hand-written without one: minted, and written back with it
        }
        rig.mappings.push_back(std::move(*b));
    }
    // Two records with one id would make the panel edit the wrong one: the later is renamed.
    for (std::size_t i = 0; i < rig.mappings.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (!rig.mappings[i].id.empty() && rig.mappings[i].id == rig.mappings[j].id) {
                rig.warnings.push_back(fmt::format("MIDI rig: id '{}' is used twice; the second was renamed",
                                                   rig.mappings[i].id));
                rig.mappings[i].id = newMidiMappingId();
                break;
            }
        }
    }
    return rig;
}

Result<void> MidiRig::save(const std::filesystem::path& path) const {
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return fail("cannot write '{}'", temp.string());
        }
        out << toJson().dump(2) << '\n';
        if (!out) {
            return fail("failed writing '{}'", temp.string());
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return fail("cannot replace '{}': {}", path.string(), ec.message());
    }
    return {};
}

Result<MidiRig> MidiRig::load(const std::filesystem::path& path) {
    if (path.empty()) {
        return MidiRig{};
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return MidiRig{};
    }
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    auto rig = fromJson(doc);
    if (!rig) {
        return fail("'{}': {}", path.string(), rig.error().message);
    }
    return rig;
}

std::filesystem::path MidiRig::pathIn(const std::filesystem::path& preferencesDir) {
    return preferencesDir.empty() ? std::filesystem::path{} : preferencesDir / "midi-rig.json";
}

std::string midiRigFingerprint(const std::vector<MidiBinding>& mappings) {
    nlohmann::json list = nlohmann::json::array();
    for (const MidiBinding& b : mappings) {
        list.push_back(midiBindingToJson(b));
    }
    return list.dump();
}

bool midiRigCommonParameter(std::string_view path) {
    // The engine's own registrations, made for every project whatever it renders (measured over the live set).
    for (const std::string_view prefix : {"post/", "temporal/", "sonic/response/", "camera/lens/", "camera/exposure/",
                                          "camera/focus/"}) {
        if (path.starts_with(prefix)) {
            return true;
        }
    }
    for (const std::string_view exact : {"audio/inputGain", "scene/brightness", "scene/gridIntensity", "camera/distance",
                                         "camera/height", "camera/orbitSpeed"}) {
        if (path == exact) {
            return true;
        }
    }
    return false;
}

std::string promoteMidiMappingToRig(ControlMap& map, std::string_view id) {
    const auto user = std::find_if(map.midiUser.begin(), map.midiUser.end(), [&](const MidiBinding& b) { return b.id == id; });
    MidiBinding moved;
    if (user != map.midiUser.end()) {
        if (user->invalid()) {
            return {};
        }
        moved = std::move(*user);
        map.midiUser.erase(user);
    } else {
        const auto def = std::find_if(map.midi.begin(), map.midi.end(), [&](const MidiBinding& b) { return b.id == id; });
        if (def == map.midi.end()) {
            return {};
        }
        moved = *def; // the scene's default stays; the rig gets its own copy
        moved.id = newMidiMappingId();
    }
    moved.scope = MidiScope::Rig;
    moved.scene.clear();
    moved.overrides.clear();
    moved.idAuthored = false;
    moved.enabled = true;
    std::string newId = moved.id;
    map.midiRig.push_back(std::move(moved));
    return newId;
}

bool demoteMidiRigMappingToScene(ControlMap& map, std::string_view sceneKey, std::string_view id) {
    const auto it = std::find_if(map.midiRig.begin(), map.midiRig.end(), [&](const MidiBinding& b) { return b.id == id; });
    if (it == map.midiRig.end() || it->invalid()) {
        return false;
    }
    MidiBinding moved = std::move(*it);
    map.midiRig.erase(it);
    moved.scope = MidiScope::Scene;
    moved.scene = std::string(sceneKey);
    moved.overrides.clear();
    // Overrides of it in other scenes now name nothing: they are plain scene mappings there (ADR-1700's orphan rule).
    map.midiUser.push_back(std::move(moved));
    return true;
}

std::string overrideMidiRigMappingInScene(ControlMap& map, std::string_view sceneKey, std::string_view id) {
    const auto it = std::find_if(map.midiRig.begin(), map.midiRig.end(), [&](const MidiBinding& b) { return b.id == id; });
    if (it == map.midiRig.end() || it->invalid()) {
        return {};
    }
    for (const MidiBinding& b : map.midiUser) {
        if (b.overrides == id && b.scope == MidiScope::Scene && b.scene == sceneKey) {
            return b.id;
        }
    }
    MidiBinding copy = *it;
    copy.id = newMidiMappingId();
    copy.scope = MidiScope::Scene;
    copy.scene = std::string(sceneKey);
    copy.overrides = std::string(id);
    copy.enabled = true;
    std::string newId = copy.id;
    map.midiUser.push_back(std::move(copy));
    return newId;
}

} // namespace avgen::control
