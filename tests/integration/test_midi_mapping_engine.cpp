// MIDI performance mapping through the engine (ADR-1700..1704): the same-frame path, scene
// activation, save and load, devices switched off, a CC flood, and a real CoreMIDI virtual source
// learned and played end to end.

#include "app/engine.hpp"
#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_learn.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

control::ControlMap quietMap() {
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false; // no devices; inject instead
    return map;
}

control::MidiBinding userCc(int number, const std::string& id, control::MidiScope scope, const std::string& scene = {}) {
    control::MidiBinding b;
    b.kind = control::MidiBindKind::ControlChange;
    b.number = number;
    b.id = id;
    b.scope = scope;
    b.scene = scene;
    return b;
}

double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

} // namespace

TEST_CASE("a mapped message reaches this frame's parameters, through a base write and through a route",
          "[integration][midi][mapping]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map = quietMap();
    control::MidiBinding scale = userCc(74, "m-scale", control::MidiScope::Project);
    scale.target.parameter = "orb/scale";
    scale.target.min = 0.5f;
    scale.target.max = 2.5f;
    map.midiUser.push_back(scale);
    control::MidiBinding glow = userCc(74, "m-glow", control::MidiScope::Project); // the same knob, a second destination
    glow.target.signal = "glow";
    map.midiUser.push_back(glow);
    engine.control().setMap(map);
    params::ModRoute r{.source = "control.glow", .target = "orb/emissive", .amount = 2.0f};
    engine.modulator().addRoute(r);
    engine.rebind();

    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    auto* emissive = engine.params().find("orb/emissive");
    auto* orbScale = engine.params().find("orb/scale");
    REQUIRE(emissive != nullptr);
    REQUIRE(orbScale != nullptr);
    const float emissiveBase = emissive->baseComponent(0);

    // Injected between frames, as a message arriving during the previous frame's render: ONE update later, both the
    // base and the route's final value carry it. No frame of delay.
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 74, 127});
    engine.update(engine.tick(clock));
    CHECK_THAT(orbScale->baseComponent(0), WithinAbs(2.5, 1e-5));
    CHECK_THAT(orbScale->finalComponent(0), WithinAbs(2.5, 1e-5));
    CHECK_THAT(emissive->finalComponent(0), WithinAbs(static_cast<double>(emissiveBase) + 2.0, 1e-4));
    const auto* status = engine.control().mapper().status("m-scale");
    REQUIRE(status != nullptr);
    CHECK(status->lastInput == 1.0f);
    CHECK(engine.control().status().applied == 1);
}

TEST_CASE("scene mappings follow the scene; a scene change releases a held gate", "[integration][midi][mapping][scope]") {
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    REQUIRE(engine.midiSceneKey() == "orb");
    control::ControlMap map = quietMap();
    control::MidiBinding gate;
    gate.kind = control::MidiBindKind::Note;
    gate.number = 36;
    gate.id = "m-gate";
    gate.scope = control::MidiScope::Scene;
    gate.scene = "orb";
    gate.target.signal = "gate";
    map.midiUser.push_back(gate);
    control::MidiBinding everywhere = userCc(1, "m-all", control::MidiScope::Project);
    everywhere.target.signal = "mod";
    map.midiUser.push_back(everywhere);
    engine.control().setMap(map);

    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 36, 127});
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("gate") == 1.0f);

    engine.newComposition(); // another scene: "inline"
    engine.update(engine.tick(clock));
    CHECK(engine.control().sceneKey() == "inline");
    CHECK(engine.controlSource().value("gate") == 0.0f); // released, not left held
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 36, 127});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 1, 127});
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("gate") == 0.0f); // the orb's scene mapping is not active here
    CHECK(engine.controlSource().value("mod") == 1.0f);  // the project mapping is
}

TEST_CASE("user mappings and tombstones survive a project save and load; the defaults are not written",
          "[integration][midi][mapping][persistence]") {
    const auto project = std::filesystem::temp_directory_path() / ("avgen_midi_mapping_" + std::to_string(::getpid()) + ".json");
    std::string defaultId;
    {
        app::Engine engine(app::EngineMode::Offline);
        control::ControlMap map = quietMap();
        control::MidiBinding def;
        def.kind = control::MidiBindKind::ControlChange;
        def.number = 21;
        def.target.signal = "cloud";
        map.midi.push_back(def);
        engine.control().setMap(map);
        auto& m = engine.control().map();
        auto parsed = control::ControlMap::fromJson(m.toJson()); // ids as a load assigns them
        REQUIRE(parsed.has_value());
        engine.control().setMap(*parsed);
        defaultId = engine.control().map().midi[0].id;
        control::MidiBinding* edit = control::editableMidiMapping(engine.control().map(), "orb", defaultId);
        REQUIRE(edit != nullptr);
        edit->target.max = 0.5f;
        control::MidiBinding proj = userCc(5, "", control::MidiScope::Project);
        proj.target.parameter = "orb/scale";
        control::commitMidiLearn(engine.control().map(), "orb", proj, control::MidiLearnCommit::Add);
        engine.control().mappingsChanged();
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const auto& map = engine.control().map();
    REQUIRE(map.midi.size() == 1);
    CHECK(map.midi[0].id == defaultId);
    CHECK(map.midi[0].target.max == 1.0f); // the shipped default as it was
    REQUIRE(map.midiUser.size() == 2);
    CHECK(map.midiUser[0].overrides == defaultId);
    CHECK(map.midiUser[0].scene == "orb");
    CHECK(map.midiUser[1].scope == control::MidiScope::Project);
    FixedStepClock clock(60.0);
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 21, 127});
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("cloud") == 0.5f); // the override applies, the default does not
    CHECK(engine.control().mapper().status(defaultId)->state == control::MidiMappingState::Overridden);
    std::filesystem::remove(project);
}

TEST_CASE("a device switched off is seen but not applied; an unplugged device's held notes are released",
          "[integration][midi][mapping][device]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map = quietMap();
    control::MidiBinding gate;
    gate.kind = control::MidiBindKind::Note;
    gate.number = -1;
    gate.id = "m-gate";
    gate.scope = control::MidiScope::Project;
    gate.target.signal = "gate";
    map.midiUser.push_back(gate);
    engine.control().setMap(map);
    FixedStepClock clock(60.0);

    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 40, 100}, "pads");
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("gate") > 0.0f);
    engine.control().injectMidiRemoval("pads"); // unplugged with the key down
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("gate") == 0.0f);

    engine.control().setDisabledDevices({"inject:keys"});
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0x90, 41, 100}, "keys");
    engine.update(engine.tick(clock));
    CHECK(engine.controlSource().value("gate") == 0.0f);
    CHECK(engine.control().status().ignored == 1);
    const auto& monitor = engine.control().monitor();
    REQUIRE(monitor.size() > 0);
    CHECK(monitor.row(monitor.size() - 1).ignored);
}

TEST_CASE("an NRPN knob drives a mapping as one control", "[integration][midi][mapping][adr1702]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map = quietMap();
    control::MidiBinding b;
    b.kind = control::MidiBindKind::Nrpn;
    b.number = 1234;
    b.id = "m-nrpn";
    b.scope = control::MidiScope::Project;
    b.target.parameter = "orb/scale";
    b.target.min = 0.0f;
    b.target.max = 2.0f;
    map.midiUser.push_back(b);
    engine.control().setMap(map);
    engine.control().learn().arm();
    FixedStepClock clock(60.0);
    engine.control().injectMidi(std::array<std::uint8_t, 8>{0xB0, 99, 9, 98, 82, 6, 127, 38}); // ... running status
    engine.control().injectMidi(std::array<std::uint8_t, 1>{127});
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(2.0, 1e-4));
    REQUIRE(engine.control().learn().state() == control::MidiLearn::State::Captured);
    CHECK(engine.control().learn().captured().kind == control::MidiKind::Nrpn); // not CC 99
    CHECK(engine.control().status().unmatched == 0); // the select CCs are neither applied nor unmatched
}

namespace {

// Four producer threads, one per device, each streaming eight knobs; `perSecond` 0 = as fast as the CPU allows.
struct Flood {
    app::Engine& engine;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> sent{0};
    std::vector<std::thread> threads;
    std::vector<double> worstInjectUs = std::vector<double>(4, 0.0);
    Flood(app::Engine& e, int perSecond) : engine(e) {
        for (int d = 0; d < 4; ++d) {
            threads.emplace_back([this, d, perSecond] {
                const std::string name = "dev" + std::to_string(d);
                const auto period = perSecond > 0 ? std::chrono::nanoseconds(1'000'000'000LL * 8 / perSecond)
                                                  : std::chrono::nanoseconds(0);
                auto next = std::chrono::steady_clock::now();
                std::uint8_t v = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    for (std::uint8_t n = 1; n <= 8; ++n) {
                        const auto t = std::chrono::steady_clock::now();
                        engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, n, v}, name);
                        auto& w = worstInjectUs[static_cast<std::size_t>(d)];
                        w = std::max(w, msSince(t) * 1000.0);
                        sent.fetch_add(1, std::memory_order_relaxed);
                    }
                    v = static_cast<std::uint8_t>((v + 1) & 0x7F);
                    if (perSecond > 0) {
                        next += period;
                        std::this_thread::sleep_until(next);
                    }
                }
            });
        }
    }
    double worstInject() const { return *std::max_element(worstInjectUs.begin(), worstInjectUs.end()); }
    void join() {
        stop = true;
        for (auto& t : threads) t.join();
    }
};

control::ControlMap floodMap() {
    control::ControlMap map = quietMap();
    for (int d = 0; d < 4; ++d) {
        for (int n = 0; n < 8; ++n) {
            control::MidiBinding b = userCc(n + 1, "m-" + std::to_string(d) + "-" + std::to_string(n), control::MidiScope::Project);
            b.source = "dev" + std::to_string(d);
            b.target.signal = "k" + std::to_string(d) + std::to_string(n);
            map.midiUser.push_back(b);
        }
    }
    control::MidiBinding gate;
    gate.kind = control::MidiBindKind::Note;
    gate.number = -1;
    gate.id = "m-gate";
    gate.scope = control::MidiScope::Project;
    gate.target.signal = "gate";
    map.midiUser.push_back(gate);
    return map;
}

// Runs `frames` engine updates at ~30 fps with a note on/off pair each frame; returns {mean ms, worst ms}.
std::pair<double, double> runFrames(app::Engine& engine, FixedStepClock& clock, int frames) {
    double worst = 0.0;
    double total = 0.0;
    for (int f = 0; f < frames; ++f) {
        engine.control().injectMidi(std::array<std::uint8_t, 6>{0x90, 60, 100, 0x80, 60, 0}, "keys");
        const auto t = std::chrono::steady_clock::now();
        engine.update(engine.tick(clock));
        const double ms = msSince(t);
        worst = std::max(worst, ms);
        total += ms;
        std::this_thread::sleep_until(t + std::chrono::milliseconds(33));
    }
    return {total / frames, worst};
}

} // namespace

TEST_CASE("a CC flood from several devices costs the frame little, stays bounded and keeps note order",
          "[integration][midi][mapping][perf]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.control().setMap(floodMap());
    FixedStepClock clock(30.0);
    engine.update(engine.tick(clock));
    runFrames(engine, clock, 10); // warm
    const auto [baseMean, baseWorst] = runFrames(engine, clock, 60);

    // Heavy but real: four devices at 3,000 messages a second each, twelve times what a 5-pin DIN port can carry.
    Flood flood(engine, 3000);
    const auto [mean, worst] = runFrames(engine, clock, 60);
    flood.join();
    const auto stats = engine.control().status().midi;
    WARN("CC flood, 4 devices x 3000 msg/s (" << flood.sent.load() << " messages in 2 s): engine update mean " << mean
                                             << " ms (no MIDI: " << baseMean << "), worst " << worst << " ms (no MIDI: "
                                             << baseWorst << "); worst producer inject " << flood.worstInject()
                                             << " us; coalesced " << stats.coalesced << ", dropped " << stats.dropped);
    CHECK(flood.sent.load() > 15000);
    CHECK(stats.dropped == 0);       // drained every frame: nothing near the queue's bound
    CHECK(stats.droppedNotes == 0);
    CHECK(mean - baseMean < 2.0);    // the MIDI work per frame (about 400 messages) is a small part of the 33 ms
    CHECK(engine.controlSource().value("gate") == 0.0f); // every on had its off, in order
}

TEST_CASE("an unthrottled flood is bounded, loses no note and keeps every knob's final position",
          "[integration][midi][mapping][perf]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.control().setMap(floodMap());
    FixedStepClock clock(30.0);
    engine.update(engine.tick(clock));
    Flood flood(engine, 0); // as fast as four threads can call inject: thousands of times any device
    const auto [mean, worst] = runFrames(engine, clock, 30);
    flood.join();
    engine.control().injectMidi(std::array<std::uint8_t, 3>{0xB0, 1, 77}, "dev2");
    engine.update(engine.tick(clock));
    const auto stats = engine.control().status().midi;
    WARN("unthrottled flood: " << flood.sent.load() << " messages in 1 s; update mean " << mean << " ms, worst " << worst
                               << " ms; coalesced " << stats.coalesced << ", dropped " << stats.dropped
                               << ", notes dropped " << stats.droppedNotes);
    CHECK(stats.coalesced > 0);       // the queue's continuous share filled, and the latest values were kept
    CHECK(stats.droppedNotes == 0);   // the notes had their quarter
    CHECK_THAT(engine.controlSource().value("k20"), WithinAbs(77.0 / 127.0, 1e-5));
    CHECK(engine.controlSource().value("gate") == 0.0f);
}

TEST_CASE("Learn and play through a real CoreMIDI virtual source", "[integration][midi][mapping][device]") {
    if (!control::hasMidiBackend()) {
        SKIP("no MIDI backend on this platform");
    }
    const std::string name = "avgen-test-learn-" + std::to_string(::getpid());
    control::MidiVirtualSource source;
    if (auto created = source.open(name); !created) {
        SKIP("cannot create a virtual MIDI source here: " << created.error().message);
    }
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = true;
    map.midiFilter = name;
    engine.control().setMap(map);
    REQUIRE(engine.control().status().midiOpen);
    FixedStepClock clock(60.0);
    const auto pump = [&](auto done) {
        for (int i = 0; i < 400 && !done(); ++i) {
            engine.update(engine.tick(clock));
            if (!done()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    };

    auto& learn = engine.control().learn();
    learn.arm();
    REQUIRE(source.send(std::vector<std::uint8_t>{0xB4, 74, 20, 0xB4, 74, 21, 0xB4, 74, 22}).has_value()); // a knob turned
    pump([&] { return learn.state() == control::MidiLearn::State::Captured && learn.updates() >= 2; });
    REQUIRE(learn.state() == control::MidiLearn::State::Captured);
    const auto& devices = engine.control().midiDevices();
    control::MidiBinding learned = control::midiBindingFromMessage(learn.captured(), devices, false, false, false);
    CHECK(learned.kind == control::MidiBindKind::ControlChange);
    CHECK(learned.number == 74);
    CHECK(learned.channel == 4);
    CHECK(learned.deviceName == name);
    std::string systemId;
    for (const auto& d : control::listMidiInputs()) {
        if (d.name == name) systemId = d.id;
    }
    CHECK(learned.device == systemId); // the system's stable id
    learned.target.parameter = "orb/scale";
    learned.target.min = 0.0f;
    learned.target.max = 4.0f;
    learned.scope = control::MidiScope::Project;
    control::commitMidiLearn(engine.control().map(), engine.control().sceneKey(), learned, control::MidiLearnCommit::Add);
    engine.control().mappingsChanged();
    learn.cancel();

    REQUIRE(source.send(std::vector<std::uint8_t>{0xB4, 74, 127}).has_value());
    pump([&] { return engine.params().find("orb/scale")->baseComponent(0) > 3.9f; });
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(4.0, 1e-4));
    // The same CC on another channel does not reach it.
    REQUIRE(source.send(std::vector<std::uint8_t>{0xB0, 74, 0}).has_value());
    for (int i = 0; i < 20; ++i) {
        engine.update(engine.tick(clock));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(4.0, 1e-4));
    // The devices table says it is receiving.
    const auto& activity = engine.control().deviceActivity();
    bool heard = false;
    for (std::size_t i = 0; i < devices.size() && i < activity.size(); ++i) {
        if (devices[i].name == name) {
            heard = activity[i].messages >= 5 && activity[i].channelSeconds[4] >= 0.0 && activity[i].channelSeconds[0] >= 0.0;
        }
    }
    CHECK(heard);
}
