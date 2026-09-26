#include "signals/audio_signals.hpp"

#include <algorithm>
#include <array>

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
    // ADR-897 / ADR-898, each with the words an artist picking a route source would look for.
    static constexpr std::array<const char*, 5> kLevelNames{"audio.bassLevel", "audio.lowMidLevel", "audio.midLevel",
                                                            "audio.highMidLevel", "audio.trebleLevel"};
    static constexpr std::array<const char*, 5> kLevelLabels{
        "bass level (steady, not auto-gained)", "low-mid level (steady, not auto-gained)",
        "mid level (steady, not auto-gained)", "high-mid level (steady, not auto-gained)",
        "treble level (steady, not auto-gained)"};
    for (std::size_t b = 0; b < kLevelNames.size(); ++b) {
        s.bandLevels[b] = bus.declare(kLevelNames[b]);
        bus.setLabel(s.bandLevels[b], kLevelLabels[b]);
    }
    s.energy = bus.declare("audio.energy");
    bus.setLabel(s.energy, "energy (how much is happening, loudness-independent)");
    s.onsetRate = bus.declare("audio.onsetRate", 0.0f, 20.0f);
    bus.setLabel(s.onsetRate, "density (percussive hits per second)");
    s.width = bus.declare("audio.width", 0.0f, 2.0f);
    bus.setLabel(s.width, "stereo width");
    s.onsetLow = bus.declare("audio.onsetLow", 0.0f, 1.0f, true);
    bus.setLabel(s.onsetLow, "kick (low-band onset)");
    s.onsetMid = bus.declare("audio.onsetMid", 0.0f, 1.0f, true);
    bus.setLabel(s.onsetMid, "snare / clap (mid-band onset)");
    s.onsetHigh = bus.declare("audio.onsetHigh", 0.0f, 1.0f, true);
    bus.setLabel(s.onsetHigh, "hats (high-band onset)");
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
    for (std::size_t b = 0; b < bandLevels.size(); ++b) {
        bus.set(bandLevels[b], b < frame.bandCount && b < analysis::kMaxBands ? frame.bandLevels[b] : 0.0f);
    }
    bus.set(energy, frame.energy);
    bus.set(onsetRate, frame.onsetRate);
    bus.set(width, frame.stereo ? frame.width : 0.0f);
    // A band onset fires with its strength, floored so a quiet one still moves an event route: the
    // strength is how hard it hit against the band's strongest nearby, and a route that triggers on
    // the flag must never see a fired event with value 0 (ADR-898).
    bus.setEvent(onsetLow, frame.lowOnset, std::max(0.05f, frame.lowOnsetStrength));
    bus.setEvent(onsetMid, frame.midOnset, std::max(0.05f, frame.midOnsetStrength));
    bus.setEvent(onsetHigh, frame.highOnset, std::max(0.05f, frame.highOnsetStrength));
}

void AudioSignals::publishSilence(SignalBus& bus) const {
    for (const SignalId id : {rms, peak, bass, lowMid, mid, highMid, treble, centroid, flux, onsetStrength,
                              tempo, tempoConfidence, beatPhase, beatCount, energy, onsetRate, width}) {
        bus.set(id, 0.0f);
    }
    for (const SignalId id : bandLevels) {
        bus.set(id, 0.0f);
    }
    clearEvents(bus);
}

void AudioSignals::clearEvents(SignalBus& bus) const {
    for (const SignalId id : {onset, beat, onsetLow, onsetMid, onsetHigh}) {
        bus.setEvent(id, false);
    }
}

} // namespace avgen::signals
