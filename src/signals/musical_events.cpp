#include "signals/musical_events.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::signals {
namespace {
constexpr std::array<std::pair<MusicalEvent, const char*>, 11> kNames{{
    {MusicalEvent::Beat, "beat"},
    {MusicalEvent::Downbeat, "downbeat"},
    {MusicalEvent::BarStart, "bar"},
    {MusicalEvent::PhraseStart, "phrase"},
    {MusicalEvent::SectionChange, "section"},
    {MusicalEvent::EnergyRise, "energyRise"},
    {MusicalEvent::EnergyDrop, "energyDrop"},
    {MusicalEvent::Build, "build"},
    {MusicalEvent::Break, "break"},
    {MusicalEvent::Drop, "drop"},
    {MusicalEvent::Impact, "impact"},
}};

// One-pole smoothing with a time constant in seconds, so the classifier behaves the same at any
// frame rate. A per-frame coefficient would make a 30 fps offline render disagree with a 120 fps
// window about where a drop is, which for a deterministic engine is unacceptable.
float smooth(float current, float target, double dt, double tau) {
    if (tau <= 0.0) {
        return target;
    }
    const auto k = static_cast<float>(1.0 - std::exp(-dt / tau));
    return current + (target - current) * k;
}
} // namespace

const char* musicalEventName(MusicalEvent e) {
    for (const auto& [kind, name] : kNames) {
        if (kind == e) {
            return name;
        }
    }
    return "beat";
}

std::optional<MusicalEvent> musicalEventFromName(std::string_view name) {
    for (const auto& [kind, text] : kNames) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

MusicalEventDetector::MusicalEventDetector(MusicalEventSettings settings)
    : settings_(settings) {
    moments_.reserve(8);
    reset();
}

void MusicalEventDetector::reset() {
    moments_.clear();
    for (auto& t : lastFired_) {
        t = -1e9;
    }
    shortEnergy_ = longEnergy_ = peakEnergy_ = 0.0f;
    lastCentroid_ = 0.0f;
    trendSince_ = 0.0;
    trendSign_ = 0;
    inBreak_ = false;
    breakSince_ = 0.0;
    breakEndedAt_ = 0.0;
    dropArmed_ = false;
    lastBar_ = lastPhrase_ = lastSection_ = 0;
    started_ = false;
    lastTime_ = 0.0;
}

bool MusicalEventDetector::allow(MusicalEvent e, double now) {
    const auto i = static_cast<std::size_t>(e);
    // Beats and bars are supposed to repeat; only the structural events get a cooldown, because a
    // Drop firing twice in a second is a classifier bug presenting as a visual one.
    if (e == MusicalEvent::Beat || e == MusicalEvent::Downbeat || e == MusicalEvent::BarStart) {
        return true;
    }
    return now - lastFired_[i] >= settings_.cooldownSeconds;
}

void MusicalEventDetector::emit(MusicalEvent e, double now, float strength) {
    lastFired_[static_cast<std::size_t>(e)] = now;
    moments_.push_back(MusicalMoment{e, now, std::clamp(strength, 0.0f, 1.0f)});
}

const std::vector<MusicalMoment>& MusicalEventDetector::update(const MusicalFrame& f) {
    moments_.clear();
    const double now = f.timeSeconds;
    const double dt = started_ ? std::max(now - lastTime_, 0.0) : 0.0;
    lastTime_ = now;

    // ---- energy trends -------------------------------------------------------------------------
    // Two time constants: a short one that follows the music and a long one that remembers what it
    // has been doing. Their ratio is the trend, which is far steadier than a derivative.
    const float level = std::max(f.rms, f.bass * 0.6f);
    if (!started_) {
        shortEnergy_ = longEnergy_ = peakEnergy_ = level;
        lastCentroid_ = f.spectralCentroid;
        lastBar_ = f.barCount;
        lastPhrase_ = f.phraseCount;
        lastSection_ = f.sectionCount;
        trendSince_ = now;
        started_ = true;
        return moments_;
    }
    shortEnergy_ = smooth(shortEnergy_, level, dt, 0.45);
    longEnergy_ = smooth(longEnergy_, level, dt, 4.0);
    peakEnergy_ = std::max(peakEnergy_ * static_cast<float>(std::exp(-dt / 12.0)), shortEnergy_);

    // ---- the metre -----------------------------------------------------------------------------
    if (f.beat) {
        emit(MusicalEvent::Beat, now, std::clamp(shortEnergy_, 0.0f, 1.0f));
        if (f.beatInBar == 0) {
            emit(MusicalEvent::Downbeat, now, std::clamp(shortEnergy_, 0.0f, 1.0f));
        }
    }
    if (f.barCount != lastBar_) {
        lastBar_ = f.barCount;
        emit(MusicalEvent::BarStart, now, 1.0f);
    }
    if (f.phraseCount != lastPhrase_) {
        lastPhrase_ = f.phraseCount;
        if (allow(MusicalEvent::PhraseStart, now)) {
            emit(MusicalEvent::PhraseStart, now, 1.0f);
        }
    }
    if (f.sectionCount != lastSection_) {
        lastSection_ = f.sectionCount;
        if (allow(MusicalEvent::SectionChange, now)) {
            emit(MusicalEvent::SectionChange, now, 1.0f);
        }
    }

    // ---- impacts -------------------------------------------------------------------------------
    if (f.onsetStrength >= settings_.impactOnset && allow(MusicalEvent::Impact, now)) {
        emit(MusicalEvent::Impact, now,
             std::clamp(f.onsetStrength / (settings_.impactOnset * 2.0f), 0.0f, 1.0f));
    }

    // ---- breaks --------------------------------------------------------------------------------
    // A break is energy sitting well under what this piece has recently been capable of. Measured
    // against a decaying peak rather than an absolute level, so a quiet piece is not permanently
    // in a break.
    const float relative = peakEnergy_ > 1e-5f ? shortEnergy_ / peakEnergy_ : 1.0f;
    const bool quiet = relative < settings_.breakLevel;
    if (quiet && !inBreak_) {
        inBreak_ = true;
        breakSince_ = now;
        if (allow(MusicalEvent::Break, now)) {
            emit(MusicalEvent::Break, now, std::clamp(1.0f - relative, 0.0f, 1.0f));
        }
    } else if (!quiet && inBreak_) {
        inBreak_ = false;
        breakEndedAt_ = now;
        dropArmed_ = true;
    }
    // A break that resolves into loudness is a drop, and that two-part shape is the whole
    // definition: without the break beforehand a loud bar is just a loud bar, and firing Drop on
    // every loud bar is exactly the failure this classifier exists to avoid.
    //
    // The test has to be a *window* rather than the instant the break ends. At that instant energy
    // has only just crept back over the break threshold, so it is by definition not yet loud --
    // the first version checked there and never fired once.
    if (dropArmed_) {
        if (now - breakEndedAt_ > settings_.dropWindowSeconds) {
            dropArmed_ = false; // it recovered, but only into an ordinary passage
        } else if (relative > 0.75f && allow(MusicalEvent::Drop, now)) {
            dropArmed_ = false;
            emit(MusicalEvent::Drop, now, std::clamp(relative, 0.0f, 1.0f));
        }
    }

    // ---- sustained trends ----------------------------------------------------------------------
    const float ratio = longEnergy_ > 1e-5f ? shortEnergy_ / longEnergy_ : 1.0f;
    const int sign = ratio > settings_.riseRatio ? 1 : (ratio < settings_.dropRatio ? -1 : 0);
    if (sign != trendSign_) {
        trendSign_ = sign;
        trendSince_ = now;
    } else if (sign != 0 && now - trendSince_ >= settings_.sustainSeconds) {
        const float strength = std::clamp(std::abs(ratio - 1.0f), 0.0f, 1.0f);
        if (sign > 0) {
            // Brightening as well as loudening is what separates a build from merely getting
            // louder: a build is going somewhere.
            const bool brightening = f.spectralCentroid - lastCentroid_ > settings_.buildCentroid;
            const MusicalEvent e = brightening ? MusicalEvent::Build : MusicalEvent::EnergyRise;
            if (allow(e, now)) {
                emit(e, now, strength);
                trendSince_ = now; // a sustained trend re-arms rather than firing every frame
            }
        } else if (allow(MusicalEvent::EnergyDrop, now)) {
            emit(MusicalEvent::EnergyDrop, now, strength);
            trendSince_ = now;
        }
    }
    lastCentroid_ = smooth(lastCentroid_, f.spectralCentroid, dt, 2.0);
    return moments_;
}

} // namespace avgen::signals
