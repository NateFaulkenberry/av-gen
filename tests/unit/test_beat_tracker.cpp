#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace avgen;
using namespace avgen::analysis;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr float kHop = 512.0f / 48000.0f;
constexpr double kHopD = 512.0 / 48000.0;

// Synthetic onset-strength envelope: unit impulses every 60 / bpm seconds (nearest hop), on a
// small floor. Impulses are split across the two nearest hops when the beat falls between them.
std::vector<float> impulseEnvelope(float bpm, double seconds, float floor = 0.0f) {
    const auto n = static_cast<std::size_t>(seconds / kHopD);
    std::vector<float> env(n, floor);
    const double periodHops = 60.0 / (static_cast<double>(bpm) * kHopD);
    for (double t = 0.0; t < static_cast<double>(n); t += periodHops) {
        const auto lo = static_cast<std::size_t>(std::floor(t));
        const auto frac = static_cast<float>(t - std::floor(t));
        if (lo < n) {
            env[lo] += 1.0f - frac;
        }
        if (lo + 1 < n) {
            env[lo + 1] += frac;
        }
    }
    return env;
}

std::vector<AnalysisFrame> analyze(const std::vector<float>& samples) {
    AnalyzerConfig config;
    config.sampleRate = kRate;
    Analyzer analyzer(config);
    std::vector<AnalysisFrame> frames;
    AnalysisFrame frame;
    for (std::size_t offset = 0; offset < samples.size(); offset += config.hopSize) {
        const std::size_t take = std::min<std::size_t>(config.hopSize, samples.size() - offset);
        analyzer.push(std::span<const float>(samples.data() + offset, take));
        while (analyzer.pop(frame)) {
            frames.push_back(frame);
        }
    }
    return frames;
}

std::vector<float> onsetStrengths(const std::vector<AnalysisFrame>& frames) {
    std::vector<float> out;
    out.reserve(frames.size());
    for (const auto& f : frames) {
        out.push_back(f.onsetStrength);
    }
    return out;
}

// Click times of testsupport::clickTrack (starting at 0).
std::vector<double> clickTimes(float bpm, double seconds, double offset = 0.0) {
    std::vector<double> out;
    for (double t = 0.0; t < seconds; t += 60.0 / static_cast<double>(bpm)) {
        out.push_back(t + offset);
    }
    return out;
}

double signedNearestError(double time, const std::vector<double>& clicks) {
    double best = 1e9;
    double signedError = 0.0;
    for (const double c : clicks) {
        if (std::fabs(time - c) < best) {
            best = std::fabs(time - c);
            signedError = time - c;
        }
    }
    return signedError;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// estimateTempo
// ------------------------------------------------------------------------------------------------

TEST_CASE("estimateTempo finds the tempo of synthetic impulse envelopes", "[analysis][beat]") {
    const BeatTrackerConfig config;
    for (const float bpm : {120.0f, 90.0f, 160.0f}) {
        INFO("bpm " << bpm);
        const auto env = impulseEnvelope(bpm, 6.0);
        const auto [estimate, confidence] = estimateTempo(env, kHop, config);
        WARN("estimateTempo " << bpm << " BPM -> " << estimate << " (confidence " << confidence << ")");
        CHECK_THAT(static_cast<double>(estimate), WithinAbs(static_cast<double>(bpm), 1.5));
        CHECK(confidence > 0.5f);
    }
}

TEST_CASE("estimateTempo reports no confidence on flat, zero and short input", "[analysis][beat]") {
    const BeatTrackerConfig config;
    const std::vector<float> zeros(600, 0.0f);
    const std::vector<float> flat(600, 0.7f);
    CHECK(estimateTempo(zeros, kHop, config).second < config.confidenceFloor);
    CHECK(estimateTempo(flat, kHop, config).second < config.confidenceFloor);
    CHECK(estimateTempo(flat, kHop, config).first == 0.0f);
    // Fewer hops than one period of the fastest tempo: nothing to correlate.
    const auto tooShort = impulseEnvelope(120.0f, 0.2);
    const auto [bpm, confidence] = estimateTempo(tooShort, kHop, config);
    CHECK(bpm == 0.0f);
    CHECK(confidence == 0.0f);
    CHECK(estimateTempo(std::span<const float>{}, kHop, config).second == 0.0f);
}

TEST_CASE("estimateTempo stays unconfident on white-noise envelopes", "[analysis][beat]") {
    const BeatTrackerConfig config;
    const auto noise = testsupport::whiteNoise(560, 1.0f, 3);
    std::vector<float> env(noise.size());
    for (std::size_t i = 0; i < env.size(); ++i) {
        env[i] = std::fabs(noise[i]);
    }
    const auto [bpm, confidence] = estimateTempo(env, kHop, config);
    WARN("estimateTempo on noise -> " << bpm << " (confidence " << confidence << ")");
    CHECK(confidence < 0.4f);
}

// ------------------------------------------------------------------------------------------------
// trackBeatsOffline
// ------------------------------------------------------------------------------------------------

TEST_CASE("trackBeatsOffline follows a 120 BPM click track analysed by the Analyzer", "[analysis][beat]") {
    constexpr double kSeconds = 10.0;
    const auto frames =
        analyze(testsupport::clickTrack(120.0f, kRate, static_cast<std::size_t>(kSeconds * kRate)));
    REQUIRE(frames.size() > 800);
    const auto beats = trackBeatsOffline(onsetStrengths(frames), kHop);
    WARN("offline tempo " << beats.tempoBpm << " (confidence " << beats.confidence << "), "
                          << beats.beatTimes.size() << " beats");
    CHECK_THAT(static_cast<double>(beats.tempoBpm), WithinAbs(120.0, 1.5));
    CHECK(beats.confidence > 0.5f);
    CHECK(beats.beatTimes.size() >= 19);
    CHECK(beats.beatTimes.size() <= 21);

    // Beat times are hop-relative: hop k is frame k, whose centre is frames[k].timeSeconds.
    const auto clicks = clickTimes(120.0f, kSeconds);
    const double firstCentre = frames.front().timeSeconds;
    double worst = 0.0;
    double sumSigned = 0.0;
    for (std::size_t i = 0; i < beats.beatTimes.size(); ++i) {
        if (i > 0) {
            CHECK(beats.beatTimes[i] > beats.beatTimes[i - 1]);
        }
        const double error = signedNearestError(beats.beatTimes[i] + firstCentre, clicks);
        worst = std::max(worst, std::fabs(error));
        sumSigned += error;
        INFO("beat " << i << " at " << beats.beatTimes[i] + firstCentre << " s, error " << error * 1000.0
                     << " ms");
        CHECK(std::fabs(error) <= 0.035);
    }
    WARN("offline beat timing: worst " << worst * 1000.0 << " ms, mean signed "
                                       << sumSigned / static_cast<double>(beats.beatTimes.size()) * 1000.0
                                       << " ms (negative = early)");

    // Every click except possibly the first (at t = 0, before the first full window) has a beat.
    std::size_t matched = 0;
    for (std::size_t c = 1; c < clicks.size(); ++c) {
        const bool hit = std::any_of(beats.beatTimes.begin(), beats.beatTimes.end(), [&](double b) {
            return std::fabs(b + firstCentre - clicks[c]) <= 0.035;
        });
        matched += hit ? 1 : 0;
    }
    CHECK(matched == clicks.size() - 1);
}

TEST_CASE("trackBeatsOffline handles empty, silent and short input", "[analysis][beat]") {
    CHECK(trackBeatsOffline(std::span<const float>{}, kHop).beatTimes.empty());
    const std::vector<float> zeros(1000, 0.0f);
    const auto silent = trackBeatsOffline(zeros, kHop);
    CHECK(silent.tempoBpm == 0.0f);
    CHECK(silent.beatTimes.empty());
    const auto shortEnv = impulseEnvelope(120.0f, 0.3);
    CHECK(trackBeatsOffline(shortEnv, kHop).beatTimes.empty());
    CHECK(trackBeatsOffline(shortEnv, 0.0f).beatTimes.empty());
}

TEST_CASE("trackBeatsOffline is deterministic and ignores a tempo-consistent scale", "[analysis][beat]") {
    const auto env = impulseEnvelope(100.0f, 12.0, 0.05f);
    const auto a = trackBeatsOffline(env, kHop);
    const auto b = trackBeatsOffline(env, kHop);
    CHECK(a.tempoBpm == b.tempoBpm);
    CHECK(a.beatTimes == b.beatTimes);
    CHECK_THAT(static_cast<double>(a.tempoBpm), WithinAbs(100.0, 1.5));
    CHECK(a.beatTimes.size() >= 19);
    CHECK(a.beatTimes.size() <= 21);
    for (std::size_t i = 1; i < a.beatTimes.size(); ++i) {
        CHECK_THAT(a.beatTimes[i] - a.beatTimes[i - 1], WithinAbs(0.6, 0.015));
    }
}

// ------------------------------------------------------------------------------------------------
// BeatTracker (live)
// ------------------------------------------------------------------------------------------------

TEST_CASE("BeatTracker locks to a 120 BPM click track hop by hop", "[analysis][beat]") {
    constexpr double kSeconds = 12.0;
    const auto frames =
        analyze(testsupport::clickTrack(120.0f, kRate, static_cast<std::size_t>(kSeconds * kRate)));
    const auto clicks = clickTimes(120.0f, kSeconds);

    BeatTracker tracker(BeatTrackerConfig{}, kHop);
    CHECK(tracker.hopSeconds() == kHop);
    std::vector<BeatState> states;
    states.reserve(frames.size());
    for (const auto& f : frames) {
        states.push_back(tracker.push(f.onsetStrength, f.onset));
    }
    REQUIRE(states.size() == frames.size());

    // Tempo is known within 2 BPM from 4 s on.
    double firstKnown = -1.0;
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i].tempoBpm > 0.0f && firstKnown < 0.0) {
            firstKnown = frames[i].timeSeconds;
        }
        if (frames[i].timeSeconds >= 4.0) {
            INFO("t = " << frames[i].timeSeconds);
            CHECK_THAT(static_cast<double>(states[i].tempoBpm), WithinAbs(120.0, 2.0));
            CHECK(states[i].confidence >= tracker.config().confidenceFloor);
        }
    }
    WARN("live tracker: tempo first known at " << firstKnown << " s, final " << states.back().tempoBpm
                                               << " BPM");
    CHECK(firstKnown >= 0.0);
    CHECK(firstKnown <= 4.0);

    // Beats: phase resets to 0 on beat hops and rises monotonically in between; count increments.
    std::uint32_t lastCount = 0;
    double worstLate = 0.0;
    double sumAbs = 0.0;
    std::size_t lateBeats = 0;
    for (std::size_t i = 0; i < states.size(); ++i) {
        const auto& s = states[i];
        CHECK(s.phase >= 0.0f);
        CHECK(s.phase <= 1.0f);
        if (s.beat) {
            CHECK(s.beatCount == lastCount + 1);
            CHECK(s.phase == 0.0f);
            if (frames[i].timeSeconds >= 6.0) {
                const double error = signedNearestError(frames[i].timeSeconds, clicks);
                INFO("beat at " << frames[i].timeSeconds << " s, error " << error * 1000.0 << " ms");
                CHECK(std::fabs(error) <= 0.040);
                worstLate = std::max(worstLate, std::fabs(error));
                sumAbs += std::fabs(error);
                ++lateBeats;
            }
        } else {
            CHECK(s.beatCount == lastCount);
            if (i > 0 && !states[i - 1].beat && s.tempoBpm > 0.0f && states[i - 1].tempoBpm > 0.0f) {
                CHECK(s.phase >= states[i - 1].phase - 1e-6f);
            }
        }
        lastCount = s.beatCount;
    }
    WARN("live beats after 6 s: " << lateBeats << ", worst " << worstLate * 1000.0 << " ms, mean "
                                  << (lateBeats ? sumAbs / static_cast<double>(lateBeats) * 1000.0 : 0.0)
                                  << " ms");
    CHECK(lateBeats >= 11); // 6..12 s at 2 beats/s, allowing one edge miss
    CHECK(lateBeats <= 13);
    // Every click from 6 s on has a beat within 40 ms.
    for (const double c : clicks) {
        if (c < 6.5 || c > kSeconds - 0.5) {
            continue;
        }
        bool hit = false;
        for (std::size_t i = 0; i < states.size(); ++i) {
            if (states[i].beat && std::fabs(frames[i].timeSeconds - c) <= 0.040) {
                hit = true;
            }
        }
        INFO("click at " << c);
        CHECK(hit);
    }
}

TEST_CASE("BeatTracker follows a tempo change within 6 seconds", "[analysis][beat]") {
    constexpr double kHalf = 8.0;
    auto samples = testsupport::clickTrack(120.0f, kRate, static_cast<std::size_t>(kHalf * kRate));
    const auto second =
        testsupport::clickTrack(150.0f, kRate, static_cast<std::size_t>(kHalf * kRate), 256, 0.9f, 9);
    samples.insert(samples.end(), second.begin(), second.end());
    const auto frames = analyze(samples);

    BeatTracker tracker(BeatTrackerConfig{}, kHop);
    double switched = -1.0;
    for (const auto& f : frames) {
        const auto s = tracker.push(f.onsetStrength, f.onset);
        if (f.timeSeconds >= kHalf && switched < 0.0 && std::fabs(s.tempoBpm - 150.0f) <= 2.0f) {
            switched = f.timeSeconds;
        }
        if (f.timeSeconds >= 4.0 && f.timeSeconds < kHalf) {
            INFO("t = " << f.timeSeconds);
            CHECK_THAT(static_cast<double>(s.tempoBpm), WithinAbs(120.0, 2.0));
        }
        if (f.timeSeconds >= kHalf + 6.0) {
            INFO("t = " << f.timeSeconds);
            CHECK_THAT(static_cast<double>(s.tempoBpm), WithinAbs(150.0, 2.0));
        }
    }
    WARN("tempo change 120 -> 150 BPM at 8 s followed at " << switched << " s");
    CHECK(switched >= kHalf);
    CHECK(switched <= kHalf + 6.0);

    // After the change the emitted beats line up with the 150 BPM clicks.
    const auto clicks = clickTimes(150.0f, kHalf, kHalf);
    BeatTracker verify(BeatTrackerConfig{}, kHop);
    std::size_t lateBeats = 0;
    for (const auto& f : frames) {
        const auto s = verify.push(f.onsetStrength, f.onset);
        if (s.beat && f.timeSeconds >= kHalf + 6.0) {
            const double error = signedNearestError(f.timeSeconds, clicks);
            INFO("beat at " << f.timeSeconds << " s, error " << error * 1000.0 << " ms");
            CHECK(std::fabs(error) <= 0.040);
            ++lateBeats;
        }
    }
    CHECK(lateBeats >= 3);
}

TEST_CASE("BeatTracker is deterministic and reset clears its state", "[analysis][beat]") {
    const auto frames = analyze(testsupport::clickTrack(100.0f, kRate, kRate * 8));
    BeatTracker a(BeatTrackerConfig{}, kHop);
    BeatTracker b(BeatTrackerConfig{}, kHop);
    for (const auto& f : frames) {
        const auto sa = a.push(f.onsetStrength, f.onset);
        const auto sb = b.push(f.onsetStrength, f.onset);
        CHECK(sa.tempoBpm == sb.tempoBpm);
        CHECK(sa.confidence == sb.confidence);
        CHECK(sa.beat == sb.beat);
        CHECK(sa.phase == sb.phase);
        CHECK(sa.beatCount == sb.beatCount);
    }
    CHECK(a.state().beatCount > 5);
    CHECK(a.state().tempoBpm > 0.0f);

    a.reset();
    CHECK(a.state().tempoBpm == 0.0f);
    CHECK(a.state().confidence == 0.0f);
    CHECK(a.state().beatCount == 0);
    CHECK(a.state().phase == 0.0f);
    CHECK_FALSE(a.state().beat);
    // A reset tracker behaves exactly like a fresh one.
    BeatTracker fresh(BeatTrackerConfig{}, kHop);
    for (const auto& f : frames) {
        const auto sa = a.push(f.onsetStrength, f.onset);
        const auto sf = fresh.push(f.onsetStrength, f.onset);
        CHECK(sa.tempoBpm == sf.tempoBpm);
        CHECK(sa.beat == sf.beat);
        CHECK(sa.beatCount == sf.beatCount);
    }
}

TEST_CASE("BeatTracker free-runs through a gap and forgets the tempo after a silent window",
          "[analysis][beat]") {
    auto env = impulseEnvelope(120.0f, 8.0);
    const auto silence = std::vector<float>(static_cast<std::size_t>(14.0 / kHopD), 0.0f);
    env.insert(env.end(), silence.begin(), silence.end());

    BeatTracker tracker(BeatTrackerConfig{}, kHop);
    std::uint32_t beatsDuringGap = 0;
    double forgotAt = -1.0;
    for (std::size_t i = 0; i < env.size(); ++i) {
        const auto s = tracker.push(env[i], env[i] > 0.5f);
        const double t = static_cast<double>(i) * kHopD;
        if (t > 8.0 && t < 10.0 && s.beat) {
            ++beatsDuringGap; // still predicting from the last period
        }
        if (t > 8.0 && forgotAt < 0.0 && s.tempoBpm == 0.0f) {
            forgotAt = t;
        }
    }
    WARN("tempo forgotten " << forgotAt - 8.0 << " s after the last onset");
    CHECK(beatsDuringGap >= 3);
    CHECK(forgotAt > 8.0);
    CHECK(forgotAt <= 8.0 + 2.0 * static_cast<double>(tracker.config().windowSeconds));
    CHECK(tracker.state().tempoBpm == 0.0f);
    CHECK_FALSE(tracker.state().beat);
}

TEST_CASE("BeatTracker reports nothing on silence", "[analysis][beat]") {
    BeatTracker tracker(BeatTrackerConfig{}, kHop);
    for (std::size_t i = 0; i < 2000; ++i) {
        const auto s = tracker.push(0.0f, false);
        CHECK(s.tempoBpm == 0.0f);
        CHECK_FALSE(s.beat);
        CHECK(s.beatCount == 0);
    }
}

// ------------------------------------------------------------------------------------------------
// AnalysisTrack
// ------------------------------------------------------------------------------------------------

TEST_CASE("AnalysisTrack stamps beat fields from the offline tracker", "[analysis][beat][track]") {
    constexpr double kSeconds = 10.0;
    const auto samples = testsupport::clickTrack(120.0f, kRate, static_cast<std::size_t>(kSeconds * kRate));
    const auto file = audio::AudioFile::fromInterleaved(samples, 1, kRate);
    const auto track = AnalysisTrack::analyze(file, AnalyzerConfig{});
    REQUIRE_FALSE(track.empty());
    const auto& frames = track.frames();
    const auto& beats = track.beats();
    CHECK_THAT(static_cast<double>(beats.tempoBpm), WithinAbs(120.0, 1.5));
    CHECK(beats.beatTimes.size() >= 19);
    CHECK(beats.beatTimes.size() <= 21);

    // Beat times are on the frames' clock (nearest frame centre) and near the clicks.
    const auto clicks = clickTimes(120.0f, kSeconds);
    for (const double t : beats.beatTimes) {
        const auto& f = track.at(t);
        CHECK(f.timeSeconds == t);
        CHECK(f.beat);
        INFO("beat at " << t);
        CHECK(std::fabs(signedNearestError(t, clicks)) <= 0.035);
    }

    std::uint32_t lastCount = 0;
    std::size_t beatFrames = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        CHECK(f.tempoBpm == beats.tempoBpm);
        CHECK(f.tempoConfidence == beats.confidence);
        CHECK(f.beatPhase >= 0.0f);
        CHECK(f.beatPhase <= 1.0f);
        CHECK(f.beatCount >= lastCount);
        if (f.beat) {
            ++beatFrames;
            CHECK(f.beatCount == lastCount + 1);
            CHECK(f.beatPhase == 0.0f);
        } else if (i > 0 && !frames[i - 1].beat && lastCount > 0 && f.beatCount < beats.beatTimes.size()) {
            CHECK(f.beatPhase >= frames[i - 1].beatPhase);
        }
        lastCount = f.beatCount;
    }
    CHECK(beatFrames == beats.beatTimes.size());
    CHECK(frames.back().beatCount == beats.beatTimes.size());
    // Phase keeps cycling with the tempo after the last beat.
    CHECK(frames.back().beatPhase > 0.0f);
}

TEST_CASE("AnalysisTrack of an unrhythmic file has no beats", "[analysis][beat][track]") {
    const auto file =
        audio::AudioFile::fromInterleaved(testsupport::sine(440.0f, kRate, kRate * 3), 1, kRate);
    const auto track = AnalysisTrack::analyze(file, AnalyzerConfig{});
    REQUIRE_FALSE(track.empty());
    CHECK(track.beats().beatTimes.empty());
    CHECK(track.beats().tempoBpm == 0.0f);
    for (const auto& f : track.frames()) {
        CHECK_FALSE(f.beat);
        CHECK(f.beatCount == 0);
        CHECK(f.beatPhase == 0.0f);
    }
}
