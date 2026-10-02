#pragma once

// The response model (ADR-1062, brief §9-11): signals by KIND -- hits, levels, presence -- shaped by one
// conditioning chain and the performer's five controls, on both the file and the live path.
//
//   raw -> floor gate -> normalise -> sensitivity -> response curve -> attack/release -> response.*
//
// Levels (bass, level, sustain, flux) are read in dB on a fixed range, never auto-gained, so a quiet passage looks
// quiet. Sensitivity MOVES THE FLOOR (+-18 dB) and the curve (gamma = 2^((0.5 - s) 1.6)); it never multiplies the
// top, so a loud sound cannot be pushed past 1 and a quiet one is lifted rather than scaled. Hits (kick, snare,
// hat, low, onset) come from ADR-1060's causal ratios, which are level-free by construction: sensitivity moves
// their firing threshold and curves their strength. Every hit is an EVENT and has an envelope beside it.
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
    float floorDb = -60.0f;         // a level at the floor reads 0 at neutral sensitivity
    float rangeDb = 48.0f;          // ...and floor + range reads 1
    float sensitivityDb = 18.0f;    // how far sensitivity 0 or 1 moves the floor
    // Envelope release per hit, seconds (Kick, Low, Snare, Hat, Onset, Note); attack is one hop (instant) x attack.
    std::array<float, kResponseHitCount> hitRelease{0.16f, 0.18f, 0.12f, 0.06f, 0.09f, 0.22f};
    // Levels' followers, seconds: attack / release.
    float bassAttack = 0.015f, bassRelease = 0.20f;
    float levelAttack = 0.02f, levelRelease = 0.25f;
    float sustainAttack = 0.25f, sustainRelease = 0.9f;
    float fluxAttack = 0.01f, fluxRelease = 0.15f;
    float intensitySeconds = 12.0f;
    float hatRateSeconds = 1.5f; // the hat-rate's leaky window
    float hatRateFull = 12.0f;   // hats a second that read 1
    float melodicFull = 6.0f;    // pitch moves a second that read 1

    [[nodiscard]] Result<void> applyJson(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json toJson() const;
};

// The two effective sensitivities the controls give: hits and levels, 0..1.
[[nodiscard]] float hitSensitivity(const ResponseControls& c);
[[nodiscard]] float levelSensitivity(const ResponseControls& c);
// The level chain for one dB value at one sensitivity: floor gate, normalise, the moved floor and the curve.
[[nodiscard]] float conditionLevel(float db, float sensitivity, const ResponseSettings& s);

struct ResponseSignals {
    std::array<signals::SignalId, kResponseHitCount> hit{};
    std::array<signals::SignalId, kResponseHitCount> env{};
    signals::SignalId bass = signals::kInvalidSignal, level = signals::kInvalidSignal,
                      transient = signals::kInvalidSignal, sustain = signals::kInvalidSignal,
                      flux = signals::kInvalidSignal, hatRate = signals::kInvalidSignal,
                      melodic = signals::kInvalidSignal, pitch = signals::kInvalidSignal,
                      intensity = signals::kInvalidSignal;
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

private:
    ResponseSignals ids_;
    std::array<analysis::HitPicker, analysis::kHitClassCount> pickers_{};
    bool pickersReady_ = false;
    std::array<float, kResponseHitCount> env_{};
    std::array<bool, kResponseHitCount> pending_{};
    std::array<float, kResponseHitCount> pendingStrength_{};
    std::array<std::uint64_t, kResponseHitCount> count_{};
    float bass_ = 0.0f, level_ = 0.0f, sustain_ = 0.0f, flux_ = 0.0f, intensity_ = 0.0f;
    float fastDb_ = -120.0f, slowDb_ = -120.0f, transient_ = 0.0f, percussive_ = 0.0f;
    float hatRate_ = 0.0f, melodic_ = 0.0f;
    float lastSemitone_ = -1.0f, audioPitch_ = -1.0f;
    bool primed_ = false;
};

} // namespace avgen::sonic
