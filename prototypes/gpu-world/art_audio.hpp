// GPU World research spike, Phase 3 (art) -- DISPOSABLE. Real audio for the art prototype.
//
// A whole track is analysed once with production's offline analysis (analysis::AnalysisTrack, the
// same analyzer, beat tracker and band-onset pass a render uses) and reduced to what the GPU systems
// read: a 64-bin log-frequency spectrogram of the WHOLE song (so any element can read any past moment
// of it), band envelopes, and the kick / snare / hat onset lists. Everything is indexed by timeline
// seconds, so a frame is a pure function of t and seeks exactly.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace gpuworld {

struct SongAnalysis {
    static constexpr int kBins = 64;
    float hopRate = 0.0f; // spectrogram rows per second
    float t0 = 0.0f;      // time of row 0
    int hops = 0;
    double duration = 0.0;
    std::vector<float> spec;       // hops * kBins, 0..1 per-bin normalised over the track, fast-release smoothed
    std::vector<glm::vec4> env0;   // per row: bass, lowMid, mid, high (attack/release envelopes, 0..1)
    std::vector<glm::vec4> env1;   // per row: air, energy, beatPhase, rms
    std::vector<float> kickT, kickS, snareT, snareS, hatT, hatS;
    double analyseSeconds = 0.0;   // wall time the analysis took (0 when read from the cache)
};

// Loads `path`, analyses it (or reads `cachePath` if it exists and matches), returns the reduction.
SongAnalysis loadSong(const std::string& path, const std::string& cachePath);

struct AudioAtT {
    glm::vec4 env0{0.0f}, env1{0.0f};
    float hopF = 0.0f;
    float rms = 0.0f, kickEnv = 0.0f, snareEnv = 0.0f, hatEnv = 0.0f;
    float kickT[8], kickS[8]; // the last eight kicks at or before t, newest first (-1e4 = none)
    float snareT[4], snareS[4];
};

AudioAtT sampleSong(const SongAnalysis& song, double t);

} // namespace gpuworld
