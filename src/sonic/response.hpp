#pragma once

// The response model (ADR-1062, brief §9-11): signals by KIND -- hits, levels, presence -- shaped by one
// conditioning chain and the performer's five controls, on both the file and the live path.
//
//   raw -> floor gate -> normalise -> sensitivity -> response curve -> attack/release -> response.*
//
// Levels (bass, level, levelSlow, intensity, sustain) are read in dB against an ADAPTIVE LOUDNESS REFERENCE (ADR-1510):
// the level averaged over a few seconds, rising to louder music in seconds and falling over a minute, so a mastered
// track, a quiet interface channel and a live synth all read across the range, and a breakdown still reads quiet
// for as long as a breakdown lasts. Each level has its own window below the reference (its top above it and its span,
// in dB); below the gate a level reads 0 and the reference does not move. Sensitivity MOVES THE WINDOW (by a share of
// its span) and the curve (gamma = 2^((0.5 - s) 1.6)); it never multiplies the top, so a loud sound cannot be
// pushed past 1. Hits (kick, snare, hat, low, onset) come from ADR-1060's causal ratios, which are level-free by
// construction: sensitivity moves their firing threshold and curves their strength. Every hit is an EVENT and has
// an envelope beside it.
//
// Transient sensitivity drives the hits, Sustain sensitivity the levels, and Sensitivity both: a quiet synth gets
// its attacks back without its pad lifting the whole world. Attack and Release multiply every time constant.
//
// Steps on the hop clock inside `SonicRuntime::step` (a pure function of the frame sequence and the controls), so a
// backward seek replays it from zero exactly as the character does.

#include "analysis/causal_onsets.hpp"
#include "core/error.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>

namespace avgen::sonic {

struct TimbreFeatures;
struct NoteTrack;

// The performer's controls: the parameters `sonic/response/*`, the Live panel's Response group.
struct ResponseControls {
    float sensitivity = 0.5f; // 0..1, 0.5 neutral: both kinds
    float transient = 0.5f;   // 0..1: the hits
    float sustain = 0.5f;     // 0..1: the levels
    float attack = 1.0f;      // 0.25..4: multiplies every attack time
    float release = 1.0f;     // 0.25..4: multiplies every release and decay time (the feel knob)
    [[nodiscard]] bool operator==(const ResponseControls&) const = default;
};

enum class ResponseHit : std::uint8_t { Kick = 0, Low, Snare, Hat, Onset, Note, Count };
inline constexpr std::size_t kResponseHitCount = static_cast<std::size_t>(ResponseHit::Count);

// The project's detail (`sonic.response`, optional): what is not on the panel.
struct ResponseSettings {
    ResponseControls controls;      // the parameters' defaults
    // ADR-1510: the levels' adaptive reference and windows (were a fixed floorDb -60 / rangeDb 48 window, which
    // mastered music always topped).
    float gateDb = -60.0f;            // below the gate a level reads 0, and the reference does not move
    float referenceMinDb = -40.0f;    // the reference never sinks below this: noise is never normalised up
    float referenceSeconds = 3.0f;    // the reference follows the gated level averaged over this...
    float referenceRise = 2.0f;       // ...rising to louder music with this time constant...
    float referenceFall = 60.0f;      // ...and falling this slowly, so a breakdown reads quiet
    float sensitivityShare = 0.375f;  // Sensitivity 0 or 1 moves a window by this share of its span
    struct LevelWindow {
        float topDb = 6.0f;   // the level that reads 1, above the reference
        float spanDb = 12.0f; // ...and how far below that reads 0
        [[nodiscard]] bool operator==(const LevelWindow&) const = default;
    };
    LevelWindow bassWindow{5.5f, 12.0f};      // the followed 30-150 Hz level
    LevelWindow levelWindow{6.0f, 10.0f};     // the followed broadband level
    LevelWindow sustainWindow{4.0f, 9.0f};    // the instant level sustain is made of (less its percussive share)
    LevelWindow levelSlowWindow{1.5f, 6.0f};  // the level over levelSlowSeconds
    LevelWindow intensityWindow{1.5f, 6.0f};  // the level over intensitySeconds
    // Envelope release per hit, seconds (Kick, Low, Snare, Hat, Onset, Note); attack is one hop (instant) x attack.
    std::array<float, kResponseHitCount> hitRelease{0.16f, 0.18f, 0.12f, 0.06f, 0.09f, 0.22f};
    // Levels' followers, seconds: attack / release.
    float bassAttack = 0.015f, bassRelease = 0.20f;
    float levelAttack = 0.02f, levelRelease = 0.25f;
    float sustainAttack = 0.25f, sustainRelease = 0.9f;
    float fluxAttack = 0.01f, fluxRelease = 0.15f;
    float intensitySeconds = 12.0f;
    float levelSlowSeconds = 3.0f; // ADR-1361: `response.levelSlow`, the level over a few seconds (attack/release x)
    float hatRateSeconds = 1.5f; // the hat-rate's leaky window
    float hatRateFull = 12.0f;   // hats a second that read 1
    float melodicFull = 6.0f;    // pitch moves a second that read 1
    float midiDrumness = 0.9f;   // ADR-1068: a snare or hat on a MIDI note-on must be at least this percussive

    [[nodiscard]] Result<void> applyJson(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json toJson() const;
};

// The two effective sensitivities the controls give: hits and levels, 0..1.
[[nodiscard]] float hitSensitivity(const ResponseControls& c);
[[nodiscard]] float levelSensitivity(const ResponseControls& c);
// The level chain for one dB value (ADR-1510): the gate, the window below the reference (`referenceDb`, held at or above
// `referenceMinDb`), moved by the sensitivity, then the curve.
[[nodiscard]] float conditionLevel(float db, float referenceDb, const ResponseSettings::LevelWindow& window,
                                   float sensitivity, const ResponseSettings& s);

struct ResponseSignals {
    std::array<signals::SignalId, kResponseHitCount> hit{};
    std::array<signals::SignalId, kResponseHitCount> env{};
    signals::SignalId bass = signals::kInvalidSignal, level = signals::kInvalidSignal,
                      transient = signals::kInvalidSignal, sustain = signals::kInvalidSignal,
                      flux = signals::kInvalidSignal, hatRate = signals::kInvalidSignal,
                      melodic = signals::kInvalidSignal, pitch = signals::kInvalidSignal,
                      intensity = signals::kInvalidSignal, levelSlow = signals::kInvalidSignal; // ADR-1361
};

class ResponseModel {
public:
    void declare(signals::SignalBus& bus);
    // One analysis frame, `dt` after the previous.
    void step(const TimbreFeatures& f, double dt, const ResponseControls& c, const ResponseSettings& s);
    // This render frame's values. `noteOn`/`noteVelocity` are the notes begun since the last publish, `notes` the
    // track the context reads (the note envelope is a pure function of it and `seconds`), and `midiMelodic` /
    // `midiPitch` (< 0 when none) the MIDI half of `response.melodic` and `response.pitch`.
    void publish(signals::SignalBus& bus, const NoteTrack& notes, double seconds, bool noteOn, float noteVelocity,
                 float midiMelodic, float midiPitch, const ResponseControls& c, const ResponseSettings& s);
    void publishZeros(signals::SignalBus& bus);
    [[nodiscard]] const ResponseSignals& ids() const { return ids_; }
    void restoreIds(const ResponseSignals& ids) { ids_ = ids; }

    // Read-outs (tests, the panel's hit lights).
    [[nodiscard]] float envelope(ResponseHit h) const { return env_[static_cast<std::size_t>(h)]; }
    [[nodiscard]] std::uint64_t hits(ResponseHit h) const { return count_[static_cast<std::size_t>(h)]; }
    [[nodiscard]] std::uint64_t demoted(ResponseHit h) const { return demoted_[static_cast<std::size_t>(h)]; }
    // ADR-1510: the loudness reference the levels are read against, dBFS (the gate until a sound is heard).
    [[nodiscard]] float referenceDb() const { return referenceDb_; }

private:
    ResponseSignals ids_;
    std::array<float, kResponseHitCount> env_{};
    std::array<bool, kResponseHitCount> pending_{};
    std::array<float, kResponseHitCount> pendingStrength_{};
    std::array<float, kResponseHitCount> pendingDrumness_{}; // ADR-1068
    std::array<float, kResponseHitCount> envBefore_{};
    std::array<std::uint64_t, kResponseHitCount> demoted_{};
    std::array<std::uint64_t, kResponseHitCount> count_{};
    float bass_ = 0.0f, level_ = 0.0f, sustain_ = 0.0f, flux_ = 0.0f, intensity_ = 0.0f, levelSlow_ = 0.0f;
    float fastDb_ = -120.0f, slowDb_ = -120.0f, transient_ = 0.0f, percussive_ = 0.0f;
    // ADR-1510: the fast followers in dB (primed at the gate), the slow ones and the reference's source as power
    // (an average of power is a loudness; an average of dB sinks into every gap between two hits).
    float bassDb_ = -120.0f, levelDb_ = -120.0f, referenceDb_ = -120.0f;
    double levelSlowPower_ = 0.0, intensityPower_ = 0.0, referencePower_ = 0.0;
    double heardSeconds_ = 0.0, sinceHeard_ = 0.0; // gated sound heard; time since the first of it
    bool levelsPrimed_ = false;
    float hatRate_ = 0.0f, melodic_ = 0.0f;
    float lastSemitone_ = -1.0f, audioPitch_ = -1.0f;
    bool primed_ = false;
};

} // namespace avgen::sonic
