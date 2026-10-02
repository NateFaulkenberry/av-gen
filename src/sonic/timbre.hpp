#pragma once

// Raw timbre measurements from one analysis frame (ADR-1020, Sonic Garden POC).
//
// Every quantity here is computed from what `analysis::Analyzer` already produced -- the sine-normalised magnitude
// spectrum and a few scalar fields -- so no second FFT runs, and a frame's features are a pure function of that
// frame. That makes the offline pass (`analyzeTimbre` over a whole `AnalysisTrack`) deterministic, and lets live
// input (Phase 7) call the same `TimbreAnalyzer::analyze` on the analysis thread.
//
// Definitions follow Essentia and librosa where one exists (docs/prototypes/sonic-garden/RESEARCH.md §4.1):
// spread about the centroid, 85% rolloff, flatness as geometric over arithmetic mean power, inharmonicity as the
// energy-weighted misfit of spectral peaks against a harmonic series, sensory dissonance as Sethares' Plomp-Levelt
// pair sum. The pitch estimate is multi-f0 (Klapuri's harmonic-sum salience with estimate-and-cancel) so that a
// chord of harmonic notes reads harmonic, not inharmonic.
//
// Nothing here is normalised to a running maximum: each value is in physical units or a fixed ratio, so two
// different sounds stay different. Mapping to 0..1 is the Sonic Character's job (character.hpp).

#include "analysis/analyzer.hpp"
#include "analysis/analysis_track.hpp"

#include <cstdint>
#include <vector>

namespace avgen::sonic {

struct TimbreFeatures {
    bool silent = true;          // no spectral power at all
    float loudnessDb = -120.0f;  // frame rms in dBFS (a full-scale sine is -3 dB)
    float centroidHz = 0.0f;
    float bandwidthHz = 0.0f;    // magnitude-weighted standard deviation about the centroid
    float rolloffHz = 0.0f;      // 85% of the power lies below
    float flatness = 0.0f;       // 0 tonal .. 1 white, over 60 Hz - 16 kHz
    float lowRatioDb = -120.0f;  // power below 250 Hz over the total
    float highRatioDb = -120.0f; // power above 2 kHz over the total (the analyzer's ADR-897 field)
    float relativeFlux = 0.0f;   // the analyzer's level-free flux
    float f0Hz = 0.0f;           // the most salient fundamental, 0 when none
    float pitchConfidence = 0.0f; // share of peak energy the first fundamental explains, 0..1
    float harmonicity = 0.0f;    // share of ALL spectral power explained by up to maxF0 harmonic series, 0..1
    float inharmonicity = 0.0f;  // energy-weighted misfit of the prominent peaks, 0 harmonic .. 1 none fit
    float tonalness = 0.0f;      // share of spectral power in prominent peaks, 0 noise .. 1 pure partials
    float dissonance = 0.0f;     // Plomp-Levelt roughness of the strongest peaks, amplitude-normalised
    float peakCount = 0.0f;      // prominent peaks
    int f0Count = 0;             // fundamentals found
    float width = 0.0f;          // the analyzer's stereo side/mid
    bool stereo = false;
    analysis::CausalOnsets causal; // ADR-1060: the frame's causal onsets (the response model's hits and levels)
};

struct TimbreConfig {
    float minF0Hz = 40.0f;
    float maxF0Hz = 2000.0f;
    int maxF0Count = 4;          // fundamentals estimated per frame (estimate-and-cancel)
    int maxPeaks = 48;           // strongest prominent peaks kept
    float peakRangeDb = 60.0f;   // peaks more than this below the strongest are ignored
    float prominenceDb = 8.0f;   // a peak must stand this far above its neighbourhood's mean
    float harmonicTolerance = 0.03f; // a peak within 3% of h*f0 (or one bin) is harmonic h
    float nextF0Ratio = 0.25f;
    int minHarmonics = 3;        // partials a second (third, ...) fundamental must explain   // a further fundamental must reach this share of the first's salience
    int dissonancePeaks = 32;
};

class TimbreAnalyzer {
public:
    TimbreAnalyzer(std::uint32_t sampleRate, std::uint32_t windowSize, TimbreConfig config = {});

    // Features of one frame. Allocation-free after the first call.
    [[nodiscard]] TimbreFeatures analyze(const analysis::AnalysisFrame& frame);

    [[nodiscard]] const TimbreConfig& config() const { return config_; }

    struct Peak {
        float hz = 0.0f;
        float amplitude = 0.0f; // linear, sine-normalised
        float energy = 0.0f;    // power in the main lobe (bins k-2..k+2)
        int f0 = -1;            // which fundamental explains it, -1 none
        float misfit = 1.0f;    // 0 exactly harmonic .. 1 at the tolerance edge (or unexplained)
    };
    // The prominent peaks of the last analysed frame, by frequency (tests and the diagnostic view).
    [[nodiscard]] const std::vector<Peak>& peaks() const { return peaks_; }
    [[nodiscard]] const std::vector<float>& fundamentals() const { return f0s_; }

private:
    void findPeaks(const std::vector<float>& magnitude);
    void estimateFundamentals(TimbreFeatures& out, double totalPower);
    float salience(std::size_t candidate, int minMatched) const;
    [[nodiscard]] int nearestPeak(float hz, float toleranceHz) const;

    std::uint32_t sampleRate_;
    std::uint32_t windowSize_;
    float binHz_;
    TimbreConfig config_;
    std::vector<float> db_;
    std::vector<Peak> peaks_;
    std::vector<Peak> scratch_;
    std::vector<float> candidates_;
    std::vector<float> f0s_;
    std::vector<float> residual_; // per-peak amplitude still unexplained during estimate-and-cancel
    // candidate x harmonic -> peak index, -1 no peak, kNoHarmonic past the top harmonic
    static constexpr int kMaxHarmonics = 30;
    static constexpr std::int16_t kNoHarmonic = -2;
    std::vector<std::int16_t> matches_;
};

// The whole track, one entry per analysis frame (the same index). Empty for an empty track.
[[nodiscard]] std::vector<TimbreFeatures> analyzeTimbre(const analysis::AnalysisTrack& track, TimbreConfig config = {});

} // namespace avgen::sonic
