// ADR-1361: every reader of the audio history hears it through the performer's Response controls (`sonic/response/*`,
// ADR-1062) -- the spectrum and onset fields, the GPU ring, THE ASTRAL FORGE's two conductors -- and the neutral
// controls are the identity, exactly, so a project that never moves the sliders reads what it always read.

#include "scene/astral_conductor.hpp"
#include "scene/astral_forge.hpp"
#include "scene/astral_song.hpp"
#include "sonic/response.hpp"
#include "spatial/audio_history.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

spatial::AudioHistory sampleHistory() {
    std::vector<float> rows(200 * spatial::kAudioBins);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i] = static_cast<float>((i * 37u) % 101u) / 100.0f; // 0..1, every bin different
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    onsets[0] = {{0.2, 0.9f}, {0.6, 0.2f}, {1.0, 0.15f}, {1.4, 0.5f}};
    onsets[3] = {{0.5, 1.0f}, {1.0, 1.0f}};
    return spatial::AudioHistory::whole(100.0, 0.0, std::move(rows), std::move(onsets));
}

spatial::AudioResponse responseOf(const sonic::ResponseControls& c) {
    spatial::AudioResponse r;
    r.hitSensitivity = sonic::hitSensitivity(c);
    r.levelSensitivity = sonic::levelSensitivity(c);
    r.attack = c.attack;
    r.release = c.release;
    return r;
}

struct Onsets {
    int n = 0;
    std::array<float, spatial::kOnsetHistory> ages{};
    std::array<float, spatial::kOnsetHistory> strengths{};
};

Onsets lastLow(const spatial::AudioHistory& h, double clock) {
    Onsets o;
    o.ages.fill(-1.0f);
    o.n = h.lastOnsets(spatial::OnsetSource::Low, clock, o.ages, o.strengths);
    return o;
}

} // namespace

TEST_CASE("Neutral Response controls are the identity on the audio history", "[audio][fields][adr1361]") {
    const spatial::AudioHistory raw = sampleHistory();
    spatial::AudioHistory heard = sampleHistory();
    const auto revision = heard.revision();
    heard.setResponse(responseOf(sonic::ResponseControls{}));
    CHECK(heard.response().neutral());
    CHECK(heard.revision() == revision); // nothing handed out changed meaning
    for (std::int64_t row = 0; row < raw.rowCount(); ++row) {
        for (int bin = 0; bin < spatial::kAudioBins; ++bin) {
            REQUIRE(heard.value(row, bin) == raw.value(row, bin)); // bit for bit
        }
    }
    const Onsets a = lastLow(raw, 2.0);
    const Onsets b = lastLow(heard, 2.0);
    REQUIRE(a.n == 4);
    REQUIRE(b.n == a.n);
    CHECK(a.ages == b.ages);
    CHECK(a.strengths == b.strengths);
    CHECK(spatial::onsetDecayScale(heard.response()) == 1.0f);
    CHECK(spatial::spectrumRange(heard, 1.5, 0.0f, 0.1f, 0.9f) == spatial::spectrumRange(raw, 1.5, 0.0f, 0.1f, 0.9f));
}

TEST_CASE("Transients decide which onsets the fields hear, and how hard", "[audio][fields][adr1361]") {
    spatial::AudioHistory h = sampleHistory();
    const Onsets neutral = lastLow(h, 2.0);

    sonic::ResponseControls low;
    low.transient = 0.2f; // hit sensitivity 0.2: an onset under 0.36 is not heard
    const auto revision = h.revision();
    h.setResponse(responseOf(low));
    CHECK(h.revision() > revision); // a GPU ring holding the old meaning refills
    const Onsets quiet = lastLow(h, 2.0);
    REQUIRE(quiet.n == 2); // the 0.2 and the 0.15 are gone ...
    CHECK(quiet.ages[0] == Approx(0.6f)); // ... and the next ones are the 0.5 (newest) and the 0.9 behind them
    CHECK(quiet.ages[1] == Approx(1.8f));
    CHECK(quiet.strengths[0] < 0.5f);
    CHECK(quiet.strengths[1] < 0.9f);

    sonic::ResponseControls high;
    high.transient = 0.9f;
    h.setResponse(responseOf(high));
    const Onsets loud = lastLow(h, 2.0);
    REQUIRE(loud.n == neutral.n);
    for (int k = 0; k < loud.n; ++k) {
        CHECK(loud.strengths[static_cast<std::size_t>(k)] > neutral.strengths[static_cast<std::size_t>(k)]);
    }
    // The beats are not hits a performer can lose: the history's beat lane is read raw by its readers.
    CHECK(h.onsets(spatial::OnsetSource::Beat).size() == 2);

    // Sensitivity 0 hears no hit at all.
    sonic::ResponseControls none;
    none.sensitivity = 0.0f;
    none.transient = 0.5f;
    h.setResponse(responseOf(none));
    CHECK(lastLow(h, 2.0).n == 0);
}

TEST_CASE("Sustain curves the spectrogram the fields hear; Release lengthens an onset's decay", "[audio][fields][adr1361]") {
    spatial::AudioHistory h = sampleHistory();
    const std::int64_t row = 50;
    const int bin = 7;
    const float raw = h.value(row, bin);
    REQUIRE(raw > 0.05f);
    REQUIRE(raw < 0.95f);
    float last = -1.0f;
    for (const float sustain : {0.15f, 0.3f, 0.5f, 0.7f, 0.9f}) {
        sonic::ResponseControls c;
        c.sustain = sustain;
        h.setResponse(responseOf(c));
        const float v = h.value(row, bin);
        CHECK(v > last); // more sustain sensitivity: a quiet bin reads louder
        last = v;
    }
    sonic::ResponseControls silent;
    silent.sustain = 0.0f;
    silent.sensitivity = 0.0f;
    h.setResponse(responseOf(silent));
    CHECK(h.value(row, bin) == 0.0f);

    // Attack and Release are the readers' time constants: they do not change what the rows mean.
    sonic::ResponseControls slow;
    slow.release = 2.0f;
    slow.attack = 0.5f;
    h.setResponse(responseOf(sonic::ResponseControls{}));
    const auto revision = h.revision();
    h.setResponse(responseOf(slow));
    CHECK(h.revision() == revision);
    CHECK(spatial::onsetDecayScale(h.response()) == Approx(0.5f));
    // An onset field's response a second after its onset is higher with the longer release.
    const std::array<float, 1> age{1.0f};
    const std::array<float, 1> strength{1.0f};
    const float neutralDecay = spatial::onsetResponse(age, strength, 0.0f, 0.0f, 0.0f, 2.0f);
    const float slowDecay = spatial::onsetResponse(age, strength, 0.0f, 0.0f, 0.0f, 2.0f * spatial::onsetDecayScale(h.response()));
    CHECK(slowDecay > neutralDecay);
}

TEST_CASE("THE ASTRAL FORGE's song, heard: neutral is the song, Transients drop kicks, Release slows the envelopes",
          "[astral][adr1361]") {
    astral::SongAnalysis s;
    s.hops = 400;
    s.hopRate = 100.0f;
    s.spec.assign(static_cast<std::size_t>(s.hops) * astral::SongAnalysis::kBins, 0.05f);
    for (int h = 100; h < 110; ++h) { // a burst at 1.0 s
        for (int k = 0; k < astral::SongAnalysis::kBins; ++k) {
            s.spec[static_cast<std::size_t>(h) * astral::SongAnalysis::kBins + static_cast<std::size_t>(k)] = 0.9f;
        }
    }
    s.env1.resize(static_cast<std::size_t>(s.hops), glm::vec4(0.0f, 0.5f, 0.0f, 0.4f));
    astral::foldSongEnvelopes(s, 1.0f, 1.0f);
    s.kickT = {0.5f, 1.0f, 2.0f};
    s.kickS = {0.25f, 0.6f, 1.0f};

    const astral::SongAnalysis same = astral::conditionSong(s, spatial::AudioResponse{});
    CHECK(same.spec == s.spec);
    CHECK(same.env0 == s.env0);
    CHECK(same.env1 == s.env1);
    CHECK(same.kickS == s.kickS);
    CHECK(same.release == 1.0f);

    sonic::ResponseControls few;
    few.transient = 0.2f;
    const astral::SongAnalysis fewer = astral::conditionSong(s, responseOf(few));
    CHECK(fewer.kickT.size() == 2); // the 0.25 kick is under the floor
    CHECK(fewer.kickS[0] < 0.6f);

    sonic::ResponseControls longer;
    longer.release = 4.0f;
    const astral::SongAnalysis slow = astral::conditionSong(s, responseOf(longer));
    // half a second after the burst the band envelope has fallen less, and so has the kick envelope
    CHECK(slow.env0[160].x > s.env0[160].x);
    CHECK(astral::sampleSong(slow, 2.3).kickEnv > astral::sampleSong(s, 2.3).kickEnv);
}

TEST_CASE("THE ASTRAL FORGE's live conductor hears its kicks through the Response", "[astral][adr1361]") {
    // A live history: a second of rows, beats every half second, one kick of strength 0.45 at 1.0 s.
    auto blastAfterKick = [](float transient) {
        spatial::AudioHistory h = spatial::AudioHistory::livePlaceholder(100.0);
        std::array<float, spatial::kAudioBins> row{};
        row.fill(0.3f);
        for (int i = 0; i <= 105; ++i) {
            const double t = i / 100.0;
            h.appendLive(t, row);
            if (i % 50 == 0) {
                h.addLiveOnset(spatial::OnsetSource::Beat, t, 1.0f);
            }
            if (i == 100) {
                h.addLiveOnset(spatial::OnsetSource::Low, t, 0.45f);
            }
        }
        sonic::ResponseControls c;
        c.transient = transient;
        h.setResponse(responseOf(c));
        scene::AstralLiveConductor conductor;
        const astral::State s = conductor.update(h, 1.05, astral::Controls{}, false);
        return s.blast; // 14 + 10 x the last kick's strength as heard
    };
    const float low = blastAfterKick(0.2f);
    const float neutral = blastAfterKick(0.5f);
    const float high = blastAfterKick(0.9f);
    CHECK(neutral == Approx(14.0f + 10.0f * 0.45f));
    CHECK(low < neutral);
    CHECK(high > neutral);
}
