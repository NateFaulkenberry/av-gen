#include "analysis/analyzer.hpp"

#include "analysis/fft.hpp"
#include "analysis/window.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::analysis {

namespace {

constexpr float kLogFloor = 1e-6f; // added before log10 so silence maps to -120 dB, not -inf
constexpr float kSpectrumRangeDb = 60.0f;
constexpr float kCentroidLowHz = 20.0f;

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace

std::vector<BandDefinition> AnalyzerConfig::defaultBands() {
    return {
        {"bass", 20.0f, 150.0f},       {"lowMid", 150.0f, 400.0f},    {"mid", 400.0f, 2000.0f},
        {"highMid", 2000.0f, 6000.0f}, {"treble", 6000.0f, 16000.0f},
    };
}

float bandLevelFromPower(double power) {
    if (!(power > 0.0)) {
        return 0.0f;
    }
    const double db = 10.0 * std::log10(power);
    return clamp01(static_cast<float>(1.0 - db / static_cast<double>(kBandLevelFloorDb)));
}

float energyComposite(const EnergyTerms& terms) {
    if (terms.silent) {
        return 0.0f;
    }
    const auto on = [](float value, float lo, float hi) { return clamp01((value - lo) / (hi - lo)); };
    // The fixed ranges are where these measures sit in produced music, dark to bright: the high band
    // from a low-passed break (-34 dB) to a riser's crest (-17 dB) on "Rebuild", the centroid from a
    // pull-back's 620 Hz to the crest's 6 kHz. A term past its range saturates rather than wraps.
    double sum = 0.0;
    double weight = 0.0;
    const auto add = [&](float term, double w) {
        sum += static_cast<double>(term) * w;
        weight += w;
    };
    add(on(terms.highRatioDb, -36.0f, -12.0f), 2.0);
    const float octaves = terms.centroidHz > 0.0f ? std::log2(terms.centroidHz / 250.0f) / std::log2(24.0f) : 0.0f;
    add(clamp01(octaves), 2.0);
    add(on(terms.relativeFlux, 0.08f, 0.24f), 1.0);
    if (terms.hasOnsetRate) {
        add(on(terms.onsetRate, 2.0f, 12.0f), 1.0);
    }
    if (terms.hasWidth) {
        add(on(terms.width, 0.1f, 0.4f), 1.0);
    }
    return weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
}

// FFT, window and every preallocated scratch buffer. Immutable after construction except for
// the scratch arrays, which are only touched by computeFrame().
struct Analyzer::Impl {
    struct BandRange {
        std::size_t first = 0; // inclusive bin
        std::size_t last = 0;  // exclusive bin
    };

    explicit Impl(const AnalyzerConfig& config)
        : fft(config.windowSize)
        , window(hannWindow(config.windowSize))
        , gain(coherentGain(window))
        , windowed(config.windowSize, 0.0f)
        , binFrequency(config.windowSize / 2 + 1, 0.0f)
        , medianScratch(config.onsetMedianFrames, 0.0f) {
        const auto bins = binFrequency.size();
        const double hzPerBin =
            static_cast<double>(config.sampleRate) / static_cast<double>(config.windowSize);
        for (std::size_t k = 0; k < bins; ++k) {
            binFrequency[k] = static_cast<float>(hzPerBin * static_cast<double>(k));
        }
        // Each band covers the bins whose centre frequency f satisfies low <= f < high.
        bandCount = std::min(config.bands.size(), kMaxBands);
        for (std::size_t b = 0; b < bandCount; ++b) {
            const auto& def = config.bands[b];
            BandRange range;
            while (range.first < bins && binFrequency[range.first] < def.lowHz) {
                ++range.first;
            }
            range.last = range.first;
            while (range.last < bins && binFrequency[range.last] < def.highHz) {
                ++range.last;
            }
            bandRanges[b] = range;
        }
        hopSeconds = static_cast<double>(config.hopSize) / static_cast<double>(config.sampleRate);
        peakDecay = config.normalizationDecaySeconds > 0.0f
                        ? static_cast<float>(
                              std::exp(-hopSeconds / static_cast<double>(config.normalizationDecaySeconds)))
                        : 0.0f;
        nyquistHz = static_cast<float>(config.sampleRate) * 0.5f;
        centroidLogRange = std::log(nyquistHz / kCentroidLowHz);
        magnitudeScale = 1.0f / gain; // on top of the FFT's own 2/N
        const auto keep = [this](float seconds) {
            return seconds > 0.0f ? std::exp(-hopSeconds / static_cast<double>(seconds)) : 0.0;
        };
        levelKeep = keep(config.levelSmoothingSeconds);
        energyKeep = keep(config.energySmoothingSeconds);
        rateKeep = keep(config.onsetRateSeconds);
        rateImpulse = config.onsetRateSeconds > 0.0f ? 1.0 / static_cast<double>(config.onsetRateSeconds) : 0.0;
    }

    RealFFT fft;
    std::vector<float> window;
    float gain;
    std::vector<float> windowed;
    std::vector<float> binFrequency;
    std::vector<float> medianScratch;
    std::array<BandRange, kMaxBands> bandRanges{};
    std::size_t bandCount = 0;
    double hopSeconds = 0.0;
    float peakDecay = 0.0f;
    float nyquistHz = 0.0f;
    float centroidLogRange = 1.0f;
    float magnitudeScale = 1.0f;
    // ADR-897: per-hop retention of each one-pole, and what one onset adds to the leaky rate.
    double levelKeep = 0.0;
    double energyKeep = 0.0;
    double rateKeep = 0.0;
    double rateImpulse = 0.0;
};

Analyzer::Analyzer(AnalyzerConfig config)
    : config_(std::move(config)) {
    if (config_.windowSize < 2 || (config_.windowSize % 2) != 0) {
        log::warn("Analyzer: windowSize {} must be even and >= 2; using 2048", config_.windowSize);
        config_.windowSize = 2048;
    }
    if (config_.hopSize == 0 || config_.hopSize > config_.windowSize) {
        log::warn("Analyzer: hopSize {} must be in [1, windowSize]; clamping", config_.hopSize);
        config_.hopSize = std::clamp<std::uint32_t>(config_.hopSize, 1, config_.windowSize);
    }
    if (config_.sampleRate == 0) {
        log::warn("Analyzer: sampleRate 0 is invalid; using 48000");
        config_.sampleRate = 48000;
    }
    if (config_.bands.size() > kMaxBands) {
        log::warn("Analyzer: {} bands requested, only the first {} are used", config_.bands.size(),
                  kMaxBands);
    }
    if (config_.onsetMedianFrames == 0) {
        config_.onsetMedianFrames = 1;
    }
    impl_ = std::make_shared<Impl>(config_);
    fifo_.reserve(config_.windowSize);
    previousMagnitude_.assign(binCount(), 0.0f);
}

float Analyzer::binHz(std::size_t bin) const {
    return static_cast<float>(static_cast<double>(bin) * static_cast<double>(config_.sampleRate) /
                              static_cast<double>(config_.windowSize));
}

void Analyzer::push(std::span<const float> mono, std::span<const float> side) {
    const std::size_t windowSize = config_.windowSize;
    const std::size_t hopSize = config_.hopSize;
    if (!mono.empty() && side.size() != mono.size() && stereo_) {
        // One push without a side channel and the width is unknowable from here on: a window half
        // of whose side samples are missing would measure a narrower image than the audio has.
        stereo_ = false;
        sideFifo_.clear();
    }
    std::size_t offset = 0;
    // The fifo never holds more than one window, so it never reallocates after construction and
    // the frame sequence depends only on the sample stream, not on how it was chunked.
    while (offset < mono.size()) {
        const std::size_t room = windowSize - fifo_.size();
        const std::size_t take = std::min(room, mono.size() - offset);
        fifo_.insert(fifo_.end(), mono.begin() + static_cast<std::ptrdiff_t>(offset),
                     mono.begin() + static_cast<std::ptrdiff_t>(offset + take));
        if (stereo_) {
            sideFifo_.insert(sideFifo_.end(), side.begin() + static_cast<std::ptrdiff_t>(offset),
                             side.begin() + static_cast<std::ptrdiff_t>(offset + take));
        }
        offset += take;
        if (fifo_.size() == windowSize) {
            computeFrame();
            fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(hopSize));
            if (stereo_) {
                sideFifo_.erase(sideFifo_.begin(), sideFifo_.begin() + static_cast<std::ptrdiff_t>(hopSize));
            }
            fifoStartFrame_ += hopSize;
        }
    }
}

bool Analyzer::pop(AnalysisFrame& out) {
    if (ready_.empty()) {
        return false;
    }
    out = std::move(ready_.front());
    ready_.pop_front();
    return true;
}

void Analyzer::reset(std::uint64_t startFrameIndex) {
    fifo_.clear();
    sideFifo_.clear();
    stereo_ = true;
    bandPower_.fill(0.0);
    onsetRate_ = 0.0f;
    energy_ = 0.0f;
    haveLevels_ = false;
    fifoStartFrame_ = startFrameIndex;
    ready_.clear();
    std::fill(previousMagnitude_.begin(), previousMagnitude_.end(), 0.0f);
    fluxHistory_.clear();
    bandPeak_.fill(0.0f);
    lastOnsetTime_ = -1.0;
    lastFlux_ = 0.0f;
    prevFlux_ = 0.0f;
    havePrevious_ = false;
}

void Analyzer::computeFrame() {
    Impl& impl = *impl_;
    const std::size_t windowSize = config_.windowSize;
    const std::size_t bins = binCount();

    AnalysisFrame frame;
    frame.frameIndex = fifoStartFrame_ + windowSize / 2;
    frame.timeSeconds = static_cast<double>(frame.frameIndex) / static_cast<double>(config_.sampleRate);

    // ---- time-domain level: RMS and peak over the unwindowed window ----
    double sumSquares = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < windowSize; ++i) {
        const float x = fifo_[i];
        sumSquares += static_cast<double>(x) * static_cast<double>(x);
        peak = std::max(peak, std::fabs(x));
        impl.windowed[i] = x * impl.window[i];
    }
    frame.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(windowSize)));
    frame.peak = peak;

    // ---- magnitude spectrum, sine-normalised: |X| * 2 / (N * coherentGain) ----
    frame.magnitude.resize(bins);
    frame.spectrum.resize(bins);
    impl.fft.magnitude(impl.windowed, frame.magnitude);
    for (std::size_t k = 0; k < bins; ++k) {
        frame.magnitude[k] *= impl.magnitudeScale;
    }
    for (std::size_t k = 0; k < bins; ++k) {
        const float db = 20.0f * std::log10(frame.magnitude[k] + kLogFloor);
        frame.spectrum[k] = clamp01((db + kSpectrumRangeDb) / kSpectrumRangeDb);
    }

    // ---- bands: linear amplitude per band, then running-maximum normalisation ----
    frame.bandCount = static_cast<std::uint32_t>(impl.bandCount);
    std::array<double, kMaxBands> bandEnergy{};
    for (std::size_t b = 0; b < impl.bandCount; ++b) {
        const auto& range = impl.bandRanges[b];
        double energy = 0.0;
        for (std::size_t k = range.first; k < range.last; ++k) {
            const float m = frame.magnitude[k];
            energy += static_cast<double>(m) * static_cast<double>(m);
        }
        bandEnergy[b] = energy;
        const float raw = std::min(1.0f, static_cast<float>(std::sqrt(energy)));
        frame.bandsRaw[b] = raw;
        const float decayed = std::max(config_.normalizationFloor, bandPeak_[b] * impl.peakDecay);
        bandPeak_[b] = std::max(raw, decayed);
        frame.bands[b] = bandPeak_[b] > 0.0f ? raw / bandPeak_[b] : 0.0f;
    }

    // ---- spectral centroid ----
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t k = 0; k < bins; ++k) {
        const auto m = static_cast<double>(frame.magnitude[k]);
        weighted += static_cast<double>(impl.binFrequency[k]) * m;
        total += m;
    }
    if (total > 0.0) {
        frame.centroidHz = static_cast<float>(weighted / total);
        frame.centroidNorm =
            frame.centroidHz > 0.0f && impl.centroidLogRange > 0.0f
                ? clamp01(std::log(frame.centroidHz / kCentroidLowHz) / impl.centroidLogRange)
                : 0.0f;
    }

    // ---- half-wave-rectified spectral flux ----
    double rawFlux = 0.0;
    if (havePrevious_) {
        for (std::size_t k = 0; k < bins; ++k) {
            const float d = frame.magnitude[k] - previousMagnitude_[k];
            if (d > 0.0f) {
                rawFlux += static_cast<double>(d);
            }
        }
        frame.flux = std::min(1.0f, static_cast<float>(rawFlux));
    }
    std::copy(frame.magnitude.begin(), frame.magnitude.end(), previousMagnitude_.begin());
    havePrevious_ = true;

    // ---- onset: flux against an adaptive median threshold, rising edge, refractory time ----
    float median = 0.0f;
    if (!fluxHistory_.empty()) {
        const std::size_t n = fluxHistory_.size();
        std::copy(fluxHistory_.begin(), fluxHistory_.end(), impl.medianScratch.begin());
        auto* first = impl.medianScratch.data();
        auto* last = first + n;
        auto* mid = first + n / 2;
        std::nth_element(first, mid, last);
        if ((n % 2) == 1) {
            median = *mid;
        } else {
            const float upper = *mid;
            const float lower = *std::max_element(first, mid);
            median = 0.5f * (lower + upper);
        }
    }
    const float threshold = median * config_.onsetThresholdScale + config_.onsetThresholdDelta;
    frame.onsetStrength = threshold > 0.0f ? frame.flux / threshold : 0.0f;
    const bool refractoryElapsed =
        lastOnsetTime_ < 0.0 ||
        frame.timeSeconds - lastOnsetTime_ >= static_cast<double>(config_.minOnsetIntervalSeconds);
    frame.onset =
        frame.onsetStrength >= 1.0f && frame.flux >= lastFlux_ && frame.flux > 0.0f && refractoryElapsed;
    if (frame.onset) {
        lastOnsetTime_ = frame.timeSeconds;
    }

    fluxHistory_.push_back(frame.flux);
    while (fluxHistory_.size() > config_.onsetMedianFrames) {
        fluxHistory_.pop_front();
    }
    prevFlux_ = lastFlux_;
    lastFlux_ = frame.flux;

    // ---- ADR-897: the features that survive a flat master ----
    // What a limiter cannot move: how much of the spectrum is above 2 kHz, how much it changes
    // against its own size, how wide the image is -- and the band powers on a fixed scale, smoothed
    // but never divided by their own running maximum.
    double totalPower = 0.0;
    double highPower = 0.0;
    double magnitudeSum = 0.0;
    for (std::size_t k = 1; k < bins; ++k) {
        const auto m = static_cast<double>(frame.magnitude[k]);
        totalPower += m * m;
        magnitudeSum += m;
        if (impl.binFrequency[k] >= 2000.0f) {
            highPower += m * m;
        }
    }
    const bool silent = !(totalPower > 1e-12);
    frame.highRatioDb = silent ? -120.0f : static_cast<float>(10.0 * std::log10(std::max(highPower, 1e-12) / totalPower));
    frame.relativeFlux = magnitudeSum > 1e-9 ? static_cast<float>(rawFlux / magnitudeSum) : 0.0f;
    for (std::size_t b = 0; b < impl.bandCount; ++b) {
        const double power = bandEnergy[b] / kHannEnergyGain; // sine-amplitude units: 0 dB = full scale
        bandPower_[b] = haveLevels_ ? impl.levelKeep * bandPower_[b] + (1.0 - impl.levelKeep) * power : power;
        frame.bandLevels[b] = bandLevelFromPower(bandPower_[b]);
    }
    if (stereo_ && sideFifo_.size() == windowSize) {
        double sideSquares = 0.0;
        for (std::size_t i = 0; i < windowSize; ++i) {
            sideSquares += static_cast<double>(sideFifo_[i]) * static_cast<double>(sideFifo_[i]);
        }
        // Side over mid, both RMS over the unwindowed window: 0 for a mono source, 1 when the
        // channels are uncorrelated at equal level. The mono downmix *is* the mid channel.
        frame.width = sumSquares > 1e-12 ? static_cast<float>(std::sqrt(sideSquares / sumSquares)) : 0.0f;
        frame.stereo = true;
    }
    onsetRate_ = static_cast<float>(impl.rateKeep * static_cast<double>(onsetRate_) +
                                    (frame.onset ? impl.rateImpulse : 0.0));
    frame.onsetRate = onsetRate_;
    frame.onsetRateIsPercussive = false;
    // The streaming composite has no percussive onset rate -- the band onsets are an offline pass --
    // so it is the level-free spectral terms (and the width, when stereo). AnalysisTrack recomputes
    // it with the density term once the band onsets exist.
    EnergyTerms terms;
    terms.highRatioDb = frame.highRatioDb;
    terms.centroidHz = frame.centroidHz;
    terms.relativeFlux = frame.relativeFlux;
    terms.width = frame.width;
    terms.hasWidth = frame.stereo;
    terms.silent = silent;
    const float raw = energyComposite(terms);
    energy_ = haveLevels_ ? static_cast<float>(impl.energyKeep * static_cast<double>(energy_) +
                                               (1.0 - impl.energyKeep) * static_cast<double>(raw))
                          : raw;
    frame.energy = energy_;
    haveLevels_ = true;

    ready_.push_back(std::move(frame));
}

void LiveEventLatch::apply(AnalysisFrame& frame) {
    if (frame.liveSerial == 0) {
        return; // not a live runner's frame
    }
    if (frame.liveSerial < serial) {
        *this = LiveEventLatch{};
    }
    serial = frame.liveSerial;
    const auto latch = [](bool& flag, std::uint64_t stamp, std::uint64_t& last) {
        if (!flag || stamp == 0) {
            return; // not a carried live event (a file frame, or none)
        }
        if (stamp <= last) {
            flag = false; // already fired on an earlier render frame
        } else {
            last = stamp;
        }
    };
    latch(frame.onset, frame.onsetStamp, onset);
    latch(frame.beat, frame.beatStamp, beat);
    latch(frame.lowOnset, frame.lowStamp, low);
    latch(frame.midOnset, frame.midStamp, mid);
    latch(frame.highOnset, frame.highStamp, high);
}

} // namespace avgen::analysis
