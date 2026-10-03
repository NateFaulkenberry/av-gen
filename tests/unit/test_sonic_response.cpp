// The response model and the per-note facts (ADR-1062): hits, levels and presence by kind, one conditioning chain
// with the performer's controls; voice slots, pitch-class lanes and per-note scalars, the same live and from a file.

#include "analysis/analysis_track.hpp"
#include "signals/signal_bus.hpp"
#include "params/parameter_set.hpp"
#include "sonic/interpret_source.hpp"
#include "sonic/live.hpp"
#include "sonic/notes.hpp"
#include "sonic/response.hpp"
#include "sonic/sonic_runtime.hpp"
#include "support/drum_kit.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

struct Run {
    std::vector<double> kicks, snares, hats, lows;
    double meanSustain = 0.0, meanBass = 0.0, meanTransient = 0.0, meanLevel = 0.0, meanHatRate = 0.0;
};

// Steps a runtime over a track and publishes at every analysis frame, as the engine's walk does.
Run run(const analysis::AnalysisTrack& track, const sonic::ResponseControls& controls = {}) {
    sonic::SonicSetup setup;
    setup.analyse(track);
    signals::SignalBus bus;
    sonic::SonicRuntime rt;
    rt.declare(bus);
    rt.setControls(controls);
    const double hop = 512.0 / 48000.0;
    const auto find = [&](const char* n) { return *bus.find(n); };
    const auto kick = find("response.kick"), snare = find("response.snare"), hat = find("response.hat"),
               low = find("response.low"), sustain = find("response.sustain"), bass = find("response.bass"),
               transient = find("response.transient"), level = find("response.level"),
               hatRate = find("response.hatRate");
    Run r;
    std::size_t n = 0;
    for (std::size_t i = 0; i < track.frames().size(); ++i) {
        const double t = track.frames()[i].timeSeconds;
        rt.step(setup, setup.timbre[i], hop);
        rt.publish(&setup, bus, t);
        if (bus.event(kick)) r.kicks.push_back(t);
        if (bus.event(snare)) r.snares.push_back(t);
        if (bus.event(hat)) r.hats.push_back(t);
        if (bus.event(low)) r.lows.push_back(t);
        if (t > 1.0) {
            r.meanSustain += bus.value(sustain);
            r.meanBass += bus.value(bass);
            r.meanTransient += bus.value(transient);
            r.meanLevel += bus.value(level);
            r.meanHatRate += bus.value(hatRate);
            ++n;
        }
        bus.clearEvents();
    }
    if (n > 0) {
        r.meanSustain /= double(n);
        r.meanBass /= double(n);
        r.meanTransient /= double(n);
        r.meanLevel /= double(n);
        r.meanHatRate /= double(n);
    }
    return r;
}

// The art agent's window (-30..+90 ms): a frame is centred on its window, and the kick is decided a hop after its
// low band peaks, the snare four hops after its attack (ADR-1067).
std::size_t near(const std::vector<double>& got, const std::vector<double>& want) {
    std::size_t hits = 0;
    for (const double w : want) {
        for (const double g : got) {
            if (g >= w - 0.03 && g <= w + 0.09) {
                ++hits;
                break;
            }
        }
    }
    return hits;
}

sonic::NoteEvent note(double start, double duration, int key, float velocity) {
    sonic::NoteEvent e;
    e.start = start;
    e.duration = duration;
    e.key = static_cast<std::uint8_t>(key);
    e.pitch = static_cast<float>(key);
    e.velocity = velocity;
    return e;
}

} // namespace

TEST_CASE("Response hits are the kit's parts, and transient sensitivity decides how many fire", "[sonic][adr1062]") {
    // The drums and the pad (a bright bass line reads as kicks: ADR-1067; test_drum_recall.cpp has the mixes).
    const testsupport::Kit kit = testsupport::makeKit(8, true, false, true);
    const auto track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    const Run neutral = run(track);
    INFO("kicks " << neutral.kicks.size() << "/" << kit.kicks.size() << " snares " << neutral.snares.size() << "/"
                  << kit.snares.size() << " hats " << neutral.hats.size() << "/" << kit.hats.size());
    CHECK(near(neutral.kicks, kit.kicks) >= kit.kicks.size() * 95 / 100);
    CHECK(neutral.kicks.size() <= kit.kicks.size() + 1);
    CHECK(near(neutral.snares, kit.snares) >= kit.snares.size() * 95 / 100);
    CHECK(near(neutral.hats, kit.hats) >= kit.hats.size() * 85 / 100);
    CHECK(neutral.meanHatRate > 0.1);

    sonic::ResponseControls off;
    off.transient = 0.0f;
    off.sensitivity = 0.5f;
    const Run none = run(track, off);
    CHECK(none.kicks.empty());
    CHECK(none.hats.empty());
    CHECK(none.meanTransient == 0.0);
    // The levels do not move with Transient: the sustain and bass channels are the same.
    CHECK(none.meanBass == Approx(neutral.meanBass).margin(1e-6));
    CHECK(none.meanSustain == Approx(neutral.meanSustain).margin(1e-6));
}

TEST_CASE("A pad reads sustained, drums read transient", "[sonic][adr1062]") {
    const auto pad = analysis::AnalysisTrack::analyze(testsupport::makeKit(6, false, false, true).file,
                                                      analysis::AnalyzerConfig{});
    const auto drums = analysis::AnalysisTrack::analyze(testsupport::makeKit(6, true, false, false).file,
                                                        analysis::AnalyzerConfig{});
    const Run p = run(pad);
    const Run d = run(drums);
    INFO("pad sustain " << p.meanSustain << " transient " << p.meanTransient << "; drums sustain " << d.meanSustain
                        << " transient " << d.meanTransient);
    CHECK(p.meanSustain > 0.2);
    CHECK(p.meanSustain > 2.0 * d.meanSustain);
    CHECK(d.meanTransient > 3.0 * p.meanTransient);
    CHECK(p.kicks.empty());
    CHECK(p.snares.empty());
}

TEST_CASE("Sensitivity moves the floor, never multiplies the top", "[sonic][adr1062]") {
    const sonic::ResponseSettings s;
    for (const float sens : {0.2f, 0.5f, 0.8f, 1.0f}) {
        INFO(sens);
        CHECK(sonic::conditionLevel(0.0f, sens, s) <= 1.0f); // full scale: never past 1
        CHECK(sonic::conditionLevel(20.0f, sens, s) == 1.0f);
        CHECK(sonic::conditionLevel(-130.0f, sens, s) == 0.0f); // silence is silent at any sensitivity
    }
    // A quiet sound is lifted by sensitivity rather than scaled: -40 dBFS.
    const float low = sonic::conditionLevel(-40.0f, 0.2f, s);
    const float mid = sonic::conditionLevel(-40.0f, 0.5f, s);
    const float high = sonic::conditionLevel(-40.0f, 0.9f, s);
    CHECK(low < mid);
    CHECK(mid < high);
    CHECK(sonic::conditionLevel(-40.0f, 0.0f, s) == 0.0f); // 0: the kind is off
    // Overall sensitivity moves both kinds; the two kinds move one each.
    sonic::ResponseControls c;
    c.sensitivity = 0.8f;
    c.transient = 0.3f;
    CHECK(sonic::hitSensitivity(c) == Approx(0.6f));
    CHECK(sonic::levelSensitivity(c) == Approx(0.8f));
}

TEST_CASE("A quieter take keeps its hits and reads quieter levels", "[sonic][adr1062]") {
    // The same kit 12 dB down: the hits are level-free, the levels are not (a quiet passage looks quiet).
    const auto loud = analysis::AnalysisTrack::analyze(testsupport::makeKit(6).file, analysis::AnalyzerConfig{});
    const auto quiet = analysis::AnalysisTrack::analyze(
        testsupport::makeKit(6, true, true, true, 124.0, 0.2f).file, analysis::AnalyzerConfig{});
    const Run a = run(loud);
    const Run b = run(quiet);
    const testsupport::Kit qk = testsupport::makeKit(6, true, true, true, 124.0, 0.2f);
    INFO("quiet kicks on bass " << near(b.kicks, qk.bass) << " on kicks " << near(b.kicks, qk.kicks) << " on snares " << near(b.kicks, qk.snares) << " on hats " << near(b.kicks, qk.hats));
    INFO("kicks " << a.kicks.size() << " vs " << b.kicks.size() << "; level " << a.meanLevel << " vs " << b.meanLevel);
    CHECK(std::abs(static_cast<int>(a.kicks.size()) - static_cast<int>(b.kicks.size())) <= 2);
    CHECK(b.meanLevel < a.meanLevel - 0.05);
}

TEST_CASE("Voice slots: one note owns one slot for its life, the lowest free one", "[sonic][adr1062]") {
    sonic::NoteTrack t;
    t.notes = {note(0.0, 2.0, 60, 0.8f), note(0.0, 2.0, 64, 0.7f), note(0.0, 0.5, 67, 0.6f), note(1.0, 0.5, 72, 0.9f),
               note(2.5, 1.0, 48, 0.5f)};
    t.finish();
    REQUIRE(t.notes.size() == 5);
    CHECK(t.notes[0].voice == 0); // C4
    CHECK(t.notes[1].voice == 1); // E4
    CHECK(t.notes[2].voice == 2); // G4, ends at 0.5
    CHECK(t.notes[3].voice == 2); // C5 at 1.0 takes the slot G4 freed
    CHECK(t.notes[4].voice == 0); // C3 after everything ended

    const sonic::NoteFacts f = sonic::noteFactsAt(t, 1.2);
    CHECK(f.voices[0].held);
    CHECK(f.voices[0].pitch == 60.0f);
    CHECK(f.voices[2].held);
    CHECK(f.voices[2].pitch == 72.0f);
    CHECK(f.voices[2].age == Approx(0.2));
    CHECK_FALSE(f.voices[3].held);
    CHECK(f.lastPitch == 72.0f);
    CHECK(f.interval == Approx(72.0f - 67.0f)); // from the latest onset at another time (the chord's top note)
    CHECK(f.lowest == 60.0f);
    CHECK(f.highest == 72.0f);
    CHECK(f.pitchClass[0] == Approx(0.9f)); // C4 and C5 sound: the loudest
    CHECK(f.pitchClass[4] == Approx(0.7f));
    CHECK(f.pitchClass[7] == 0.0f); // G4 has ended
    CHECK(f.held == Approx(1.2));

    const sonic::NoteFactEvents e = sonic::noteFactEventsBetween(t, 0.9, 1.1);
    CHECK(e.voiceOn[2]);
    CHECK(e.classOn[0]);
    CHECK(e.high);
    CHECK_FALSE(e.low);
    const sonic::NoteFactEvents off = sonic::noteFactEventsBetween(t, 0.4, 0.6);
    CHECK(off.release);
    CHECK(off.releaseSeconds == Approx(0.5f));
}

TEST_CASE("Live notes get the slots the file gives the same notes", "[sonic][adr1062]") {
    sonic::NoteTrack file;
    file.notes = {note(0.0, 2.0, 60, 0.8f), note(0.1, 0.4, 64, 0.7f), note(0.6, 1.0, 67, 0.6f),
                  note(0.7, 0.2, 71, 0.9f), note(1.0, 0.5, 74, 0.5f)};
    file.finish();
    sonic::LiveNotes live;
    // Play them in time order, refreshing the track between events as the engine does every frame.
    struct Ev {
        double t;
        bool on;
        int key;
        float v;
    };
    std::vector<Ev> evs;
    for (const auto& n : file.notes) {
        evs.push_back({n.start, true, n.key, n.velocity});
        evs.push_back({n.end(), false, n.key, 0.0f});
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    for (const Ev& e : evs) {
        live.at(e.t);
        if (e.on) {
            live.noteOn(e.t, 0, static_cast<std::uint8_t>(e.key), e.v);
        } else {
            live.noteOff(e.t, 0, static_cast<std::uint8_t>(e.key));
        }
    }
    const auto& got = live.at(2.5).notes;
    REQUIRE(got.size() == file.notes.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        INFO(i);
        CHECK(got[i].key == file.notes[i].key);
        CHECK(got[i].voice == file.notes[i].voice);
    }
}

TEST_CASE("The response and per-note signals are appended after every older Sonic id", "[sonic][adr1062]") {
    signals::SignalBus bus;
    sonic::SonicRuntime rt;
    rt.declare(bus);
    const auto phrase = bus.find("notes.phrase");
    const auto first = bus.find("notes.lastPitch");
    REQUIRE(phrase);
    REQUIRE(first);
    CHECK(*first == *phrase + 1);
    for (const char* n : {"response.kick", "response.kickEnv", "response.note", "response.sustain", "response.bass",
                          "notes.voice.7.on", "notes.class.11", "notes.classOn.0", "notes.release", "notes.held"}) {
        INFO(n);
        CHECK(bus.find(n).has_value());
    }
}

TEST_CASE("An interpreter mapping keeps a bias and gain past the old clamps (a narrow register bump)",
          "[sonic][adr1062]") {
    sonic::InterpretSource src("place");
    REQUIRE(src.settingsFromJson(nlohmann::json::parse(R"({"mappings": [
        {"name": "near", "inputs": [{"signal": "notes.lastPitch"}], "bias": -20.0, "gain": 33.0}]})"))
                .has_value());
    signals::SignalBus bus;
    params::ParameterSet params;
    src.attach(bus, params);
    auto* bias = params.findAs<float>("sources/place/near/bias");
    auto* gain = params.findAs<float>("sources/place/near/gain");
    REQUIRE(bias != nullptr);
    REQUIRE(gain != nullptr);
    CHECK(bias->value() == -20.0f); // was clamped to -4
    CHECK(gain->value() == 33.0f);  // was clamped to 16
    // The bump: 0 well below c = 0.6, 1 at it.
    CHECK(sonic::InterpretSource::shape(0.5f, -20.0f, 33.0f, 1.0f) == 0.0f);
    CHECK(sonic::InterpretSource::shape(0.637f, -20.0f, 33.0f, 1.0f) == Approx(1.0f).margin(0.03f));
}
