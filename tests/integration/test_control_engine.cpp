// Milestone 1.1 (ADR-021): MIDI and OSC through the engine — bindings to control signals that
// routes consume, direct parameter writes, presets and transport over OSC, project round trip,
// and a real UDP loopback into the hub.

#include "app/engine.hpp"
#include "control/control_map.hpp"
#include "control/osc.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace avgen;
using Catch::Matchers::WithinAbs;

TEST_CASE("MIDI bindings drive control signals and parameters through the engine", "[integration][control]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false; // no devices; inject instead
    control::MidiBinding fader;
    fader.kind = control::MidiBindKind::ControlChange;
    fader.number = 1;
    fader.target.signal = "mod";
    map.midi.push_back(fader);
    control::MidiBinding knob;
    knob.kind = control::MidiBindKind::ControlChange;
    knob.number = 7;
    knob.target.parameter = "orb/scale";
    knob.target.min = 0.5f;
    knob.target.max = 2.5f;
    map.midi.push_back(knob);
    control::MidiBinding kick;
    kick.kind = control::MidiBindKind::NoteEvent;
    kick.number = 36;
    kick.target.signal = "kick";
    map.midi.push_back(kick);
    engine.control().setMap(map);
    // The control source declares the bound channels before any message arrives.
    CHECK(engine.controlSource().hasChannel("mod"));
    CHECK(engine.controlSource().isEventChannel("kick"));

    params::ModRoute r{.source = "control.mod", .target = "orb/emissive", .amount = 2.0f};
    engine.modulator().addRoute(r);
    params::ModRoute k{.source = "control.kick", .target = "orb/impulse", .amount = 1.0f};
    k.chain.envelope = params::EnvelopeMode::PeakHold;
    engine.modulator().addRoute(k);
    engine.rebind();
    CHECK(engine.modulator().bound());

    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    auto* emissive = engine.params().find("orb/emissive");
    auto* scale = engine.params().find("orb/scale");
    REQUIRE(emissive != nullptr);
    REQUIRE(scale != nullptr);
    const float emissiveBase = emissive->baseComponent(0);

    const std::array<std::uint8_t, 3> cc1{0xB0, 1, 127};
    engine.control().injectMidi(cc1);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.signals().value(*engine.signals().find("control.mod")), WithinAbs(1.0, 1e-6));
    CHECK_THAT(emissive->finalComponent(0), WithinAbs(static_cast<double>(emissiveBase) + 2.0, 1e-4));

    const std::array<std::uint8_t, 3> cc7{0xB0, 7, 127};
    engine.control().injectMidi(cc7);
    engine.update(engine.tick(clock));
    CHECK_THAT(scale->baseComponent(0), WithinAbs(2.5, 1e-5));

    const std::array<std::uint8_t, 3> noteOn{0x90, 36, 100};
    engine.control().injectMidi(noteOn);
    engine.update(engine.tick(clock));
    auto* impulse = engine.params().find("orb/impulse");
    REQUIRE(impulse != nullptr);
    CHECK(impulse->finalComponent(0) > 0.5f); // the event pulsed through the route
    engine.update(engine.tick(clock));
    CHECK(engine.signals().value(*engine.signals().find("control.kick")) == 0.0f); // one frame only

    const auto status = engine.control().status();
    CHECK(status.applied == 3);
    CHECK(status.unmatched == 0);
    REQUIRE(engine.control().lastMidi().has_value());
    CHECK(engine.control().lastMidi()->data1 == 36);
}

TEST_CASE("Direct OSC commands set parameters, signals, presets and transport", "[integration][control]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    engine.control().setMap(map);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));

    control::OscMessage set;
    set.address = "/avgen/param/orb/scale";
    set.args = {1.75f};
    engine.control().injectOsc(set);
    control::OscMessage colour;
    colour.address = "/avgen/param/orb/baseColor";
    colour.args = {0.1f, 0.2f, 0.3f};
    engine.control().injectOsc(colour);
    control::OscMessage sig;
    sig.address = "/avgen/signal/energy";
    sig.args = {0.6f};
    engine.control().injectOsc(sig);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(1.75, 1e-5));
    CHECK_THAT(engine.params().find("orb/baseColor")->baseComponent(2), WithinAbs(0.3, 1e-5));
    REQUIRE(engine.signals().find("control.energy").has_value());
    CHECK_THAT(engine.signals().value(*engine.signals().find("control.energy")), WithinAbs(0.6, 1e-6));

    engine.params().find("orb/scale")->setBaseComponent(0, 3.0f);
    engine.storePreset("big");
    engine.params().find("orb/scale")->setBaseComponent(0, 1.0f);
    control::OscMessage recall;
    recall.address = "/avgen/preset/recall";
    recall.args = {std::string("big")};
    engine.control().injectOsc(recall);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(3.0, 1e-5));

    control::OscMessage unknown;
    unknown.address = "/avgen/param/nope/missing";
    unknown.args = {1.0f};
    engine.control().injectOsc(unknown);
    control::OscMessage outside;
    outside.address = "/somewhere/else";
    outside.args = {1.0f};
    engine.control().injectOsc(outside);
    engine.update(engine.tick(clock));
    CHECK(engine.control().status().unmatched == 2);
    CHECK(engine.control().status().applied == 4);
}

TEST_CASE("OSC over real UDP loopback reaches the engine", "[integration][control][network]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscPort = 0; // any free port
    map.oscBind = "127.0.0.1";
    map.midiEnabled = false;
    control::OscBinding fader;
    fader.address = "/fader/*";
    fader.inMax = 127.0f;
    fader.target.signal = "fader";
    map.osc.push_back(fader);
    engine.control().setMap(map);
    const auto status = engine.control().status();
    if (!status.oscOpen) {
        SKIP("cannot open a UDP socket here: " << status.oscError);
    }
    control::OscSender sender;
    REQUIRE(sender.open("127.0.0.1", status.oscPort).has_value());
    control::OscMessage m;
    m.address = "/fader/3";
    m.args = {std::int32_t{127}};
    REQUIRE(sender.send(m).has_value());
    control::OscMessage direct;
    direct.address = "/avgen/param/orb/scale";
    direct.args = {2.0f};
    REQUIRE(sender.send(direct).has_value());

    FixedStepClock clock(60.0);
    bool arrived = false;
    for (int i = 0; i < 200 && !arrived; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        engine.update(engine.tick(clock));
        arrived = engine.control().status().applied >= 2;
    }
    REQUIRE(arrived);
    CHECK_THAT(engine.controlSource().value("fader"), WithinAbs(1.0, 1e-6));
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(2.0, 1e-5));
}

TEST_CASE("Projects round-trip the control map and the control source survives scene swaps",
          "[integration][control][json]") {
    const auto project = std::filesystem::temp_directory_path() / "avgen_control_project.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        control::ControlMap map;
        map.oscEnabled = false;
        map.midiEnabled = false;
        map.oscPort = 9123;
        control::MidiBinding b;
        b.kind = control::MidiBindKind::ControlChange;
        b.number = 20;
        b.target.signal = "twenty";
        map.midi.push_back(b);
        engine.control().setMap(map);
        params::ModRoute r{.source = "control.twenty", .target = "orb/scale", .amount = 0.5f};
        engine.modulator().addRoute(r);
        engine.rebind();
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    CHECK(engine.control().map().oscPort == 9123);
    REQUIRE(engine.control().map().midi.size() == 1);
    CHECK(engine.control().map().midi[0].target.signal == "twenty");
    CHECK(engine.controlSource().hasChannel("twenty"));
    CHECK(engine.modulator().bound()); // the route to control.twenty resolved
    // Scene swap: the control source is re-attached and the channel still exists.
    engine.newComposition();
    engine.loadOrbScene();
    CHECK(engine.sources().find("control", "control") != nullptr);
    CHECK(engine.controlSource().hasChannel("twenty"));
    FixedStepClock clock(60.0);
    const std::array<std::uint8_t, 3> cc{0xB0, 20, 64};
    engine.control().injectMidi(cc);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.controlSource().value("twenty"), WithinAbs(64.0 / 127.0, 1e-4));
    std::filesystem::remove(project);
}
