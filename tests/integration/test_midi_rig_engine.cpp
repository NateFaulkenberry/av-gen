// ADR-1705: the MIDI rig through the engine -- it survives a live scene switch (a project load) and an app restart
// (its own file), applies in the frame a message arrives, releases its gates on a switch, keeps ADR-1606 pickup, fires
// app actions, never reaches a project file, and its common parameters exist in every live scene.

#include "app/engine.hpp"
#include "app/examples.hpp"
#include "app/live_scenes.hpp"
#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_learn.hpp"
#include "control/midi_rig.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

control::ControlMap quietMap() {
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    return map;
}

control::MidiBinding rigCc(int number, const std::string& id) {
    control::MidiBinding b;
    b.kind = control::MidiBindKind::ControlChange;
    b.number = number;
    b.id = id;
    b.scope = control::MidiScope::Rig;
    return b;
}

std::filesystem::path temp(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("avgen_midi_rig_engine_" + std::to_string(::getpid()) + "_" + name);
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Two projects standing in for two live scenes: the first with a scene mapping, the second with none.
void writeProjects(const std::filesystem::path& a, const std::filesystem::path& b) {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map = quietMap();
    control::MidiBinding gate;
    gate.kind = control::MidiBindKind::Note;
    gate.number = 50;
    gate.target.signal = "scene-a";
    gate.id = "m-scenea";
    gate.scope = control::MidiScope::Project;
    map.midiUser.push_back(gate);
    engine.control().setMap(map);
    REQUIRE(engine.saveProject(a).has_value());
    engine.control().setMap(quietMap());
    REQUIRE(engine.saveProject(b).has_value());
}

} // namespace

TEST_CASE("the rig survives a live scene switch and applies in the frame a message arrives",
          "[integration][midi][rig][adr1705]") {
    const auto a = temp("a.json");
    const auto b = temp("b.json");
    writeProjects(a, b);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(a).has_value());
    std::vector<control::MidiBinding> rig;
    control::MidiBinding bloom = rigCc(10, "m-rigbloom");
    bloom.target.parameter = "post/bloom/intensity";
    bloom.target.min = 0.0f;
    bloom.target.max = 2.0f;
    rig.push_back(bloom);
    control::MidiBinding gate = rigCc(36, "m-riggate");
    gate.kind = control::MidiBindKind::Note;
    gate.target.signal = "riggate";
    rig.push_back(gate);
    engine.control().setRig(rig);
    const std::uint64_t revision = engine.control().rigRevision();

    FixedStepClock clock(60.0);
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 10, 127});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 36, 127});
    engine.update(engine.tick(clock));
    auto* intensity = engine.params().find("post/bloom/intensity");
    REQUIRE(intensity != nullptr);
    CHECK_THAT(intensity->baseComponent(0), WithinAbs(2.0, 1e-5));
    CHECK(engine.controlSource().value("riggate") == 1.0f);

    // The live switcher's move: another project. The rig is still there; the held rig gate is released (its note-off
    // will come to a scene that may not map it); and a knob moved in the first frame of the new scene applies then.
    REQUIRE(engine.loadProject(b).has_value());
    CHECK(engine.control().rig().size() == 2);
    CHECK(engine.control().rigRevision() == revision); // a project load is not a rig change: nothing to save
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 10, 0});
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("riggate") == 0.0f);
    intensity = engine.params().find("post/bloom/intensity");
    REQUIRE(intensity != nullptr);
    CHECK_THAT(intensity->baseComponent(0), WithinAbs(0.0, 1e-5));
    // The first project's own mapping did not come along.
    CHECK(engine.control().map().midiUser.empty());

    // And back: the first project's mapping and the rig together.
    REQUIRE(engine.loadProject(a).has_value());
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 50, 127});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 10, 64});
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("scene-a") == 1.0f);
    CHECK_THAT(engine.params().find("post/bloom/intensity")->baseComponent(0), WithinAbs(2.0 * 64.0 / 127.0, 1e-4));
    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST_CASE("a rig pickup knob waits for the new scene's value after a switch", "[integration][midi][rig][adr1606]") {
    const auto a = temp("pa.json");
    const auto b = temp("pb.json");
    writeProjects(a, b);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(a).has_value());
    control::MidiBinding knob = rigCc(20, "m-rigpick");
    knob.target.parameter = "post/bloom/intensity";
    knob.target.min = 0.0f;
    knob.target.max = 1.0f;
    knob.pickup = true;
    engine.control().setRig({knob});
    FixedStepClock clock(60.0);
    auto* p = engine.params().find("post/bloom/intensity");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, 0.5f);
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 20, 64}); // reaches 0.5: latched
    engine.update(engine.tick(clock));
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 20, 127});
    engine.update(engine.tick(clock));
    CHECK_THAT(p->baseComponent(0), WithinAbs(1.0, 1e-5));

    REQUIRE(engine.loadProject(b).has_value());
    p = engine.params().find("post/bloom/intensity");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, 0.2f);
    engine.update(engine.tick(clock));
    // The knob sits at the top; the new scene's value is 0.2. It must not jump.
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 20, 120});
    engine.update(engine.tick(clock));
    CHECK_THAT(p->baseComponent(0), WithinAbs(0.2, 1e-5));
    CHECK(engine.control().mapper().status("m-rigpick")->waitingForPickup);
    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST_CASE("the rig survives an app restart through its own file", "[integration][midi][rig][persistence][adr1705]") {
    const auto file = temp("restart-rig.json");
    std::filesystem::remove(file);
    {
        // Session one: Learn into the rig, as the panel does, and the app's save on a revision change.
        app::Engine engine(app::EngineMode::Offline);
        auto load = control::MidiRig::load(file);
        REQUIRE(load.has_value());
        engine.control().setRig(load->mappings);
        const std::uint64_t saved = engine.control().rigRevision();
        control::MidiBinding candidate = rigCc(74, "");
        candidate.target.parameter = "post/bloom/intensity";
        candidate.target.max = 3.0f;
        control::commitMidiLearn(engine.control().map(), engine.control().sceneKey(), candidate,
                                 control::MidiLearnCommit::Add);
        engine.control().mappingsChanged();
        FixedStepClock clock(60.0);
        engine.update(engine.tick(clock));
        REQUIRE(engine.control().rigRevision() != saved);
        control::MidiRig rig;
        rig.mappings = engine.control().rig();
        REQUIRE(rig.save(file).has_value());
    }
    // Session two: a fresh engine, the rig read back, the knob live in the first frame.
    app::Engine engine(app::EngineMode::Offline);
    auto load = control::MidiRig::load(file);
    REQUIRE(load.has_value());
    REQUIRE(load->mappings.size() == 1);
    engine.control().setRig(load->mappings);
    FixedStepClock clock(60.0);
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 74, 127});
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("post/bloom/intensity")->baseComponent(0), WithinAbs(3.0, 1e-5));
    std::filesystem::remove(file);
}

TEST_CASE("a project saves byte-for-byte the same with a rig as without one", "[integration][midi][rig][persistence][adr1705]") {
    const auto without = temp("without.json");
    const auto with = temp("with.json");
    // A project with authored defaults and a user mapping, so the midi block is not empty.
    const auto source = temp("source.json");
    {
        app::Engine engine(app::EngineMode::Offline);
        control::ControlMap map = quietMap();
        control::MidiBinding def;
        def.kind = control::MidiBindKind::ControlChange;
        def.number = 21;
        def.target.signal = "cloud";
        map.midi.push_back(def);
        control::MidiBinding user = rigCc(5, "m-user");
        user.scope = control::MidiScope::Project;
        user.target.signal = "u";
        map.midiUser.push_back(user);
        engine.control().setMap(map);
        REQUIRE(engine.saveProject(source).has_value());
    }
    // The same session twice -- load, two frames, a CC 10 move, save -- once with no rig and once with one.
    const auto session = [&](const std::filesystem::path& out, bool useRig) {
        app::Engine engine(app::EngineMode::Offline);
        if (useRig) {
            control::MidiBinding r = rigCc(10, "m-rig");
            r.target.parameter = "post/bloom/intensity";
            control::MidiBinding next = rigCc(40, "m-next");
            next.kind = control::MidiBindKind::Note;
            next.target.action = control::MidiAction::LiveNextScene;
            engine.control().setRig({r, next});
        }
        REQUIRE(engine.loadProject(source).has_value());
        FixedStepClock clock(60.0);
        engine.update(engine.tick(clock));
        const float base = engine.params().find("post/bloom/intensity")->baseComponent(0);
        engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 10, 100});
        engine.update(engine.tick(clock));
        // The rig knob moved a base value -- that is the project's, as any slider move is. Put it back to compare.
        engine.params().find("post/bloom/intensity")->setBaseComponent(0, base);
        REQUIRE(engine.saveProject(out).has_value());
    };
    session(without, false);
    session(with, true);
    const std::string a = bytes(without);
    const std::string b = bytes(with);
    INFO("without rig: " << a.size() << " bytes, with: " << b.size());
    CHECK(a == b);
    CHECK(b.find("m-rig") == std::string::npos);
    CHECK(b.find("live.next-scene") == std::string::npos);
    CHECK(b.find("\"rig\"") == std::string::npos);
    std::filesystem::remove(source);
    std::filesystem::remove(without);
    std::filesystem::remove(with);
}

TEST_CASE("mapped app actions reach the transport and the live scene switcher", "[integration][midi][rig][action][adr1705]") {
    app::Engine engine(app::EngineMode::Offline);
    control::MidiBinding next = rigCc(40, "m-next");
    next.kind = control::MidiBindKind::Note;
    next.target.action = control::MidiAction::LiveNextScene;
    control::MidiBinding prev = rigCc(41, "m-prev");
    prev.kind = control::MidiBindKind::Note;
    prev.target.action = control::MidiAction::LivePreviousScene;
    control::MidiBinding play = rigCc(64, "m-play");
    play.target.action = control::MidiAction::TransportPlay;
    engine.control().setRig({next, prev, play});
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 40, 100});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x80, 40, 0});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 40, 100});
    engine.update(engine.tick(clock));
    CHECK(engine.control().takeLiveSceneSteps() == 2);
    CHECK(engine.control().takeLiveSceneSteps() == 0); // taken once
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 41, 100});
    engine.update(engine.tick(clock));
    CHECK(engine.control().takeLiveSceneSteps() == -1);
    CHECK(app::steppedLiveScene(0, -1, 30) == 29); // the same stepping Page Up uses

    const bool before = engine.isPlaying();
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 64, 127});
    engine.update(engine.tick(clock));
    CHECK(engine.isPlaying() != before);
    CHECK(engine.control().actionsFired() == 4);
}

TEST_CASE("every live scene has the rig's common parameters", "[integration][midi][rig][liveset][adr1705]") {
    auto examples = app::loadExamples({std::filesystem::path(AVGEN_SOURCE_DIR) / "examples"});
    REQUIRE(examples.has_value());
    const auto live = app::liveSceneList(*examples);
    REQUIRE(live.size() >= 20);
    std::set<std::string> shared;
    std::set<std::string> calledCommon; // every path any scene has that the rig calls common
    bool first = true;
    int loaded = 0;
    for (const app::ExampleInfo& scene : live) {
        app::Engine engine(app::EngineMode::Offline);
        auto r = engine.loadProject(scene.file);
        INFO(scene.file.string());
        if (!r) {
            // A scene whose private assets are absent on this machine is not a verdict on the parameter set.
            WARN("not loaded: " << scene.file.filename().string() << ": " << r.error().message);
            continue;
        }
        ++loaded;
        std::set<std::string> paths;
        for (params::IParameter* p : engine.params().ordered()) {
            if (p->flags().exposed && p->flags().modulatable) {
                paths.insert(p->path());
                if (control::midiRigCommonParameter(p->path())) {
                    calledCommon.insert(p->path());
                }
            }
        }
        if (first) {
            shared = paths;
            first = false;
        } else {
            std::set<std::string> kept;
            for (const std::string& s : shared) {
                if (paths.contains(s)) kept.insert(s);
            }
            shared = std::move(kept);
        }
        for (const char* path : {"post/bloom/intensity", "post/lens/chromaticAberration", "audio/inputGain",
                                 "temporal/feedback/amount", "sonic/response/sensitivity"}) {
            CHECK(engine.params().find(path) != nullptr);
        }
    }
    REQUIRE(loaded >= 20);
    // And the shared set, as measured, is what midiRigCommonParameter says: every modulatable parameter all of them have
    // is common, and every common one they all have.
    std::ostringstream list;
    int notCommon = 0;
    for (const std::string& path : shared) {
        if (!control::midiRigCommonParameter(path)) {
            list << path << ' ';
            ++notCommon;
        }
    }
    std::ostringstream missing;
    int notShared = 0;
    for (const std::string& path : calledCommon) {
        if (!shared.contains(path)) {
            missing << path << ' ';
            ++notShared;
        }
    }
    INFO("shared but not called common: " << list.str());
    INFO("called common but missing from some live scene: " << missing.str());
    WARN(loaded << " live scenes loaded; " << shared.size() << " modulatable parameters shared by all");
    CHECK(notCommon == 0);
    CHECK(notShared == 0);
    CHECK(loaded == static_cast<int>(live.size()));
}
