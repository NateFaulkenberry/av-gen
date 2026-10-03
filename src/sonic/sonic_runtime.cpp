#include "sonic/sonic_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>

namespace avgen::sonic {

namespace {

constexpr double kFollowerSeconds = 0.15;  // the level follower the transient is measured against
constexpr double kSlopeSeconds = 0.2;      // smoothing of the follower's slope
constexpr double kStatsSeconds = 1.0;      // the centroid's running mean and variance
constexpr double kFluxSeconds = 0.5;       // the flux average
constexpr float kTransientRangeDb = 18.0f; // a rise this far over the follower is a transient of 1

double alpha(double dt, double tau) {
    return tau > 1e-6 ? 1.0 - std::exp(-dt / tau) : 1.0;
}

float norm01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace

// ---- setup ---------------------------------------------------------------------------------------------------

Result<SonicSetup> SonicSetup::fromJson(const nlohmann::json& j, const std::filesystem::path& baseDir) {
    if (!j.is_object()) {
        return fail("'sonic' must be an object");
    }
    SonicSetup s;
    s.document = j;
    if (const auto l = j.find("live"); l != j.end()) {
        if (!l->is_boolean()) {
            return fail("'sonic.live' must be true or false");
        }
        s.live = l->get<bool>();
    }
    if (const auto c = j.find("character"); c != j.end()) {
        if (auto r = s.character.applyJson(*c); !r) {
            return std::unexpected(r.error());
        }
    }
    if (const auto t = j.find("timbre"); t != j.end() && t->is_object()) {
        s.timbreConfig.minF0Hz = t->value("minF0Hz", s.timbreConfig.minF0Hz);
        s.timbreConfig.maxF0Hz = t->value("maxF0Hz", s.timbreConfig.maxF0Hz);
        s.timbreConfig.maxF0Count = t->value("maxF0Count", s.timbreConfig.maxF0Count);
        s.timbreConfig.prominenceDb = t->value("prominenceDb", s.timbreConfig.prominenceDb);
        s.timbreConfig.harmonicTolerance = t->value("harmonicTolerance", s.timbreConfig.harmonicTolerance);
    }
    if (const auto c = j.find("context"); c != j.end() && c->is_object()) {
        s.context.tau = c->value("tau", s.context.tau);
        s.context.window = c->value("window", s.context.window);
        s.context.chordSeconds = c->value("chordSeconds", s.context.chordSeconds);
        s.context.rangeSeconds = c->value("rangeSeconds", s.context.rangeSeconds);
        s.context.phraseSeconds = c->value("phraseSeconds", s.context.phraseSeconds);
        s.context.phraseGapSeconds = c->value("phraseGapSeconds", s.context.phraseGapSeconds);
        s.context.repeatLookback = c->value("repeatLookback", s.context.repeatLookback);
    }
    if (const auto c = j.find("scale"); c != j.end() && c->is_object()) {
        ContextScale& k = s.scale;
        k.polyphony = c->value("polyphony", k.polyphony);
        k.density = c->value("density", k.density);
        k.rhythm = c->value("rhythm", k.rhythm);
        k.lowPitch = c->value("lowPitch", k.lowPitch);
        k.highPitch = c->value("highPitch", k.highPitch);
        k.range = c->value("range", k.range);
        k.motion = c->value("motion", k.motion);
        k.shortNote = c->value("shortNote", k.shortNote);
        k.longNote = c->value("longNote", k.longNote);
    }
    if (const auto r = j.find("response"); r != j.end()) {
        if (auto ok = s.response.applyJson(*r); !ok) {
            return std::unexpected(ok.error());
        }
        s.splitKey = std::clamp(r->value("splitKey", s.splitKey), 0, 127);
    }
    if (const auto n = j.find("notes"); n != j.end()) {
        if (!n->is_string()) {
            return fail("'sonic.notes' must be a path to a MIDI file");
        }
        std::filesystem::path path = n->get<std::string>();
        if (path.is_relative()) {
            path = baseDir / path;
        }
        auto track = NoteTrack::fromMidiFile(path);
        if (!track) {
            return std::unexpected(track.error());
        }
        s.notes = std::move(*track);
        s.notesPath = path.lexically_normal();
    }
    return s;
}

void SonicSetup::analyse(const analysis::AnalysisTrack& track) {
    if (trackFrames == track.frames().size() && timbre.size() == track.frames().size()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    timbre = analyzeTimbre(track, timbreConfig);
    trackFrames = track.frames().size();
    timbreMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

std::uint64_t SonicSetup::key() const {
    std::uint64_t h = std::hash<std::string>{}(document.dump());
    h ^= trackFrames + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h ^= notes.notes.size() + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

// ---- runtime -------------------------------------------------------------------------------------------------

void SonicRuntime::declare(signals::SignalBus& bus) {
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        const std::string name = std::string("sonic.") + dimensionName(static_cast<Dimension>(i));
        ids_.medium[i] = bus.declare(name);
        ids_.slow[i] = bus.declare(name + ".slow");
    }
    ids_.transient = bus.declare("sonic.transient", 0.0f, 1.0f, true);
    bus.setLabel(ids_.transient, "sonic transient (a fast rise in level; strength = how fast)");

    ids_.loudness = bus.declare("timbre.loudness", -120.0f, 0.0f);
    ids_.pitch = bus.declare("timbre.pitch", 0.0f, 4000.0f);
    ids_.pitchConfidence = bus.declare("timbre.pitchConfidence");
    ids_.harmonicity = bus.declare("timbre.harmonicity");
    ids_.inharmonicity = bus.declare("timbre.inharmonicity");
    ids_.tonalness = bus.declare("timbre.tonalness");
    ids_.flatness = bus.declare("timbre.flatness");
    ids_.dissonance = bus.declare("timbre.dissonance", 0.0f, 0.5f);
    ids_.rolloff = bus.declare("timbre.rolloff", 0.0f, 24000.0f);
    ids_.bandwidth = bus.declare("timbre.bandwidth", 0.0f, 12000.0f);
    ids_.rawTransient = bus.declare("timbre.transient");
    ids_.levelSlope = bus.declare("timbre.levelSlope", -200.0f, 200.0f);
    bus.setLabel(ids_.loudness, "loudness, dBFS (raw)");
    bus.setLabel(ids_.pitch, "fundamental, Hz (raw, 0 = none)");

    ids_.noteOn = bus.declare("notes.noteOn", 0.0f, 1.0f, true);
    ids_.noteOff = bus.declare("notes.noteOff", 0.0f, 1.0f, true);
    ids_.phraseStart = bus.declare("notes.phraseStart", 0.0f, 1.0f, true);
    ids_.active = bus.declare("notes.active", 0.0f, 32.0f);
    ids_.polyphony = bus.declare("notes.polyphony");
    ids_.density = bus.declare("notes.density");
    ids_.rhythm = bus.declare("notes.rhythm");
    ids_.velocity = bus.declare("notes.velocity");
    ids_.notePitch = bus.declare("notes.pitch");
    ids_.range = bus.declare("notes.range");
    ids_.motion = bus.declare("notes.motion");
    ids_.direction = bus.declare("notes.direction", -1.0f, 1.0f);
    ids_.duration = bus.declare("notes.duration");
    ids_.legato = bus.declare("notes.legato");
    ids_.regularity = bus.declare("notes.regularity");
    ids_.chord = bus.declare("notes.chord");
    ids_.tension = bus.declare("notes.tension");
    ids_.repetition = bus.declare("notes.repetition");
    ids_.phrase = bus.declare("notes.phrase");
    bus.setLabel(ids_.noteOn, "note on (MIDI; strength = velocity)");
    bus.setLabel(ids_.active, "notes sounding (a count)");
    bus.setLabel(ids_.notePitch, "pitch centre of the notes (0 = C1, 1 = C8)");
    bus.setLabel(ids_.direction, "melodic direction (-1 falling, +1 rising)");

    // ADR-1062, appended so every id above is the one it was: the per-note facts, then the response model.
    ids_.lastPitch = bus.declare("notes.lastPitch");
    bus.setLabel(ids_.lastPitch, "pitch of the latest note-on (0 = C1, 1 = C8)");
    ids_.lastVelocity = bus.declare("notes.lastVelocity");
    ids_.interval = bus.declare("notes.interval", -1.0f, 1.0f);
    bus.setLabel(ids_.interval, "the last melodic step, signed (an octave = 1)");
    ids_.lowest = bus.declare("notes.lowest");
    bus.setLabel(ids_.lowest, "lowest sounding note (0 = C1, 1 = C8; 0 when none)");
    ids_.highest = bus.declare("notes.highest");
    ids_.velocitySpread = bus.declare("notes.velocitySpread");
    ids_.held = bus.declare("notes.held");
    bus.setLabel(ids_.held, "how long the longest-sounding note has been held (log, 0.05-4 s)");
    ids_.channel = bus.declare("notes.channel");
    ids_.release = bus.declare("notes.release", 0.0f, 1.0f, true);
    bus.setLabel(ids_.release, "note off (strength = how long the note was, log 0.05-4 s)");
    ids_.low = bus.declare("notes.low", 0.0f, 1.0f, true);
    bus.setLabel(ids_.low, "note on below the split key (strength = velocity)");
    ids_.high = bus.declare("notes.high", 0.0f, 1.0f, true);
    bus.setLabel(ids_.high, "note on at or above the split key (strength = velocity)");
    for (int v = 0; v < kVoiceSlots; ++v) {
        const std::string p = "notes.voice." + std::to_string(v) + ".";
        auto& ids = ids_.voices[static_cast<std::size_t>(v)];
        ids.held = bus.declare(p + "held");
        ids.velocity = bus.declare(p + "velocity");
        ids.pitch = bus.declare(p + "pitch");
        ids.age = bus.declare(p + "age");
        ids.on = bus.declare(p + "on", 0.0f, 1.0f, true);
    }
    for (int k = 0; k < 12; ++k) {
        ids_.pitchClass[static_cast<std::size_t>(k)] = bus.declare("notes.class." + std::to_string(k));
        ids_.classOn[static_cast<std::size_t>(k)] = bus.declare("notes.classOn." + std::to_string(k), 0.0f, 1.0f, true);
    }
    bus.setLabel(ids_.pitchClass[0], "pitch class C (the loudest sounding velocity)");
    response_.declare(bus);
}

void SonicRuntime::reset() {
    const SonicSignals ids = ids_;
    const float timeScale = timeScale_;
    const ResponseSignals responseIds = response_.ids();
    const ResponseControls controls = controls_;
    *this = SonicRuntime{};
    ids_ = ids;
    timeScale_ = timeScale;
    controls_ = controls;
    response_ = ResponseModel{};
    restoreResponseIds(responseIds);
}

void SonicRuntime::step(const SonicSetup& setup, const TimbreFeatures& f, double dt) {
    const CharacterSpec& spec = setup.character;
    latest_ = f;
    const float loud = std::max(f.loudnessDb, -120.0f);
    // The level follower: the transient is the frame's level against it, the slope is how it moves.
    float transient = 0.0f;
    if (!statsPrimed_ && followerDb_ <= -119.0f) {
        followerDb_ = loud;
    } else {
        transient = norm01((loud - followerDb_) / kTransientRangeDb);
        const float before = followerDb_;
        followerDb_ += static_cast<float>(alpha(dt, kFollowerSeconds)) * (loud - followerDb_);
        const float slope = dt > 0.0 ? static_cast<float>((followerDb_ - before) / dt) : 0.0f;
        slopeDb_ += static_cast<float>(alpha(dt, kSlopeSeconds)) * (slope - slopeDb_);
    }
    const bool open = !f.silent && loud >= spec.gateDb;
    if (open) {
        const double x = std::log2(std::max(f.centroidHz, 20.0f));
        if (!statsPrimed_) {
            logCentroidMean_ = x;
            logCentroidSquare_ = x * x;
            fluxAverage_ = f.relativeFlux;
            statsPrimed_ = true;
        } else {
            const double a = alpha(dt, kStatsSeconds);
            logCentroidMean_ += a * (x - logCentroidMean_);
            logCentroidSquare_ += a * (x * x - logCentroidSquare_);
            fluxAverage_ += static_cast<float>(alpha(dt, kFluxSeconds)) * (f.relativeFlux - fluxAverage_);
        }
    }
    const double deviation = std::sqrt(std::max(0.0, logCentroidSquare_ - logCentroidMean_ * logCentroidMean_));

    auto& v = features_;
    const auto set = [&v](Feature which, float value) { v[static_cast<std::size_t>(which)] = value; };
    set(Feature::Loudness, loud);
    set(Feature::Centroid, f.centroidHz);
    set(Feature::Bandwidth, f.bandwidthHz);
    set(Feature::Rolloff, f.rolloffHz);
    set(Feature::Flatness, f.flatness);
    set(Feature::LowRatio, f.lowRatioDb);
    set(Feature::HighRatio, f.highRatioDb);
    set(Feature::Flux, f.relativeFlux);
    set(Feature::Pitch, f.f0Hz);
    set(Feature::PitchConfidence, f.pitchConfidence);
    set(Feature::Harmonicity, f.harmonicity);
    set(Feature::Inharmonicity, f.inharmonicity);
    set(Feature::Tonalness, f.tonalness);
    set(Feature::Dissonance, f.dissonance);
    set(Feature::Peaks, f.peakCount);
    set(Feature::Fundamentals, static_cast<float>(f.f0Count));
    set(Feature::Width, f.stereo ? f.width : 0.0f);
    set(Feature::Transient, transient);
    set(Feature::LevelSlope, slopeDb_);
    set(Feature::CentroidDeviation, static_cast<float>(deviation));
    set(Feature::FluxAverage, fluxAverage_);

    instant_ = evaluateCharacter(spec, v);
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        const DimensionSpec& d = spec.dimensions[i];
        if (d.gated && !open) {
            continue; // hold: the sound keeps its character between notes
        }
        openSeconds_[i] += dt;
        // Until a tier has seen as much sound as its time constant it is the plain average of what it has seen,
        // so the first seconds of a piece are not spent climbing from zero (an EMA's bias correction).
        const double warm = openSeconds_[i] > 0.0 ? dt / openSeconds_[i] : 1.0;
        const float target = instant_[i];
        const double scale = static_cast<double>(timeScale_);
        const double tau = (target > medium_[i] ? d.attack : d.release) * scale;
        medium_[i] += static_cast<float>(std::max(alpha(dt, tau), warm)) * (target - medium_[i]);
        slow_[i] += static_cast<float>(std::max(alpha(dt, d.slow * scale), warm)) * (medium_[i] - slow_[i]);
    }
    primed_ = true;

    response_.step(f, dt, controls_, setup.response); // ADR-1062

    // The fast tier: a transient event on the rising edge, with a refractory time and hysteresis.
    sinceTransient_ += dt;
    if (transient >= spec.transientThreshold && transientArmed_ && sinceTransient_ >= spec.transientRefractory) {
        transientPending_ = true;
        transientStrength_ = std::max(transientStrength_, transient);
        transientArmed_ = false;
        sinceTransient_ = 0.0;
    } else if (transient < 0.5f * spec.transientThreshold) {
        transientArmed_ = true;
    }
}

void SonicRuntime::advance(const SonicSetup& setup, const analysis::AnalysisTrack& track, double seconds) {
    const auto& frames = track.frames();
    if (frames.empty() || setup.timbre.size() != frames.size()) {
        return;
    }
    if (seconds + 1e-9 < lastSeconds_) {
        // Back in time: start again. The character is history, and the history a play reaches is from zero.
        const SonicSignals ids = ids_;
        const double lastPublish = lastPublish_;
        const float timeScale = timeScale_;
        const ResponseSignals responseIds = response_.ids();
        const ResponseControls controls = controls_;
        *this = SonicRuntime{};
        ids_ = ids;
        lastPublish_ = lastPublish;
        timeScale_ = timeScale;
        controls_ = controls;
        restoreResponseIds(responseIds);
    }
    const double hop = static_cast<double>(track.config().hopSize) / static_cast<double>(track.config().sampleRate);
    while (cursor_ < frames.size() && frames[cursor_].timeSeconds <= seconds) {
        step(setup, setup.timbre[cursor_], hop);
        ++cursor_;
    }
    lastSeconds_ = seconds;
}

void SonicRuntime::publish(const SonicSetup* setup, signals::SignalBus& bus, double seconds) {
    if (ids_.transient == signals::kInvalidSignal) {
        return; // never declared on this bus
    }
    if (setup == nullptr) {
        if (!zeroed_) {
            for (std::size_t i = 0; i < kDimensionCount; ++i) {
                bus.set(ids_.medium[i], 0.0f);
                bus.set(ids_.slow[i], 0.0f);
            }
            for (const signals::SignalId id :
                 {ids_.loudness, ids_.pitch, ids_.pitchConfidence, ids_.harmonicity, ids_.inharmonicity, ids_.tonalness,
                  ids_.flatness, ids_.dissonance, ids_.rolloff, ids_.bandwidth, ids_.rawTransient, ids_.levelSlope,
                  ids_.active, ids_.polyphony, ids_.density, ids_.rhythm, ids_.velocity, ids_.notePitch, ids_.range,
                  ids_.motion, ids_.direction, ids_.duration, ids_.legato, ids_.regularity, ids_.chord, ids_.tension,
                  ids_.repetition, ids_.phrase}) {
                bus.set(id, 0.0f);
            }
            for (const signals::SignalId id : {ids_.lastPitch, ids_.lastVelocity, ids_.interval, ids_.lowest,
                                               ids_.highest, ids_.velocitySpread, ids_.held, ids_.channel}) {
                bus.set(id, 0.0f);
            }
            for (const auto& v : ids_.voices) {
                for (const signals::SignalId id : {v.held, v.velocity, v.pitch, v.age}) {
                    bus.set(id, 0.0f);
                }
            }
            for (const signals::SignalId id : ids_.pitchClass) {
                bus.set(id, 0.0f);
            }
            response_.publishZeros(bus);
            zeroed_ = true;
        }
        return;
    }
    publish(*setup, setup->notes, bus, seconds);
}

void SonicRuntime::publish(const SonicSetup& setupRef, const NoteTrack& notes, signals::SignalBus& bus,
                           double seconds) {
    if (ids_.transient == signals::kInvalidSignal) {
        return;
    }
    const SonicSetup* setup = &setupRef;
    zeroed_ = false;
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        bus.set(ids_.medium[i], medium_[i]);
        bus.set(ids_.slow[i], slow_[i]);
    }
    bus.setEvent(ids_.transient, transientPending_, transientStrength_);
    transientPending_ = false;
    transientStrength_ = 0.0f;

    bus.set(ids_.loudness, primed_ ? latest_.loudnessDb : -120.0f);
    bus.set(ids_.pitch, latest_.f0Hz);
    bus.set(ids_.pitchConfidence, latest_.pitchConfidence);
    bus.set(ids_.harmonicity, latest_.harmonicity);
    bus.set(ids_.inharmonicity, latest_.inharmonicity);
    bus.set(ids_.tonalness, latest_.tonalness);
    bus.set(ids_.flatness, latest_.flatness);
    bus.set(ids_.dissonance, latest_.dissonance);
    bus.set(ids_.rolloff, latest_.rolloffHz);
    bus.set(ids_.bandwidth, latest_.bandwidthHz);
    bus.set(ids_.rawTransient, features_[static_cast<std::size_t>(Feature::Transient)]);
    bus.set(ids_.levelSlope, slopeDb_);

    const ContextScale& k = setup->scale;
    context_ = contextAt(notes, seconds, setup->context);
    const MusicalContext& c = context_;
    bus.set(ids_.active, static_cast<float>(c.active));
    bus.set(ids_.polyphony, norm01(static_cast<float>(c.active) / k.polyphony));
    bus.set(ids_.density, norm01(c.density / k.density));
    bus.set(ids_.rhythm, norm01(c.rhythm / k.rhythm));
    bus.set(ids_.velocity, norm01(c.velocity));
    bus.set(ids_.notePitch, c.pitch > 0.0f ? norm01((c.pitch - k.lowPitch) / (k.highPitch - k.lowPitch)) : 0.0f);
    bus.set(ids_.range, norm01(c.range / k.range));
    bus.set(ids_.motion, norm01(c.motion / k.motion));
    bus.set(ids_.direction, std::clamp(c.direction, -1.0f, 1.0f));
    bus.set(ids_.duration, c.duration > 0.0f ? norm01(std::log(c.duration / k.shortNote) /
                                                      std::log(k.longNote / k.shortNote))
                                             : 0.0f);
    bus.set(ids_.legato, norm01(c.legato));
    bus.set(ids_.regularity, norm01(c.regularity));
    bus.set(ids_.chord, norm01(c.chord));
    bus.set(ids_.tension, norm01(c.tension));
    bus.set(ids_.repetition, norm01(c.repetition));
    bus.set(ids_.phrase, norm01(c.phrase));

    // Events since the last publish. A first publish looks back a millisecond (a note at exactly 0 fires on the
    // first frame); a jump of more than a quarter second, either way, fires nothing -- a seek is not a burst of
    // every note it skipped.
    const double from = std::isnan(lastPublish_) ? seconds - 1e-3 : lastPublish_;
    NoteEvents e;
    if (seconds > from && seconds - from <= 0.25) {
        e = eventsBetween(notes, from, seconds, setup->context);
    }
    bus.setEvent(ids_.noteOn, e.noteOn, e.onVelocity);
    bus.setEvent(ids_.noteOff, e.noteOff, 1.0f);
    bus.setEvent(ids_.phraseStart, e.phraseStart, e.onVelocity);

    // ADR-1062: the per-note facts and the response model.
    float midiMelodic = 0.0f;
    float midiPitch = -1.0f;
    publishNoteFacts(*setup, notes, bus, seconds, from, seconds > from && seconds - from <= 0.25, midiMelodic,
                     midiPitch);
    response_.publish(bus, notes, seconds, e.noteOn, e.onVelocity, midiMelodic, midiPitch, controls_, setup->response);
    lastPublish_ = seconds;
}

void SonicRuntime::restoreResponseIds(const ResponseSignals& ids) {
    response_.restoreIds(ids);
}

void SonicRuntime::publishNoteFacts(const SonicSetup& setup, const NoteTrack& notes, signals::SignalBus& bus,
                                    double seconds, double from, bool interval, float& midiMelodic,
                                    float& midiPitch) {
    if (ids_.lastPitch == signals::kInvalidSignal) {
        return;
    }
    const ContextScale& k = setup.scale;
    const auto pitch01 = [&](float midi) {
        return midi >= 0.0f ? norm01((midi - k.lowPitch) / (k.highPitch - k.lowPitch)) : 0.0f;
    };
    const auto log01 = [&](double seconds01) {
        return seconds01 > 0.0 ? norm01(static_cast<float>(std::log(seconds01 / k.shortNote) /
                                                           std::log(k.longNote / k.shortNote)))
                               : 0.0f;
    };
    const NoteFacts f = noteFactsAt(notes, seconds, setup.context);
    bus.set(ids_.lastPitch, pitch01(f.lastPitch));
    bus.set(ids_.lastVelocity, f.lastVelocity);
    bus.set(ids_.interval, std::clamp(f.interval / 12.0f, -1.0f, 1.0f));
    bus.set(ids_.lowest, pitch01(f.lowest));
    bus.set(ids_.highest, pitch01(f.highest));
    bus.set(ids_.velocitySpread, norm01(f.velocitySpread / 0.5f));
    bus.set(ids_.held, log01(f.held));
    bus.set(ids_.channel, f.lastChannel >= 0 ? static_cast<float>(f.lastChannel) / 15.0f : 0.0f);
    for (std::size_t v = 0; v < ids_.voices.size(); ++v) {
        const NoteFacts::Voice& voice = f.voices[v];
        bus.set(ids_.voices[v].held, voice.held ? 1.0f : 0.0f);
        bus.set(ids_.voices[v].velocity, voice.held ? voice.velocity : 0.0f);
        bus.set(ids_.voices[v].pitch, voice.held ? pitch01(voice.pitch) : 0.0f);
        bus.set(ids_.voices[v].age, voice.held ? log01(voice.age) : 0.0f);
    }
    for (std::size_t c = 0; c < 12; ++c) {
        bus.set(ids_.pitchClass[c], f.pitchClass[c]);
    }
    NoteFactEvents e;
    if (interval) {
        e = noteFactEventsBetween(notes, from, seconds, setup.splitKey);
    }
    bus.setEvent(ids_.release, e.release, std::max(0.05f, log01(e.releaseSeconds)));
    bus.setEvent(ids_.low, e.low, e.lowVelocity);
    bus.setEvent(ids_.high, e.high, e.highVelocity);
    for (std::size_t v = 0; v < ids_.voices.size(); ++v) {
        bus.setEvent(ids_.voices[v].on, e.voiceOn[v], e.voiceVelocity[v]);
    }
    for (std::size_t c = 0; c < 12; ++c) {
        bus.setEvent(ids_.classOn[c], e.classOn[c], e.classVelocity[c]);
    }
    // The MIDI half of response.melodic and response.pitch: single notes in a moving line, and the latest note
    // while it is recent.
    const MusicalContext& c = context_;
    midiMelodic = (1.0f - norm01(c.chord)) * norm01(c.rhythm / k.rhythm) * norm01(3.0f * c.motion / k.motion);
    if (f.lastPitch >= 0.0f && !notes.notes.empty()) {
        const auto it = std::upper_bound(notes.notes.begin(), notes.notes.end(), seconds,
                                         [](double t, const NoteEvent& n) { return t < n.start; });
        if (it != notes.notes.begin() && seconds - (it - 1)->start <= 2.0) {
            midiPitch = pitch01(f.lastPitch);
        }
    }
}

} // namespace avgen::sonic
