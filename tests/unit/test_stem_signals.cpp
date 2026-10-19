// stems-multichannel M2: stem keys, the stem file check, and the stem.<key>.* vocabulary.

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
    auto file = audio::AudioFile::load(s.file);
    const audio::MixFacts mix{.sampleRate = 48000, .durationSeconds = 20.0};
    auto r = audio::checkStem(s, file ? &*file : nullptr, file ? "" : file.error().message);
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
    const auto r = audio::checkStem({.key = "n", .label = "N", .file = "n.wav"}, &nan, "");
    CHECK_FALSE(r.ok());
    CHECK(r.nonFinite == 1);
    CHECK(has(r, StemSeverity::Error, "not finite"));
}

TEST_CASE("stem signals: one stable vocabulary per key; relabel moves no id; inactive is zero with no event",
          "[stems][signals]") {
    signals::SignalBus bus;
    const auto drums = signals::StemSignals::declare(bus, "drums", "Drums");
    const auto names = signals::StemSignals::names("drums");
    REQUIRE(names.size() == 16);
    for (const auto& n : names) {
        CHECK(bus.find(n).has_value());
    }
    CHECK(bus.info(drums.onset).isEvent);
    CHECK(bus.info(drums.kick).isEvent);
    CHECK_FALSE(bus.info(drums.level).isEvent);
    CHECK(bus.info(drums.level).label == "Drums: level (against its own loudness)");

    const auto again = signals::StemSignals::declare(bus, "drums", "Drum Bus");
    CHECK(again.ids() == drums.ids());
    CHECK(bus.info(drums.level).label == "Drum Bus: level (against its own loudness)");
    const auto bass = signals::StemSignals::declare(bus, "bass", "Bass");
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
