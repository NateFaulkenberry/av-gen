// MIDI performance mapping (ADR-1700..1703): decoding, matching, the mapper's transforms and note
// behaviour, combination, scopes and activation -- everything below the engine, driven through a
// fake set of destinations.

#include "control/control_map.hpp"
#include "control/midi.hpp"
#include "control/midi_mapper.hpp"
#include "control/midi_nrpn.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace avgen::control;
using Catch::Matchers::WithinAbs;

namespace {

// Destinations: parameters with a hard range (as IParameter clamps), control channels, pulses.
struct FakeTargets final : MidiTargets {
    struct Param {
        float value = 0.0f;
        float lo = -1e9f;
        float hi = 1e9f;
    };
    std::map<std::string, Param> params;
    std::map<std::string, float> signals;
    std::map<std::string, float> rests;
    std::map<std::string, float> pulses;
    int writes = 0;

    std::optional<float> parameter(const std::string& path, int) override {
        const auto it = params.find(path);
        return it == params.end() ? std::nullopt : std::optional<float>(it->second.value);
    }
    void setParameter(const std::string& path, int, float value) override {
        const auto it = params.find(path);
        if (it != params.end()) {
            it->second.value = std::clamp(value, it->second.lo, it->second.hi);
            ++writes;
        }
    }
    float signal(const std::string& name) override { return signals.count(name) ? signals[name] : 0.0f; }
    bool signalHasRest(const std::string& name) override { return rests.count(name) != 0; }
    void setSignal(const std::string& name, float value) override {
        signals[name] = value;
        ++writes;
    }
    void pulseSignal(const std::string& name, float strength) override { pulses[name] = std::max(pulses[name], strength); }
};

MidiMessage cc(std::uint8_t number, std::uint8_t value, std::uint8_t channel = 0, std::uint16_t device = 0) {
    MidiMessage m;
    m.kind = MidiKind::ControlChange;
    m.data1 = number;
    m.data2 = value;
    m.channel = channel;
    m.device = device;
    return m;
}
MidiMessage noteOn(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel = 0, std::uint16_t device = 0) {
    MidiMessage m;
    m.kind = velocity == 0 ? MidiKind::NoteOff : MidiKind::NoteOn;
    m.data1 = note;
    m.data2 = velocity;
    m.channel = channel;
    m.device = device;
    return m;
}
MidiMessage noteOff(std::uint8_t note, std::uint8_t channel = 0, std::uint16_t device = 0) {
    MidiMessage m = noteOn(note, 0, channel, device);
    return m;
}
MidiMessage removed(std::uint16_t device) {
    MidiMessage m;
    m.kind = MidiKind::DeviceRemoved;
    m.device = device;
    return m;
}

MidiBinding ccTo(int number, const std::string& signal) {
    MidiBinding b;
    b.kind = MidiBindKind::ControlChange;
    b.number = number;
    b.target.signal = signal;
    return b;
}
MidiBinding ccToParam(int number, const std::string& path, float lo, float hi) {
    MidiBinding b;
    b.kind = MidiBindKind::ControlChange;
    b.number = number;
    b.target.parameter = path;
    b.target.min = lo;
    b.target.max = hi;
    return b;
}
MidiBinding user(MidiBinding b, MidiScope scope, const std::string& id, const std::string& scene = "s") {
    b.scope = scope;
    b.id = id;
    if (scope == MidiScope::Scene) {
        b.scene = scene;
    }
    return b;
}

std::vector<MidiDeviceSlot> twoDevices() {
    return {{"100", "Keys", true, false}, {"200", "Pads", true, false}};
}

// Builds a mapper over `map` for scene "s" with two devices.
struct Rig {
    ControlMap map;
    FakeTargets targets;
    MidiMapper mapper;
    void build(const std::string& scene = "s") {
        // Ids for defaults, as fromJson would assign them.
        for (std::size_t i = 0; i < map.midi.size(); ++i) {
            if (map.midi[i].id.empty()) {
                map.midi[i].id = "d" + std::to_string(i);
            }
        }
        mapper.rebuild(map, scene, targets);
        mapper.setDevices(twoDevices());
    }
    int send(const MidiMessage& m) { return mapper.apply(m, targets, 1.0); }
};

} // namespace

// ---- 8.1 decoding and matching -------------------------------------------------------------------------------------

TEST_CASE("decoding: CC, pitch bend, notes, velocity 0, pressure and their normalised values", "[midi][mapping][decode]") {
    MidiParserState state;
    std::vector<MidiMessage> out;
    parseMidiBytes(std::vector<std::uint8_t>{0xB3, 74, 92, 0xE0, 0x00, 0x00, 0xE0, 0x00, 0x40, 0xE0, 0x7F, 0x7F, 0x91,
                                             60, 108, 0x91, 60, 0, 0xD2, 77, 0xA0, 61, 33},
                   state, out, 0, 3);
    REQUIRE(out.size() == 8);
    CHECK(out[0].kind == MidiKind::ControlChange);
    CHECK(out[0].channel == 3);
    CHECK_THAT(out[0].normalized(), WithinAbs(92.0 / 127.0, 1e-6));
    CHECK(out[1].normalized() == 0.0f);   // bend bottom
    CHECK(out[2].normalized() == 0.5f);   // bend centre: exactly the middle (ADR-1703)
    CHECK(out[3].normalized() == 1.0f);   // bend top
    CHECK(out[4].kind == MidiKind::NoteOn);
    CHECK(out[5].kind == MidiKind::NoteOff); // velocity 0 is a note-off (MIDI semantics)
    CHECK(out[6].kind == MidiKind::ChannelPressure);
    CHECK(out[6].data2 == 77);
    CHECK(out[7].kind == MidiKind::PolyPressure);
    CHECK(out[7].data1 == 61);
    CHECK(out[7].data2 == 33);
    for (const auto& m : out) {
        CHECK(m.device == 3);
    }
    // Invalid and unsupported: sysex is dropped, stray data bytes are ignored, system common is Other.
    out.clear();
    MidiParserState fresh;
    parseMidiBytes(std::vector<std::uint8_t>{0x40, 0x41, 0xF0, 1, 2, 3, 0xF7, 0xF3, 5}, fresh, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == MidiKind::Other);
}

TEST_CASE("ADR-1703: a channel pressure binding reads the pressure", "[midi][mapping][decode][adr1703]") {
    MidiBinding b;
    b.kind = MidiBindKind::ChannelPressure;
    MidiMessage m;
    m.kind = MidiKind::ChannelPressure;
    m.data2 = 127; // where the parser puts it; data1 stays 0
    const auto v = midiSourceValue(b, m);
    REQUIRE(v.has_value());
    CHECK(*v == 1.0f); // was 0 for every message before: data1 / 127
}

TEST_CASE("ADR-1702: NRPN and RPN decode from the CC stream", "[midi][mapping][nrpn][adr1702]") {
    NrpnDecoder d;
    auto feed = [&](std::uint8_t number, std::uint8_t value, std::uint8_t channel = 0, std::uint16_t device = 0) {
        MidiMessage m = cc(number, value, channel, device);
        auto out = d.feed(m);
        return std::make_pair(m, out);
    };
    SECTION("a 14-bit NRPN: select, MSB, LSB") {
        auto [s1, o1] = feed(99, 9);   // parameter MSB
        auto [s2, o2] = feed(98, 82);  // parameter LSB: 9 * 128 + 82 = 1234
        CHECK((s1.flags & kMidiNrpnPart) != 0);
        CHECK((s2.flags & kMidiNrpnPart) != 0);
        CHECK_FALSE(o1.has_value());
        CHECK_FALSE(o2.has_value());
        auto [s3, o3] = feed(6, 64);   // value MSB
        REQUIRE(o3.has_value());
        CHECK(o3->kind == MidiKind::Nrpn);
        CHECK(o3->param == 1234);
        CHECK(o3->value14 == 64 << 7);
        CHECK((o3->flags & kMidiHiRes) == 0); // no LSB seen yet: 7-bit
        CHECK_THAT(o3->normalized(), WithinAbs(64.0 / 127.0, 1e-6));
        auto [s4, o4] = feed(38, 5);   // value LSB
        REQUIRE(o4.has_value());
        CHECK(o4->value14 == (64 << 7 | 5));
        CHECK((o4->flags & kMidiHiRes) != 0);
        CHECK_THAT(o4->normalized(), WithinAbs((64.0 * 128 + 5) / 16383.0, 1e-6));
        // From now on this parameter is 14-bit: a lone MSB keeps the resolution (LSB reset to 0).
        auto [s5, o5] = feed(6, 127);
        CHECK((o5->flags & kMidiHiRes) != 0);
        // Increment / decrement step one unit of the resolution.
        auto [s6, o6] = feed(96, 0);
        CHECK(o6->value14 == (127 << 7) + 1);
        auto [s7, o7] = feed(97, 0);
        CHECK(o7->value14 == (127 << 7));
    }
    SECTION("a 7-bit device reaches 1.0 at 127") {
        feed(99, 0);
        feed(98, 3);
        auto [s, o] = feed(6, 127);
        REQUIRE(o.has_value());
        CHECK(o->normalized() == 1.0f);
    }
    SECTION("RPN, the null RPN, and data entry with nothing selected") {
        auto [plain, none] = feed(6, 10); // nothing selected: an ordinary CC 6
        CHECK_FALSE(none.has_value());
        CHECK((plain.flags & kMidiNrpnPart) == 0);
        feed(101, 0);
        feed(100, 0); // RPN 0: pitch bend range
        auto [s, o] = feed(6, 2);
        REQUIRE(o.has_value());
        CHECK(o->kind == MidiKind::Rpn);
        CHECK(o->param == 0);
        feed(101, 127);
        feed(100, 127); // null: deselects
        auto [after, nothing] = feed(6, 3);
        CHECK_FALSE(nothing.has_value());
        CHECK((after.flags & kMidiNrpnPart) == 0);
    }
    SECTION("interrupted and interleaved sequences stay apart") {
        feed(99, 1, 0);
        feed(98, 1, 0);        // channel 1: NRPN 129
        feed(99, 2, 1);        // channel 2 selects another, without its LSB yet
        auto [o7, x] = feed(7, 100, 0); // an unrelated CC between select and data entry does not break it
        CHECK_FALSE(x.has_value());
        CHECK((o7.flags & kMidiNrpnPart) == 0);
        auto [s, o] = feed(6, 50, 0);
        REQUIRE(o.has_value());
        CHECK(o->param == 129);
        CHECK(o->channel == 0);
        auto [s2, o2] = feed(6, 60, 1); // the missing LSB counts as 0: 2 * 128
        REQUIRE(o2.has_value());
        CHECK(o2->param == 256);
        // Another device is another decoder state.
        auto [s3, o3] = feed(6, 70, 0, 1);
        CHECK_FALSE(o3.has_value());
        // A new parameter starts over: its resolution is not inherited.
        feed(38, 1, 0); // param 129 becomes 14-bit
        feed(98, 2, 0); // now NRPN 130
        auto [s4, o4] = feed(6, 10, 0);
        REQUIRE(o4.has_value());
        CHECK(o4->param == 130);
        CHECK((o4->flags & kMidiHiRes) == 0);
        // A device gone forgets its selection.
        d.resetDevice(0);
        auto [s5, o5] = feed(6, 10, 0);
        CHECK_FALSE(o5.has_value());
    }
}

TEST_CASE("relative encoders decode their three formats", "[midi][mapping][encoder]") {
    CHECK(relativeEncoderDelta(MidiEncoder::Absolute, 65) == 0);
    CHECK(relativeEncoderDelta(MidiEncoder::TwosComplement, 1) == 1);
    CHECK(relativeEncoderDelta(MidiEncoder::TwosComplement, 127) == -1);
    CHECK(relativeEncoderDelta(MidiEncoder::TwosComplement, 120) == -8);
    CHECK(relativeEncoderDelta(MidiEncoder::BinaryOffset, 64) == 0);
    CHECK(relativeEncoderDelta(MidiEncoder::BinaryOffset, 65) == 1);
    CHECK(relativeEncoderDelta(MidiEncoder::BinaryOffset, 60) == -4);
    CHECK(relativeEncoderDelta(MidiEncoder::SignMagnitude, 3) == 3);
    CHECK(relativeEncoderDelta(MidiEncoder::SignMagnitude, 65) == -1);
}

TEST_CASE("note names: middle C (60) is C4", "[midi][mapping]") {
    CHECK(midiNoteName(60) == "C4");
    CHECK(midiNoteName(0) == "C-1");
    CHECK(midiNoteName(69) == "A4");
    CHECK(midiNoteName(127) == "G9");
}

TEST_CASE("device and channel filtering, duplicate names, reconnection and the name fallback", "[midi][mapping][device]") {
    Rig rig;
    MidiBinding keys = ccTo(1, "keys");
    keys.device = "100";
    keys.deviceName = "Keys";
    keys.channel = 0;
    rig.map.midiUser.push_back(user(keys, MidiScope::Project, "k"));
    rig.build();
    CHECK(rig.send(cc(1, 127, 0, 0)) == 1);
    CHECK(rig.send(cc(1, 127, 0, 1)) == 0); // another device sending the same CC: no collision
    CHECK(rig.send(cc(1, 127, 1, 0)) == 0); // another channel
    CHECK(rig.mapper.status("k")->device == MidiDeviceState::Connected);

    // Two devices with one name (two identical keyboards): told apart by id.
    rig.mapper.setDevices({{"100", "Keys", true, false}, {"101", "Keys", true, false}});
    CHECK(rig.send(cc(1, 10, 0, 1)) == 0);
    CHECK(rig.send(cc(1, 10, 0, 0)) == 1);

    // Unplugged: kept, says so, matches nothing.
    rig.mapper.setDevices({{"100", "Keys", false, false}, {"200", "Pads", true, false}});
    CHECK(rig.mapper.status("k")->device == MidiDeviceState::Absent);
    // Back under the same identity (the input gives it its old slot 0): works again, nothing to redo.
    rig.mapper.setDevices(twoDevices());
    CHECK(rig.send(cc(1, 64, 0, 0)) == 1);

    // Another computer: the id is unknown there, a connected device has the saved name -- used, and said.
    rig.mapper.setDevices({{"999", "Pads", true, false}, {"555", "Keys", true, false}});
    CHECK(rig.mapper.status("k")->device == MidiDeviceState::ByName);
    CHECK(rig.send(cc(1, 64, 0, 1)) == 1);
    CHECK(rig.send(cc(1, 64, 0, 0)) == 0);
}

// ---- 8.2 transformations -------------------------------------------------------------------------------------------

TEST_CASE("continuous transforms: range, invert, scale and offset, curves", "[midi][mapping][transform]") {
    Rig rig;
    rig.targets.params["p"] = {0.0f};
    MidiBinding b = ccToParam(7, "p", 10.0f, 20.0f);
    rig.map.midi.push_back(b);
    rig.build();
    rig.send(cc(7, 0));
    CHECK(rig.targets.params["p"].value == 10.0f);
    rig.send(cc(7, 127));
    CHECK(rig.targets.params["p"].value == 20.0f);
    rig.send(cc(7, 64));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(10.0 + 10.0 * 64.0 / 127.0, 1e-4));

    rig.map.midi[0].invert = true;
    rig.mapper.rebuild(rig.map, "s", rig.targets);
    rig.send(cc(7, 127));
    CHECK(rig.targets.params["p"].value == 10.0f);

    rig.map.midi[0].invert = false;
    rig.map.midi[0].scale = 0.5f;
    rig.map.midi[0].offset = 0.25f;
    rig.mapper.rebuild(rig.map, "s", rig.targets);
    rig.send(cc(7, 127));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(17.5, 1e-4)); // 1 * 0.5 + 0.25 = 0.75
    rig.send(cc(7, 0));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(12.5, 1e-4));

    // Curves: fixed ends, monotonic, the shape between.
    for (MidiCurve c : {MidiCurve::Linear, MidiCurve::Exponential, MidiCurve::Logarithmic, MidiCurve::SCurve}) {
        CHECK(applyMidiCurve(c, 0.0f) == 0.0f);
        CHECK(applyMidiCurve(c, 1.0f) == 1.0f);
        float last = 0.0f;
        for (int i = 1; i <= 20; ++i) {
            const float v = applyMidiCurve(c, static_cast<float>(i) / 20.0f);
            CHECK(v >= last);
            last = v;
        }
    }
    CHECK(applyMidiCurve(MidiCurve::Exponential, 0.5f) == 0.25f);
    CHECK(applyMidiCurve(MidiCurve::Logarithmic, 0.5f) == 0.75f);
    CHECK(applyMidiCurve(MidiCurve::SCurve, 0.5f) == 0.5f);
    CHECK(applyMidiCurve(MidiCurve::SCurve, 0.25f) < 0.25f);
}

TEST_CASE("resolution: a 7-bit CC is not made finer, pitch bend and NRPN keep their 14 bits", "[midi][mapping][transform]") {
    Rig rig;
    rig.targets.params["p"] = {0.0f};
    rig.targets.params["q"] = {0.0f};
    MidiBinding bend;
    bend.kind = MidiBindKind::PitchBend;
    bend.target.parameter = "q";
    bend.target.min = -1.0f;
    bend.target.max = 1.0f;
    rig.map.midi.push_back(ccToParam(1, "p", 0.0f, 16383.0f));
    rig.map.midi.push_back(bend);
    rig.build();
    rig.send(cc(1, 1));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(16383.0 / 127.0, 1e-2)); // one step of 127
    MidiMessage m;
    m.kind = MidiKind::PitchBend;
    m.value14 = 8192;
    rig.send(m);
    CHECK(rig.targets.params["q"].value == 0.0f); // the wheel at rest is exactly the middle of a symmetric range
    m.value14 = 8193;
    rig.send(m);
    CHECK(rig.targets.params["q"].value > 0.0f);
    CHECK(rig.targets.params["q"].value < 0.001f); // one 14-bit step
}

TEST_CASE("smoothing glides toward the knob and lands on it", "[midi][mapping][transform]") {
    Rig rig;
    rig.targets.params["p"] = {0.0f};
    MidiBinding b = ccToParam(7, "p", 0.0f, 1.0f);
    b.smoothMs = 100.0f;
    rig.map.midi.push_back(b);
    rig.build();
    rig.send(cc(7, 127));
    CHECK(rig.targets.params["p"].value == 0.0f); // written by advance, not by the message
    rig.mapper.advance(1.0 / 30.0, rig.targets);
    const float first = rig.targets.params["p"].value;
    CHECK(first > 0.2f);
    CHECK(first < 0.4f); // 1 - e^(-33/100) = 0.28
    for (int i = 0; i < 60; ++i) {
        rig.mapper.advance(1.0 / 30.0, rig.targets);
    }
    CHECK(rig.targets.params["p"].value == 1.0f); // settles exactly
}

TEST_CASE("pickup: a knob does not jump the value until it reaches it", "[midi][mapping][transform][adr1606]") {
    Rig rig;
    rig.targets.params["p"] = {0.5f};
    MidiBinding b = ccToParam(7, "p", 0.0f, 1.0f);
    b.pickup = true;
    rig.map.midi.push_back(b);
    rig.build();
    rig.send(cc(7, 10));
    CHECK(rig.targets.params["p"].value == 0.5f);
    CHECK(rig.mapper.status("d0")->waitingForPickup);
    rig.send(cc(7, 50)); // still below
    CHECK(rig.targets.params["p"].value == 0.5f);
    rig.send(cc(7, 70)); // passes through 0.5 between two messages: picked up
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(70.0 / 127.0, 1e-5));
    CHECK_FALSE(rig.mapper.status("d0")->waitingForPickup);
    CHECK(rig.mapper.pickupHeld() == 2);
}

TEST_CASE("a relative encoder steps from where the destination is", "[midi][mapping][encoder]") {
    Rig rig;
    rig.targets.params["p"] = {0.5f};
    MidiBinding b = ccToParam(20, "p", 0.0f, 1.0f);
    b.encoder = MidiEncoder::TwosComplement;
    rig.map.midi.push_back(b);
    rig.build();
    rig.send(cc(20, 1));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(0.5 + 1.0 / 127.0, 1e-5));
    rig.send(cc(20, 127));
    rig.send(cc(20, 127));
    CHECK_THAT(rig.targets.params["p"].value, WithinAbs(0.5 - 1.0 / 127.0, 1e-5));
    for (int i = 0; i < 200; ++i) {
        rig.send(cc(20, 8));
    }
    CHECK(rig.targets.params["p"].value == 1.0f); // clamps rather than wraps
}

TEST_CASE("notes: gate over a range, velocity, note-off and velocity 0, toggle, trigger", "[midi][mapping][notes]") {
    Rig rig;
    MidiBinding gate;
    gate.kind = MidiBindKind::Note;
    gate.number = 36;
    gate.numberHigh = 39;
    gate.target.signal = "gate";
    MidiBinding flat = gate;
    flat.velocity = false;
    flat.target.signal = "flat";
    MidiBinding toggle = gate;
    toggle.toggle = true;
    toggle.number = 48;
    toggle.numberHigh = -1;
    toggle.target.signal = "toggle";
    MidiBinding trigger;
    trigger.kind = MidiBindKind::NoteEvent;
    trigger.number = 60;
    trigger.target.signal = "hit";
    rig.map.midi = {gate, flat, toggle, trigger};
    rig.build();

    rig.send(noteOn(36, 64));
    CHECK_THAT(rig.targets.signals["gate"], WithinAbs(64.0 / 127.0, 1e-6));
    CHECK(rig.targets.signals["flat"] == 1.0f);
    rig.send(noteOn(38, 127)); // a second key of the range
    rig.send(noteOff(36));
    CHECK(rig.targets.signals["gate"] == 1.0f); // still held by 38
    CHECK(rig.mapper.status("d0")->held == 1);
    rig.send(noteOn(38, 0)); // velocity 0 = note-off: the last key up closes the gate
    CHECK(rig.targets.signals["gate"] == 0.0f);
    CHECK(rig.targets.signals["flat"] == 0.0f);
    rig.send(noteOn(40, 100)); // outside the range
    CHECK(rig.targets.signals["gate"] == 0.0f);

    rig.send(noteOn(48, 1));
    CHECK(rig.targets.signals["toggle"] == 1.0f);
    rig.send(noteOff(48));
    CHECK(rig.targets.signals["toggle"] == 1.0f); // note-off does not flip it
    rig.send(noteOn(48, 1));
    CHECK(rig.targets.signals["toggle"] == 0.0f);

    rig.send(noteOn(60, 127));
    CHECK(rig.targets.pulses["hit"] == 1.0f);
    rig.targets.pulses.clear();
    rig.send(noteOff(60));
    CHECK(rig.targets.pulses.empty()); // a trigger fires on note-on only
}

TEST_CASE("no stuck gates: device removal, a lost note, disabling, another scene", "[midi][mapping][notes]") {
    Rig rig;
    MidiBinding gate;
    gate.kind = MidiBindKind::Note;
    gate.number = -1; // any note
    gate.target.signal = "gate";
    rig.map.midiUser.push_back(user(gate, MidiScope::Scene, "g", "s"));
    rig.build();

    rig.send(noteOn(60, 100, 0, 0));
    rig.send(noteOn(61, 100, 0, 1)); // a second device holds one too
    rig.send(removed(0));
    CHECK(rig.targets.signals["gate"] > 0.0f); // device 1 still holds
    rig.send(removed(1));
    CHECK(rig.targets.signals["gate"] == 0.0f);

    rig.send(noteOn(60, 100));
    rig.send(removed(kAllMidiDevices)); // notes were dropped in a full queue: everything is released
    CHECK(rig.targets.signals["gate"] == 0.0f);

    rig.send(noteOn(60, 100));
    rig.map.midiUser[0].enabled = false;
    rig.mapper.rebuild(rig.map, "s", rig.targets); // an edit: the leaving mapping's gate is released
    CHECK(rig.targets.signals["gate"] == 0.0f);

    rig.map.midiUser[0].enabled = true;
    rig.mapper.rebuild(rig.map, "s", rig.targets);
    rig.send(noteOn(60, 100));
    rig.mapper.rebuild(rig.map, "other", rig.targets, false); // a scene change
    CHECK(rig.targets.signals["gate"] == 0.0f);
    CHECK(rig.send(noteOn(60, 100)) == 0); // and the scene mapping is not active there
}

TEST_CASE("one source, many destinations; many sources, one destination", "[midi][mapping][combine]") {
    Rig rig;
    rig.targets.params["palette"] = {0.0f};
    rig.targets.params["crystal"] = {0.0f};
    // One filter knob drives three destinations, each with its own range.
    rig.map.midi = {ccToParam(74, "palette", 0.0f, 1.0f), ccToParam(74, "crystal", 2.0f, 4.0f), ccTo(74, "refract")};
    rig.build();
    CHECK(rig.send(cc(74, 127)) == 3);
    CHECK(rig.targets.params["palette"].value == 1.0f);
    CHECK(rig.targets.params["crystal"].value == 4.0f);
    CHECK(rig.targets.signals["refract"] == 1.0f);
    // Disabling one leaves the others.
    rig.map.disabledDefaults = {"d1"};
    rig.mapper.rebuild(rig.map, "s", rig.targets);
    CHECK(rig.send(cc(74, 0)) == 2);
    CHECK(rig.targets.params["crystal"].value == 4.0f);

    // Many sources on one destination.
    Rig many;
    many.targets.params["level"] = {0.0f, 0.0f, 1.0f};
    many.map.midi = {ccToParam(1, "level", 0.0f, 0.6f), ccToParam(2, "level", 0.0f, 0.6f)};
    many.build();
    SECTION("replace: the latest message wins") {
        many.send(cc(1, 127));
        many.send(cc(2, 0));
        CHECK(many.targets.params["level"].value == 0.0f);
    }
    SECTION("max: the larger") {
        many.map.setCombine("level", MidiCombine::Max);
        many.mapper.rebuild(many.map, "s", many.targets);
        many.send(cc(1, 127));
        many.send(cc(2, 0));
        CHECK_THAT(many.targets.params["level"].value, WithinAbs(0.6, 1e-5));
    }
    SECTION("add: the sum, clamped to the destination") {
        many.map.setCombine("level", MidiCombine::Add);
        many.mapper.rebuild(many.map, "s", many.targets);
        many.send(cc(1, 127));
        CHECK_THAT(many.targets.params["level"].value, WithinAbs(0.6, 1e-5)); // only one has contributed
        many.send(cc(2, 64));
        CHECK_THAT(many.targets.params["level"].value, WithinAbs(0.6 + 0.6 * 64.0 / 127.0, 1e-4));
        many.send(cc(2, 127));
        CHECK(many.targets.params["level"].value == 1.0f); // the parameter's own range
    }
}

TEST_CASE("an invalid destination is reported, never crashes, and writes nothing", "[midi][mapping]") {
    Rig rig;
    rig.map.midiUser.push_back(user(ccToParam(1, "gone/param", 0.0f, 1.0f), MidiScope::Project, "x"));
    rig.build();
    CHECK(rig.mapper.status("x")->destinationMissing);
    rig.send(cc(1, 127));
    CHECK(rig.targets.writes == 0);
}

// ---- scopes, precedence, activation -------------------------------------------------------------------------------

TEST_CASE("ADR-1700 precedence: tombstones, overrides, shadowing, other scenes", "[midi][mapping][scope][adr1700]") {
    ControlMap map;
    MidiBinding def = ccTo(21, "cloud");
    def.id = "d-cloud";
    map.midi.push_back(def);
    MidiBinding def2 = ccTo(22, "crash");
    def2.id = "d-crash";
    map.midi.push_back(def2);

    const auto stateOf = [&](const std::string& scene, const std::string& id) {
        for (const auto& r : resolveMidiMappings(map, scene)) {
            if (r.binding->id == id) return r.state;
        }
        FAIL("no mapping " << id);
        return MidiMappingState::Invalid;
    };
    CHECK(stateOf("s", "d-cloud") == MidiMappingState::Active);

    map.disabledDefaults = {"d-crash"};
    CHECK(stateOf("s", "d-crash") == MidiMappingState::DefaultOff);

    // An edit of a default: a scene mapping overriding it (its source changed to CC 23).
    MidiBinding edit = user(ccTo(23, "cloud"), MidiScope::Scene, "u-edit", "s");
    edit.overrides = "d-cloud";
    map.midiUser.push_back(edit);
    CHECK(stateOf("s", "d-cloud") == MidiMappingState::Overridden);
    CHECK(stateOf("other", "d-cloud") == MidiMappingState::Active); // the override is this scene's only
    CHECK(stateOf("other", "u-edit") == MidiMappingState::OtherScene);

    // Coincidence: a project mapping on CC 21 to the same channel shadows the (now restored) default.
    map.midiUser.clear();
    map.midiUser.push_back(user(ccTo(21, "cloud"), MidiScope::Project, "u-proj"));
    CHECK(stateOf("s", "d-cloud") == MidiMappingState::Shadowed);
    // A different destination from the same source is one source, two destinations: both apply.
    map.midiUser[0].target.signal = "glow";
    CHECK(stateOf("s", "d-cloud") == MidiMappingState::Active);
    CHECK(stateOf("s", "u-proj") == MidiMappingState::Active);
    // A device-specific user mapping overlaps an any-device default, and the scene beats the project.
    map.midiUser[0].target.signal = "cloud";
    map.midiUser[0].device = "100";
    map.midiUser[0].deviceName = "Keys";
    MidiBinding scene = user(ccTo(21, "cloud"), MidiScope::Scene, "u-scene", "s");
    scene.channel = 3;
    map.midiUser.push_back(scene);
    CHECK(stateOf("s", "u-proj") == MidiMappingState::Shadowed);
    CHECK(stateOf("s", "d-cloud") == MidiMappingState::Shadowed);
    CHECK(stateOf("s", "u-scene") == MidiMappingState::Active);
    // Disabled user mappings never shadow.
    map.midiUser[1].enabled = false;
    CHECK(stateOf("s", "u-scene") == MidiMappingState::Disabled);
    CHECK(stateOf("s", "u-proj") == MidiMappingState::Active);
}

TEST_CASE("mappings apply in precedence order and a scene change activates its own", "[midi][mapping][scope]") {
    Rig rig;
    rig.targets.params["p"] = {0.0f};
    rig.map.midi.push_back(ccToParam(5, "p", 0.0f, 1.0f));
    MidiBinding a = user(ccToParam(5, "p", 0.0f, 10.0f), MidiScope::Scene, "a", "s");
    a.channel = 0; // overlaps the default on the same destination: the default is shadowed in "s"
    rig.map.midiUser.push_back(a);
    rig.build("s");
    rig.send(cc(5, 127));
    CHECK(rig.targets.params["p"].value == 10.0f);
    rig.mapper.rebuild(rig.map, "t", rig.targets, false);
    rig.send(cc(5, 127));
    CHECK(rig.targets.params["p"].value == 1.0f); // in "t" only the default applies
}

TEST_CASE("mapping lookup stays cheap with a thousand assignments", "[midi][mapping][perf]") {
    Rig rig;
    for (int i = 0; i < 1000; ++i) {
        MidiBinding b = ccTo(i % 128, "ch" + std::to_string(i % 64));
        b.channel = (i / 128) % 16;
        rig.map.midiUser.push_back(user(b, MidiScope::Project, "m" + std::to_string(i)));
    }
    const auto t0 = std::chrono::steady_clock::now();
    rig.build();
    const auto t1 = std::chrono::steady_clock::now();
    int moved = 0;
    constexpr int kMessages = 200000;
    for (int i = 0; i < kMessages; ++i) {
        moved += rig.send(cc(static_cast<std::uint8_t>(i % 128), static_cast<std::uint8_t>(i % 128),
                             static_cast<std::uint8_t>(i % 8)));
    }
    const auto t2 = std::chrono::steady_clock::now();
    const double rebuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double perMessageUs = std::chrono::duration<double, std::micro>(t2 - t1).count() / kMessages;
    INFO("rebuild " << rebuildMs << " ms, " << perMessageUs << " us per message, moved " << moved);
    WARN("1000 mappings: rebuild " << rebuildMs << " ms; " << perMessageUs << " us per message");
    CHECK(moved > 0);
    CHECK(perMessageUs < 5.0); // an indexed lookup: a few candidates, not a scan of 1000
}
