// Milestone 1.1 (ADR-021): MIDI and OSC through the engine — bindings to control signals that
// routes consume, direct parameter writes, presets and transport over OSC, project round trip,
// and a real UDP loopback into the hub.

#include "app/engine.hpp"
#include "control/control_map.hpp"
#include "control/osc.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
        map.feedbackHost = "127.0.0.1";
        map.feedbackPort = 9124;
        map.feedbackEnabled = true;
        control::MidiBinding b;
        b.kind = control::MidiBindKind::ControlChange;
        b.number = 20;
        b.target.signal = "twenty";
        map.midi.push_back(b);
        engine.control().setMap(map);
        params::ModRoute r{.source = "control.twenty", .target = "orb/scale", .amount = 0.5f};
        engine.modulator().addRoute(r);
        engine.rebind();
        engine.setTempoSource(app::TempoSource::MidiClock);
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    CHECK(engine.tempoSource() == app::TempoSource::Analysis);
    REQUIRE(engine.loadProject(project).has_value());
    CHECK(engine.control().map().oscPort == 9123);
    CHECK(engine.control().map().feedbackHost == "127.0.0.1");
    CHECK(engine.control().map().feedbackPort == 9124);
    CHECK(engine.control().map().feedbackEnabled);
    CHECK(engine.tempoSource() == app::TempoSource::MidiClock);
    engine.newProject();
    CHECK(engine.tempoSource() == app::TempoSource::Analysis);
    REQUIRE(engine.loadProject(project).has_value());
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

TEST_CASE("MIDI clock drives the beat clock when it is the tempo source", "[integration][control][clock]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    engine.control().setMap(map);
    engine.setTempoSource(app::TempoSource::MidiClock);
    CHECK(engine.tempoSource() == app::TempoSource::MidiClock);
    const auto bpmId = *engine.signals().find("beat.bpm");
    const auto countId = *engine.signals().find("beat.count");

    // 96 fps, one clock every second frame: 48 ticks/s = 120 BPM at 24 ppqn. Injected messages
    // carry no timestamp, so the tracker uses the frame time.
    FixedStepClock clock(96.0);
    const std::array<std::uint8_t, 1> start{0xFA};
    const std::array<std::uint8_t, 1> tick{0xF8};
    const std::array<std::uint8_t, 1> stop{0xFC};
    engine.control().injectMidi(start);
    int pulses = 0;
    for (int frame = 0; frame < 96 * 4; ++frame) {
        if (frame % 2 == 0) {
            engine.control().injectMidi(tick);
        }
        engine.update(engine.tick(clock));
        pulses += engine.sourceContext().beatEvent ? 1 : 0; // beat.pulse is cleared after each update
    }
    CHECK(engine.midiClockActive());
    CHECK_THAT(engine.signals().value(bpmId), WithinAbs(120.0, 0.5));
    CHECK(engine.control().midiClock().tickCount() == 192);
    CHECK(engine.signals().value(countId) == 7.0f); // tick 191 lies in beat 7
    // Beats 1..7 pulsed once each; the downbeat at tick 0 precedes the first interval, so no
    // tempo is known yet and the analyzer (silent) still owns that frame.
    CHECK(pulses == 7);
    CHECK_THAT(engine.sourceContext().tempoBpm, WithinAbs(120.0, 0.5));
    CHECK(std::floor(engine.sourceContext().musicalBeats) == 7.0); // a MIDI clock's beat 0 is bar 1 beat 1
    CHECK_THAT(engine.timelineClock().beats, WithinAbs(7.0 + static_cast<double>(engine.sourceContext().beatPhase), 1e-4));
    const auto status = engine.control().status();
    CHECK(status.unmatched == 0); // clock messages never count as unmatched
    CHECK(status.clockMessages == 193);
    CHECK_FALSE(engine.control().lastMidi().has_value()); // and are not learnable

    // Stop: the beat clock falls back to the analyzer (silent here: no tempo).
    engine.control().injectMidi(stop);
    engine.update(engine.tick(clock));
    CHECK_FALSE(engine.midiClockActive());
    CHECK(engine.signals().value(bpmId) == 0.0f);

    // Analysis source: a running MIDI clock is ignored.
    engine.setTempoSource(app::TempoSource::Analysis);
    engine.control().injectMidi(start);
    for (int frame = 0; frame < 96; ++frame) {
        if (frame % 2 == 0) {
            engine.control().injectMidi(tick);
        }
        engine.update(engine.tick(clock));
    }
    CHECK(engine.control().midiClock().running());
    CHECK_FALSE(engine.midiClockActive());
    CHECK(engine.signals().value(bpmId) == 0.0f);
}

namespace {

// Drains `receiver` until `done(received)` or the timeout.
template <typename Done>
bool waitForOsc(control::OscReceiver& receiver, std::vector<control::OscMessage>& received, Done done,
                int timeoutMs = 2000) {
    for (int i = 0; i < timeoutMs / 5; ++i) {
        receiver.drain(received);
        if (done(received)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    receiver.drain(received);
    return done(received);
}

const control::OscMessage* findMessage(const std::vector<control::OscMessage>& messages, const std::string& address) {
    for (const auto& m : messages) {
        if (m.address == address) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("OSC queries are answered to the sender over UDP", "[integration][control][network]") {
    control::OscReceiver rx;
    if (!rx.open(0, "127.0.0.1")) {
        SKIP("cannot open a UDP socket here");
    }
    const std::string sender = "127.0.0.1:" + std::to_string(rx.port());
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false; // inject with an explicit sender: replies still travel over UDP
    map.midiEnabled = false;
    engine.control().setMap(map);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    engine.params().find("orb/scale")->setBaseComponent(0, 1.25f);
    engine.params().find("orb/baseColor")->setBaseComponent(2, 0.4f);

    control::OscMessage query;
    query.address = "/avgen/query";
    query.args = {std::string("orb/scale")};
    query.sender = sender;
    engine.control().injectOsc(query);
    control::OscMessage pathForm;
    pathForm.address = "/avgen/query/orb/baseColor";
    pathForm.sender = sender;
    engine.control().injectOsc(pathForm);
    engine.update(engine.tick(clock));
    std::vector<control::OscMessage> received;
    REQUIRE(waitForOsc(rx, received, [](const auto& r) { return r.size() >= 2; }));
    const auto* scale = findMessage(received, "/avgen/param/orb/scale");
    REQUIRE(scale != nullptr);
    REQUIRE(scale->args.size() == 1);
    CHECK_THAT(scale->number(0), WithinAbs(1.25, 1e-6));
    const auto* colour = findMessage(received, "/avgen/param/orb/baseColor");
    REQUIRE(colour != nullptr);
    REQUIRE(colour->args.size() == 3);
    CHECK_THAT(colour->number(2), WithinAbs(0.4, 1e-6));

    // Presets.
    engine.storePreset("one");
    engine.storePreset("two");
    control::OscMessage presets;
    presets.address = "/avgen/query/presets";
    presets.sender = sender;
    engine.control().injectOsc(presets);
    engine.update(engine.tick(clock));
    received.clear();
    REQUIRE(waitForOsc(rx, received, [](const auto& r) { return !r.empty(); }));
    CHECK(received[0].address == "/avgen/presets");
    CHECK(received[0].text(0) == "one");
    CHECK(received[0].text(1) == "two");

    // Everything, bundled.
    std::size_t serialized = 0;
    for (const auto* p : engine.params().ordered()) {
        serialized += p->flags().serialized ? 1 : 0;
    }
    REQUIRE(serialized > 50); // more than one bundle chunk
    control::OscMessage all;
    all.address = "/avgen/query/all";
    all.sender = sender;
    engine.control().injectOsc(all);
    engine.update(engine.tick(clock));
    received.clear();
    REQUIRE(waitForOsc(rx, received, [&](const auto& r) { return r.size() >= serialized; }));
    CHECK(received.size() == serialized);
    CHECK(rx.stats().packets >= 2);
    const auto* again = findMessage(received, "/avgen/param/orb/scale");
    REQUIRE(again != nullptr);
    CHECK_THAT(again->number(0), WithinAbs(1.25, 1e-6));

    // An unknown path or no reply address: unmatched / nothing sent, never an error.
    control::OscMessage unknown;
    unknown.address = "/avgen/query";
    unknown.args = {std::string("nope/missing")};
    unknown.sender = sender;
    engine.control().injectOsc(unknown);
    engine.update(engine.tick(clock));
    const auto status = engine.control().status();
    CHECK(status.unmatched == 1);
    CHECK(status.applied == 4);
    CHECK(status.feedbackSent == 3 + serialized);
}

TEST_CASE("OSC feedback pushes parameter changes to the feedback host but never echoes OSC writes",
          "[integration][control][network]") {
    control::OscReceiver rx;
    if (!rx.open(0, "127.0.0.1")) {
        SKIP("cannot open a UDP socket here");
    }
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    map.feedbackHost = "127.0.0.1";
    map.feedbackPort = rx.port();
    map.feedbackEnabled = true;
    engine.control().setMap(map);
    REQUIRE(engine.control().status().feedbackOpen);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock)); // primes the change cache: nothing is sent yet
    engine.update(engine.tick(clock));

    // A UI-style change (a slider) is pushed once.
    engine.params().find("orb/scale")->setBaseComponent(0, 2.5f);
    engine.update(engine.tick(clock));
    engine.update(engine.tick(clock));
    std::vector<control::OscMessage> received;
    REQUIRE(waitForOsc(rx, received, [](const auto& r) { return !r.empty(); }));
    REQUIRE(received.size() == 1);
    CHECK(received[0].address == "/avgen/param/orb/scale");
    CHECK_THAT(received[0].number(0), WithinAbs(2.5, 1e-6));

    // A change that arrived through OSC is not echoed; a later API change still is (and would
    // arrive after any echo, so its arrival alone proves the echo was skipped).
    control::OscMessage viaOsc;
    viaOsc.address = "/avgen/param/orb/emissive";
    viaOsc.args = {3.0f};
    engine.control().injectOsc(viaOsc);
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("orb/emissive")->baseComponent(0), WithinAbs(3.0, 1e-6));
    engine.params().find("orb/scale")->setBaseComponent(0, 0.75f);
    engine.update(engine.tick(clock));
    received.clear();
    REQUIRE(waitForOsc(rx, received, [](const auto& r) { return !r.empty(); }));
    CHECK(findMessage(received, "/avgen/param/orb/emissive") == nullptr);
    const auto* scale = findMessage(received, "/avgen/param/orb/scale");
    REQUIRE(scale != nullptr);
    CHECK_THAT(scale->number(0), WithinAbs(0.75, 1e-6));
    CHECK(received.size() == 1);

    // With a feedback host, query replies go there too (whatever the sender).
    control::OscMessage query;
    query.address = "/avgen/query/orb/scale";
    engine.control().injectOsc(query);
    engine.update(engine.tick(clock));
    received.clear();
    REQUIRE(waitForOsc(rx, received, [](const auto& r) { return !r.empty(); }));
    CHECK(received[0].address == "/avgen/param/orb/scale");

    // Disabled feedback: silence, even though the host is set.
    engine.control().map().feedbackEnabled = false;
    engine.params().find("orb/scale")->setBaseComponent(0, 1.0f);
    engine.update(engine.tick(clock));
    received.clear();
    CHECK_FALSE(waitForOsc(rx, received, [](const auto& r) { return !r.empty(); }, 100));
    CHECK(engine.control().status().feedbackSent == 3);
}

// ADR-1606: soft takeover. A knob bound with pickup does not jump a parameter that something else moved: it waits for
// the knob to reach (or pass) the value, then follows.
TEST_CASE("MIDI pickup: a knob takes over a parameter only when it reaches its value", "[integration][control][adr1606]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    control::MidiBinding knob;
    knob.kind = control::MidiBindKind::ControlChange;
    knob.number = 7;
    knob.target.parameter = "orb/scale";
    knob.target.min = 0.0f;
    knob.target.max = 2.0f;
    knob.pickup = true;
    map.midi.push_back(knob);
    engine.control().setMap(map);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    auto* scale = engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    scale->setBaseComponent(0, 1.0f); // the value a preset recall would leave: 0.5 on the knob's scale
    const auto cc = [&](int v) {
        const std::array<std::uint8_t, 3> m{0xB0, 7, static_cast<std::uint8_t>(v)};
        engine.control().injectMidi(m);
        engine.update(engine.tick(clock));
        return scale->baseComponent(0);
    };
    CHECK(cc(10) == 1.0f);  // far below: ignored, no jump
    CHECK(cc(40) == 1.0f);  // still below
    CHECK_THAT(cc(70), WithinAbs(70.0 / 127.0 * 2.0, 1e-5)); // passed through 0.5: picked up, follows
    CHECK_THAT(cc(90), WithinAbs(90.0 / 127.0 * 2.0, 1e-5));
    // Something else moves it again (a state, the UI): the knob lets go until it comes back.
    scale->setBaseComponent(0, 0.4f); // 0.2 on the knob's scale
    CHECK_THAT(cc(100), WithinAbs(0.4, 1e-6));
    CHECK_THAT(cc(26), WithinAbs(26.0 / 127.0 * 2.0, 1e-5)); // within the tolerance of 0.2: picked up
    CHECK(engine.control().status().pickupHeld == 3);

    // Without pickup the same knob jumps, as before.
    control::ControlMap plain = map;
    plain.midi[0].pickup = false;
    engine.control().setMap(plain);
    scale->setBaseComponent(0, 1.0f);
    CHECK_THAT(cc(10), WithinAbs(10.0 / 127.0 * 2.0, 1e-5));
}

TEST_CASE("a control channel holds its rest until its knob moves, returns to it on a reset, and a pickup knob waits "
          "for it",
          "[integration][control][adr1612]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    map.channelRests.push_back({"glow", 0.5f, true});
    map.channelRests.push_back({"haze", 0.25f, false});
    control::MidiBinding glow;
    glow.kind = control::MidiBindKind::ControlChange;
    glow.number = 1;
    glow.target.signal = "glow";
    map.midi.push_back(glow);
    control::MidiBinding haze = glow;
    haze.number = 2;
    haze.target.signal = "haze";
    map.midi.push_back(haze);
    engine.control().setMap(map);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    const auto bus = [&](const char* name) {
        const auto id = engine.signals().find(name);
        REQUIRE(id.has_value());
        return engine.signals().value(*id);
    };
    // Before any knob: the rests, not 0 -- a bipolar route reads 0.5 as "no change".
    CHECK(bus("control.glow") == 0.5f);
    CHECK(bus("control.haze") == 0.25f);
    const auto cc = [&](int number, int v) {
        const std::array<std::uint8_t, 3> m{0xB0, static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(v)};
        engine.control().injectMidi(m);
        engine.update(engine.tick(clock));
    };
    // No pickup: the first move takes over at once (0 attenuates as far as 1 boosts).
    cc(2, 0);
    CHECK(bus("control.haze") == 0.0f);
    // Pickup: the knob sitting at 0.1 does nothing until it reaches the rest, then follows.
    cc(1, 13);
    CHECK(bus("control.glow") == 0.5f);
    cc(1, 50);
    CHECK(bus("control.glow") == 0.5f);
    cc(1, 70); // passed through 0.5
    CHECK_THAT(bus("control.glow"), WithinAbs(70.0 / 127.0, 1e-5));
    cc(1, 20);
    CHECK_THAT(bus("control.glow"), WithinAbs(20.0 / 127.0, 1e-5));
    CHECK(engine.control().status().pickupHeld == 2);
    // A reset: every channel back to rest; the pickup knob lets go until it comes back.
    control::OscMessage reset;
    reset.address = "/avgen/control/reset";
    engine.control().injectOsc(reset);
    engine.update(engine.tick(clock));
    CHECK(bus("control.glow") == 0.5f);
    CHECK(bus("control.haze") == 0.25f);
    cc(1, 10);
    CHECK(bus("control.glow") == 0.5f);
    cc(1, 64);
    CHECK_THAT(bus("control.glow"), WithinAbs(64.0 / 127.0, 1e-5));
}
