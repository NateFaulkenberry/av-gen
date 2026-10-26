// MIDI performance mapping (ADR-1700): persistence of defaults and user mappings, and the Learn,
// edit and monitor logic the MIDI panel draws.

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_learn.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen::control;
using nlohmann::json;

namespace {

MidiMessage cc(std::uint8_t number, std::uint8_t value, std::uint8_t channel = 0, std::uint16_t device = 0) {
    MidiMessage m;
    m.kind = MidiKind::ControlChange;
    m.data1 = number;
    m.data2 = value;
    m.channel = channel;
    m.device = device;
    return m;
}

json projectControl() {
    return json::parse(R"({
      "midi": {
        "enabled": true, "filter": "*",
        "bindings": [
          {"source": "*", "channel": -1, "kind": "cc", "number": 21, "signal": "cloud"},
          {"source": "*", "channel": -1, "kind": "noteEvent", "number": 36, "signal": "pad"}
        ]
      }
    })");
}

// Equal JSON, numbers compared as floats: a binding's min/max are floats, so 1.2 is written back as 1.2000000476837158 --
// as it always was, before the user layer existed.
bool sameJson(const json& a, const json& b) {
    if (a.is_number() && b.is_number()) {
        return std::abs(a.get<double>() - b.get<double>()) <= 1e-6 * std::max(1.0, std::abs(b.get<double>()));
    }
    if (a.type() != b.type() || a.size() != b.size()) {
        return false;
    }
    if (a.is_object()) {
        for (const auto& [key, value] : b.items()) {
            if (!a.contains(key) || !sameJson(a[key], value)) return false;
        }
        return true;
    }
    if (a.is_array()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!sameJson(a[i], b[i])) return false;
        }
        return true;
    }
    return a == b;
}

std::vector<MidiDeviceSlot> devices() {
    return {{"100", "Keys", true, false}, {"200", "Pads", true, false}};
}

} // namespace

// ---- 8.3 persistence -----------------------------------------------------------------------------------------------

TEST_CASE("a project without user mappings saves exactly as it was written", "[midi][persistence]") {
    // Every tracked project that ships MIDI bindings: fromJson -> toJson leaves its midi block unchanged, so
    // adding the user layer rewrote nothing an author wrote.
    int checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(std::string(AVGEN_SOURCE_DIR) + "/examples")) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("control") || !doc["control"].contains("midi")) {
            continue;
        }
        auto map = ControlMap::fromJson(doc["control"]);
        INFO(entry.path().string());
        REQUIRE(map.has_value());
        CHECK(map->warnings.empty());
        const json written = map->toJson()["midi"];
        const json& authored = doc["control"]["midi"];
        CHECK(sameJson(written.value("bindings", json::array()), authored.value("bindings", json::array())));
        CHECK_FALSE(written.contains("mappings"));
        CHECK_FALSE(written.contains("disabledDefaults"));
        ++checked;
    }
    CHECK(checked >= 20);
}

TEST_CASE("defaults get stable ids; user mappings, tombstones and combine round-trip deterministically",
          "[midi][persistence][adr1700]") {
    auto map = ControlMap::fromJson(projectControl());
    REQUIRE(map.has_value());
    REQUIRE(map->midi.size() == 2);
    const std::string cloudId = map->midi[0].id;
    CHECK(cloudId.starts_with("d-"));
    CHECK_FALSE(map->midi[0].idAuthored);
    // The same file loads to the same ids (they name what the binding is, not when it was read).
    CHECK(ControlMap::fromJson(projectControl())->midi[0].id == cloudId);

    MidiBinding mine;
    mine.kind = MidiBindKind::ControlChange;
    mine.number = 74;
    mine.device = "100";
    mine.deviceName = "Keys";
    mine.channel = 2;
    mine.curve = MidiCurve::SCurve;
    mine.smoothMs = 80.0f;
    mine.invert = true;
    mine.scale = 0.5f;
    mine.offset = 0.1f;
    mine.encoder = MidiEncoder::BinaryOffset;
    mine.target.parameter = "orb/scale";
    mine.target.min = 0.5f;
    mine.target.max = 2.0f;
    mine.scope = MidiScope::Scene;
    const std::string id = commitMidiLearn(*map, "aurora.scene.json", mine, MidiLearnCommit::Add);
    MidiBinding project;
    project.kind = MidiBindKind::Note;
    project.number = 36;
    project.numberHigh = 51;
    project.velocity = false;
    project.target.signal = "drums";
    project.target.max = 0.8f;
    project.scope = MidiScope::Project;
    commitMidiLearn(*map, "aurora.scene.json", project, MidiLearnCommit::Add);
    setMidiMappingEnabled(*map, map->midi[1].id, false);
    map->setCombine("control.drums", MidiCombine::Max);

    const json saved = map->toJson();
    CHECK(saved["midi"]["bindings"] == projectControl()["midi"]["bindings"]); // the default was not written to
    REQUIRE(saved["midi"]["mappings"].size() == 2);
    CHECK(saved["midi"]["mappings"][0]["scope"] == "scene");
    CHECK(saved["midi"]["mappings"][0]["scene"] == "aurora.scene.json");
    CHECK(saved["midi"]["mappings"][1]["scope"] == "project");
    CHECK(saved["midi"]["disabledDefaults"] == json::array({map->midi[1].id}));
    CHECK(saved["midi"]["combine"]["control.drums"] == "max");

    auto back = ControlMap::fromJson(saved);
    REQUIRE(back.has_value());
    CHECK(back->warnings.empty());
    const MidiBinding* m = back->findMidi(id);
    REQUIRE(m != nullptr);
    CHECK(m->device == "100");
    CHECK(m->deviceName == "Keys");
    CHECK(m->channel == 2);
    CHECK(m->curve == MidiCurve::SCurve);
    CHECK(m->smoothMs == 80.0f);
    CHECK(m->invert);
    CHECK(m->scale == 0.5f);
    CHECK(m->offset == 0.1f);
    CHECK(m->encoder == MidiEncoder::BinaryOffset);
    CHECK(m->target.parameter == "orb/scale");
    CHECK(back->midiUser[1].numberHigh == 51);
    CHECK_FALSE(back->midiUser[1].velocity);
    CHECK(back->midiUser[1].target.max == 0.8f); // a channel's own range survives a save (it used not to)
    CHECK(back->combineFor("control.drums") == MidiCombine::Max);
    // Deterministic: the same state writes the same bytes, through any number of round trips.
    CHECK(back->toJson().dump() == saved.dump());
    CHECK(ControlMap::fromJson(back->toJson())->toJson().dump() == saved.dump());
}

TEST_CASE("an unreadable user mapping is kept, reported and written back; unknown keys are reported",
          "[midi][persistence]") {
    json control = projectControl();
    control["midi"]["mappings"] = json::parse(R"([
      {"id": "m-good", "scope": "project", "kind": "cc", "number": 3, "signal": "a", "futureKey": 1},
      {"id": "m-bad", "scope": "project", "kind": "cc", "number": 300, "signal": "b"},
      {"id": "m-nokind", "scope": "project", "kind": "sysex", "signal": "c"},
      {"id": "m-noscene", "scope": "scene", "kind": "cc", "number": 4, "signal": "d"}
    ])");
    auto map = ControlMap::fromJson(control);
    REQUIRE(map.has_value()); // a bad user record never fails the project
    REQUIRE(map->midiUser.size() == 4);
    CHECK_FALSE(map->midiUser[0].invalid());
    CHECK(map->midiUser[1].invalid());
    CHECK(map->midiUser[1].id == "m-bad");
    CHECK(map->midiUser[2].invalid());
    CHECK(map->midiUser[3].invalid()); // a scene mapping must say which scene
    CHECK(map->warnings.size() == 4); // three set aside, one unknown key
    // Never applied.
    for (const auto& r : resolveMidiMappings(*map, "s")) {
        if (r.binding->invalid()) CHECK(r.state == MidiMappingState::Invalid);
    }
    // Written back verbatim, so nothing the person had is lost to this build.
    const json saved = map->toJson();
    CHECK(saved["midi"]["mappings"][1] == control["midi"]["mappings"][1]);
    CHECK(saved["midi"]["mappings"][2] == control["midi"]["mappings"][2]);
    // An invalid authored default still fails the load, as it always did.
    json bad = projectControl();
    bad["midi"]["bindings"][0]["kind"] = "sysex";
    CHECK_FALSE(ControlMap::fromJson(bad).has_value());
}

TEST_CASE("missing devices and destinations keep their mappings; another computer's devices", "[midi][persistence]") {
    auto map = ControlMap::fromJson(projectControl());
    MidiBinding learned = midiBindingFromMessage(cc(74, 10, 0, 0), devices(), false, false, false);
    learned.target.parameter = "no/such/param";
    learned.scope = MidiScope::Project;
    commitMidiLearn(*map, "s", learned, MidiLearnCommit::Add);
    // Saved, then loaded where the device has another id and the parameter does not exist: nothing is deleted.
    auto back = ControlMap::fromJson(map->toJson());
    REQUIRE(back.has_value());
    REQUIRE(back->midiUser.size() == 1);
    CHECK(back->midiUser[0].device == "100");
    CHECK(back->midiUser[0].deviceName == "Keys");
    CHECK(back->midiUser[0].target.parameter == "no/such/param");
    CHECK(resolveMidiMappings(*back, "s").back().state == MidiMappingState::Active); // the mapper marks it unresolved
}

TEST_CASE("reset and restore return to the shipped configuration", "[midi][persistence]") {
    auto map = ControlMap::fromJson(projectControl());
    const std::string cloud = map->midi[0].id;
    const std::string pad = map->midi[1].id;
    // Edit a default: a scene mapping overrides it, the default is untouched.
    MidiBinding* edit = editableMidiMapping(*map, "s", cloud);
    REQUIRE(edit != nullptr);
    CHECK(edit->scope == MidiScope::Scene);
    CHECK(edit->overrides == cloud);
    edit->number = 25;
    CHECK(map->midi[0].number == 21);
    CHECK(editableMidiMapping(*map, "s", cloud) == &map->midiUser[0]); // editing again reuses the override
    setMidiMappingEnabled(*map, pad, false);
    MidiBinding project;
    project.kind = MidiBindKind::ControlChange;
    project.number = 1;
    project.target.signal = "x";
    project.scope = MidiScope::Project;
    commitMidiLearn(*map, "s", project, MidiLearnCommit::Add);

    restoreMidiDefault(*map, cloud);
    CHECK(map->midiUser.size() == 1); // the override went, the project mapping stayed
    resetSceneMidiMappings(*map, "s");
    CHECK(map->disabledDefaults.empty());
    CHECK(map->midiUser.size() == 1);
    CHECK(map->midiUser[0].scope == MidiScope::Project);
    CHECK(map->toJson()["midi"]["bindings"] == projectControl()["midi"]["bindings"]);
}

// ---- 8.4 Learn and the panel's logic -------------------------------------------------------------------------------

TEST_CASE("Learn captures the first qualifying message and then only follows it", "[midi][learn]") {
    MidiLearn learn;
    CHECK(learn.state() == MidiLearn::State::Idle);
    CHECK_FALSE(learn.feed(cc(74, 10))); // not armed
    learn.arm();
    MidiMessage clock;
    clock.kind = MidiKind::Clock;
    CHECK_FALSE(learn.feed(clock));
    MidiMessage off;
    off.kind = MidiKind::NoteOff;
    CHECK_FALSE(learn.feed(off));
    CHECK_FALSE(learn.feed(cc(0, 1)));   // bank select
    CHECK_FALSE(learn.feed(cc(123, 0))); // all notes off
    CHECK(learn.feed(cc(74, 10)));
    CHECK(learn.state() == MidiLearn::State::Captured);
    // The rest of the knob's stream follows the captured source; another control does not replace it.
    CHECK_FALSE(learn.feed(cc(74, 90)));
    CHECK_FALSE(learn.feed(cc(75, 127)));
    CHECK(learn.captured().data1 == 74);
    CHECK_THAT(learn.liveValue(), Catch::Matchers::WithinAbs(90.0 / 127.0, 1e-6));
    CHECK(learn.updates() == 1);
    learn.cancel();
    CHECK(learn.state() == MidiLearn::State::Idle);
}

TEST_CASE("Learn respects its device and channel filter", "[midi][learn]") {
    MidiLearn learn;
    learn.setFilter(1, 9);
    learn.arm();
    CHECK_FALSE(learn.feed(cc(74, 10, 9, 0))); // other device
    CHECK_FALSE(learn.feed(cc(74, 10, 0, 1))); // other channel
    CHECK(learn.feed(cc(74, 10, 9, 1)));
}

TEST_CASE("Learn takes the decoded NRPN, never the CCs that carry it", "[midi][learn][adr1702]") {
    MidiLearn learn;
    learn.arm();
    MidiMessage select = cc(99, 9);
    select.flags = kMidiNrpnPart;
    CHECK_FALSE(learn.feed(select));
    MidiMessage data = cc(6, 64);
    data.flags = kMidiNrpnPart;
    CHECK_FALSE(learn.feed(data));
    MidiMessage nrpn;
    nrpn.kind = MidiKind::Nrpn;
    nrpn.param = 1234;
    nrpn.value14 = 64 << 7;
    CHECK(learn.feed(nrpn));
    const MidiBinding b = midiBindingFromMessage(learn.captured(), devices(), false, false, false);
    CHECK(b.kind == MidiBindKind::Nrpn);
    CHECK(b.number == 1234);
    CHECK(midiSourceLabel(b) == "NRPN 1234");
}

TEST_CASE("a learned note is a gate or a trigger; the device and channel are kept unless widened", "[midi][learn]") {
    MidiMessage note;
    note.kind = MidiKind::NoteOn;
    note.data1 = 36;
    note.data2 = 100;
    note.channel = 9;
    note.device = 1;
    const MidiBinding gate = midiBindingFromMessage(note, devices(), false, false, false);
    CHECK(gate.kind == MidiBindKind::Note);
    CHECK_FALSE(gate.toggle);
    CHECK(gate.number == 36);
    CHECK(gate.channel == 9);
    CHECK(gate.device == "200");
    CHECK(gate.deviceName == "Pads");
    const MidiBinding trigger = midiBindingFromMessage(note, devices(), true, true, true);
    CHECK(trigger.kind == MidiBindKind::NoteEvent);
    CHECK(trigger.device.empty());
    CHECK(trigger.channel == -1);
}

TEST_CASE("Learn collisions: detected, and never resolved by destroying a mapping", "[midi][learn]") {
    auto map = ControlMap::fromJson(projectControl());
    MidiBinding candidate = midiBindingFromMessage(cc(21, 64, 0, 0), devices(), false, false, false);
    const auto hits = midiCollisions(*map, "s", candidate);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0]->target.signal == "cloud");

    candidate.target.signal = "glow";
    candidate.scope = MidiScope::Scene;
    SECTION("add a destination: both stay") {
        commitMidiLearn(*map, "s", candidate, MidiLearnCommit::Add);
        int active = 0;
        for (const auto& r : resolveMidiMappings(*map, "s")) active += r.state == MidiMappingState::Active ? 1 : 0;
        CHECK(active == 3);
    }
    SECTION("replace: the default is switched off, not deleted") {
        commitMidiLearn(*map, "s", candidate, MidiLearnCommit::Replace);
        CHECK(map->midi.size() == 2);
        CHECK(map->disabledDefaults == std::vector<std::string>{map->midi[0].id});
        // Replacing a user mapping switches it off too.
        MidiBinding again = candidate;
        again.target.signal = "haze";
        commitMidiLearn(*map, "s", again, MidiLearnCommit::Replace);
        CHECK(map->midiUser.size() == 2);
        CHECK_FALSE(map->midiUser[0].enabled);
        CHECK(map->midiUser[1].enabled);
    }
}

TEST_CASE("duplicate, remove, enable and promote by stable id", "[midi][learn]") {
    auto map = ControlMap::fromJson(projectControl());
    const std::string pad = map->midi[1].id;
    CHECK_FALSE(removeMidiMapping(*map, pad)); // a default cannot be removed, only switched off
    const std::string copy = duplicateMidiMapping(*map, "s", pad);
    REQUIRE_FALSE(copy.empty());
    CHECK(copy != pad);
    CHECK(map->findMidi(copy)->scope == MidiScope::Scene);
    setMidiMappingEnabled(*map, copy, false);
    CHECK_FALSE(midiMappingEnabled(*map, copy));
    setMidiMappingEnabled(*map, copy, true);
    CHECK(midiMappingEnabled(*map, copy));
    map->findMidi(copy)->number = 40;
    CHECK(promoteMidiMappingToDefault(*map, copy));
    CHECK(map->midi.size() == 3);
    CHECK(map->midi.back().number == 40);
    CHECK(map->midi.back().idAuthored); // a promoted default keeps its id in the file
    CHECK(map->midiUser.empty());
    const std::string removable = duplicateMidiMapping(*map, "s", pad);
    CHECK(removeMidiMapping(*map, removable));
    CHECK(map->findMidi(removable) == nullptr);
}

TEST_CASE("the monitor is bounded, coalesces a knob's stream, and describes messages", "[midi][monitor]") {
    MidiMonitor monitor;
    for (int i = 0; i < 2000; ++i) {
        monitor.push(cc(static_cast<std::uint8_t>(i % 100), 1), i * 0.001, 0, false); // distinct controls
    }
    CHECK(monitor.size() == MidiMonitor::kCapacity);
    CHECK(monitor.row(monitor.size() - 1).message.data1 == 1999 % 100);
    monitor.clear();
    CHECK(monitor.size() == 0);
    for (int i = 0; i < 300; ++i) {
        monitor.push(cc(74, static_cast<std::uint8_t>(i % 128)), 1.0 + i * 0.001, 1, false);
    }
    REQUIRE(monitor.size() == 1); // one knob turning is one row
    CHECK(monitor.row(0).repeats == 300);
    MidiMessage on;
    on.kind = MidiKind::NoteOn;
    on.data1 = 60;
    on.data2 = 108;
    monitor.push(on, 2.0, 0, false);
    monitor.push(cc(74, 3), 2.0, 0, false);
    CHECK(monitor.size() == 3); // a note between breaks the run
    MidiMessage clock;
    clock.kind = MidiKind::Clock;
    monitor.push(clock, 2.0, 0, false);
    CHECK(monitor.size() == 3); // not shown by default
    monitor.setCoalesce(false);
    monitor.push(cc(74, 4), 2.1, 0, false);
    CHECK(monitor.size() == 4);

    CHECK(midiMessageDescription(cc(74, 92)) == "CC 74  92/127");
    CHECK(midiMessageDescription(on) == "Note On C4  vel 108");
    MidiMessage bend;
    bend.kind = MidiKind::PitchBend;
    bend.value14 = 8192;
    CHECK(midiMessageDescription(bend) == "Pitch bend  centre");
    bend.value14 = 8192 + 1474; // +18%
    CHECK(midiMessageDescription(bend) == "Pitch bend  centre +18%");
    MidiMessage nrpn;
    nrpn.kind = MidiKind::Nrpn;
    nrpn.param = 1234;
    nrpn.value14 = 8192;
    nrpn.flags = kMidiHiRes;
    CHECK(midiMessageDescription(nrpn) == "NRPN 1234  8192/16383");
}
