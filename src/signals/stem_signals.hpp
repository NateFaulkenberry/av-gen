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
//   stem.<key>.kick .snare .hat event  the ADR-1060 causal classifier, on sources with `hits` only
//
// Cadence: values change once per analysis hop (512 samples, 10.7 ms at 48 kHz); a render frame publishes the newest
// hop at or before its instant, and ORs the events of every hop since the previous frame (none is lost at 30 fps).
// Inactive: every value 0 and no event -- never a held stale value, never a fabricated event.

#include "analysis/stem_analysis.hpp"
#include "signals/signal_bus.hpp"

#include <array>
#include <string>

namespace avgen::signals {

struct StemSignals {
    std::string key;
    SignalId active = kInvalidSignal;
    SignalId rms = kInvalidSignal;
    SignalId peak = kInvalidSignal;
    SignalId level = kInvalidSignal;
    SignalId onset = kInvalidSignal;
    SignalId onsetStrength = kInvalidSignal;
    std::array<SignalId, 5> bands{kInvalidSignal, kInvalidSignal, kInvalidSignal, kInvalidSignal, kInvalidSignal};
    SignalId centroid = kInvalidSignal;
    SignalId width = kInvalidSignal;
    SignalId kick = kInvalidSignal;
    SignalId snare = kInvalidSignal;
    SignalId hat = kInvalidSignal;

    // Declares (idempotently) every signal of `key` and labels them with `label` ("Drums: level (...)").
    static StemSignals declare(SignalBus& bus, const std::string& key, const std::string& label);
    // Every name `declare` makes for `key`, in declaration order.
    [[nodiscard]] static std::vector<std::string> names(const std::string& key);
    // Re-labels in place after the artist renames the source. Ids do not change.
    void relabel(SignalBus& bus, const std::string& label) const;

    // One frame. `events` false publishes the levels and drops the event pulses (a frame that repeats one already
    // published).
    void publish(SignalBus& bus, const analysis::StemFrame& frame, bool events = true) const;
    // The source has no data now: every value 0, no event, active 0.
    void publishInactive(SignalBus& bus) const;
    void clearEvents(SignalBus& bus) const;
    // Every id, in declaration order (for copying values between buses by name).
    [[nodiscard]] std::vector<SignalId> ids() const;
};

} // namespace avgen::signals
