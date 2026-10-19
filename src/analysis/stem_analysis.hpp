#pragma once

// Per-source analysis for stems and live input channels (stems-multichannel, ADR-1520).
//
// One analysis source -- an offline stem file or a live input channel (or linked pair) -- yields a stream of
// `StemFrame`s: the main mix's `Analyzer` run on that source alone, reduced to the fields the `stem.<key>.*`
// vocabulary publishes (signals/stem_signals.hpp). The same `StemAnalyzer` serves both paths, so a stem file and the
// same audio arriving on an input channel publish the same numbers.
//
// Deliberately NOT here: the beat tracker, the meter, the ADR-898 band onsets, the Sonic timbre stage. Those read the
// main mix only (ADR-1520 §scope).
//
// Two tiers, chosen from measured cost (docs/prototypes/stems-multichannel/02-research-report.md §9.1):
//   levels (always): the Analyzer -- rms, peak, level, onset, the five bands, centroid, width. ~16 us per hop.
//   hits (opt-in):   + the ADR-1060 causal kick/snare/hat classifier. ~80-100 us more per hop.
// A time-domain-only tier would save ~15 us per hop per source, which is not worth a third tier.

#include "analysis/analyzer.hpp"
#include "analysis/causal_onsets.hpp"
#include "audio/audio_file.hpp"
#include "audio/stem_source.hpp"
#include "core/error.hpp"

#include <filesystem>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

// One analysis hop of one source. Times are the SOURCE's own clock (window centre); the caller adds the source's
// timeline offset.
struct StemFrame {
    double timeSeconds = 0.0;
    float rms = 0.0f;           // 0..1 over the 2048-sample window (full-scale sine 0.707); the raw envelope
    float peak = 0.0f;          // 0..1 over the window
    float level = 0.0f;         // the followed level against the source's own loudness reference (ADR-1510's law), 0..1
    float onsetStrength = 0.0f; // log-spectral flux over its adaptive threshold (>= 1 is over), 0..~4
    float centroid = 0.0f;      // log-normalised spectral centroid, 0..1
    float width = 0.0f;         // side/mid RMS, meaningful when `stereo`
    std::array<float, 5> bands{}; // bass, lowMid, mid, highMid, treble -- auto-gained (running max) like audio.<band>
    bool onset = false;         // an attack in this hop (see `StemOnsetConfig`)
    bool kick = false, snare = false, hat = false; // hits tier only
    float kickStrength = 0.0f, snareStrength = 0.0f, hatStrength = 0.0f;
    bool stereo = false;        // the source carried a side channel (a stereo stem, a linked input pair)
    [[nodiscard]] bool operator==(const StemFrame&) const = default;
};

// The stem onset: half-wave-rectified flux of the LOG spectrum (the Analyzer's 60 dB display spectrum, 0..1 per bin),
// averaged over the bins below `maxHz`, against an adaptive threshold -- median of the last `medianFrames` times
// `scale`, plus `delta` -- with the Analyzer's rising-edge and refractory rules.
//
// Not the main mix's `audio.onset` (linear flux), measured on the stem fixture: on a sustained 55 Hz bass note the
// linear flux swings 0.005-0.18 every few hops (the Hann window's negative-frequency image beating against a tone 2.3
// bins from DC) and fired an onset every ~75 ms for the length of the note. The log flux of the same note stays under
// 0.0007 while a 10 ms bass attack reads 0.0068, a pluck 0.016, a hat 0.037 and a kick 0.12 -- and it does not move
// with the stem's level, which a stem's quiet passages need.
struct StemOnsetConfig {
    float maxHz = 16000.0f;
    std::uint32_t medianFrames = 11;
    float scale = 1.5f;
    float delta = 0.002f;
    float minIntervalSeconds = 0.06f;
};

// `level` is read the ADR-1510 way, not on a fixed dB window: the window's broadband level (dBFS, a full-scale sine
// 0 dB) is followed in dB with `response.level`'s attack and release (20 ms / 1.0 s), and read in `response.level`'s
// window (6.5 dB above, 12 dB span) below THIS SOURCE's own loudness reference -- the gated level's power over 3 s,
// warmed as the plain average of what has been heard, then followed up over 2 s and down over 60 s, never under
// -40 dBFS; below the -60 dBFS gate it reads 0 and the reference holds. Every constant is `sonic::ResponseSettings`'
// default, read from there, so a stem's level and `response.level` mean the same thing. A fixed -60..0 dBFS scale was
// the first version of this signal and was dropped for the reason ADR-1510 gives: a mastered stem lives in its top
// sixth and moves a tenth of the range.

struct StemAnalysisConfig {
    AnalyzerConfig analyzer{};
    StemOnsetConfig onset{};
    bool hits = false;
};

// Streaming: feed mono samples (and optionally the side channel, one per mono sample) in any chunk size; pop frames.
// Deterministic and chunking-invariant like the Analyzer it wraps.
class StemAnalyzer {
public:
    explicit StemAnalyzer(StemAnalysisConfig config);
    void push(std::span<const float> mono, std::span<const float> side = {});
    bool pop(StemFrame& out);
    // Clears all history; the next sample pushed is PCM frame `startFrame` of the source.
    void reset(std::uint64_t startFrame = 0);
    [[nodiscard]] const StemAnalysisConfig& config() const { return config_; }

private:
    void detectOnset(const AnalysisFrame& frame, StemFrame& out);
    float followLevel(float rms);

    StemAnalysisConfig config_;
    Analyzer analyzer_;
    CausalOnsetDetector causal_;
    AnalysisFrame scratch_;
    // The level (ADR-1510's law, per source).
    float levelDb_ = -120.0f;
    float referenceDb_ = -120.0f;
    double referencePower_ = 0.0;
    double heardSeconds_ = 0.0;
    bool levelPrimed_ = false;
    // The onset (StemOnsetConfig).
    std::vector<float> previousSpectrum_;
    bool haveSpectrum_ = false;
    std::vector<float> fluxHistory_; // ring of the last medianFrames
    std::size_t fluxCount_ = 0;
    std::vector<float> medianScratch_;
    float lastFlux_ = 0.0f;
    double lastOnsetTime_ = -1.0;
    std::size_t onsetBins_ = 0;
    float binHz_ = 0.0f;
    double hopSeconds_ = 0.0;
};

// A whole stem file, analysed once at its own sample rate (no resampling: frame times are seconds, bands are Hz).
// Holds only the compact frames; the PCM is not kept (a stem is never played).
class StemTrack {
public:
    static StemTrack analyze(const audio::AudioFile& file, StemAnalysisConfig config);
    // The same analysis while decoding: the file's PCM is never held, so N stems analysed together cost N chunk
    // buffers, not N files (measured: 8 stems of 3.5 min held at once raised the peak RSS by ~1.2 GB). `facts` gets what
    // reading found (rate, channels, length, peak, non-finite samples, silence, format) for the stem's report.
    static Result<StemTrack> analyzeFile(const std::filesystem::path& path, StemAnalysisConfig config,
                                         audio::StemFileFacts& facts);

    [[nodiscard]] const std::vector<StemFrame>& frames() const { return frames_; }
    [[nodiscard]] double durationSeconds() const { return duration_; }
    [[nodiscard]] std::uint32_t sampleRate() const { return sampleRate_; }
    [[nodiscard]] double hopSeconds() const { return hopSeconds_; }
    [[nodiscard]] bool hits() const { return hits_; }

private:
    std::vector<StemFrame> frames_;
    double duration_ = 0.0;
    double hopSeconds_ = 0.0;
    std::uint32_t sampleRate_ = 0;
    bool hits_ = false;
};

} // namespace avgen::analysis
