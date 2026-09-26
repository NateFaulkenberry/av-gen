#pragma once

// The fixed audio signal vocabulary published from AnalysisFrames (ADR-004 §5).

#include "analysis/analyzer.hpp"
#include "signals/signal_bus.hpp"

#include <array>

namespace avgen::signals {

struct AudioSignals {
    SignalId rms = kInvalidSignal;
    SignalId peak = kInvalidSignal;
    SignalId bass = kInvalidSignal;
    SignalId lowMid = kInvalidSignal;
    SignalId mid = kInvalidSignal;
    SignalId highMid = kInvalidSignal;
    SignalId treble = kInvalidSignal;
    SignalId centroid = kInvalidSignal;      // audio.spectralCentroid, 0..1 log-normalised
    SignalId flux = kInvalidSignal;          // audio.spectralFlux
    SignalId onsetStrength = kInvalidSignal; // audio.onsetStrength
    SignalId onset = kInvalidSignal;         // audio.onset (event)
    SignalId tempo = kInvalidSignal;         // audio.tempo (BPM, 0 unknown)
    SignalId tempoConfidence = kInvalidSignal; // audio.tempoConfidence 0..1
    SignalId beat = kInvalidSignal;          // audio.beat (event)
    SignalId beatPhase = kInvalidSignal;     // audio.beatPhase 0..1 (hop rate)
    SignalId beatCount = kInvalidSignal;     // audio.beatCount

    // ---- ADR-897: features that survive a flat master ----
    // audio.<band>Level: the long-term band level, 0 = -60 dB, 1 = 0 dB, not auto-gained -- beside
    // the auto-gained audio.<band>, which read full scale again within seconds of any change.
    std::array<SignalId, 5> bandLevels{kInvalidSignal, kInvalidSignal, kInvalidSignal, kInvalidSignal,
                                       kInvalidSignal};
    SignalId energy = kInvalidSignal;    // audio.energy: the loudness-independent composite, 0..1
    SignalId onsetRate = kInvalidSignal; // audio.onsetRate: percussive onsets per second (density)
    SignalId width = kInvalidSignal;     // audio.width: stereo side/mid RMS, 0 for mono
    // ---- ADR-898: band-limited onsets (events; strength = against the band's recent strongest) ----
    SignalId onsetLow = kInvalidSignal;  // audio.onsetLow: the kick
    SignalId onsetMid = kInvalidSignal;  // audio.onsetMid: snare / clap
    SignalId onsetHigh = kInvalidSignal; // audio.onsetHigh: hats

    static AudioSignals declare(SignalBus& bus);

    // Publishes one frame. Band signals map by index to the analyzer's band list (0..4).
    void publish(SignalBus& bus, const analysis::AnalysisFrame& frame) const;

    // Publishes silence (all zeros, no event).
    void publishSilence(SignalBus& bus) const;

    // Drops this frame's event pulses (onset, beat, the band onsets) and keeps every level.
    void clearEvents(SignalBus& bus) const;
};

} // namespace avgen::signals
