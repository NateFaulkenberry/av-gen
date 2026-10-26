// ADR-1705: the MIDI rig -- the performer's mappings for every scene, kept outside any project. Precedence
// (default < rig < project < scene), the rig file, the editing helpers, app actions and inactive destinations,
// below the engine and driven through a fake set of destinations.

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_learn.hpp"
#include "control/midi_mapper.hpp"
#include "control/midi_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

using namespace avgen::control;
using nlohmann::json;

namespace {

struct FakeTargets final : MidiTargets {
    std::map<std::string, float> params;
    std::map<std::string, float> signals;
    std::vector<MidiAction> actions;
    std::optional<float> parameter(const std::string& path, int) override {
        const auto it = params.find(path);
        return it == params.end() ? std::nullopt : std::optional<float>(it->second);
    }
    void setParameter(const std::string& path, int, float value) override {
        if (const auto it = params.find(path); it != params.end()) {
            it->second = value;
        }
    }
    float signal(const std::string& name) override { return signals.count(name) ? signals[name] : 0.0f; }
    bool signalHasRest(const std::string&) override { return false; }
    void setSignal(const std::string& name, float value) override { signals[name] = value; }
    void pulseSignal(const std::string& name, float strength) override { signals[name] = strength; }
    void action(MidiAction a) override { actions.push_back(a); }
};

MidiMessage cc(std::uint8_t number, std::uint8_t value) {
    MidiMessage m;
    m.kind = MidiKind::ControlChange;
    m.data1 = number;
    m.data2 = value;
    return m;
}
MidiMessage note(std::uint8_t number, std::uint8_t velocity) {
    MidiMessage m;
    m.kind = velocity == 0 ? MidiKind::NoteOff : MidiKind::NoteOn;
    m.data1 = number;
    m.data2 = velocity;
    return m;
}

MidiBinding ccTo(int number, const std::string& signal, MidiScope scope, const std::string& id) {
    MidiBinding b;
    b.kind = MidiBindKind::ControlChange;
    b.number = number;
    b.target.signal = signal;
    b.scope = scope;
    b.id = id;
    if (scope == MidiScope::Scene) {
        b.scene = "s";
    }
    return b;
}
MidiBinding ccToParam(int number, const std::string& path, float lo, float hi, MidiScope scope, const std::string& id) {
    MidiBinding b = ccTo(number, "", scope, id);
    b.target.parameter = path;
    b.target.min = lo;
    b.target.max = hi;
    return b;
}

MidiMappingState stateOf(const ControlMap& map, const std::string& scene, const std::string& id) {
    for (const auto& r : resolveMidiMappings(map, scene)) {
        if (r.binding->id == id) {
            return r.state;
        }
    }
    FAIL("no mapping " << id);
    return MidiMappingState::Invalid;
}

std::filesystem::path tempFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("avgen_midi_rig_" + std::to_string(::getpid()) + "_" + name);
}

} // namespace

TEST_CASE("ADR-1705 precedence: default < rig < project < scene, with ADR-1700's rules", "[midi][rig][scope][adr1705]") {
    ControlMap map;
    map.midi.push_back(ccTo(21, "cloud", MidiScope::Default, "d-cloud"));
    map.midiRig.push_back(ccTo(21, "cloud", MidiScope::Rig, "r-cloud"));

    // The rig beats the scene's default on an overlapping source to the same destination.
    CHECK(stateOf(map, "s", "d-cloud") == MidiMappingState::Shadowed);
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Active);
    CHECK(stateOf(map, "elsewhere", "r-cloud") == MidiMappingState::Active); // every scene

    // A project mapping beats the rig; a scene mapping beats both.
    map.midiUser.push_back(ccTo(21, "cloud", MidiScope::Project, "p-cloud"));
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Shadowed);
    map.midiUser.push_back(ccTo(21, "cloud", MidiScope::Scene, "s-cloud"));
    CHECK(stateOf(map, "s", "p-cloud") == MidiMappingState::Shadowed);
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Shadowed);
    CHECK(stateOf(map, "s", "s-cloud") == MidiMappingState::Active);
    CHECK(stateOf(map, "other", "s-cloud") == MidiMappingState::OtherScene);
    map.midiUser.clear();

    // One source, different destinations: the rig and the default both apply.
    map.midiRig[0].target.signal = "glow";
    CHECK(stateOf(map, "s", "d-cloud") == MidiMappingState::Active);
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Active);
    map.midiRig[0].target.signal = "cloud";

    // A disabled rig mapping shadows nothing; a tombstone does not reach the rig.
    map.midiRig[0].enabled = false;
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Disabled);
    CHECK(stateOf(map, "s", "d-cloud") == MidiMappingState::Active);
    map.midiRig[0].enabled = true;
    map.disabledDefaults = {"r-cloud"};
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Active);
    map.disabledDefaults.clear();

    // A scene mapping that overrides the rig mapping replaces it in that scene only, even on another source.
    MidiBinding local = ccTo(30, "cloud", MidiScope::Scene, "s-local");
    local.overrides = "r-cloud";
    map.midiUser.push_back(local);
    CHECK(stateOf(map, "s", "r-cloud") == MidiMappingState::Overridden);
    CHECK(stateOf(map, "t", "r-cloud") == MidiMappingState::Active);
}

TEST_CASE("the mapper applies the rig between the defaults and the project", "[midi][rig][scope][adr1705]") {
    ControlMap map;
    FakeTargets t;
    t.params["p"] = 0.0f;
    // Three scopes on one knob and one destination: the highest active one is the only one that applies.
    map.midi.push_back(ccToParam(5, "p", 0.0f, 1.0f, MidiScope::Default, "d"));
    map.midiRig.push_back(ccToParam(5, "p", 0.0f, 2.0f, MidiScope::Rig, "r"));
    MidiMapper mapper;
    mapper.rebuild(map, "s", t);
    mapper.apply(cc(5, 127), t, 1.0);
    CHECK(t.params["p"] == 2.0f);
    map.midiUser.push_back(ccToParam(5, "p", 0.0f, 3.0f, MidiScope::Project, "u"));
    mapper.rebuild(map, "s", t);
    mapper.apply(cc(5, 127), t, 1.0);
    CHECK(t.params["p"] == 3.0f);

    // Different destinations on one knob: written in precedence order (default, rig, project), so under `replace` the
    // last writer of a shared channel is the higher scope -- the candidates are merged in order, never sorted.
    ControlMap order;
    order.midi.push_back(ccTo(7, "a", MidiScope::Default, "d7"));
    order.midiRig.push_back(ccTo(7, "b", MidiScope::Rig, "r7"));
    MidiBinding anyCc = ccTo(-1, "c", MidiScope::Project, "u-any");
    order.midiUser.push_back(anyCc);
    FakeTargets t2;
    MidiMapper m2;
    m2.rebuild(order, "s", t2);
    CHECK(m2.apply(cc(7, 127), t2, 1.0) == 3);
    CHECK(t2.signals["a"] == 1.0f);
    CHECK(t2.signals["b"] == 1.0f);
    CHECK(t2.signals["c"] == 1.0f);
}

TEST_CASE("a rig mapping whose destination the scene lacks is inactive here, kept, and not a warning",
          "[midi][rig][adr1705]") {
    ControlMap map;
    map.midiRig.push_back(ccToParam(10, "post/bloom/intensity", 0.0f, 2.0f, MidiScope::Rig, "r-bloom"));
    map.midiRig.push_back(ccToParam(11, "rift/fog/density", 0.0f, 1.0f, MidiScope::Rig, "r-fog"));
    map.midiUser.push_back(ccToParam(12, "rift/fog/density", 0.0f, 1.0f, MidiScope::Project, "u-fog"));
    FakeTargets t;
    t.params["post/bloom/intensity"] = 0.0f;
    MidiMapper mapper;
    mapper.rebuild(map, "s", t);
    const MidiMappingStatus* bloom = mapper.status("r-bloom");
    const MidiMappingStatus* fog = mapper.status("r-fog");
    const MidiMappingStatus* userFog = mapper.status("u-fog");
    REQUIRE(bloom != nullptr);
    REQUIRE(fog != nullptr);
    REQUIRE(userFog != nullptr);
    CHECK_FALSE(bloom->inactiveForScene);
    CHECK(fog->inactiveForScene);
    CHECK(fog->state == MidiMappingState::Active); // its precedence state is untouched: only this scene lacks it
    CHECK_FALSE(userFog->inactiveForScene);        // a project's missing destination is still "not in this scene"
    CHECK(userFog->destinationMissing);
    CHECK(mapper.activeCount() == 2);               // the inactive rig mapping is not applied here
    CHECK(mapper.apply(cc(11, 127), t, 1.0) == 0);
    CHECK(map.midiRig.size() == 2); // kept

    // The next scene has it: active there, from the same rig.
    t.params["rift/fog/density"] = 0.0f;
    mapper.rebuild(map, "rift", t, false);
    CHECK_FALSE(mapper.status("r-fog")->inactiveForScene);
    CHECK(mapper.apply(cc(11, 127), t, 1.0) == 1);
    CHECK(t.params["rift/fog/density"] == 1.0f);
}

TEST_CASE("app actions fire once per press", "[midi][rig][action][adr1705]") {
    ControlMap map;
    MidiBinding next;
    next.kind = MidiBindKind::Note;
    next.number = 40;
    next.target.action = MidiAction::LiveNextScene;
    next.scope = MidiScope::Rig;
    next.id = "r-next";
    map.midiRig.push_back(next);
    MidiBinding play;
    play.kind = MidiBindKind::ControlChange;
    play.number = 64;
    play.target.action = MidiAction::TransportPlay;
    play.scope = MidiScope::Rig;
    play.id = "r-play";
    map.midiRig.push_back(play);
    FakeTargets t;
    MidiMapper mapper;
    mapper.rebuild(map, "s", t);

    mapper.apply(note(40, 100), t, 1.0);
    mapper.apply(note(40, 0), t, 1.0);
    mapper.apply(note(40, 90), t, 1.0);
    // A CC button: 127 then 0 is one press; a held value repeated is not another.
    mapper.apply(cc(64, 127), t, 1.0);
    mapper.apply(cc(64, 127), t, 1.0);
    mapper.apply(cc(64, 0), t, 1.0);
    mapper.apply(cc(64, 40), t, 1.0);
    REQUIRE(t.actions.size() == 3);
    CHECK(t.actions[0] == MidiAction::LiveNextScene);
    CHECK(t.actions[1] == MidiAction::LiveNextScene);
    CHECK(t.actions[2] == MidiAction::TransportPlay);
    CHECK(t.signals.empty());
    CHECK(midiDestinationKeys(next.target) == std::vector<std::string>{"action.live.next-scene"});

    // The JSON names the keyboard's command ids; an unknown action is refused (a user record is set aside).
    const json j = midiBindingToJson(next);
    CHECK(j["action"] == "live.next-scene");
    CHECK(j["scope"] == "rig");
    auto back = midiBindingFromJson(j, MidiScope::Rig, nullptr);
    REQUIRE(back.has_value());
    CHECK(back->target.action == MidiAction::LiveNextScene);
    json bad = j;
    bad["action"] = "file.delete-everything";
    CHECK_FALSE(midiBindingFromJson(bad, MidiScope::Rig, nullptr).has_value());
    json both = j;
    both["signal"] = "x";
    CHECK_FALSE(midiBindingFromJson(both, MidiScope::Rig, nullptr).has_value());
}

TEST_CASE("the rig file round-trips, keeps what it cannot read, and a project refuses rig records",
          "[midi][rig][persistence][adr1705]") {
    MidiRig rig;
    rig.mappings.push_back(ccToParam(10, "post/bloom/intensity", 0.0f, 2.0f, MidiScope::Rig, "m-aaaaaaaaaaaa"));
    rig.mappings.push_back(ccTo(11, "fx", MidiScope::Rig, "m-bbbbbbbbbbbb"));
    const auto path = tempFile("roundtrip.json");
    REQUIRE(rig.save(path).has_value());
    auto loaded = MidiRig::load(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->warnings.empty());
    REQUIRE(loaded->mappings.size() == 2);
    CHECK(loaded->mappings[0].scope == MidiScope::Rig);
    CHECK(loaded->mappings[0].id == "m-aaaaaaaaaaaa");
    CHECK(loaded->toJson() == rig.toJson());

    // A record it cannot read is kept, reported, never applied and written back verbatim.
    json doc = rig.toJson();
    doc["mappings"].push_back(json{{"id", "m-bad"}, {"kind", "cc"}, {"number", 400}, {"signal", "x"}});
    doc["mappings"].push_back(json{{"id", "m-scene"}, {"scope", "scene"}, {"scene", "x"}, {"kind", "cc"}, {"signal", "x"}});
    auto withBad = MidiRig::fromJson(doc);
    REQUIRE(withBad.has_value());
    CHECK(withBad->warnings.size() == 2);
    REQUIRE(withBad->mappings.size() == 4);
    CHECK(withBad->mappings[2].invalid());
    CHECK(withBad->mappings[3].invalid());
    CHECK(withBad->toJson()["mappings"][2] == doc["mappings"][2]);
    ControlMap map;
    map.midiRig = withBad->mappings;
    CHECK(stateOf(map, "s", "m-bad") == MidiMappingState::Invalid);

    // Missing: an empty rig. Another format: refused (the file is then left alone).
    CHECK(MidiRig::load(tempFile("missing.json"))->mappings.empty());
    CHECK_FALSE(MidiRig::fromJson(json{{"format", "avgen-settings"}, {"version", 1}}).has_value());
    CHECK(MidiRig::pathIn("/prefs") == std::filesystem::path("/prefs/midi-rig.json"));
    CHECK(MidiRig::pathIn({}).empty());

    // A project's control block never carries the rig, and a "rig" record found in one is set aside.
    map.midiRig = rig.mappings;
    const json control = map.toJson();
    CHECK_FALSE(control["midi"].contains("mappings"));
    CHECK(control.dump().find("rig") == std::string::npos);
    json project = json::parse(R"({"midi": {"mappings": [{"id": "m-x", "scope": "rig", "kind": "cc", "number": 1,
                                                          "signal": "x"}]}})");
    auto parsed = ControlMap::fromJson(project);
    REQUIRE(parsed.has_value());
    CHECK(parsed->midiRig.empty());
    REQUIRE(parsed->midiUser.size() == 1);
    CHECK(parsed->midiUser[0].invalid());
    std::filesystem::remove(path);
}

TEST_CASE("Learn, promote, demote and override move mappings between the rig and the project",
          "[midi][rig][learn][adr1705]") {
    ControlMap map;
    map.midi.push_back(ccTo(21, "cloud", MidiScope::Default, "d-cloud"));

    // Rig Learn goes to the rig.
    MidiBinding candidate = ccTo(22, "glow", MidiScope::Rig, "");
    const std::string rigId = commitMidiLearn(map, "s", candidate, MidiLearnCommit::Add);
    REQUIRE(map.midiRig.size() == 1);
    CHECK(map.midiRig[0].id == rigId);
    CHECK(map.midiRig[0].scene.empty());
    CHECK(map.midiUser.empty());

    // A scene Learn that replaces the rig mapping switches it off in this scene only.
    MidiBinding local = ccTo(22, "other", MidiScope::Scene, "");
    commitMidiLearn(map, "s", local, MidiLearnCommit::Replace);
    CHECK(map.midiRig[0].enabled); // the rig itself is untouched
    CHECK(stateOf(map, "s", rigId) == MidiMappingState::Overridden);
    CHECK(stateOf(map, "t", rigId) == MidiMappingState::Active);
    map.midiUser.clear();

    // Editing a rig mapping edits the rig (no scene copy).
    MidiBinding* edit = editableMidiMapping(map, "s", rigId);
    REQUIRE(edit == &map.midiRig[0]);

    // Promote a project mapping: it leaves the project for the rig.
    map.midiUser.push_back(ccTo(30, "fx", MidiScope::Scene, "u-fx"));
    const std::string promoted = promoteMidiMappingToRig(map, "u-fx");
    CHECK(promoted == "u-fx");
    CHECK(map.midiUser.empty());
    REQUIRE(map.midiRig.size() == 2);
    CHECK(map.midiRig[1].scope == MidiScope::Rig);
    CHECK(map.midiRig[1].scene.empty());
    // Promote a default: copied (the scene keeps its default, which the rig copy now shadows here).
    const std::string fromDefault = promoteMidiMappingToRig(map, "d-cloud");
    CHECK_FALSE(fromDefault.empty());
    CHECK(map.midi.size() == 1);
    CHECK(stateOf(map, "s", "d-cloud") == MidiMappingState::Shadowed);

    // Override in this scene: a scene copy replaces it here; asked again, the same copy.
    const std::string copy = overrideMidiRigMappingInScene(map, "s", "u-fx");
    CHECK(overrideMidiRigMappingInScene(map, "s", "u-fx") == copy);
    CHECK(stateOf(map, "s", "u-fx") == MidiMappingState::Overridden);
    CHECK(stateOf(map, "t", "u-fx") == MidiMappingState::Active);

    // Demote: out of the rig into this scene.
    CHECK(demoteMidiRigMappingToScene(map, "s", rigId));
    CHECK(map.findMidi(rigId)->scope == MidiScope::Scene);
    CHECK(map.findMidi(rigId)->scene == "s");
    CHECK(stateOf(map, "t", rigId) == MidiMappingState::OtherScene);
    CHECK_FALSE(demoteMidiRigMappingToScene(map, "s", "d-cloud"));

    // Remove, enable and duplicate reach the rig; a reset of the scene does not.
    setMidiMappingEnabled(map, fromDefault, false);
    CHECK_FALSE(midiMappingEnabled(map, fromDefault));
    const std::string dup = duplicateMidiMapping(map, "s", fromDefault);
    CHECK(map.findMidi(dup)->scope == MidiScope::Rig);
    resetSceneMidiMappings(map, "s");
    CHECK(map.findMidi(dup) != nullptr);
    CHECK(removeMidiMapping(map, dup));
    CHECK(map.findMidi(dup) == nullptr);
}

TEST_CASE("the rig's common parameters are the engine's own", "[midi][rig][adr1705]") {
    CHECK(midiRigCommonParameter("post/bloom/intensity"));
    CHECK(midiRigCommonParameter("audio/inputGain"));
    CHECK(midiRigCommonParameter("temporal/feedback/amount"));
    CHECK(midiRigCommonParameter("scene/brightness"));
    CHECK_FALSE(midiRigCommonParameter("scene/fog/density"));
    CHECK_FALSE(midiRigCommonParameter("orb/scale"));
    CHECK_FALSE(midiRigCommonParameter("postcard/x"));
}
