// The Audio Response Lab's causal replay (ADR-1725): a file through the LIVE analysis path, deterministically.
//
//   avgen_audio_lab_replay <in.wav> <out-prefix> [--fps 30] [--input-latency-ms 0]
//
// The live path (`analysis::AnalysisRunner`, ADR-1060) pushes the input's mono samples a hop at a time into the same
// `Analyzer`, steps the causal `BeatTracker` and `CausalOnsetDetector` on every frame, and publishes the newest frame
// through a triple buffer; the render thread acquires it once per frame, and an event raised on a frame it never
// acquired rides on the next one it does (the carry). This tool runs exactly those three objects, in the runner's
// order, on the file's samples, without a thread, and replays the render side at a fixed frame rate:
//
//   - a frame EXISTS at the time its last sample arrives: (window centre + half a window) / rate, plus
//     --input-latency-ms (the device and capture buffers, which a file does not have; default 0);
//   - render frame n at n / fps acquires every frame that exists by then; its events are the OR of theirs (the carry),
//     its values the newest frame's.
//
// It is the live analysis, not the live engine: no device, no thread scheduling, no Live Sonic response model (whose
// hits at neutral controls are these causal hits, ADR-1062). The beat it reports is the tracker's own: the engine's
// live clock re-syncs to every tracked beat (advanceClock, `bpm > 0`), so `beat.pulse` live is this beat.
//
// Writes <out-prefix>.hop.csv (one row per analysis frame) and <out-prefix>.frame.csv (one row per render frame).

#include "analysis/analyzer.hpp"
#include "analysis/beat_tracker.hpp"
#include "analysis/causal_onsets.hpp"
#include "audio/audio_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <vector>

using namespace avgen;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: avgen_audio_lab_replay <in.wav> <out-prefix> [--fps 30] [--input-latency-ms 0]\n");
        return 2;
    }
    double fps = 30.0;
    double latency = 0.0;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--fps") {
            fps = std::atof(argv[i + 1]);
        } else if (a == "--input-latency-ms") {
            latency = std::atof(argv[i + 1]) / 1000.0;
        }
    }
    auto file = audio::AudioFile::load(argv[1]);
    if (!file) {
        std::fprintf(stderr, "replay: %s\n", file.error().message.c_str());
        return 3;
    }
    analysis::AnalyzerConfig config;
    config.sampleRate = file->sampleRate();
    const double rate = static_cast<double>(config.sampleRate);
    const double hopSeconds = static_cast<double>(config.hopSize) / rate;
    analysis::Analyzer analyzer(config);
    analysis::BeatTracker tracker(analysis::BeatTrackerConfig{}, static_cast<float>(hopSeconds));
    analysis::CausalOnsetDetector causal;
    const float binHz = static_cast<float>(rate) / static_cast<float>(config.windowSize);

    struct Hop {
        double centre = 0.0, exists = 0.0;
        bool onset = false, low = false, causalOnset = false, beat = false;
        float onsetStrength = 0.0f, lowStrength = 0.0f, causalStrength = 0.0f;
        float phase = 0.0f, tempo = 0.0f, confidence = 0.0f;
        std::uint32_t beatCount = 0;
    };
    std::vector<Hop> hops;
    const auto mono = file->mono();
    analysis::AnalysisFrame frame;
    for (std::size_t at = 0; at < mono.size(); at += config.hopSize) {
        const std::size_t n = std::min<std::size_t>(config.hopSize, mono.size() - at);
        analyzer.push(std::span<const float>(mono.data() + at, n));
        while (analyzer.pop(frame)) {
            const analysis::BeatState beat = tracker.push(frame.onsetStrength, frame.onset);
            frame.tempoBpm = beat.tempoBpm;
            frame.tempoConfidence = beat.confidence;
            frame.beat = beat.beat;
            frame.beatPhase = beat.phase;
            frame.beatCount = beat.beatCount;
            causal.process(frame, binHz, hopSeconds);
            Hop h;
            h.centre = frame.timeSeconds;
            h.exists = frame.timeSeconds + static_cast<double>(config.windowSize) / 2.0 / rate + latency;
            h.onset = frame.onset;
            h.onsetStrength = frame.onset ? std::min(1.0f, frame.onsetStrength / 2.0f) : 0.0f;
            constexpr auto kLow = static_cast<std::size_t>(analysis::HitClass::Low);
            constexpr auto kOnset = static_cast<std::size_t>(analysis::HitClass::Onset);
            h.low = frame.causal.hit[kLow];
            h.lowStrength = frame.causal.strength[kLow];
            h.causalOnset = frame.causal.hit[kOnset];
            h.causalStrength = frame.causal.strength[kOnset];
            h.beat = frame.beat;
            h.phase = frame.beatPhase;
            h.tempo = frame.tempoBpm;
            h.confidence = frame.tempoConfidence;
            h.beatCount = frame.beatCount;
            hops.push_back(h);
        }
    }
    const std::string prefix = argv[2];
    {
        std::ofstream out(prefix + ".hop.csv");
        out << "centre,exists,audio.onset,causal.low,causal.onset,beat.pulse,beat.phase,beat.bpm,audio.tempoConfidence\n";
        for (const Hop& h : hops) {
            char line[256];
            std::snprintf(line, sizeof line, "%.6f,%.6f,%.4f,%.4f,%.4f,%d,%.4f,%.3f,%.4f\n", h.centre, h.exists,
                          h.onsetStrength, h.low ? h.lowStrength : 0.0f, h.causalOnset ? h.causalStrength : 0.0f,
                          h.beat ? 1 : 0, h.phase, h.tempo, h.confidence);
            out << line;
        }
    }
    {
        // Column names as the offline trace's, so the lab scores both the same way (response.* = the causal hits at
        // neutral controls; see the header).
        std::ofstream out(prefix + ".frame.csv");
        out << "time,newestCentre,audio.onset,response.low,response.onset,beat.pulse,beat.phase,beat.bpm,"
               "audio.tempoConfidence\n";
        std::size_t next = 0;
        const Hop* newest = nullptr;
        const double end = hops.empty() ? 0.0 : hops.back().exists;
        for (std::uint64_t f = 0; static_cast<double>(f) / fps <= end + 1.0 / fps; ++f) {
            const double t = static_cast<double>(f) / fps;
            float onset = 0.0f, low = 0.0f, causalOnset = 0.0f;
            bool beat = false;
            while (next < hops.size() && hops[next].exists <= t + 1e-9) {
                const Hop& h = hops[next++];
                onset = std::max(onset, h.onsetStrength);
                low = std::max(low, h.low ? std::max(h.lowStrength, 1e-3f) : 0.0f);
                causalOnset = std::max(causalOnset, h.causalOnset ? std::max(h.causalStrength, 1e-3f) : 0.0f);
                beat = beat || h.beat;
                newest = &h;
            }
            char line[256];
            std::snprintf(line, sizeof line, "%.4f,%.6f,%.4f,%.4f,%.4f,%d,%.4f,%.3f,%.4f\n", t,
                          newest != nullptr ? newest->centre : -1.0, onset, low, causalOnset, beat ? 1 : 0,
                          newest != nullptr ? newest->phase : 0.0f, newest != nullptr ? newest->tempo : 0.0f,
                          newest != nullptr ? newest->confidence : 0.0f);
            out << line;
        }
    }
    std::printf("replay: %zu analysis frames -> %s.hop.csv, %s.frame.csv\n", hops.size(), prefix.c_str(),
                prefix.c_str());
    return 0;
}
