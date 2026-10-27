#pragma once

// The `stem.<key>.*` vocabulary (stems-multichannel, ADR-1520): one analysis source on the bus -- an offline stem file
// or a live input channel / linked pair, published identically so a scene routed on `stem.drums.level` offline plays
// live with the drum bus patched to an input keyed `drums`.
//
//   stem.<key>.active          0/1   the source has data at this instant (0 outside a stem's span, a stem that failed
//                                    to load, an absent channel, a disconnected or stalled device)
//   stem.<key>.rms             0..1  window RMS (2048 samples, centred), unsmoothed: the envelope
//   stem.<key>.peak            0..1  window peak
//   stem.<key>.level           0..1  the followed level against the source's own loudness reference (ADR-1510's law:
//                                    `response.level`'s window and follower; 0 below -60 dBFS)
//   stem.<key>.onset           event spectral-flux onset, strength 0..1
//   stem.<key>.onsetStrength   0..4  flux over its adaptive threshold
//   stem.<key>.bass .lowMid .mid .highMid .treble   0..1 auto-gained bands, like audio.<band>
//   stem.<key>.centroid        0..1  log-normalised spectral centroid
//   stem.<key>.width           0..2  side/mid RMS of a stereo stem or a linked pair; 0 for mono
//   stem.<key>.onsetLow .onsetMid .onsetHigh  0..1  ADR-1604: the register of the latest onset -- the low (30-250 Hz),
//                                    mid and high (4-16 kHz) shares of its attack, summing to 1 -- from its own hop on,
//                                    held until the next onset
//
// Cadence: values change once per analysis hop (512 samples, 10.7 ms at 48 kHz); a render frame publishes the newest
// hop at or before its instant, and ORs the events of every hop since the previous frame (none is lost at 30 fps).
// Inactive: every value 0 and no event -- never a held stale value, never a fabricated event.
//
// ADR-1630, by role: `role.<role>.*` (drums, bass, synth, vocal, other) is the same vocabulary for the first ACTIVE
// source of that role -- the live input sources first (while an input is open), then the stems, each in list order --
// and inactive when none is. It is declared once a source of the role exists. A scene that names `role.drums.onset`
// follows a drum bus keyed `drums`, `kit` or `in3` alike. Every role publishes the same names: activity, never an
// instrument's identity (ADR-1784 cut the drums role's kick/snare/hat classifier).

#include "analysis/stem_analysis.hpp"
#include "audio/source_role.hpp"
#include "signals/signal_bus.hpp"

#include <array>
#include <string>

namespace avgen::signals {

struct StemSignals {
    std::string key;                                // the stem / input key; "" for a role alias
    audio::SourceRole role = audio::SourceRole::Other;
    SignalId active = kInvalidSignal;
    SignalId rms = kInvalidSignal;
    SignalId peak = kInvalidSignal;
    SignalId level = kInvalidSignal;
    SignalId onset = kInvalidSignal;
    SignalId onsetStrength = kInvalidSignal;
    std::array<SignalId, 5> bands{kInvalidSignal, kInvalidSignal, kInvalidSignal, kInvalidSignal, kInvalidSignal};
    SignalId centroid = kInvalidSignal;
    SignalId width = kInvalidSignal;
    // ADR-1604: stem.<key>.onsetLow / onsetMid / onsetHigh
    std::array<SignalId, 3> onsetRegister{kInvalidSignal, kInvalidSignal, kInvalidSignal};

    // Declares (idempotently) every signal of `key` and labels them with `label` ("Drums: level (...)").
    static StemSignals declare(SignalBus& bus, const std::string& key, const std::string& label, audio::SourceRole role);
    // ADR-1630: declares `role.<role>.*`.
    static StemSignals declareRole(SignalBus& bus, audio::SourceRole role);
    // Every name `declare` makes for `key` and `role`, in declaration order.
    [[nodiscard]] static std::vector<std::string> names(const std::string& key, audio::SourceRole role);
    // ADR-1630: every name `declareRole` makes for `role`.
    [[nodiscard]] static std::vector<std::string> roleNames(audio::SourceRole role);
    // Re-labels in place after the artist renames the source. Ids do not change.
    void relabel(SignalBus& bus, const std::string& label) const;

    // One frame. `events` false publishes the levels and drops the event pulses (a frame that repeats one already
    // published).
    void publish(SignalBus& bus, const analysis::StemFrame& frame, bool events = true) const;
    // The source has no data now: every value 0, no event, active 0.
    void publishInactive(SignalBus& bus) const;
    void clearEvents(SignalBus& bus) const;
    // An offline stem at project time `to`: the newest frame of `track` at or before it (the stem starts `offset`
    // seconds into the project), with the events of every frame in (from, to] OR-ed at their strongest. A pure
    // function of (from, to): a play and a seek's replay stepping the same instants publish the same thing. `from`
    // < 0 or >= `to` (a first frame, a jump) carries no event. Inactive outside [offset, offset + duration) and with
    // no track.
    void publishTrack(SignalBus& bus, const analysis::StemTrack* track, double offset, double from, double to) const;
    // Every declared id, in declaration order (for copying values between buses by name).
    [[nodiscard]] std::vector<SignalId> ids() const;
    // ADR-1630: this (a role alias) publishes what `source` published on `bus` this frame, value for value and event
    // for event. `source` of the same role, so their names correspond.
    void copyFrom(SignalBus& bus, const StemSignals& source) const;
};

} // namespace avgen::signals
