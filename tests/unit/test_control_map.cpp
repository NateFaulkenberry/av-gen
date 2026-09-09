// Milestone 1.1: control map (ADR-021) — MIDI/OSC binding matches, the direct OSC scheme, JSON.

#include "control/control_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

using namespace avgen::control;
using Catch::Matchers::WithinAbs;

namespace {
MidiMessage cc(std::uint8_t channel, std::uint8_t number, std::uint8_t value, std::string source = "pad") {
    MidiMessage m;
    m.kind = MidiKind::ControlChange;
    m.channel = channel;
    m.data1 = number;
    m.data2 = value;
    m.source = std::move(source);
    return m;
}
MidiMessage note(MidiKind kind, std::uint8_t number, std::uint8_t velocity) {
    MidiMessage m;
    m.kind = kind;
    m.data1 = number;
    m.data2 = velocity;
    m.source = "keys";
    return m;
}
OscMessage osc(std::string address, std::vector<OscArg> args = {}) {
    OscMessage m;
    m.address = std::move(address);
    m.args = std::move(args);
    return m;
}
} // namespace

TEST_CASE("MIDI bindings match by source, channel, kind and number", "[control][map]") {
    MidiBinding b;
    b.kind = MidiBindKind::ControlChange;
    b.number = 7;
    b.channel = 2;
    b.source = "PAD";
    b.target.signal = "fader";
    auto m = matchMidi(b, cc(2, 7, 127));
    REQUIRE(m.has_value());
    CHECK_THAT(m->value, WithinAbs(1.0, 1e-6));
    CHECK_FALSE(m->event);
    CHECK_FALSE(matchMidi(b, cc(1, 7, 127)).has_value());          // wrong channel
    CHECK_FALSE(matchMidi(b, cc(2, 8, 127)).has_value());          // wrong number
    CHECK_FALSE(matchMidi(b, cc(2, 7, 127, "keys")).has_value());  // wrong source
    b.channel = -1;
    b.source = "*";
    CHECK(matchMidi(b, cc(9, 7, 64, "anything")).has_value());
    b.number = -1;
    CHECK(matchMidi(b, cc(9, 99, 64)).has_value());

    // Notes: value follows velocity, off = 0; toggle flips on note-on only; NoteEvent pulses.
    MidiBinding n;
    n.kind = MidiBindKind::Note;
    n.number = 36;
    CHECK_THAT(matchMidi(n, note(MidiKind::NoteOn, 36, 127))->value, WithinAbs(1.0, 1e-6));
    CHECK_THAT(matchMidi(n, note(MidiKind::NoteOff, 36, 0))->value, WithinAbs(0.0, 1e-6));
    CHECK_FALSE(matchMidi(n, note(MidiKind::NoteOn, 37, 127)).has_value());
    n.toggle = true;
    CHECK_THAT(matchMidi(n, note(MidiKind::NoteOn, 36, 100))->value, WithinAbs(1.0, 1e-6));
    CHECK_FALSE(matchMidi(n, note(MidiKind::NoteOff, 36, 0)).has_value());
    CHECK_THAT(matchMidi(n, note(MidiKind::NoteOn, 36, 100))->value, WithinAbs(0.0, 1e-6));
    MidiBinding e;
    e.kind = MidiBindKind::NoteEvent;
    auto ev = matchMidi(e, note(MidiKind::NoteOn, 40, 64));
    REQUIRE(ev.has_value());
    CHECK(ev->event);
    CHECK_FALSE(matchMidi(e, note(MidiKind::NoteOff, 40, 0)).has_value());

    MidiBinding pb;
    pb.kind = MidiBindKind::PitchBend;
    MidiMessage bend;
    bend.kind = MidiKind::PitchBend;
    bend.value14 = 8192;
    CHECK_THAT(matchMidi(pb, bend)->value, WithinAbs(0.5, 1e-3));
    MidiBinding pr;
    pr.kind = MidiBindKind::Program;
    pr.number = 3;
    MidiMessage prog;
    prog.kind = MidiKind::ProgramChange;
    prog.data1 = 3;
    CHECK(matchMidi(pr, prog)->event);
    prog.data1 = 4;
    CHECK_FALSE(matchMidi(pr, prog).has_value());
}

TEST_CASE("OSC bindings match addresses and patterns and scale the argument", "[control][map]") {
    OscBinding b;
    b.address = "/fader/1";
    b.target.signal = "fader1";
    auto m = matchOsc(b, osc("/fader/1", {0.25f}));
    REQUIRE(m.has_value());
    CHECK_THAT(m->value, WithinAbs(0.25, 1e-6));
    CHECK_FALSE(matchOsc(b, osc("/fader/2", {0.25f})).has_value());
    CHECK_FALSE(matchOsc(b, osc("/fader/1", {std::string("x")})).has_value()); // no number
    b.address = "/fader/*";
    CHECK(matchOsc(b, osc("/fader/7", {std::int32_t{1}})).has_value());
    b.argIndex = 1;
    b.inMin = 0.0f;
    b.inMax = 127.0f;
    CHECK_THAT(matchOsc(b, osc("/fader/7", {std::int32_t{0}, std::int32_t{127}}))->value, WithinAbs(1.0, 1e-6));
    CHECK_THAT(matchOsc(b, osc("/fader/7", {std::int32_t{0}, std::int32_t{254}}))->value, WithinAbs(1.0, 1e-6)); // clamped
    OscBinding e;
    e.address = "/hit";
    e.event = true;
    e.target.signal = "hit";
    auto ev = matchOsc(e, osc("/hit"));
    REQUIRE(ev.has_value());
    CHECK(ev->event);
    CHECK_THAT(ev->value, WithinAbs(1.0, 1e-6)); // no argument = full strength
    CHECK_THAT(matchOsc(e, osc("/hit", {0.5f}))->value, WithinAbs(0.5, 1e-6));
}

TEST_CASE("Direct OSC commands parse under the prefix", "[control][map]") {
    using K = DirectCommand::Kind;
    auto p = parseDirectOsc(osc("/avgen/param/orb/scale", {1.5f}), "/avgen");
    REQUIRE(p.has_value());
    CHECK(p->kind == K::SetParameter);
    CHECK(p->path == "orb/scale");
    REQUIRE(p->values.size() == 1);
    CHECK_THAT(p->values[0], WithinAbs(1.5, 1e-6));
    auto c = parseDirectOsc(osc("/avgen/param/orb/baseColor", {0.1f, 0.2f, 0.3f}), "/avgen");
    REQUIRE(c.has_value());
    CHECK(c->values.size() == 3);
    CHECK_FALSE(parseDirectOsc(osc("/avgen/param/orb/scale"), "/avgen").has_value()); // no value
    CHECK_FALSE(parseDirectOsc(osc("/other/param/orb/scale", {1.0f}), "/avgen").has_value());
    CHECK_FALSE(parseDirectOsc(osc("/avgenx/param/orb/scale", {1.0f}), "/avgen").has_value());
    CHECK(parseDirectOsc(osc("/param/orb/scale", {1.0f}), "")->kind == K::SetParameter);
    CHECK(parseDirectOsc(osc("/avgen/signal/energy", {0.7f}), "/avgen")->kind == K::SetSignal);
    auto pulse = parseDirectOsc(osc("/avgen/pulse/kick"), "/avgen");
    REQUIRE(pulse.has_value());
    CHECK(pulse->kind == K::Pulse);
    CHECK_THAT(pulse->values.at(0), WithinAbs(1.0, 1e-6));
    auto recall = parseDirectOsc(osc("/avgen/preset/recall", {std::string("big")}), "/avgen");
    REQUIRE(recall.has_value());
    CHECK(recall->kind == K::PresetRecall);
    CHECK(recall->path == "big");
    CHECK_FALSE(parseDirectOsc(osc("/avgen/preset/recall"), "/avgen").has_value());
    auto morph = parseDirectOsc(osc("/avgen/preset/morph", {std::string("a"), std::string("b"), 0.4f}), "/avgen");
    REQUIRE(morph.has_value());
    CHECK(morph->kind == K::PresetMorph);
    CHECK(morph->second == "b");
    CHECK_THAT(morph->values.at(0), WithinAbs(0.4, 1e-6));
    CHECK(parseDirectOsc(osc("/avgen/transport/play"), "/avgen")->kind == K::Play);
    CHECK(parseDirectOsc(osc("/avgen/transport/toggle"), "/avgen")->kind == K::Toggle);
    CHECK(parseDirectOsc(osc("/avgen/transport/seek", {12.0f}), "/avgen")->kind == K::Seek);
    CHECK_FALSE(parseDirectOsc(osc("/avgen/transport/seek"), "/avgen").has_value());
    CHECK_FALSE(parseDirectOsc(osc("/avgen/transport/dance"), "/avgen").has_value());
    CHECK(parameterAddress("/avgen", "orb/scale") == "/avgen/param/orb/scale");
}

TEST_CASE("Control maps round-trip JSON and list their channels", "[control][map][json]") {
    ControlMap m;
    m.oscPort = 9100;
    m.oscPrefix = "/show";
    m.midiFilter = "Launch";
    MidiBinding mb;
    mb.kind = MidiBindKind::Note;
    mb.number = 36;
    mb.channel = 9;
    mb.toggle = true;
    mb.target.signal = "kick";
    mb.target.parameter = "orb/emissive";
    mb.target.min = 0.5f;
    mb.target.max = 4.0f;
    m.midi.push_back(mb);
    MidiBinding me;
    me.kind = MidiBindKind::NoteEvent;
    me.target.signal = "hit";
    m.midi.push_back(me);
    OscBinding ob;
    ob.address = "/fader/*";
    ob.argIndex = 1;
    ob.inMax = 127.0f;
    ob.target.signal = "fader";
    m.osc.push_back(ob);
    const auto j = m.toJson();
    auto back = ControlMap::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->oscPort == 9100);
    CHECK(back->oscPrefix == "/show");
    CHECK(back->midiFilter == "Launch");
    REQUIRE(back->midi.size() == 2);
    CHECK(back->midi[0].kind == MidiBindKind::Note);
    CHECK(back->midi[0].number == 36);
    CHECK(back->midi[0].channel == 9);
    CHECK(back->midi[0].toggle);
    CHECK(back->midi[0].target.signal == "kick");
    CHECK(back->midi[0].target.parameter == "orb/emissive");
    CHECK(back->midi[0].target.max == 4.0f);
    REQUIRE(back->osc.size() == 1);
    CHECK(back->osc[0].address == "/fader/*");
    CHECK(back->osc[0].argIndex == 1);
    CHECK(back->osc[0].inMax == 127.0f);
    const auto channels = back->channels();
    REQUIRE(channels.size() == 3);
    CHECK(channels[0].name == "kick");
    CHECK_FALSE(channels[0].event);
    CHECK(channels[1].name == "hit");
    CHECK(channels[1].event);
    CHECK(channels[2].name == "fader");

    CHECK(ControlMap::fromJson(nlohmann::json::object()).has_value()); // defaults
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json::array()).has_value());
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json{{"osc", {{"port", 70000}}}}).has_value());
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json{{"osc", {{"prefix", "nope"}}}}).has_value());
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json{{"midi", {{"bindings", {{{"kind", "cc"}}}}}}}).has_value()); // no target
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json{{"midi", {{"bindings", {{{"kind", "wat"}, {"signal", "x"}}}}}}}).has_value());
    CHECK_FALSE(ControlMap::fromJson(nlohmann::json{{"osc", {{"bindings", {{{"signal", "x"}}}}}}}).has_value()); // no address
}
