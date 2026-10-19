// ADR-1510: the response levels keep their range on loud-mastered music. The fixed window of ADR-1062 (-60..-12 dBFS)
// read a commercial master as 1 almost every frame, and every route on response.bass/level/sustain/levelSlow/
// intensity was a constant; the tests here used a moderate synthetic kit, so nothing disagreed. This one is mastered
// like the real thing (about -8 dBFS RMS, brickwall-limited), with a breakdown and a drop, and is played again
// 24 dB down. Against main's fixed window it fails (every level above 0.9 on 80-100% of the frames).

#include "analysis/analysis_track.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/response.hpp"
#include "sonic/sonic_runtime.hpp"
#include "support/mastered_song.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::array<const char*, 5> kLevels{"response.bass", "response.level", "response.sustain",
                                             "response.levelSlow", "response.intensity"};

struct Trace {
    std::vector<double> t;
    std::array<std::vector<float>, kLevels.size()> v;
};

Trace play(const testsupport::MasteredSong& song, const sonic::ResponseControls& controls = {}) {
    const auto track = analysis::AnalysisTrack::analyze(song.file, analysis::AnalyzerConfig{});
    sonic::SonicSetup setup;
    setup.analyse(track);
    signals::SignalBus bus;
    sonic::SonicRuntime rt;
    rt.declare(bus);
    rt.setControls(controls);
    std::array<signals::SignalId, kLevels.size()> ids{};
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        ids[i] = *bus.find(kLevels[i]);
    }
    const double hop =
        static_cast<double>(track.config().hopSize) / static_cast<double>(track.config().sampleRate);
    Trace out;
    for (std::size_t f = 0; f < track.frames().size(); ++f) {
        const double t = track.frames()[f].timeSeconds;
        rt.step(setup, setup.timbre[f], hop);
        rt.publish(&setup, bus, t);
        out.t.push_back(t);
        for (std::size_t i = 0; i < kLevels.size(); ++i) {
            out.v[i].push_back(bus.value(ids[i]));
        }
        bus.clearEvents();
    }
    return out;
}

std::vector<float> span(const Trace& tr, std::size_t signal, double from, double to) {
    std::vector<float> out;
    for (std::size_t f = 0; f < tr.t.size(); ++f) {
        if (tr.t[f] >= from && tr.t[f] < to) {
            out.push_back(tr.v[signal][f]);
        }
    }
    return out;
}

float percentile(std::vector<float> v, double q) {
    if (v.empty()) {
        return 0.0f;
    }
    const auto k = static_cast<std::size_t>(std::clamp(q, 0.0, 1.0) * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

float mean(const std::vector<float>& v) {
    double s = 0.0;
    for (const float x : v) {
        s += x;
    }
    return v.empty() ? 0.0f : static_cast<float>(s / static_cast<double>(v.size()));
}

float shareAbove(const std::vector<float>& v, float x) {
    return v.empty() ? 0.0f
                     : static_cast<float>(std::count_if(v.begin(), v.end(), [x](float y) { return y > x; })) /
                           static_cast<float>(v.size());
}

struct Stats {
    float p10 = 0, p50 = 0, p90 = 0, above = 0, breakdown = 0, drop = 0;
};

std::array<Stats, kLevels.size()> measure(const testsupport::MasteredSong& song, const Trace& tr) {
    std::array<Stats, kLevels.size()> s;
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        // The whole song past its first bars; the breakdown's second half (the 12 s intensity needs that long to
        // hear it); the drop past its first bars.
        const auto all = span(tr, i, song.verseStart + 4.0, song.end);
        s[i].p10 = percentile(all, 0.1);
        s[i].p50 = percentile(all, 0.5);
        s[i].p90 = percentile(all, 0.9);
        s[i].above = shareAbove(all, 0.9f);
        s[i].breakdown = mean(span(tr, i, 0.5 * (song.breakdownStart + song.dropStart), song.dropStart));
        s[i].drop = mean(span(tr, i, song.dropStart + 6.0, song.end));
    }
    return s;
}

std::string table(const std::array<Stats, kLevels.size()>& s) {
    std::ostringstream o;
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        o << kLevels[i] << " p10 " << s[i].p10 << " p50 " << s[i].p50 << " p90 " << s[i].p90 << " >0.9 "
          << s[i].above << " breakdown " << s[i].breakdown << " drop " << s[i].drop << "\n";
    }
    return o.str();
}

void checkRange(const std::array<Stats, kLevels.size()>& s) {
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        INFO(kLevels[i]);
        CHECK(s[i].above < 0.25f);              // never a constant near the top
        CHECK(s[i].p90 - s[i].p10 > 0.2f);      // a real spread
        CHECK(s[i].p50 > 0.2f);                 // ...and not a constant near the bottom either
        CHECK(s[i].p50 < 0.85f);
        CHECK(s[i].breakdown < s[i].drop - 0.2f); // the breakdown reads clearly lower than the drop
    }
}

} // namespace

TEST_CASE("A loud-mastered song keeps its response levels' range, and so does the same song 24 dB down",
          "[sonic][adr1510]") {
    const testsupport::MasteredSong loud = testsupport::makeMasteredSong(0.0f);
    INFO("master RMS " << loud.rmsDb << " dBFS");
    CHECK(loud.rmsDb > -9.5);
    CHECK(loud.rmsDb < -6.5);
    const Trace a = play(loud);
    const auto sa = measure(loud, a);
    INFO("0 dB:\n" << table(sa));
    checkRange(sa);

    const testsupport::MasteredSong quiet = testsupport::makeMasteredSong(-24.0f);
    const Trace b = play(quiet);
    const auto sb = measure(quiet, b);
    INFO("-24 dB:\n" << table(sb));
    checkRange(sb);
    // A quieter channel is read against its own loudness: the same distribution within a few hundredths.
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        INFO(kLevels[i]);
        CHECK(std::fabs(sa[i].p50 - sb[i].p50) < 0.05f);
        CHECK(std::fabs(sa[i].p90 - sb[i].p90) < 0.05f);
    }
}

TEST_CASE("The response levels on a mastered song: Sensitivity moves them, Sustain 0 turns them off",
          "[sonic][adr1510]") {
    const testsupport::MasteredSong song = testsupport::makeMasteredSong(0.0f, 8, 4, 8);
    sonic::ResponseControls low, high, off;
    low.sensitivity = 0.25f;
    high.sensitivity = 0.75f;
    off.sustain = 0.0f;
    const auto n = measure(song, play(song));
    const auto l = measure(song, play(song, low));
    const auto h = measure(song, play(song, high));
    const auto o = measure(song, play(song, off));
    INFO("neutral:\n" << table(n) << "low:\n" << table(l) << "high:\n" << table(h));
    for (std::size_t i = 0; i < kLevels.size(); ++i) {
        INFO(kLevels[i]);
        CHECK(l[i].p50 < n[i].p50 - 0.1f);
        CHECK(h[i].p50 > n[i].p50 + 0.1f);
        CHECK(o[i].p90 == 0.0f);
    }
}

TEST_CASE("The loudness reference survives silence and does not normalise noise up", "[sonic][adr1510]") {
    // Silence below the gate leaves the reference where the music put it; a hiss at -70 dBFS reads 0.
    const sonic::ResponseSettings s;
    CHECK(sonic::conditionLevel(-70.0f, -10.0f, s.levelWindow, 0.5f, s) == 0.0f);
    // A reference sunk below referenceMinDb is held there: -50 dBFS noise against a -55 dB "reference" reads as
    // -50 against -40, nothing.
    CHECK(sonic::conditionLevel(-50.0f, -55.0f, s.levelWindow, 0.5f, s) == 0.0f);
    // Full scale against any reference is at most 1, and the window's top reads 1.
    for (const float sens : {0.2f, 0.5f, 0.8f, 1.0f}) {
        CHECK(sonic::conditionLevel(0.0f, -10.0f, s.levelWindow, sens, s) <= 1.0f);
    }
    CHECK(sonic::conditionLevel(-10.0f + s.levelWindow.topDb, -10.0f, s.levelWindow, 0.5f, s) == 1.0f);
}
