#include "analysis/stem_analysis.hpp"

#include "sonic/response.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::analysis {

namespace {

const sonic::ResponseSettings& responseDefaults() {
    static const sonic::ResponseSettings settings{};
    return settings;
}

double alpha(double dt, double tau) {
    return tau > 1e-6 ? 1.0 - std::exp(-dt / tau) : 1.0;
}

} // namespace

StemAnalyzer::StemAnalyzer(StemAnalysisConfig config)
    : config_(std::move(config))
    , analyzer_(config_.analyzer) {
    const AnalyzerConfig& a = analyzer_.config();
    hopSeconds_ = static_cast<double>(a.hopSize) / static_cast<double>(a.sampleRate);
    binHz_ = static_cast<float>(a.sampleRate) / static_cast<float>(a.windowSize);
    const std::size_t bins = analyzer_.binCount();
    onsetBins_ = std::clamp<std::size_t>(static_cast<std::size_t>(config_.onset.maxHz / binHz_), 2, bins);
    previousSpectrum_.assign(bins, 0.0f);
    const std::size_t median = std::max<std::uint32_t>(config_.onset.medianFrames, 1);
    fluxHistory_.assign(median, 0.0f);
    medianScratch_.assign(median, 0.0f);
}

void StemAnalyzer::push(std::span<const float> mono, std::span<const float> side) {
    analyzer_.push(mono, side);
}

void StemAnalyzer::reset(std::uint64_t startFrame) {
    analyzer_.reset(startFrame);
    causal_.reset();
    levelDb_ = referenceDb_ = -120.0f;
    referencePower_ = 0.0;
    heardSeconds_ = 0.0;
    levelPrimed_ = false;
    std::fill(previousSpectrum_.begin(), previousSpectrum_.end(), 0.0f);
    haveSpectrum_ = false;
    fluxCount_ = 0;
    lastFlux_ = 0.0f;
    lastOnsetTime_ = -1.0;
}

float StemAnalyzer::followLevel(float rms) {
    // ADR-1510's level chain, per source (see the header). Mirrors `sonic::ResponseModel::step`'s level and reference
    // with its default constants.
    const sonic::ResponseSettings& s = responseDefaults();
    const double dt = hopSeconds_;
    const double power = 2.0 * static_cast<double>(rms) * static_cast<double>(rms); // sine-amplitude units
    const float db = power > 1e-12 ? static_cast<float>(10.0 * std::log10(power)) : -120.0f;
    const float gated = std::max(db, s.gateDb);
    if (!levelPrimed_) {
        levelDb_ = referenceDb_ = s.gateDb;
        levelPrimed_ = true;
    }
    if (db > s.gateDb) {
        heardSeconds_ += dt;
        const double a = std::max(alpha(dt, static_cast<double>(s.referenceSeconds)), dt / heardSeconds_);
        referencePower_ += a * (power - referencePower_);
        const float source = static_cast<float>(10.0 * std::log10(std::max(referencePower_, 1e-12)));
        if (heardSeconds_ <= 2.0 * static_cast<double>(s.referenceSeconds)) {
            referenceDb_ = source;
        } else {
            const double tau = static_cast<double>(source > referenceDb_ ? s.referenceRise : s.referenceFall);
            referenceDb_ += static_cast<float>(alpha(dt, tau)) * (source - referenceDb_);
        }
    }
    const double tau = static_cast<double>(gated > levelDb_ ? s.levelAttack : s.levelRelease);
    levelDb_ += static_cast<float>(alpha(dt, tau)) * (gated - levelDb_);
    return sonic::conditionLevel(levelDb_, referenceDb_, s.levelWindow, 0.5f, s);
}

void StemAnalyzer::detectOnset(const AnalysisFrame& f, StemFrame& out) {
    const StemOnsetConfig& c = config_.onset;
    double rise = 0.0;
    const std::size_t bins = std::min(onsetBins_, f.spectrum.size());
    if (haveSpectrum_) {
        for (std::size_t k = 1; k < bins; ++k) {
            rise += static_cast<double>(std::max(0.0f, f.spectrum[k] - previousSpectrum_[k]));
        }
        rise /= static_cast<double>(bins - 1);
    }
    std::copy(f.spectrum.begin(), f.spectrum.begin() + static_cast<std::ptrdiff_t>(bins), previousSpectrum_.begin());
    haveSpectrum_ = true;
    const auto flux = static_cast<float>(rise);
    // The median of the flux history (the frames before this one).
    float median = 0.0f;
    const std::size_t n = std::min(fluxCount_, fluxHistory_.size());
    if (n > 0) {
        std::copy(fluxHistory_.begin(), fluxHistory_.begin() + static_cast<std::ptrdiff_t>(n), medianScratch_.begin());
        auto* first = medianScratch_.data();
        auto* mid = first + n / 2;
        std::nth_element(first, mid, first + n);
        median = (n % 2) == 1 ? *mid : 0.5f * (*std::max_element(first, mid) + *mid);
    }
    const float threshold = median * c.scale + c.delta;
    out.onsetStrength = flux / threshold;
    const bool refractory = lastOnsetTime_ < 0.0 || f.timeSeconds - lastOnsetTime_ >= static_cast<double>(c.minIntervalSeconds);
    out.onset = out.onsetStrength >= 1.0f && flux >= lastFlux_ && refractory;
    if (out.onset) {
        lastOnsetTime_ = f.timeSeconds;
    }
    fluxHistory_[fluxCount_ % fluxHistory_.size()] = flux;
    ++fluxCount_;
    lastFlux_ = flux;
}

bool StemAnalyzer::pop(StemFrame& out) {
    if (!analyzer_.pop(scratch_)) {
        return false;
    }
    const AnalysisFrame& f = scratch_;
    out = StemFrame{};
    out.timeSeconds = f.timeSeconds;
    out.rms = f.rms;
    out.peak = f.peak;
    detectOnset(f, out);
    out.centroid = f.centroidNorm;
    out.stereo = f.stereo;
    out.width = f.stereo ? f.width : 0.0f;
    for (std::size_t b = 0; b < out.bands.size(); ++b) {
        out.bands[b] = b < f.bandCount ? f.bands[b] : 0.0f;
    }
    out.level = followLevel(f.rms);
    if (config_.hits) {
        causal_.process(scratch_, binHz_, hopSeconds_);
        const CausalOnsets& c = scratch_.causal;
        out.kick = c.hit[static_cast<std::size_t>(HitClass::Kick)];
        out.kickStrength = c.strength[static_cast<std::size_t>(HitClass::Kick)];
        out.snare = c.hit[static_cast<std::size_t>(HitClass::Snare)];
        out.snareStrength = c.strength[static_cast<std::size_t>(HitClass::Snare)];
        out.hat = c.hit[static_cast<std::size_t>(HitClass::Hat)];
        out.hatStrength = c.strength[static_cast<std::size_t>(HitClass::Hat)];
    }
    return true;
}

StemTrack StemTrack::analyze(const audio::AudioFile& file, StemAnalysisConfig config) {
    config.analyzer.sampleRate = file.sampleRate();
    StemTrack track;
    track.sampleRate_ = file.sampleRate();
    track.duration_ = file.durationSeconds();
    track.hits_ = config.hits;
    StemAnalyzer analyzer(config);
    const AnalyzerConfig& cfg = analyzer.config().analyzer;
    track.hopSeconds_ = static_cast<double>(cfg.hopSize) / static_cast<double>(cfg.sampleRate);
    const auto mono = file.mono();
    if (mono.size() >= cfg.windowSize) {
        track.frames_.reserve((mono.size() - cfg.windowSize) / cfg.hopSize + 1);
    }
    // The side channel of a stereo stem, (L - R) / 2 of its first two channels, for `width` -- as the mix's
    // AnalysisTrack measures `audio.width`.
    std::vector<float> side;
    if (file.channels() >= 2) {
        const auto interleaved = file.interleaved();
        const std::size_t channels = file.channels();
        side.resize(mono.size());
        for (std::size_t i = 0; i < side.size(); ++i) {
            side[i] = 0.5f * (interleaved[i * channels] - interleaved[i * channels + 1]);
        }
    }
    StemFrame frame;
    for (std::size_t offset = 0; offset < mono.size(); offset += cfg.hopSize) {
        const std::size_t take = std::min<std::size_t>(cfg.hopSize, mono.size() - offset);
        analyzer.push(mono.subspan(offset, take),
                      side.empty() ? std::span<const float>{} : std::span<const float>(side).subspan(offset, take));
        while (analyzer.pop(frame)) {
            track.frames_.push_back(frame);
        }
    }
    return track;
}

} // namespace avgen::analysis
