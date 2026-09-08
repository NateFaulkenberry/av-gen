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
    s.tempo = bus.declare("audio.tempo", 0.0f, 300.0f);
    s.tempoConfidence = bus.declare("audio.tempoConfidence");
    s.beat = bus.declare("audio.beat", 0.0f, 1.0f, true);
    s.beatPhase = bus.declare("audio.beatPhase");
    s.beatCount = bus.declare("audio.beatCount", 0.0f, 100000.0f);
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
    bus.set(tempo, frame.tempoBpm);
    bus.set(tempoConfidence, frame.tempoConfidence);
    bus.setEvent(beat, frame.beat, 1.0f);
    bus.set(beatPhase, frame.beatPhase);
    bus.set(beatCount, static_cast<float>(frame.beatCount));
}

void AudioSignals::publishSilence(SignalBus& bus) const {
    for (const SignalId id : {rms, peak, bass, lowMid, mid, highMid, treble, centroid, flux, onsetStrength,
                              tempo, tempoConfidence, beatPhase, beatCount}) {
        bus.set(id, 0.0f);
    }
    bus.setEvent(onset, false);
    bus.setEvent(beat, false);
}

} // namespace avgen::signals
