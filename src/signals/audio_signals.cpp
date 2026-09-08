#include "signals/audio_signals.hpp"

#include <algorithm>

namespace avgen::signals {

namespace {
float bandOrZero(const analysis::AnalysisFrame& frame, std::size_t index) {
    return index < frame.bandCount && index < analysis::kMaxBands ? frame.bands[index] : 0.0f;
}
} // namespace

AudioSignals AudioSignals::declare(SignalBus& bus) {
    AudioSignals s;
    s.rms = bus.declare("audio.rms");
    s.peak = bus.declare("audio.peak");
    s.bass = bus.declare("audio.bass");
    s.lowMid = bus.declare("audio.lowMid");
    s.mid = bus.declare("audio.mid");
    s.highMid = bus.declare("audio.highMid");
    s.treble = bus.declare("audio.treble");
    s.centroid = bus.declare("audio.spectralCentroid");
    s.flux = bus.declare("audio.spectralFlux");
    s.onsetStrength = bus.declare("audio.onsetStrength", 0.0f, 4.0f);
    s.onset = bus.declare("audio.onset", 0.0f, 1.0f, true);
    return s;
}

void AudioSignals::publish(SignalBus& bus, const analysis::AnalysisFrame& frame) const {
    bus.set(rms, frame.rms);
    bus.set(peak, frame.peak);
    bus.set(bass, bandOrZero(frame, 0));
    bus.set(lowMid, bandOrZero(frame, 1));
    bus.set(mid, bandOrZero(frame, 2));
    bus.set(highMid, bandOrZero(frame, 3));
    bus.set(treble, bandOrZero(frame, 4));
    bus.set(centroid, frame.centroidNorm);
    bus.set(flux, frame.flux);
    bus.set(onsetStrength, frame.onsetStrength);
    bus.setEvent(onset, frame.onset, std::min(1.0f, frame.onsetStrength * 0.5f));
}

void AudioSignals::publishSilence(SignalBus& bus) const {
    for (const SignalId id : {rms, peak, bass, lowMid, mid, highMid, treble, centroid, flux, onsetStrength}) {
        bus.set(id, 0.0f);
    }
    bus.setEvent(onset, false);
}

} // namespace avgen::signals
