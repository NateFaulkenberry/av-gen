#include "analysis/stem_analysis.hpp"

#include "analysis/audio_history_builder.hpp"
#include "analysis/causal_onsets.hpp"

#include "sonic/response.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
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

float trimGain(float gainDb) {
    if (!(gainDb == gainDb) || gainDb == 0.0f) { // NaN, or no trim
        return 1.0f;
    }
    return std::pow(10.0f, std::clamp(gainDb, kMinTrimDb, kMaxTrimDb) / 20.0f);
}

StemAnalyzer::StemAnalyzer(StemAnalysisConfig config)
    : config_(std::move(config))
    , gain_(trimGain(config_.gainDb))
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
    if (gain_ == 1.0f) {
        analyzer_.push(mono, side);
        return;
    }
    // ADR-1582: the trim, on a copy (grows to the largest chunk once; a live source's chunks are at most a hop).
    gainedMono_.resize(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) {
        gainedMono_[i] = mono[i] * gain_;
    }
    gainedSide_.resize(side.size());
    for (std::size_t i = 0; i < side.size(); ++i) {
        gainedSide_[i] = side[i] * gain_;
    }
    analyzer_.push(gainedMono_, gainedSide_);
}

void StemAnalyzer::setGainDb(float gainDb) {
    config_.gainDb = gainDb;
    gain_ = trimGain(gainDb);
}

void StemAnalyzer::reset(std::uint64_t startFrame) {
    analyzer_.reset(startFrame);
    causal_.reset();
    drums_.reset();
    levelDb_ = referenceDb_ = -120.0f;
    referencePower_ = 0.0;
    heardSeconds_ = 0.0;
    levelPrimed_ = false;
    std::fill(previousSpectrum_.begin(), previousSpectrum_.end(), 0.0f);
    haveSpectrum_ = false;
    fluxCount_ = 0;
    lastFlux_ = 0.0f;
    lastOnsetTime_ = -1.0;
    lastRegister_ = {};
    previousMagnitude_.clear();
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
        lastRegister_ = registerShares(f.magnitude, previousMagnitude_, binHz_); // ADR-1604
    }
    out.onsetRegister = lastRegister_;
    fluxHistory_[fluxCount_ % fluxHistory_.size()] = flux;
    ++fluxCount_;
    lastFlux_ = flux;
    previousMagnitude_.assign(f.magnitude.begin(), f.magnitude.end()); // ADR-1604
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
    out.beatOnsetStrength = f.onsetStrength;
    out.beatOnset = f.onset;
    foldSpectrum(f, analyzer_.config(), out.spectrumRow); // ADR-1631
    if (config_.hits) {
        causal_.process(scratch_, binHz_, hopSeconds_);
        out.feed = ResponseFeed::fromCausal(scratch_.causal);
        if (!config_.classes) {
            return true; // ADR-1630: not a drums source; no class name
        }
        const DrumHits d = drums_.process(scratch_.magnitude, causal_.binFlux(), causal_.first(), binHz_, hopSeconds_);
        out.kick = d.hit[static_cast<std::size_t>(DrumClass::Kick)];
        out.kickStrength = d.strength[static_cast<std::size_t>(DrumClass::Kick)];
        out.snare = d.hit[static_cast<std::size_t>(DrumClass::Snare)];
        out.snareStrength = d.strength[static_cast<std::size_t>(DrumClass::Snare)];
        out.hat = d.hit[static_cast<std::size_t>(DrumClass::Hat)];
        out.hatStrength = d.strength[static_cast<std::size_t>(DrumClass::Hat)];
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

Result<StemTrack> StemTrack::analyzeFile(const std::filesystem::path& path, StemAnalysisConfig config,
                                         audio::StemFileFacts& facts) {
    facts = audio::StemFileFacts{};
    facts.format = audio::nativeSampleFormat(path);
    StemTrack track;
    track.hits_ = config.hits;
    std::unique_ptr<StemAnalyzer> analyzer;
    std::optional<audio::StemProbeWriter> probe; // ADR-1632: the stem-set check's probe
    std::vector<float> mono;
    std::vector<float> side;
    StemFrame frame;
    auto decoded = audio::AudioFile::decode(path, [&](const audio::DecodeFormat& f, std::span<const float> samples) {
        if (!analyzer) {
            config.analyzer.sampleRate = f.sampleRate;
            analyzer = std::make_unique<StemAnalyzer>(config);
            probe.emplace(f.sampleRate);
            const AnalyzerConfig& cfg = analyzer->config().analyzer;
            track.sampleRate_ = f.sampleRate;
            track.hopSeconds_ = static_cast<double>(cfg.hopSize) / static_cast<double>(cfg.sampleRate);
            if (f.declaredFrames >= cfg.windowSize) {
                track.frames_.reserve(static_cast<std::size_t>((f.declaredFrames - cfg.windowSize) / cfg.hopSize + 1));
            }
        }
        const std::size_t channels = f.channels;
        const std::size_t n = samples.size() / channels;
        mono.resize(n);
        side.resize(channels >= 2 ? n : 0);
        const float inv = 1.0f / static_cast<float>(channels);
        for (std::size_t i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (std::size_t c = 0; c < channels; ++c) {
                const float v = samples[i * channels + c];
                if (!std::isfinite(v)) {
                    ++facts.nonFinite;
                    continue;
                }
                facts.peak = std::max(facts.peak, std::fabs(v));
                facts.silent = facts.silent && v == 0.0f;
                sum += v;
            }
            mono[i] = sum * inv; // as AudioFile::mono(): the average of every channel
            if (channels >= 2) {
                side[i] = 0.5f * (samples[i * channels] - samples[i * channels + 1]);
            }
        }
        analyzer->push(mono, side);
        probe->push(mono);
        while (analyzer->pop(frame)) {
            track.frames_.push_back(frame);
        }
    });
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    facts.sampleRate = decoded->sampleRate;
    facts.channels = decoded->channels;
    facts.frames = decoded->frames;
    facts.silent = facts.silent && facts.nonFinite == 0;
    if (probe) {
        facts.probe = probe->take();
    }
    track.duration_ = static_cast<double>(decoded->frames) / static_cast<double>(decoded->sampleRate);
    return track;
}

} // namespace avgen::analysis
