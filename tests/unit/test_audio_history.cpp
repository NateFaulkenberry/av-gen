// ADR-1116: the audio history audio fields read, its builder, and the audio field kinds on the CPU.
#include "analysis/audio_history_builder.hpp"
#include "scene/field_params.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/field.hpp"
#include "support/groove.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A whole-track history whose row r, bin b holds a value that is easy to predict.
std::shared_ptr<const spatial::AudioHistory> rampHistory(int rows, double rate = 100.0) {
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins);
    for (int r = 0; r < rows; ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            data[static_cast<std::size_t>(r) * spatial::kAudioBins + static_cast<std::size_t>(b)] =
                static_cast<float>(r % 100) / 100.0f + static_cast<float>(b) / 1000.0f;
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    onsets[0] = {{1.0, 1.0f}, {2.0, 0.5f}, {0.5, 0.25f}}; // unsorted on purpose
    onsets[3] = {{1.5, 1.0f}};
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(rate, 0.0, std::move(data), std::move(onsets)));
}

} // namespace

TEST_CASE("An offline audio history is a pure function of the transport second", "[unit][audio-fields]") {
    const auto h = rampHistory(1000);
    CHECK(h->newestRow(-0.01) == -1);
    CHECK(h->newestRow(0.0) == 0);
    CHECK(h->newestRow(2.345) == 234);
    CHECK(h->newestRow(1e6) == 999); // clamped to the last row
    CHECK(h->value(5, 3) == Approx(0.05f + 0.003f));
    CHECK(h->value(-1, 0) == 0.0f);
    CHECK(h->value(1000, 0) == 0.0f);

    std::array<float, 4> ages{};
    std::array<float, 4> strengths{};
    ages.fill(-1.0f);
    REQUIRE(h->lastOnsets(spatial::OnsetSource::Low, 1.2, ages, strengths) == 2);
    CHECK(ages[0] == Approx(0.2f)); // newest first: the onset at 1.0
    CHECK(strengths[0] == Approx(1.0f));
    CHECK(ages[1] == Approx(0.7f)); // then 0.5
    CHECK(ages[2] == -1.0f);
    CHECK(h->lastOnsets(spatial::OnsetSource::Mid, 10.0, ages, strengths) == 0);
    // An onset at exactly the clock has happened (age 0).
    REQUIRE(h->lastOnsets(spatial::OnsetSource::Beat, 1.5, ages, strengths) == 1);
    CHECK(ages[0] == 0.0f);
}

TEST_CASE("spectrumAt interpolates in row and bin and reads 0 beyond the ring", "[unit][audio-fields]") {
    const auto h = rampHistory(3000);
    const double clock = 20.0; // newest row 2000, value 0.00 + b/1000
    REQUIRE(h->newestRow(clock) == 2000);
    // No delay, bin 0: row 2000 -> (2000 % 100) / 100 = 0.
    CHECK(spatial::spectrumAt(*h, clock, 0.0f, 0.0f) == Approx(0.0f).margin(1e-6));
    // 0.255 s late = 25.5 rows back: halfway between rows 1975 (0.75) and 1974 (0.74).
    CHECK(spatial::spectrumAt(*h, clock, 0.255f, 0.0f) == Approx(0.745f).margin(1e-4));
    // The top bin.
    CHECK(spatial::spectrumAt(*h, clock, 0.0f, 1.0f) == Approx(0.063f).margin(1e-5));
    // A mean over bins 0..63 at no delay: 0 + mean(b)/1000 = 0.0315.
    CHECK(spatial::spectrumRange(*h, clock, 0.0f, 0.0f, 1.0f) == Approx(0.0315f).margin(1e-5));
    // Older than the ring (1536 rows): silence, exactly as the GPU ring holds nothing there.
    CHECK(spatial::spectrumAt(*h, clock, 16.0f, 0.5f) == 0.0f);
    // Before the song: silence.
    CHECK(spatial::spectrumAt(*h, -1.0, 0.0f, 0.5f) == 0.0f);
}

TEST_CASE("onsetResponse sums decaying fronts", "[unit][audio-fields]") {
    const std::array<float, 3> ages{0.5f, 2.0f, -1.0f};
    const std::array<float, 3> strengths{1.0f, 0.5f, 9.0f};
    // A flash (width 0): every element at once, decaying.
    CHECK(spatial::onsetResponse(ages, strengths, 100.0f, 10.0f, 0.0f, 1.0f) ==
          Approx(std::exp(-0.5f) + 0.5f * std::exp(-2.0f)));
    // A front: the first onset's front is at 5 m; an element there gets the full first term.
    const float atFront = spatial::onsetResponse(ages, strengths, 5.0f, 10.0f, 1.0f, 0.0f);
    CHECK(atFront == Approx(1.0f + 0.5f * std::exp(-225.0f)).margin(1e-5));
    // Far from both fronts: nothing.
    CHECK(spatial::onsetResponse(ages, strengths, 60.0f, 10.0f, 1.0f, 0.0f) == Approx(0.0f).margin(1e-6));
}

TEST_CASE("A live audio history holds rows forward, keeps two rings and restarts on a clock jump back",
          "[unit][audio-fields]") {
    auto h = spatial::AudioHistory::livePlaceholder(100.0);
    std::array<float, spatial::kAudioBins> row{};
    row.fill(0.25f);
    h.appendLive(1.0, row); // row 100
    row.fill(0.75f);
    h.appendLive(1.05, row); // rows 101..105 all 0.75 (held forward)
    CHECK(h.newestRow(123.0) == 105); // the live clock is its own, not the transport's
    CHECK(h.now(123.0) == Approx(1.05));
    CHECK(h.value(100, 0) == 0.25f);
    CHECK(h.value(103, 7) == 0.75f);
    for (int i = 0; i < 5000; ++i) {
        h.appendLive(1.05 + 0.01 * (i + 1), row);
    }
    CHECK(h.rowCount() - (h.newestRow(0.0) + 1) == 0);
    CHECK(h.value(h.newestRow(0.0) - 2 * spatial::kAudioRingRows, 0) == 0.0f); // dropped
    const std::uint64_t revision = h.revision();
    h.appendLive(0.5, row); // the input restarted
    CHECK(h.revision() > revision);
    CHECK(h.newestRow(0.0) == 50);
}

TEST_CASE("The track builder folds spectra onto 64 log bands and lists kicks and beats", "[unit][audio-fields]") {
    testsupport::GrooveSpec spec;
    spec.bars = 8;
    const auto groove = testsupport::makeGroove(spec);
    const auto track = analysis::AnalysisTrack::analyze(groove.file, analysis::AnalyzerConfig{});
    const spatial::AudioHistory h = analysis::buildAudioHistory(track);
    REQUIRE(h.rowCount() == static_cast<std::int64_t>(track.frames().size()));
    CHECK(h.rowRate() == Approx(93.75));
    // Every value is stretched into [0, 1].
    float lo = 1.0f;
    float hi = 0.0f;
    for (std::int64_t r = 0; r < h.rowCount(); ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            lo = std::min(lo, h.value(r, b));
            hi = std::max(hi, h.value(r, b));
        }
    }
    CHECK(lo >= 0.0f);
    CHECK(hi <= 1.0f);
    CHECK(hi > 0.9f);
    // The silent lead-in is dark.
    CHECK(spatial::spectrumRange(h, 0.2, 0.0f, 0.0f, 1.0f) < 0.1f);
    // Beats are the tracker's; band onsets are the track's (ADR-898).
    CHECK(h.onsets(spatial::OnsetSource::Beat).size() == track.beats().beatTimes.size());
    std::size_t lowOnsets = 0;
    for (const auto& f : track.frames()) {
        lowOnsets += f.lowOnset ? 1u : 0u;
    }
    CHECK(h.onsets(spatial::OnsetSource::Low).size() == lowOnsets);
    CHECK(lowOnsets > 0);
}

TEST_CASE("Audio fields sample the history at their delay, band and distance on the CPU", "[unit][audio-fields]") {
    spatial::FieldSet set;
    set.audio = rampHistory(3000);
    spatial::FieldSpec f;
    f.name = "echo";
    f.kind = spatial::FieldKind::Spectrum;
    f.audioBand = spatial::AudioBand::Element;
    f.bandLow = 0.0f;
    f.bandHigh = 1.0f;
    f.audioSpeed = 10.0f;           // 10 m/s
    f.waveGeometry = spatial::WaveGeometry::Spherical;
    f.falloff.kind = spatial::FalloffKind::None;
    REQUIRE(f.validate().has_value());
    set.fields.push_back(f);
    // An element 2.55 m out hears the song 0.255 s late; element 0 hears bin 0.
    CHECK(spatial::sampleScalar(f, glm::vec3(2.55f, 0.0f, 0.0f), 20.0, &set, 0.0f) == Approx(0.745f).margin(1e-4));
    // The same element, element random 1: the top bin.
    CHECK(spatial::sampleScalar(f, glm::vec3(2.55f, 0.0f, 0.0f), 20.0, &set, 1.0f) ==
          Approx(0.745f + 0.063f).margin(1e-4));
    // No history: silence.
    spatial::FieldSet quiet;
    quiet.fields.push_back(f);
    CHECK(spatial::sampleScalar(f, glm::vec3(1.0f), 20.0, &quiet, 0.5f) == 0.0f);

    spatial::FieldSpec o;
    o.name = "kick";
    o.kind = spatial::FieldKind::Onset;
    o.onsetSource = spatial::OnsetSource::Low;
    o.onsetDecay = 0.0f;
    o.onsetWidth = 0.0f;
    o.falloff.kind = spatial::FalloffKind::None;
    set.fields.push_back(o);
    // At t = 2.5 every low onset so far has happened: strengths 0.25 + 1 + 0.5 with no decay.
    CHECK(spatial::sampleScalar(o, glm::vec3(3.0f), 2.5, &set) == Approx(1.75f));
}

TEST_CASE("Audio field parameters round-trip through JSON and register only for audio kinds", "[unit][audio-fields]") {
    spatial::FieldSpec f;
    f.name = "rings";
    f.kind = spatial::FieldKind::Spectrum;
    f.audioBand = spatial::AudioBand::Angle;
    f.bandLow = 0.1f;
    f.bandHigh = 0.8f;
    f.bandRepeat = 3;
    f.audioDelay = 0.4f;
    f.audioSpeed = 9.0f;
    const nlohmann::json j = f.toJson();
    CHECK(j.at("audioBand") == "angle");
    auto back = spatial::FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->audioBand == spatial::AudioBand::Angle);
    CHECK(back->bandRepeat == 3);
    CHECK(back->audioSpeed == Approx(9.0f));
    CHECK(back->structuralHash() == f.structuralHash());

    spatial::FieldSpec plain;
    plain.name = "plain";
    CHECK_FALSE(plain.toJson().contains("audioBand")); // every other field's file is unchanged

    params::ParameterSet params;
    const auto p = scene::registerFieldParameters(params, f, "field/rings/");
    CHECK(params.find("field/rings/audioSpeed") != nullptr);
    const auto q = scene::registerFieldParameters(params, plain, "field/plain/");
    CHECK(params.find("field/plain/audioSpeed") == nullptr);

    spatial::FieldSpec bad = f;
    bad.bandHigh = 1.5f;
    CHECK_FALSE(bad.validate().has_value());
}
