#pragma once

// Band-limited onsets: kick, snare/clap, hat (ADR-898).
//
// The analyzer's one onset is broadband: it fires on a kick, a clap, a hat and a bass note alike,
// so a route that wanted "the kick" could not say so, and Glowmere Valley 3 hand-scored 475 kick
// times into a timeline source instead. These are three onset streams, one per register, detected
// offline over the whole track (AnalysisTrack's post-pass) where a detector may look a few frames
// ahead:
//
//   low  -- the kick. A percussive attack in 100-300 Hz. Two things make it a kick rather than the
//           bass: harmonic/percussive separation (median filters across time and across frequency,
//           Fitzgerald 2010) keeps a sustained or gliding bass note out of the attack band, and the
//           share of the low band that is percussive just after the attack tells a drum from a
//           plucked note. A kick on the tracked beat grid needs little evidence; one off it needs as
//           much as the strongest kick nearby -- "Rebuild"'s kick is 3-4x any off-beat hit, and in
//           most music the kick defines the beat.
//   mid  -- the snare or clap: an attack in 2-6 kHz.
//   high -- the hats: an attack in 6-16 kHz.
//
// Each is peak-picked against its own running median and scaled against the strongest of its band in
// the surrounding second, so a quiet passage still has onsets and a loud one does not have a hundred.
// Pure: the same frames and beats give the same onsets.

#include "analysis/analyzer.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace avgen::analysis {

struct BandOnsetConfig {
    // Peak picking, shared by the three bands: an attack must reach `threshold` times the band's
    // running median over +-`medianSeconds`, be the largest within its refractory window, and reach
    // `relative` of the band's largest value within +-`maxSeconds`.
    double medianSeconds = 0.5;
    double maxSeconds = 1.0;
    float threshold = 3.0f;
    // Below this power a band is silent, whatever its median says.
    double floorPower = 1e-9;

    // low (the kick)
    float lowFromHz = 100.0f;
    float lowToHz = 300.0f;
    float shareFromHz = 40.0f;     // the percussive share is measured over this wider low band...
    float shareToHz = 300.0f;
    int shareFrames = 4;           // ...across the attack and this many frames after it
    int hpssFrames = 17;           // harmonic/percussive median lengths (odd)
    int hpssBins = 17;
    double lowRefractorySeconds = 0.09;
    // Evidence required on the beat grid (within `gridBeats` of a tracked beat) and off it.
    float gridBeats = 0.12f;
    float onGridRelative = 0.05f;
    float onGridShare = 0.05f;
    float offGridRelative = 0.5f;
    float offGridShare = 0.5f;
    // With no beat grid at all: one middle setting.
    float noGridRelative = 0.2f;
    float noGridShare = 0.12f;
    // And against the whole track: a kick reaches this fraction (-10 dB) of the 90th percentile of
    // the track's candidate attacks. Without it a snare's low body is "the strongest thing nearby"
    // through any bar with no kick in it.
    float globalRelative = 0.1f;

    // mid (snare / clap) and high (hats)
    float midFromHz = 2000.0f;
    float midToHz = 6000.0f;
    float midRelative = 0.3f;
    double midRefractorySeconds = 0.05;
    float highFromHz = 6000.0f;
    float highToHz = 16000.0f;
    float highRelative = 0.1f;
    double highRefractorySeconds = 0.04;
};

struct BandOnset {
    std::size_t frame = 0; // index of the frame the attack is stamped on
    float strength = 0.0f; // 0..1, against the strongest of the band nearby
};

struct BandOnsets {
    std::vector<BandOnset> low;
    std::vector<BandOnset> mid;
    std::vector<BandOnset> high;
};

// `binHz` is the analyzer's bin spacing; `beatTimes` the tracked grid (may be empty) and
// `beatSeconds` its period (0 = unknown). Frames must carry magnitudes.
[[nodiscard]] BandOnsets detectBandOnsets(std::span<const AnalysisFrame> frames, float binHz,
                                          std::span<const double> beatTimes, double beatSeconds,
                                          const BandOnsetConfig& config = {});

// Stamps the onsets onto their frames, and derives what depends on them: the percussive onset rate
// (every band's onsets, one per 30 ms, through `AnalyzerConfig::onsetRateSeconds`) and the energy
// composite with its density term, smoothed as the analyzer smooths it. What AnalysisTrack's
// post-pass calls; exposed so a test can stamp chosen onsets.
void stampBandOnsets(std::span<AnalysisFrame> frames, const BandOnsets& onsets, const AnalyzerConfig& config);

} // namespace avgen::analysis
