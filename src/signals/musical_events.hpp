#pragma once

// Musical structure as events (ADR-063, milestone 4 of the cinematic upgrade).
//
// The analyser already publishes a great deal: rms, per-band energy, spectral centroid and flux,
// onsets, tempo, and a beat clock with phase, bar, phrase and section counters. What it does not
// publish is *structure* -- the difference between a beat and a downbeat, between a passage getting
// louder and a drop, between a break and silence. Those are the things a visual narrative is cut
// to, and today every scene has to infer them from a raw band level, which is why so much
// audio-reactive work ends up as "everything pulses to rms".
//
// This is a classifier over the signals that already exist. It holds a short history, decides what
// just happened, and emits typed events. It does not analyse audio, does not touch the audio thread
// and does not allocate while running: `update()` is called once per frame with the frame's signal
// values and returns what it recognised.
//
// It is deliberately conservative. A classifier that fires Drop on every loud bar is worse than one
// that misses some, because a visual system cut to a false drop looks broken in a way that a
// visual system cut to nothing does not.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace avgen::signals {

enum class MusicalEvent : std::uint8_t {
    Beat,          // every beat
    Downbeat,      // the first beat of a bar
    BarStart,
    PhraseStart,   // a phrase boundary from the beat clock
    SectionChange, // the analyser's own section counter moved
    EnergyRise,    // sustained increase over several bars
    EnergyDrop,    // sustained decrease
    Build,         // energy rising *and* brightening: the run-up
    Break,         // energy collapses and stays down
    Drop,          // a break resolving into a loud downbeat
    Impact,        // a single very strong onset
};
[[nodiscard]] const char* musicalEventName(MusicalEvent e);
[[nodiscard]] std::optional<MusicalEvent> musicalEventFromName(std::string_view name);

// One recognised event. `strength` is 0..1 and is what a visual system should scale by; it is not
// a confidence, it is how much of the thing happened.
struct MusicalMoment {
    MusicalEvent event = MusicalEvent::Beat;
    double timeSeconds = 0.0;
    float strength = 1.0f;
};

// The per-frame inputs, all of which the signal bus already publishes. Passing them in rather than
// reaching for a global keeps this testable without an audio device, which is most of why the
// classifier is a separate object at all.
struct MusicalFrame {
    double timeSeconds = 0.0;
    float rms = 0.0f;
    float bass = 0.0f;
    float treble = 0.0f;
    float spectralCentroid = 0.0f;
    float onsetStrength = 0.0f;
    bool beat = false;          // the beat clock ticked this frame
    int beatInBar = 0;          // 0 = downbeat
    std::uint32_t barCount = 0;
    std::uint32_t phraseCount = 0;
    std::uint32_t sectionCount = 0;
};

// Thresholds, exposed because "what counts as a drop" is an artistic decision and the defaults are
// only defensible, not correct.
struct MusicalEventSettings {
    float impactOnset = 1.6f;       // onset strength that counts as an impact on its own
    float riseRatio = 1.25f;        // short-term energy over long-term to call a rise
    float dropRatio = 0.72f;        // ...and to call a fall
    float breakLevel = 0.35f;       // fraction of recent peak below which a break is running
    float buildCentroid = 0.02f;    // centroid must also be climbing for a rise to be a build
    double sustainSeconds = 1.6;    // how long a trend must hold before it is a trend
    double dropWindowSeconds = 2.5; // a break resolving inside this window is a drop
    double cooldownSeconds = 1.2;   // minimum gap between two events of the same kind
};

class MusicalEventDetector {
public:
    explicit MusicalEventDetector(MusicalEventSettings settings = {});

    // Feeds one frame and returns everything recognised in it. The returned span is valid until the
    // next call; there is no allocation in steady state.
    [[nodiscard]] const std::vector<MusicalMoment>& update(const MusicalFrame& frame);

    void reset();

    [[nodiscard]] const MusicalEventSettings& settings() const { return settings_; }
    // Smoothed energy, exposed because it is the number every trend decision is made from and a
    // caller debugging a false Drop wants to see it.
    [[nodiscard]] float shortEnergy() const { return shortEnergy_; }
    [[nodiscard]] float longEnergy() const { return longEnergy_; }
    [[nodiscard]] bool inBreak() const { return inBreak_; }

private:
    [[nodiscard]] bool allow(MusicalEvent e, double now);
    void emit(MusicalEvent e, double now, float strength);

    MusicalEventSettings settings_;
    std::vector<MusicalMoment> moments_;
    double lastFired_[16]{};
    float shortEnergy_ = 0.0f;
    float longEnergy_ = 0.0f;
    float peakEnergy_ = 0.0f;
    float lastCentroid_ = 0.0f;
    double trendSince_ = 0.0;
    int trendSign_ = 0;          // -1 falling, 0 flat, +1 rising
    bool inBreak_ = false;
    double breakSince_ = 0.0;
    double breakEndedAt_ = 0.0;
    bool dropArmed_ = false;   // a break has ended and may still resolve into a drop
    std::uint32_t lastBar_ = 0;
    std::uint32_t lastPhrase_ = 0;
    std::uint32_t lastSection_ = 0;
    bool started_ = false;
    double lastTime_ = 0.0;
};

} // namespace avgen::signals
