// THE ASTRAL FORGE (ADR-1221) -- the song as the conductor reads it. Real audio, analysed once with production's offline analysis
// (analysis::AnalysisTrack: the same analyzer, beat tracker and band-onset pass a render uses, plus
// analysis::detectStructure for sections and repetition groups). Reduced to what the conductor and the
// GPU read. Everything is indexed by timeline seconds, so the conductor is a pure function of t.
// (Adapted from prototypes/gpu-world/art_audio.*, extended with beats, sections and a spectral centroid.)
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace avgen::analysis {
class AnalysisTrack;
}

namespace avgen::astral {

struct Section {
    double start = 0.0, end = 0.0;
    int group = -1;       // repetition group (the same music returns with the same group)
    float energy = 0.0f;  // 0..1
    float density = 0.0f; // 0..1 onsets/s relative
    std::string label;
};

struct SongAnalysis {
    static constexpr int kBins = 64;
    float hopRate = 0.0f;
    float t0 = 0.0f;
    int hops = 0;
    double duration = 0.0;
    float tempoBpm = 0.0f;
    std::vector<float> spec;      // hops * kBins, 0..1 per-bin normalised, loudness folded back in
    std::vector<glm::vec4> env0;  // bass, lowMid, mid, high
    std::vector<glm::vec4> env1;  // air, energy, beatPhase, rms
    std::vector<float> centroid;  // per row, 0..1 (log-frequency spectral centroid of the normalised spectrum)
    std::vector<float> kickT, kickS, snareT, snareS, hatT, hatS;
    std::vector<double> beats;
    std::vector<Section> sections;
    std::vector<float> novelty;   // beat-synchronous Foote novelty, one per beat
    double analyseSeconds = 0.0;
};

// From a track production already analysed (an offline render's or the editor's track): no second analysis.
// `durationSeconds` is the audio's length (the track's frames may stop short of it).
SongAnalysis buildSong(const analysis::AnalysisTrack& track, double durationSeconds);
// The prototype's path: analyse a file (cached at `cachePath` when non-empty). Exits on an unreadable file.
SongAnalysis loadSong(const std::string& path, const std::string& cachePath);

struct AudioAtT {
    glm::vec4 env0{0.0f}, env1{0.0f};
    float hopF = 0.0f, rms = 0.0f, centroid = 0.0f;
    float kickEnv = 0.0f, snareEnv = 0.0f, hatEnv = 0.0f;
    float lastKick = -1e4f, lastSnare = -1e4f, lastHat = -1e4f;     // times
    float lastKickS = 0.0f, lastSnareS = 0.0f;
};

AudioAtT sampleSong(const SongAnalysis& song, double t);

} // namespace avgen::astral
