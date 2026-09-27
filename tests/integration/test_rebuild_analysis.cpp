// The analysis against "Rebuild", the song of Glowmere Valley 3, measured by hand (ADR-896..899).
//
// The ground truth is docs/glowmere-valley-3/01-music.md and tools/gv3/music.py on the GV3 branch:
// a constant 130.000 BPM grid with the first downbeat at 0.480 s, 8-bar phrases, and 475 kicks --
// four on the floor, out for the pull-back (bars 15-16), cut on beat 4 of bars 24, 40, 56 and 72,
// and stopping after bar 122 beat 3.
//
// The song is the owner's and is never in the repository. These tests read it from
// $AVGEN_REBUILD_AUDIO, or ~/Desktop/Rebuild.mp3, and SKIP when it is not there -- or when it is not
// the master the ground truth was measured on (its SHA-256 differs).

#include "analysis/analysis_track.hpp"
#include "analysis/meter.hpp"
#include "analysis/span_profile.hpp"
#include "analysis/structure.hpp"
#include "app/engine.hpp"
#include "app/music_runtime.hpp"
#include "audio/audio_file.hpp"
#include "core/hash.hpp"
#include "core/time.hpp"
#include "signals/musical_events.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr const char* kRebuildSha256 = "53a7c00cbdca9a078338db1f5a1cc725fa6b926623dccb7728f55e948838a030";
constexpr double kBpm = 130.0;
constexpr double kBeat = 60.0 / kBpm;
constexpr double kBar = 4.0 * kBeat;
constexpr double kFirstDownbeat = 0.480;
constexpr double kLastHit = 224.788;

double beatAt(double bar, double beat = 1.0) { return kFirstDownbeat + (bar - 1.0) * kBar + (beat - 1.0) * kBeat; }

// tools/gv3/music.py kicks(), transcribed.
std::vector<double> kicks() {
    std::vector<double> out;
    for (int b = 1; b <= 122; ++b) {
        if (b == 15 || b == 16) {
            continue;
        }
        for (int n = 1; n <= 4; ++n) {
            const double t = beatAt(b, n);
            if (t > kLastHit + 1e-3) {
                break;
            }
            if (n == 4 && (b == 24 || b == 40 || b == 56 || b == 72)) {
                continue;
            }
            out.push_back(t);
        }
    }
    return out;
}

// The analysed track, once per process: 226 s of audio is a second of work.
const analysis::AnalysisTrack* rebuild() {
    static const std::optional<analysis::AnalysisTrack> track = []() -> std::optional<analysis::AnalysisTrack> {
        std::filesystem::path path;
        if (const char* env = std::getenv("AVGEN_REBUILD_AUDIO"); env != nullptr && *env != '\0') {
            path = env;
        } else if (const char* home = std::getenv("HOME"); home != nullptr) {
            path = std::filesystem::path(home) / "Desktop" / "Rebuild.mp3";
        }
        if (path.empty() || !std::filesystem::exists(path)) {
            return std::nullopt;
        }
        const auto sha = sha256File(path);
        if (!sha || *sha != kRebuildSha256) {
            return std::nullopt;
        }
        auto file = audio::AudioFile::load(path);
        if (!file) {
            return std::nullopt;
        }
        return analysis::AnalysisTrack::analyze(*file, analysis::AnalyzerConfig{});
    }();
    return track ? &*track : nullptr;
}

#define REQUIRE_REBUILD()                                                                                      \
    const analysis::AnalysisTrack* track = rebuild();                                                         \
    if (track == nullptr) {                                                                                    \
        SKIP("Rebuild.mp3 (the GV3 master, sha256 53a7c00c...) is not present: set AVGEN_REBUILD_AUDIO or put " \
             "it at ~/Desktop/Rebuild.mp3");                                                                   \
    }

struct Score {
    std::size_t hits = 0;
    std::size_t detected = 0;
    std::size_t reference = 0;
};

Score match(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance) {
    Score s;
    s.detected = detected.size();
    s.reference = reference.size();
    std::vector<bool> used(reference.size(), false);
    for (const double d : detected) {
        const auto it = std::lower_bound(reference.begin(), reference.end(), d - tolerance);
        for (auto c = it; c != reference.end() && *c <= d + tolerance; ++c) {
            const auto i = static_cast<std::size_t>(c - reference.begin());
            if (!used[i]) {
                used[i] = true;
                ++s.hits;
                break;
            }
        }
    }
    return s;
}

} // namespace

TEST_CASE("Rebuild: the bar lines are within 30 ms of the true bars over the whole track",
          "[analysis][rebuild][meter]") {
    REQUIRE_REBUILD();
    const auto& beats = track->beats().beatTimes;
    const analysis::MeterEstimate& est = track->meterEstimate();
    INFO("tempo " << track->beats().tempoBpm << ", " << beats.size() << " beats, first at " << beats.front()
                  << "; downbeat estimate " << est.downbeat << " (confidence " << est.downbeatConfidence << ")");
    CHECK(std::fabs(track->beats().tempoBpm - kBpm) < 0.1);
    REQUIRE(est.valid);
    // The first tracked beat is the first downbeat -- the audio starts on it.
    CHECK(est.downbeat == 0);
    CHECK(est.phraseBars == 8);

    analysis::Meter meter;
    meter.downbeat = est.downbeat;
    const std::vector<double> bars = meter.barTimes(beats);
    std::vector<double> truth;
    for (int b = 1; b <= 122; ++b) {
        truth.push_back(beatAt(b));
    }
    double worst = 0.0;
    double sum = 0.0;
    for (const double t : truth) {
        double nearest = 1e9;
        for (const double p : bars) {
            nearest = std::min(nearest, std::fabs(p - t));
        }
        worst = std::max(worst, nearest);
        sum += nearest;
    }
    INFO(truth.size() << " bars; mean |error| " << 1000.0 * sum / static_cast<double>(truth.size())
                      << " ms, worst " << 1000.0 * worst << " ms; " << bars.size() << " predicted");
    CHECK(worst <= 0.030);
    // No predicted bar line off the grid either.
    CHECK(match(bars, truth, 0.030).hits == bars.size());

    // The control: the convention this replaced put bar 1 on tracked beat 3 -- every bar line a
    // beat before the next true one (0.46 s out).
    analysis::Meter old;
    old.downbeat = 3;
    CHECK(match(old.barTimes(beats), truth, 0.030).hits == 0);
}

TEST_CASE("Rebuild: the engine's music.downbeat lands within 30 ms of every bar", "[analysis][rebuild][meter]") {
    REQUIRE_REBUILD();
    // The same file through the engine, at 60 fps: `music.downbeat` is stamped on the analysis frame
    // of the tracked beat, `beat.bar` wraps on the first render frame after it.
    std::filesystem::path path;
    if (const char* env = std::getenv("AVGEN_REBUILD_AUDIO"); env != nullptr && *env != '\0') {
        path = env;
    } else {
        path = std::filesystem::path(std::getenv("HOME")) / "Desktop" / "Rebuild.mp3";
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(path).has_value());
    CHECK(engine.meter().downbeat == 0);
    CHECK(engine.meter().phraseBars == 8);
    FixedStepClock clock(60.0);
    double last = app::MusicRuntime::kNever;
    std::vector<double> downbeats;
    std::vector<double> wraps;
    float lastBar = -1.0f;
    for (int i = 0; i < 60 * 226; ++i) {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        const double when = engine.music().lastEventTime(signals::MusicalEvent::Downbeat);
        if (when != last) {
            last = when;
            downbeats.push_back(when);
        }
        const float bar = engine.signals().value(engine.timeSignals().barPhase);
        // Past the last hit the clock runs on at the track's tempo -- musical time does not stop with
        // the audio -- so its bar 123 is not a bar the song has.
        if (lastBar >= 0.0f && bar < lastBar - 0.5f && t.renderTime <= kLastHit + 0.5) {
            wraps.push_back(t.renderTime);
        }
        lastBar = bar;
    }
    std::vector<double> truth;
    for (int b = 1; b <= 122; ++b) {
        truth.push_back(beatAt(b));
    }
    const auto worst = [&truth](const std::vector<double>& measured) {
        double w = 0.0;
        for (const double m : measured) {
            double nearest = 1e9;
            for (const double t : truth) {
                nearest = std::min(nearest, std::fabs(m - t));
            }
            w = std::max(w, nearest);
        }
        return w;
    };
    INFO(downbeats.size() << " downbeats, worst " << 1000.0 * worst(downbeats) << " ms; " << wraps.size()
                          << " bar wraps, worst " << 1000.0 * worst(wraps) << " ms");
    CHECK(downbeats.size() >= 121);
    CHECK(worst(downbeats) <= 0.030);
    CHECK(worst(wraps) <= 0.030 + 1.0 / 60.0);
    CHECK(match(downbeats, truth, 0.030).hits >= 121);
}

TEST_CASE("Rebuild: the low onsets are the measured kicks", "[analysis][rebuild][onsets]") {
    REQUIRE_REBUILD();
    const std::vector<double> reference = kicks();
    REQUIRE(reference.size() == 475);
    std::vector<double> lows;
    for (const analysis::AnalysisFrame& f : track->frames()) {
        if (f.lowOnset) {
            lows.push_back(f.timeSeconds);
        }
    }
    const Score s = match(lows, reference, 0.030);
    const double precision = static_cast<double>(s.hits) / static_cast<double>(s.detected);
    const double recall = static_cast<double>(s.hits) / static_cast<double>(s.reference);
    INFO(s.detected << " low onsets, " << s.hits << " within 30 ms of the 475 kicks: precision " << precision
                    << ", recall " << recall);
    CHECK(precision > 0.9);
    CHECK(recall > 0.9);
    // The pull-back has no kick, and gets none.
    std::size_t inPullBack = 0;
    for (const double t : lows) {
        inPullBack += (t > beatAt(15) + 0.05 && t < beatAt(17) - 0.05) ? 1u : 0u;
    }
    CHECK(inPullBack == 0);
}

TEST_CASE("Rebuild: energy follows the arrangement on a flat master, and density is not zero",
          "[analysis][rebuild][flatmaster]") {
    REQUIRE_REBUILD();
    // 01-music.md §1.3's segments and the owner's composite energy for each.
    struct Segment {
        const char* name;
        int first;
        int after;
        double energy;
    };
    const Segment segments[] = {
        {"cold-open", 1, 5, 0.69},       {"riff-groove", 5, 15, 0.71},  {"first-pullback", 15, 17, 0.29},
        {"groove-2", 17, 33, 0.73},      {"lift", 33, 41, 0.80},        {"arrival", 41, 49, 0.81},
        {"melodic-plateau", 49, 73, 0.81}, {"lead-forward", 73, 81, 0.76}, {"suspension", 81, 89, 0.74},
        {"submerged-break", 89, 93, 0.51}, {"riser", 93, 97, 0.87},    {"drop", 97, 121, 0.83},
        {"tail", 121, 123, 0.41}};
    std::vector<double> ours;
    std::vector<double> theirs;
    std::vector<double> density;
    for (const Segment& seg : segments) {
        const double end = seg.after == 123 ? 225.38 : beatAt(seg.after);
        const analysis::SpanProfile p = analysis::profileSpan(*track, beatAt(seg.first), end);
        INFO(seg.name << ": energy " << p.energy << ", onsets/s " << p.onsetRate << ", brightness "
                      << p.brightnessHz);
        CHECK(p.measured());
        ours.push_back(p.energy);
        theirs.push_back(seg.energy);
        density.push_back(p.onsetRate);
    }
    // Rank agreement with the owner's composite (Spearman), and the claims 01-music.md makes: the
    // break is NOT the most energetic section -- loudness peaks there -- the riser and the drop
    // outrank the arrival, and the pull-back is the lowest.
    const auto ranks = [](const std::vector<double>& v) {
        std::vector<double> r(v.size());
        for (std::size_t i = 0; i < v.size(); ++i) {
            double below = 0.0;
            double equal = 0.0;
            for (const double x : v) {
                below += x < v[i] ? 1.0 : 0.0;
                equal += x == v[i] ? 1.0 : 0.0;
            }
            r[i] = below + 0.5 * (equal - 1.0);
        }
        return r;
    };
    const auto a = ranks(ours);
    const auto b = ranks(theirs);
    const double n = static_cast<double>(a.size());
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        ma += a[i] / n;
        mb += b[i] / n;
    }
    double cov = 0.0, va = 0.0, vb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        cov += (a[i] - ma) * (b[i] - mb);
        va += (a[i] - ma) * (a[i] - ma);
        vb += (b[i] - mb) * (b[i] - mb);
    }
    const double spearman = cov / std::sqrt(va * vb);
    INFO("Spearman rank correlation with the owner's composite: " << spearman);
    CHECK(spearman > 0.9);
    const std::size_t breakIndex = 9;
    const std::size_t riser = 10;
    const std::size_t drop = 11;
    const std::size_t arrival = 5;
    CHECK(ours[breakIndex] < *std::max_element(ours.begin(), ours.end()));
    CHECK(ours[riser] > ours[arrival]);
    CHECK(ours[drop] > ours[arrival]);
    CHECK(ours[2] == *std::min_element(ours.begin(), ours.end()));
    // Density is an onset rate now: it varies, and the pull-back is the sparsest.
    CHECK(*std::max_element(density.begin(), density.end()) > 3.0);
    CHECK(density[2] < 1.0);

    // The structure detector's sections carry it too: no section reads density 0 for want of a
    // measure, and energies are ratios to the piece's largest.
    const auto structure = analysis::detectStructure(*track);
    REQUIRE(structure.has_value());
    std::size_t zeroDensity = 0;
    for (const auto& s : structure->sections) {
        zeroDensity += s.density == 0.0f ? 1u : 0u;
        CHECK(s.energy > 0.0f);
    }
    CHECK(zeroDensity < structure->sections.size());
}
