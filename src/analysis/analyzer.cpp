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

void Analyzer::push(std::span<const float> mono) {
    const std::size_t windowSize = config_.windowSize;
    const std::size_t hopSize = config_.hopSize;
    std::size_t offset = 0;
    // The fifo never holds more than one window, so it never reallocates after construction and
    // the frame sequence depends only on the sample stream, not on how it was chunked.
    while (offset < mono.size()) {
        const std::size_t room = windowSize - fifo_.size();
        const std::size_t take = std::min(room, mono.size() - offset);
        fifo_.insert(fifo_.end(), mono.begin() + static_cast<std::ptrdiff_t>(offset),
                     mono.begin() + static_cast<std::ptrdiff_t>(offset + take));
        offset += take;
        if (fifo_.size() == windowSize) {
            computeFrame();
            fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(hopSize));
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
    for (std::size_t b = 0; b < impl.bandCount; ++b) {
        const auto& range = impl.bandRanges[b];
        double energy = 0.0;
        for (std::size_t k = range.first; k < range.last; ++k) {
            const float m = frame.magnitude[k];
            energy += static_cast<double>(m) * static_cast<double>(m);
        }
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
    if (havePrevious_) {
        double flux = 0.0;
        for (std::size_t k = 0; k < bins; ++k) {
            const float d = frame.magnitude[k] - previousMagnitude_[k];
            if (d > 0.0f) {
                flux += static_cast<double>(d);
            }
        }
        frame.flux = std::min(1.0f, static_cast<float>(flux));
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

    ready_.push_back(std::move(frame));
}

} // namespace avgen::analysis
