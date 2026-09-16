#pragma once

// Musical structure as events (ADR-063, milestone 4 of the cinematic upgrade).
//
// The analyzer already publishes a great deal: rms, per-band energy, spectral centroid and flux,
// onsets, tempo, and a beat clock with phase, bar, phrase and section counters. What it does not
// publish is *structure* -- the difference between a beat and a downbeat, between a passage getting
// louder and a drop, between a break and silence. Those are the things a visual narrative is cut
// to, and today every scene has to infer them from a raw band level, which is why so much
// audio-reactive work ends up as "everything pulses to rms".
//
// This is a classifier over the signals that already exist. It holds a short history, decides what
// just happened, and emits typed events. It does not analyze audio, does not touch the audio thread
// and does not allocate while running: `update()` is called once per frame with the frame's signal
// values and returns what it recognised.
//
// It is deliberately conservative. A classifier that fires Drop on every loud bar is worse than one
// that misses some, because a visual system cut to a false drop looks broken in a way that a
// visual system cut to nothing does not.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace avgen::signals {

enum class MusicalEvent : std::uint8_t {
    Beat,          // every beat
    Downbeat,      // the first beat of a bar
    BarStart,
    PhraseStart,   // a phrase boundary from the beat clock
    SectionChange, // the analyzer's own section counter moved
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

// ---- structure ---------------------------------------------------------------------------------
//
// Events are instants; a film is cut to *spans*. `MusicalStructure` is the second half of ADR-063:
// the same recognised moments, folded into a list of named sections with a start and a length, so
// something downstream can ask "what is happening at 47 seconds" and get "the final build, at
// intensity 0.8" rather than a list of things that fired near it.
//
// The folding is where the taste lives, and all of it is subtractive. Beats, downbeats and bars
// never open a section -- a structure with a boundary every 500 ms is not structure, it is the
// metre wearing a different name, and anything cut to it cuts constantly. Energy trends never open
// one either: a trend describes a section, it does not bound one.

// ## Two vocabularies, and which is which (ADR-215)
//
// `analysis::SectionFunction` (ADR-206) describes *the music*: it is what a detector concluded, it
// carries a confidence, and it is allowed to answer `Other`, which is not a failure but the correct
// answer for a piece that has no verses and no choruses.
//
// `MusicalSection` is **the director's vocabulary**, and its rule is different: every value has to
// mean something to a shot. There is no `Unknown` here and there must not be one, because a shot
// has to be *some* shot -- `Phrase`, "an ordinary passage", is what "I do not know" becomes when
// something has to point a camera. The mapping between the two is one-way and total, and it lives
// in `seq/song_structure.hpp` next to the editable model, because that is the layer that owns the
// translation. This enum is never parsed from a detector's output directly.
//
// Six kinds were added for the song-structure work: the pop/rock half of the vocabulary, which the
// EDM-shaped original did not have. Each one was answered deliberately in all four of the switches
// that read this enum (`shotKindForSection`, `isDropSection`, `mayBeSplit`, `emphasisFor`) -- a new
// kind falling through a `default` there produces a silently bland shot, which is the failure mode
// worth more than the enum itself.
//
// `Outro` is last by construction, and `kSectionNames` is static_asserted against it: a new kind
// goes *before* Outro and cannot be added without also giving it a name.
enum class MusicalSection : std::uint8_t {
    Intro,        // before the piece has committed to anything
    Build,        // going somewhere
    Phrase,       // an ordinary passage; also what an unlabelled section becomes
    Drop,         // the payoff a build was for
    Verse,        // the analyzer's own section counter moved, without saying into what
    Breakdown,    // energy collapsed and stayed down
    FinalBuild,   // the last build, which is a different thing from the first
    FinalDrop,    // ...and the last drop
    PreChorus,    // the pop run-up: a build by another name, and cut into no more than one
    Chorus,       // the recurring payoff; the drop of a song that has words
    Break,        // the music stops holding still: quieter, and not resolving into anything
    Bridge,       // the departure, once, usually two thirds in
    Instrumental, // a passage with the voice out of it: solo, interlude, post-chorus
    FinalChorus,  // the last chorus, which is a different shot from the first
    Outro,        // after the last boundary. Always last: see kSectionNames' static_assert.
};
[[nodiscard]] const char* musicalSectionName(MusicalSection s);
[[nodiscard]] std::optional<MusicalSection> musicalSectionFromName(std::string_view name);
// Every kind, in declaration order. The one list anything that must handle them all iterates, so a
// test of "all of them" cannot quietly test fourteen of fifteen.
[[nodiscard]] std::span<const MusicalSection> allMusicalSections();

struct StructureSection {
    MusicalSection kind = MusicalSection::Intro;
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
    // How much of the thing is happening, 0..1. Carried from the moment that opened the section and
    // nudged by any energy trend inside it, because a trend is a description of a section rather
    // than a boundary of one.
    float intensity = 0.5f;

    [[nodiscard]] double endSeconds() const { return startSeconds + durationSeconds; }
};

// What counts as a section boundary. Every number here exists to stop the structure being finer
// than the music: the failure mode is not missing a section, it is finding forty of them.
struct StructureSettings {
    double minSectionSeconds = 6.0;   // an ordinary section shorter than this is absorbed
    double minBuildSeconds = 2.0;     // ...except a build feeding a drop, which may be brief
    double mergeSeconds = 1.5;        // two boundaries this close are one boundary
    double phraseSnapSeconds = 1.2;   // a boundary this near a phrase start moves onto it
    double finalFraction = 0.55;      // a drop after this much of the piece may be *the* drop
};

struct MusicalStructure {
    std::vector<StructureSection> sections;

    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] const StructureSection* at(double seconds) const;
    [[nodiscard]] int count(MusicalSection kind) const;

    // Folds a recorded stream of moments into sections. `totalSeconds` is the length of the piece,
    // which the moments cannot supply: the last event is not the end.
    [[nodiscard]] static MusicalStructure fromMoments(std::span<const MusicalMoment> moments,
                                                      double totalSeconds,
                                                      StructureSettings settings = {});
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
