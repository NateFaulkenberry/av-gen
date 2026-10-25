// avgen_music_report: what the analysis hears in a track, measured against a reference if you have
// one (ADR-896..899).
//
//   avgen_music_report AUDIO [--grid BPM FIRST_DOWNBEAT_SECONDS] [--bar-offset N]
//                            [--sections FILE] [--json OUT]
//
// Prints the tempo, the meter estimate (which tracked beat is beat 1, how sure, the phrase length),
// the onset count and, per section, the level-free profile a director is handed. With `--grid`,
// scores the bar lines the meter produces against a constant grid. With `--sections` (lines of
// "name start end"), profiles those spans instead of whole-track quarters.
//
// `--json OUT` writes everything for a generator to read: `tempo`, `beatTimes`, `meterEstimate`,
// `meter` (the downbeat, phrase length and first downbeat the engine resolves with nothing pinned),
// `barTimes` (bar 1 first), `onsetTimes` (what `audio.onset` fires on),
// and `sections`, each in the `director.inspect_scene` shape plus its name and span.
//
// The audio never leaves the process: nothing here writes a sample, only numbers.

#include "analysis/analysis_track.hpp"
#include "analysis/meter.hpp"
#include "analysis/span_profile.hpp"
#include "audio/audio_file.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

struct Section {
    std::string name;
    double start = 0.0;
    double end = 0.0;
};

std::vector<Section> readSections(const std::string& path) {
    std::vector<Section> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        Section s;
        if (words >> s.name >> s.start >> s.end) {
            out.push_back(s);
        }
    }
    return out;
}

// Greedy one-to-one matching within `tolerance`: precision, recall and the mean signed error.
struct Score {
    std::size_t hits = 0;
    std::size_t detected = 0;
    std::size_t reference = 0;
    double meanError = 0.0;
    double maxError = 0.0;
};

Score match(const std::vector<double>& detected, const std::vector<double>& reference, double tolerance) {
    Score s;
    s.detected = detected.size();
    s.reference = reference.size();
    std::vector<bool> used(reference.size(), false);
    double sum = 0.0;
    for (const double d : detected) {
        const auto it = std::lower_bound(reference.begin(), reference.end(), d);
        std::size_t best = reference.size();
        double bestError = tolerance + 1.0;
        for (auto c : {it, it == reference.begin() ? reference.end() : std::prev(it)}) {
            if (c == reference.end()) {
                continue;
            }
            const auto i = static_cast<std::size_t>(c - reference.begin());
            const double e = std::fabs(*c - d);
            if (!used[i] && e < bestError) {
                bestError = e;
                best = i;
            }
        }
        if (best < reference.size() && bestError <= tolerance) {
            used[best] = true;
            ++s.hits;
            sum += d - reference[best];
            s.maxError = std::max(s.maxError, bestError);
        }
    }
    s.meanError = s.hits > 0 ? sum / static_cast<double>(s.hits) : 0.0;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: avgen_music_report AUDIO [--grid BPM FIRST] [--bar-offset N] [--sections FILE] "
                     "[--json OUT]\n");
        return 2;
    }
    const std::string audioPath = argv[1];
    double gridBpm = 0.0;
    double gridFirst = 0.0;
    int barOffset = -1;
    std::string sectionsPath;
    std::string jsonPath;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--grid" && i + 2 < argc) {
            gridBpm = std::stod(argv[++i]);
            gridFirst = std::stod(argv[++i]);
        } else if (a == "--bar-offset" && i + 1 < argc) {
            barOffset = std::stoi(argv[++i]);
        } else if (a == "--sections" && i + 1 < argc) {
            sectionsPath = argv[++i];
        } else if (a == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }

    auto file = audio::AudioFile::load(audioPath);
    if (!file) {
        std::fprintf(stderr, "cannot load '%s': %s\n", audioPath.c_str(), file.error().message.c_str());
        return 1;
    }
    const auto track = analysis::AnalysisTrack::analyze(*file, analysis::AnalyzerConfig{});
    const auto& beats = track.beats().beatTimes;
    const analysis::MeterEstimate& est = track.meterEstimate();
    nlohmann::json report;
    report["audio"] = {{"seconds", file->durationSeconds()},
                       {"sampleRate", file->sampleRate()},
                       {"channels", file->channels()},
                       {"frames", track.frames().size()}};
    report["tempo"] = {{"bpm", track.beats().tempoBpm},
                       {"confidence", track.beats().confidence},
                       {"beats", beats.size()},
                       {"firstBeat", beats.empty() ? 0.0 : beats.front()}};
    report["beatTimes"] = beats;
    report["meterEstimate"] = {{"downbeat", est.downbeat},
                               {"confidence", est.downbeatConfidence},
                               {"scores", est.downbeatScores},
                               {"phraseBars", est.phraseBars},
                               {"phraseEvidence", est.phraseEvidence},
                               {"valid", est.valid}};
    std::printf("audio    %.3f s, %u Hz, %u ch, %zu frames\n", file->durationSeconds(), file->sampleRate(),
                file->channels(), track.frames().size());
    std::printf("tempo    %.3f bpm (confidence %.2f), %zu beats, first at %.3f s\n",
                static_cast<double>(track.beats().tempoBpm), static_cast<double>(track.beats().confidence),
                beats.size(), beats.empty() ? 0.0 : beats.front());
    std::printf("meter    downbeat = tracked beat %d (confidence %.2f; scores %.3f %.3f %.3f %.3f); phrase %d bars "
                "(evidence %.2f)\n",
                est.downbeat, static_cast<double>(est.downbeatConfidence), static_cast<double>(est.downbeatScores[0]),
                static_cast<double>(est.downbeatScores[1]), static_cast<double>(est.downbeatScores[2]),
                static_cast<double>(est.downbeatScores[3]), est.phraseBars, static_cast<double>(est.phraseEvidence));

    std::vector<double> onsets;
    for (const auto& f : track.frames()) {
        if (f.onset) {
            onsets.push_back(f.timeSeconds);
        }
    }
    std::printf("onsets   %zu\n", onsets.size());
    // The times themselves, in seconds (each the analysis frame the attack is stamped on): what
    // `audio.onset` fires on, for a generator that wants to place things on them.
    report["onsetTimes"] = onsets;

    analysis::Meter meter;
    meter.downbeat = barOffset >= 0 ? barOffset : est.downbeat;
    if (est.phraseBars > 0) {
        meter.phraseBars = est.phraseBars;
    }
    // The meter the engine would resolve with nothing pinned (or with `--bar-offset`), and the bar
    // lines it draws: bar 1 is `barTimes[0]`.
    const std::vector<double> barLines = meter.barTimes(beats);
    report["meter"] = {{"beatsPerBar", meter.beatsPerBar},
                       {"downbeatBeat", meter.downbeat},
                       {"phraseBars", meter.phraseBars},
                       {"firstDownbeatSeconds", barLines.empty() ? 0.0 : barLines.front()}};
    report["barTimes"] = barLines;
    if (gridBpm > 0.0) {
        const double bar = 4.0 * 60.0 / gridBpm;
        const std::vector<double> predicted = meter.barTimes(beats);
        std::vector<double> truth;
        for (double t = gridFirst; t <= beats.back() + 1e-6; t += bar) {
            if (t >= beats.front() - 0.05) { // the first bar line may sit just before the first tracked beat
                truth.push_back(t);
            }
        }
        // Every true bar line must have a predicted one within 30 ms, and nothing else predicted.
        double worst = 0.0;
        double sumAbs = 0.0;
        std::size_t within = 0;
        for (const double t : truth) {
            const auto it = std::lower_bound(predicted.begin(), predicted.end(), t);
            double e = 1e9;
            if (it != predicted.end()) {
                e = std::min(e, std::fabs(*it - t));
            }
            if (it != predicted.begin()) {
                e = std::min(e, std::fabs(t - *std::prev(it)));
            }
            worst = std::max(worst, e);
            sumAbs += e;
            within += e <= 0.030 ? 1u : 0u;
        }
        const Score s = match(predicted, truth, 0.030);
        std::printf("bars     %zu true bar lines, %zu predicted; %zu within 30 ms; mean |error| %.1f ms, worst %.1f "
                    "ms; predicted bar lines off the grid: %zu\n",
                    truth.size(), predicted.size(), within, 1000.0 * sumAbs / static_cast<double>(truth.size()),
                    1000.0 * worst, predicted.size() - s.hits);
        report["bars"] = {{"truth", truth.size()},     {"predicted", predicted.size()},
                          {"within30ms", within},      {"meanAbsMs", 1000.0 * sumAbs / static_cast<double>(truth.size())},
                          {"worstMs", 1000.0 * worst}, {"offGrid", predicted.size() - s.hits}};
    }
    std::vector<Section> sections = sectionsPath.empty() ? std::vector<Section>{} : readSections(sectionsPath);
    if (sections.empty()) {
        const double d = file->durationSeconds();
        for (int q = 0; q < 4; ++q) {
            sections.push_back(Section{"q" + std::to_string(q + 1), d * q / 4.0, d * (q + 1) / 4.0});
        }
    }
    std::printf("%-18s %8s %8s %6s %6s %8s %6s  bands dB (bass lowMid mid highMid treble)\n", "section",
                "start", "end", "energy", "rate/s", "brightHz", "width");
    nlohmann::json spans = nlohmann::json::array();
    for (const Section& s : sections) {
        const analysis::SpanProfile p = analysis::profileSpan(track, s.start, s.end);
        std::printf("%-18s %8.2f %8.2f %6.3f %6.2f %8.0f %6.3f ", s.name.c_str(), s.start, s.end,
                    static_cast<double>(p.energy), static_cast<double>(p.onsetRate),
                    static_cast<double>(p.brightnessHz), static_cast<double>(p.width));
        for (std::size_t b = 0; b < p.bandCount; ++b) {
            std::printf(" %6.1f", static_cast<double>(p.bandDb[b]));
        }
        std::printf("\n");
        // The same keys `director.inspect_scene` and a song plan's "audio" use (ADR-899).
        nlohmann::json span = analysis::spanProfileToJson(p);
        span["name"] = s.name;
        span["start"] = s.start;
        span["end"] = s.end;
        spans.push_back(std::move(span));
    }
    report["sections"] = std::move(spans);
    if (!jsonPath.empty()) {
        std::ofstream(jsonPath) << report.dump(1) << "\n";
    }
    return 0;
}
