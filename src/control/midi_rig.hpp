#pragma once

// ADR-1705: the MIDI rig -- the performer's controller setup, one set of mappings for every scene.
//
// The live scene switcher opens one project per live scene, so a project mapping does not cross scenes (ADR-1700).
// The rig does: it is kept in `<preferences>/midi-rig.json`, beside `settings.json`, and never in a project. In memory
// it rides in `ControlMap::midiRig` (scope Rig) so the resolver, the mapper and the panel's helpers see one list;
// `ControlHub::setMap` carries it across every project load, and `ControlMap::toJson` never writes it.
//
// The file:
//   {"format": "avgen-midi-rig", "version": 1, "mappings": [<mapping>, ...]}
// Each mapping is a `control.midi.mappings` record with `"scope": "rig"` (no `scene`, no `overrides`). An unreadable
// record is kept verbatim, reported, never applied and written back, as a project's are.

#include "control/control_map.hpp"
#include "core/error.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::control {

struct MidiRig {
    static constexpr const char* kFormatName = "avgen-midi-rig";
    static constexpr int kFormatVersion = 1;

    std::vector<MidiBinding> mappings; // every one scope Rig
    std::vector<std::string> warnings; // what fromJson set aside

    [[nodiscard]] nlohmann::json toJson() const;
    // A bad document (not an object, another format, a newer version) fails; a bad record is set aside.
    static Result<MidiRig> fromJson(const nlohmann::json& doc);
    // Written beside-and-renamed (AppSettings' discipline). An empty path is a no-op.
    [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
    // A missing file is an empty rig, not an error.
    static Result<MidiRig> load(const std::filesystem::path& path);
    // `<preferences>/midi-rig.json`; empty in, empty out (a session with no preferences directory keeps no rig).
    [[nodiscard]] static std::filesystem::path pathIn(const std::filesystem::path& preferencesDir);
};

// What the rig's file would hold for `mappings`, as text: the hub compares it after an edit to tell whether the rig
// changed (and so must be saved). Called on an edit, never per frame.
[[nodiscard]] std::string midiRigFingerprint(const std::vector<MidiBinding>& mappings);

// ADR-1705: the parameters every live scene has, so a rig mapping to one of them is never inactive for a scene. They
// are the engine's own registrations, made for every project whatever it renders: the post chain (`post/...`), the
// temporal effects (`temporal/...`), the camera's lens, exposure and focus (`camera/lens|exposure|focus/...`) and its orbit
// (`camera/distance`, `camera/height`, `camera/orbitSpeed`), the response
// (`sonic/response/...`), `scene/brightness`, `scene/gridIntensity` and the live input gain (`audio/inputGain`).
// Measured over every live example: tests/integration/test_midi_rig_engine.cpp, "every live scene has the rig's common
// parameters", fails if this set and the measured one differ in either direction.
[[nodiscard]] bool midiRigCommonParameter(std::string_view path);

// ---- editing (the panel's, beside midi_learn.hpp's) ----
// Moves a project or scene mapping into the rig (its scene and any override dropped, switched on). A default stays a
// default: it is copied into the rig instead, and the copy shadows it here and applies everywhere. Returns the rig
// mapping's id, or "" for an unknown or unreadable id.
std::string promoteMidiMappingToRig(ControlMap& map, std::string_view id);
// Moves a rig mapping into this scene's mappings (the rig loses it: other scenes stop seeing it). Returns false for an
// id that is not a rig mapping.
bool demoteMidiRigMappingToScene(ControlMap& map, std::string_view sceneKey, std::string_view id);
// Keeps the rig mapping and makes a scene copy that overrides it here only. Returns the copy's id ("" unknown).
std::string overrideMidiRigMappingInScene(ControlMap& map, std::string_view sceneKey, std::string_view id);

} // namespace avgen::control
