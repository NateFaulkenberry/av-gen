// stems-multichannel M2: stem keys, the stem file check, and the stem.<key>.* vocabulary.

#include "analysis/causal_onsets.hpp"
#include "analysis/stem_analysis.hpp"
#include "audio/audio_file.hpp"
#include "audio/stem_source.hpp"
#include "signals/stem_signals.hpp"
#include "support/stem_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
using audio::StemSeverity;

namespace {

bool has(const audio::StemReport& r, StemSeverity s, const std::string& fragment) {
    for (const auto& d : r.diagnostics) {
        if (d.severity == s && d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

audio::StemReport check(const testsupport::StemFixture& fx, const std::string& name, double offset = 0.0) {
    audio::StemSource s{.key = "x", .label = "X", .file = fx.file(name), .offsetSeconds = offset};
    audio::StemFileFacts facts;
    auto track = analysis::StemTrack::analyzeFile(s.file, {}, facts);
    const audio::MixFacts mix{.sampleRate = 48000, .durationSeconds = 20.0};
    auto r = audio::checkStem(s, track ? &facts : nullptr, track ? "" : track.error().message);
    for (auto& d : audio::mixDiagnostics(s, r, mix)) {
        r.diagnostics.push_back(std::move(d));
    }
    return r;
}

} // namespace

TEST_CASE("stem keys: made from the label once, collision-free, and only [a-z0-9_]", "[stems][signals]") {
    std::vector<std::string> taken;
    CHECK(audio::makeStemKey("Drums", taken) == "drums");
    CHECK(audio::makeStemKey("Synth Lead (L)", taken) == "synth_lead_l");
    CHECK(audio::makeStemKey("  --  ", taken) == "stem");
    CHECK(audio::makeStemKey("Kick/Drum Bus", taken) == "kick_drum_bus");
    taken = {"drums", "drums_2"};
    CHECK(audio::makeStemKey("DRUMS", taken) == "drums_3");
    CHECK(audio::validStemKey("bass_2"));
    CHECK_FALSE(audio::validStemKey("bass.rms")); // a key cannot forge a field name
    CHECK_FALSE(audio::validStemKey(""));
    CHECK_FALSE(audio::validStemKey("Bass"));
}

TEST_CASE("stem check: each problem is reported against the file that has it", "[stems][signals]") {
    const auto& fx = testsupport::requireStemFixture();
    const auto good = check(fx, "drums.wav");
    CHECK(good.ok());
    CHECK(good.diagnostics.empty());
    CHECK(good.format == "f32");
    CHECK(check(fx, "bass.wav").format == "s16");
    CHECK(check(fx, "synth.wav").format == "s24");

    const auto corrupt = check(fx, "corrupt.wav");
    CHECK_FALSE(corrupt.ok());
    CHECK(has(corrupt, StemSeverity::Error, "corrupt.wav"));

    const auto silent = check(fx, "silent.wav");
    CHECK(silent.ok());
    CHECK(silent.silent);
    CHECK(has(silent, StemSeverity::Warning, "silent"));

    const auto shortStem = check(fx, "drums_short.wav");
    CHECK(has(shortStem, StemSeverity::Warning, "drums_short.wav"));

    const auto rate = check(fx, "synth_44k1.wav");
    CHECK(rate.ok());
    CHECK(has(rate, StemSeverity::Info, "44100 Hz"));
    CHECK_FALSE(rate.hasError());

    CHECK(has(check(fx, "drums.wav", 25.0), StemSeverity::Warning, "wholly outside"));
    // A matching length with an offset is NOT called aligned: the span it covers is reported.
    CHECK(has(check(fx, "bass_late.wav", -0.25), StemSeverity::Warning, "-0.250"));

    std::vector<float> bad(4800, 0.1f);
    bad[100] = std::numeric_limits<float>::quiet_NaN();
    const auto nan = audio::AudioFile::fromInterleaved(bad, 1, 48000);
    const auto facts = audio::factsOf(nan);
    const auto r = audio::checkStem({.key = "n", .label = "N", .file = "n.wav"}, &facts, "");
    CHECK_FALSE(r.ok());
    CHECK(r.nonFinite == 1);
    CHECK(has(r, StemSeverity::Error, "not finite"));
}

TEST_CASE("stem signals: one stable vocabulary per key; relabel moves no id; inactive is zero with no event",
          "[stems][signals]") {
    signals::SignalBus bus;
    const auto drums = signals::StemSignals::declare(bus, "drums", "Drums", audio::SourceRole::Drums);
    const auto names = signals::StemSignals::names("drums", audio::SourceRole::Drums);
    REQUIRE(names.size() == 16); // ADR-1604 added onsetLow, onsetMid, onsetHigh; ADR-1784 cut kick, snare, hat
    for (const auto& n : names) {
        CHECK(bus.find(n).has_value());
    }
    CHECK(bus.info(drums.onset).isEvent);
    CHECK_FALSE(bus.find("stem.drums.kick").has_value()); // ADR-1784: no source names an instrument
    CHECK_FALSE(bus.info(drums.level).isEvent);
    CHECK(bus.info(drums.level).label == "Drums: level (against its own loudness)");

    const auto again = signals::StemSignals::declare(bus, "drums", "Drum Bus", audio::SourceRole::Drums);
    CHECK(again.ids() == drums.ids());
    CHECK(bus.info(drums.level).label == "Drum Bus: level (against its own loudness)");
    const auto bass = signals::StemSignals::declare(bus, "bass", "Bass", audio::SourceRole::Bass);
    CHECK(bass.rms != drums.rms);

    analysis::StemFrame f;
    f.rms = 0.5f;
    f.level = 0.8f;
    f.onset = true;
    f.onsetStrength = 3.0f;
    f.bands[0] = 0.9f;
    drums.publish(bus, f);
    CHECK(bus.value(drums.active) == 1.0f);
    CHECK(bus.value(drums.rms) == 0.5f);
    CHECK(bus.event(drums.onset));
    CHECK(bus.value(drums.bands[0]) == 0.9f);
    CHECK(bus.value(bass.rms) == 0.0f); // nothing crosses to another key
    drums.publish(bus, f, false);
    CHECK_FALSE(bus.event(drums.onset));
    CHECK(bus.value(drums.rms) == 0.5f);
    drums.publish(bus, f);
    drums.publishInactive(bus);
    for (const auto id : drums.ids()) {
        CHECK(bus.value(id) == 0.0f);
        CHECK_FALSE(bus.event(id));
    }
}

// ADR-1604: the register of an onset: how the power its hop gained divides between low, mid and high.
TEST_CASE("onset register: the low, mid and high shares of an attack's gained power", "[stems][signals][adr1604]") {
    const float binHz = 48000.0f / 2048.0f; // 23.4 Hz
    std::vector<float> before(1025, 0.1f);
    std::vector<float> now = before;
    CHECK(analysis::registerShares(now, before, binHz) == std::array<float, 3>{0.0f, 0.0f, 0.0f}); // nothing gained
    for (std::size_t k = 2; k < 10; ++k) {
        now[k] = 1.0f; // 47-211 Hz: a kick's body
    }
    auto r = analysis::registerShares(now, before, binHz);
    CHECK(r[0] == 1.0f);
    CHECK(r[1] == 0.0f);
    now = before;
    for (std::size_t k = 200; k < 600; ++k) {
        now[k] = 0.5f; // 4.7-14 kHz: a hat
    }
    r = analysis::registerShares(now, before, binHz);
    CHECK(r[2] == 1.0f);
    // Energy-weighted: a loud body under a faint broadband click reads low.
    now = before;
    for (std::size_t k = 2; k < 10; ++k) {
        now[k] = 2.0f;
    }
    for (std::size_t k = 10; k < 683; ++k) {
        now[k] = 0.15f;
    }
    r = analysis::registerShares(now, before, binHz);
    CHECK(r[0] > 0.6f);
    CHECK(r[0] + r[1] + r[2] > 0.999f);
    // A band that lost power does not take a negative share.
    now = before;
    now[3] = 0.0f;
    now[300] = 0.6f;
    r = analysis::registerShares(now, before, binHz);
    CHECK(r[0] == 0.0f);
    CHECK(r[2] == 1.0f);
}
