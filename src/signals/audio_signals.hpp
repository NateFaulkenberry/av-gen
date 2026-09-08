#pragma once

// The fixed audio signal vocabulary published from AnalysisFrames (ADR-004 §5).

#include "analysis/analyzer.hpp"
#include "signals/signal_bus.hpp"

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

    static AudioSignals declare(SignalBus& bus);

    // Publishes one frame. Band signals map by index to the analyzer's band list (0..4).
    void publish(SignalBus& bus, const analysis::AnalysisFrame& frame) const;

    // Publishes silence (all zeros, no event).
    void publishSilence(SignalBus& bus) const;
};

} // namespace avgen::signals
