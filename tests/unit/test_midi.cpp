#include "control/midi.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include <unistd.h>
#include <vector>

using namespace avgen;
using namespace avgen::control;

namespace {

using Bytes = std::vector<std::uint8_t>;

std::vector<MidiMessage> parse(const Bytes& bytes, MidiParserState& state, const std::string& source = "t") {
    std::vector<MidiMessage> out;
    parseMidiBytes(bytes, state, out, 42, source);
    return out;
}

std::vector<MidiMessage> parse(const Bytes& bytes) {
    MidiParserState state;
    return parse(bytes, state);
}

// Virtual sources are system-wide, and several avgen_tests processes (other worktrees, CI) may run
// the device cases at once. A per-process name keeps one process from connecting to another's.
std::string testSourceName(const char* base) {
    return std::string(base) + "-" + std::to_string(::getpid());
}

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace

TEST_CASE("parseMidiBytes decodes every channel voice message", "[control][midi]") {
    SECTION("note on") {
        auto m = parse({0x91, 60, 100});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::NoteOn);
        CHECK(m[0].channel == 1);
        CHECK(m[0].data1 == 60);
        CHECK(m[0].data2 == 100);
        CHECK(m[0].timestampNs == 42);
        CHECK(m[0].source == "t");
    }
    SECTION("note off") {
        auto m = parse({0x80, 60, 64});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::NoteOff);
        CHECK(m[0].channel == 0);
        CHECK(m[0].data1 == 60);
        CHECK(m[0].data2 == 64);
    }
    SECTION("note on with velocity 0 is a note off") {
        auto m = parse({0x9F, 61, 0});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::NoteOff);
        CHECK(m[0].channel == 15);
        CHECK(m[0].data1 == 61);
        CHECK(m[0].data2 == 0);
    }
    SECTION("control change") {
        auto m = parse({0xB2, 7, 100});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::ControlChange);
        CHECK(m[0].channel == 2);
        CHECK(m[0].data1 == 7);
        CHECK(m[0].data2 == 100);
    }
    SECTION("program change carries one data byte") {
        auto m = parse({0xC3, 12});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::ProgramChange);
        CHECK(m[0].channel == 3);
        CHECK(m[0].data1 == 12);
    }
    SECTION("channel pressure carries one data byte as the value") {
        auto m = parse({0xD4, 99});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::ChannelPressure);
        CHECK(m[0].channel == 4);
        CHECK(m[0].data2 == 99);
    }
    SECTION("poly pressure") {
        auto m = parse({0xA5, 60, 77});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::PolyPressure);
        CHECK(m[0].channel == 5);
        CHECK(m[0].data1 == 60);
        CHECK(m[0].data2 == 77);
    }
    SECTION("pitch bend centre") {
        auto m = parse({0xE0, 0x00, 0x40});
        REQUIRE(m.size() == 1);
        CHECK(m[0].kind == MidiKind::PitchBend);
        CHECK(m[0].value14 == 8192);
    }
    SECTION("pitch bend max and min") {
        auto m = parse({0xE6, 0x7F, 0x7F, 0x00, 0x00});
        REQUIRE(m.size() == 2);
        CHECK(m[0].value14 == 16383);
        CHECK(m[0].channel == 6);
        CHECK(m[1].value14 == 0);
    }
}

TEST_CASE("parseMidiBytes handles running status across calls and split bytes", "[control][midi]") {
    MidiParserState state;
    auto a = parse({0x90, 60}, state);
    CHECK(a.empty());
    auto b = parse({100, 62}, state); // completes the first note, starts the second
    REQUIRE(b.size() == 1);
    CHECK(b[0].kind == MidiKind::NoteOn);
    CHECK(b[0].data1 == 60);
    CHECK(b[0].data2 == 100);
    auto c = parse({0x00}, state); // velocity 0 -> note off for 62 via running status
    REQUIRE(c.size() == 1);
    CHECK(c[0].kind == MidiKind::NoteOff);
    CHECK(c[0].data1 == 62);
    auto d = parse({64, 127, 65, 127}, state); // two more notes, still running status
    REQUIRE(d.size() == 2);
    CHECK(d[0].data1 == 64);
    CHECK(d[1].data1 == 65);
    CHECK(d[1].kind == MidiKind::NoteOn);
    CHECK(state.runningStatus == 0x90);
}

TEST_CASE("parseMidiBytes lets real-time bytes interleave inside a message", "[control][midi]") {
    auto m = parse({0xB0, 0xF8, 1, 0xFA, 0xF8, 64});
    REQUIRE(m.size() == 4);
    CHECK(m[0].kind == MidiKind::Clock);
    CHECK(m[1].kind == MidiKind::Start);
    CHECK(m[2].kind == MidiKind::Clock);
    CHECK(m[3].kind == MidiKind::ControlChange);
    CHECK(m[3].data1 == 1);
    CHECK(m[3].data2 == 64);

    auto rt = parse({0xFB, 0xFC});
    REQUIRE(rt.size() == 2);
    CHECK(rt[0].kind == MidiKind::Continue);
    CHECK(rt[1].kind == MidiKind::Stop);

    auto sensing = parse({0xFE});
    REQUIRE(sensing.size() == 1);
    CHECK(sensing[0].kind == MidiKind::Other);
    CHECK(sensing[0].data1 == 0xFE);
}

TEST_CASE("parseMidiBytes drops sysex but keeps real-time inside it", "[control][midi]") {
    auto m = parse({0xF0, 0x7E, 0x7F, 0x09, 0xF8, 0x01, 0xF7, 0x90, 60, 100});
    REQUIRE(m.size() == 2);
    CHECK(m[0].kind == MidiKind::Clock);
    CHECK(m[1].kind == MidiKind::NoteOn);
    CHECK(m[1].data1 == 60);

    SECTION("sysex split across calls") {
        MidiParserState state;
        CHECK(parse({0xF0, 0x41, 0x10}, state).empty());
        CHECK(state.inSysex);
        CHECK(parse({0x42, 0x12}, state).empty());
        auto after = parse({0xF7, 0xB0, 1, 2}, state);
        REQUIRE(after.size() == 1);
        CHECK(after[0].kind == MidiKind::ControlChange);
        CHECK_FALSE(state.inSysex);
    }
    SECTION("a status byte terminates an unterminated sysex") {
        auto n = parse({0xF0, 0x41, 0x90, 60, 100});
        REQUIRE(n.size() == 1);
        CHECK(n[0].kind == MidiKind::NoteOn);
    }
    SECTION("sysex clears running status") {
        auto n = parse({0x90, 60, 100, 0xF0, 0x01, 0xF7, 61, 100});
        REQUIRE(n.size() == 1);
        CHECK(n[0].data1 == 60);
    }
}

TEST_CASE("parseMidiBytes ignores data bytes without a status and reports system common as Other",
          "[control][midi]") {
    CHECK(parse({1, 2, 3, 0x7F}).empty());
    auto m = parse({0x7F, 0xB0, 1, 2});
    REQUIRE(m.size() == 1);
    CHECK(m[0].kind == MidiKind::ControlChange);

    // Song position pointer (F2 + 2 data bytes), tune request (F6); their data does not leak.
    auto sc = parse({0xF2, 0x10, 0x20, 0xF6, 0x90, 60, 1});
    REQUIRE(sc.size() == 3);
    CHECK(sc[0].kind == MidiKind::Other);
    CHECK(sc[0].data1 == 0xF2);
    CHECK(sc[1].kind == MidiKind::Other);
    CHECK(sc[1].data1 == 0xF6);
    CHECK(sc[2].kind == MidiKind::NoteOn);

    // System common cancels running status: the following data bytes are garbage.
    CHECK(parse({0x90, 60, 100, 0xF6, 61, 100}).size() == 2);
}

TEST_CASE("MidiMessage::normalized and midiKindName", "[control][midi]") {
    auto cc = parse({0xB0, 7, 127});
    CHECK_THAT(cc[0].normalized(), Catch::Matchers::WithinAbs(1.0, 1e-6));
    auto half = parse({0xB0, 7, 64});
    CHECK_THAT(half.at(0).normalized(), Catch::Matchers::WithinAbs(64.0 / 127.0, 1e-6));
    auto bend = parse({0xE0, 0x7F, 0x7F});
    CHECK_THAT(bend[0].normalized(), Catch::Matchers::WithinAbs(1.0, 1e-6));
    auto centre = parse({0xE0, 0x00, 0x40});
    CHECK_THAT(centre[0].normalized(), Catch::Matchers::WithinAbs(8192.0 / 16383.0, 1e-6));
    auto off = parse({0x90, 60, 0});
    CHECK(off[0].normalized() == 0.0f);

    CHECK(std::string(midiKindName(MidiKind::NoteOn)) == "NoteOn");
    CHECK(std::string(midiKindName(MidiKind::PitchBend)) == "PitchBend");
    CHECK(std::string(midiKindName(MidiKind::Clock)) == "Clock");
    CHECK(std::string(midiKindName(MidiKind::Other)) == "Other");
}

TEST_CASE("MidiInput inbox: inject, drain order, stats and queue limit", "[control][midi]") {
    MidiInput input;
    CHECK_FALSE(input.isOpen());
    CHECK(input.connectedSources().empty());
    CHECK(input.stats().messages == 0);

    const Bytes notes = {0x90, 60, 100, 61, 100, 62, 100};
    input.inject(notes, "unit");
    std::vector<MidiMessage> out;
    CHECK(input.drain(out) == 3);
    REQUIRE(out.size() == 3);
    CHECK(out[0].data1 == 60);
    CHECK(out[1].data1 == 61);
    CHECK(out[2].data1 == 62);
    CHECK(out[0].source == "unit");
    CHECK(input.stats().messages == 3);
    CHECK(input.stats().dropped == 0);
    CHECK(input.stats().lastSource == "unit");
    CHECK(input.drain(out) == 0);

    SECTION("running status is kept per source across inject calls") {
        input.inject(Bytes{0xB0, 1}, "a");
        input.inject(Bytes{0xC0, 5}, "b");
        input.inject(Bytes{2}, "a"); // completes the CC from source "a"
        out.clear();
        REQUIRE(input.drain(out) == 2);
        CHECK(out[0].kind == MidiKind::ProgramChange);
        CHECK(out[0].source == "b");
        CHECK(out[1].kind == MidiKind::ControlChange);
        CHECK(out[1].data2 == 2);
        CHECK(out[1].source == "a");
        CHECK(input.stats().lastSource == "a");
    }
    SECTION("the queue limit drops the oldest messages") {
        input.setQueueLimit(4);
        Bytes many = {0xB0};
        for (std::uint8_t i = 0; i < 6; ++i) {
            many.push_back(i);
            many.push_back(1);
        }
        input.inject(many, "flood");
        out.clear();
        REQUIRE(input.drain(out) == 4);
        CHECK(out[0].data1 == 2);
        CHECK(out[3].data1 == 5);
        const auto stats = input.stats();
        CHECK(stats.messages == 9);
        CHECK(stats.dropped == 2);
        CHECK(stats.lastSource == "flood");
    }
    SECTION("drain appends to the output vector") {
        out.assign(1, MidiMessage{});
        input.inject(Bytes{0x90, 1, 1}, "x");
        CHECK(input.drain(out) == 1);
        CHECK(out.size() == 2);
    }
}

TEST_CASE("listMidiInputs does not crash", "[control][midi]") {
    const auto devices = listMidiInputs();
    for (const auto& d : devices) {
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.id.empty());
    }
    if (!hasMidiBackend()) {
        CHECK(devices.empty());
    }
}

TEST_CASE("MidiInput open reports missing backend or missing filter match", "[control][midi]") {
    MidiInput input;
    auto r = input.open("no-such-midi-source-avgen-xyz");
    CHECK_FALSE(r.has_value());
    CHECK_FALSE(input.isOpen());
    if (!hasMidiBackend()) {
        CHECK(r.error().message.find("no MIDI backend") != std::string::npos);
        MidiVirtualSource source;
        CHECK_FALSE(source.open(testSourceName("avgen-test-source")).has_value());
        CHECK_FALSE(source.isOpen());
    }
}

TEST_CASE("MidiInput receives from a virtual source through the OS", "[control][midi][device]") {
    if (!hasMidiBackend()) {
        SKIP("no MIDI backend on this platform");
    }
    const std::string name = testSourceName("avgen-test-source");
    MidiVirtualSource source;
    auto created = source.open(name);
    if (!created) {
        SKIP("cannot create a virtual MIDI source here: " << created.error().message);
    }
    REQUIRE(source.isOpen());

    // The new source is listed and flagged as virtual.
    bool listed = false;
    for (const auto& d : listMidiInputs()) {
        if (d.name == name) {
            listed = true;
            CHECK(d.virtualSource);
        }
    }
    CHECK(listed);

    MidiInput input;
    auto opened = input.open(name);
    REQUIRE(opened.has_value());
    CHECK(input.isOpen());
    const auto connected = input.connectedSources();
    REQUIRE(connected.size() == 1);
    CHECK(connected[0] == name);

    // CC 7 = 100 on MIDI channel 3 (index 2), a note on, and a sysex that must be dropped.
    const Bytes payload = {0xB2, 7, 100, 0x90, 60, 101, 0xF0, 0x7E, 0x01, 0xF7};
    REQUIRE(source.send(payload).has_value());

    std::vector<MidiMessage> got;
    for (int i = 0; i < 400 && got.size() < 2; ++i) {
        input.drain(got);
        if (got.size() < 2) {
            sleepMs(5);
        }
    }
    REQUIRE(got.size() == 2);
    CHECK(got[0].kind == MidiKind::ControlChange);
    CHECK(got[0].channel == 2);
    CHECK(got[0].data1 == 7);
    CHECK(got[0].data2 == 100);
    CHECK(got[0].source == name);
    CHECK(got[0].timestampNs > 0);
    CHECK(got[1].kind == MidiKind::NoteOn);
    CHECK(got[1].channel == 0);
    CHECK(got[1].data1 == 60);
    CHECK(got[1].data2 == 101);
    CHECK(got[1].source == name);
    CHECK(input.stats().messages == 2);
    CHECK(input.stats().lastSource == name);

    input.close();
    CHECK_FALSE(input.isOpen());
    CHECK(input.connectedSources().empty());
    source.close();
    CHECK_FALSE(source.isOpen());
    CHECK_FALSE(source.send(payload).has_value());
}

TEST_CASE("MidiInput with a wildcard filter auto-connects sources that appear later",
          "[control][midi][device]") {
    if (!hasMidiBackend()) {
        SKIP("no MIDI backend on this platform");
    }
    MidiInput input;
    REQUIRE(input.open("*").has_value());
    CHECK(input.isOpen());
    const auto before = input.connectedSources().size();

    const std::string name = testSourceName("avgen-test-hotplug");
    MidiVirtualSource source;
    auto created = source.open(name);
    if (!created) {
        SKIP("cannot create a virtual MIDI source here: " << created.error().message);
    }
    bool connected = false;
    for (int i = 0; i < 400 && !connected; ++i) {
        for (const auto& connectedName : input.connectedSources()) {
            connected = connected || connectedName == name;
        }
        if (!connected) {
            sleepMs(5);
        }
    }
    REQUIRE(connected);
    CHECK(input.connectedSources().size() == before + 1);

    REQUIRE(source.send(Bytes{0xE1, 0x00, 0x40}).has_value());
    std::vector<MidiMessage> got;
    for (int i = 0; i < 400; ++i) {
        input.drain(got);
        if (std::any_of(got.begin(), got.end(), [&](const MidiMessage& m) { return m.source == name; })) {
            break;
        }
        sleepMs(5);
    }
    auto it = std::find_if(got.begin(), got.end(), [&](const MidiMessage& m) { return m.source == name; });
    REQUIRE(it != got.end());
    CHECK(it->kind == MidiKind::PitchBend);
    CHECK(it->channel == 1);
    CHECK(it->value14 == 8192);

    // Removing the source disconnects it.
    source.close();
    bool gone = false;
    for (int i = 0; i < 400 && !gone; ++i) {
        gone = true;
        for (const auto& connectedName : input.connectedSources()) {
            gone = gone && connectedName != name;
        }
        if (!gone) {
            sleepMs(5);
        }
    }
    CHECK(gone);
}

// ADR-880: the CoreMIDI receive thread and the thread that opens, drains and destroys the input
// must be ordered by something the program itself owns. A sender keeps a virtual source busy
// while the main thread repeatedly connects an input (the connection is built here and first
// parsed into on CoreMIDI's thread), drains it, and destroys it with packets still in flight.
// Under ThreadSanitizer this reported the parser-state and inbox races before ADR-880.
TEST_CASE("MidiInput survives open, drain and destroy while a source is streaming",
          "[control][midi][device][adr880]") {
    if (!hasMidiBackend()) {
        SKIP("no MIDI backend on this platform");
    }
    const std::string name = testSourceName("avgen-test-churn");
    MidiVirtualSource source;
    auto created = source.open(name);
    if (!created) {
        SKIP("cannot create a virtual MIDI source here: " << created.error().message);
    }
    // The sender stops and joins on every exit from the test, a failed REQUIRE included.
    struct Sender {
        MidiVirtualSource& source;
        std::atomic<bool> stop{false};
        std::thread thread;
        explicit Sender(MidiVirtualSource& s) : source(s) {
            thread = std::thread([this] {
                std::uint8_t value = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    (void)source.send(Bytes{0xB0, 7, value, 0x90, 60, 100});
                    value = static_cast<std::uint8_t>((value + 1) & 0x7F);
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            });
        }
        ~Sender() {
            stop.store(true, std::memory_order_relaxed);
            thread.join();
        }
    };
    std::optional<Sender> sender;
    sender.emplace(source);

    int received = 0;
    for (int round = 0; round < 12; ++round) {
        MidiInput input;
        if (round % 3 == 2) {
            // Wildcard: the connection is also built by the hot-plug path on the client thread
            // for any source that appears, and every current source is connected here.
            REQUIRE(input.open("*").has_value());
        } else {
            REQUIRE(input.open(name).has_value());
        }
        std::vector<MidiMessage> got;
        for (int i = 0; i < 400 && got.empty(); ++i) {
            input.drain(got);
            if (got.empty()) {
                sleepMs(2);
            }
        }
        received += got.empty() ? 0 : 1;
        if (round % 2 == 1) {
            input.close(); // explicit close, then destruction
        }
        // Destroyed with the sender still streaming.
    }
    sender.reset();
    CHECK(received == 12);
}
