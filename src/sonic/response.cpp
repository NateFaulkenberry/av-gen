#include "sonic/response.hpp"

#include "sonic/notes.hpp"
#include "sonic/timbre.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::sonic {

namespace {

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

double alpha(double dt, double tau) {
    return tau > 1e-6 ? 1.0 - std::exp(-dt / tau) : 1.0;
}

// An asymmetric one-pole: `attack` seconds rising, `release` falling.
void follow(float& y, float x, double dt, double attack, double release) {
    y += static_cast<float>(alpha(dt, x > y ? attack : release)) * (x - y);
}

// The response curve: higher sensitivity lifts the small values; 0 and 1 stay where they are.
float curve(float x, float sensitivity) {
    const float gamma = std::exp2((0.5f - sensitivity) * 1.6f);
    return std::pow(clamp01(x), gamma);
}

// At sensitivity 0 a kind is silent; it fades in over the first tenth.
float presence(float sensitivity) {
    return clamp01(sensitivity / 0.1f);
}

constexpr std::array<analysis::HitClass, 5> kAudioHits{analysis::HitClass::Kick, analysis::HitClass::Low,
                                                       analysis::HitClass::Snare, analysis::HitClass::Hat,
                                                       analysis::HitClass::Onset};
constexpr std::array<const char*, kResponseHitCount> kHitNames{"kick", "low", "snare", "hat", "onset", "note"};
constexpr std::array<const char*, kResponseHitCount> kHitLabels{
    "kick (a drum's low strike)", "low attack (a kick or a bass note)", "snare / clap", "hat",
    "onset (any attack, level-free)", "note (MIDI note-on, curved velocity)"};

} // namespace

float hitSensitivity(const ResponseControls& c) {
    return clamp01(c.sensitivity + c.transient - 0.5f);
}

float levelSensitivity(const ResponseControls& c) {
    return clamp01(c.sensitivity + c.sustain - 0.5f);
}

float conditionLevel(float db, float sensitivity, const ResponseSettings& s) {
    const float floor = s.floorDb - (sensitivity - 0.5f) * 2.0f * s.sensitivityDb;
    const float n = clamp01((db - floor) / std::max(s.rangeDb, 1.0f));
    return curve(n, sensitivity) * presence(sensitivity);
}

Result<void> ResponseSettings::applyJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'sonic.response' must be an object");
    }
    ResponseControls& c = controls;
    c.sensitivity = std::clamp(j.value("sensitivity", c.sensitivity), 0.0f, 1.0f);
    c.transient = std::clamp(j.value("transient", c.transient), 0.0f, 1.0f);
    c.sustain = std::clamp(j.value("sustain", c.sustain), 0.0f, 1.0f);
    c.attack = std::clamp(j.value("attack", c.attack), 0.25f, 4.0f);
    c.release = std::clamp(j.value("release", c.release), 0.25f, 4.0f);
    floorDb = j.value("floorDb", floorDb);
    rangeDb = std::max(1.0f, j.value("rangeDb", rangeDb));
    sensitivityDb = j.value("sensitivityDb", sensitivityDb);
    if (const auto r = j.find("hitRelease"); r != j.end() && r->is_object()) {
        for (std::size_t h = 0; h < kResponseHitCount; ++h) {
            hitRelease[h] = std::max(0.005f, r->value(kHitNames[h], hitRelease[h]));
        }
    }
    bassAttack = j.value("bassAttack", bassAttack);
    bassRelease = j.value("bassRelease", bassRelease);
    levelAttack = j.value("levelAttack", levelAttack);
    levelRelease = j.value("levelRelease", levelRelease);
    sustainAttack = j.value("sustainAttack", sustainAttack);
    sustainRelease = j.value("sustainRelease", sustainRelease);
    fluxAttack = j.value("fluxAttack", fluxAttack);
    fluxRelease = j.value("fluxRelease", fluxRelease);
    intensitySeconds = j.value("intensitySeconds", intensitySeconds);
    hatRateSeconds = std::max(0.1f, j.value("hatRateSeconds", hatRateSeconds));
    hatRateFull = std::max(0.1f, j.value("hatRateFull", hatRateFull));
    melodicFull = std::max(0.1f, j.value("melodicFull", melodicFull));
    return {};
}

nlohmann::json ResponseSettings::toJson() const {
    nlohmann::json releases;
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        releases[kHitNames[h]] = hitRelease[h];
    }
    return {{"sensitivity", controls.sensitivity}, {"transient", controls.transient}, {"sustain", controls.sustain},
            {"attack", controls.attack},           {"release", controls.release},     {"floorDb", floorDb},
            {"rangeDb", rangeDb},                  {"sensitivityDb", sensitivityDb},  {"hitRelease", releases}};
}

void ResponseModel::declare(signals::SignalBus& bus) {
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        const std::string name = std::string("response.") + kHitNames[h];
        ids_.hit[h] = bus.declare(name, 0.0f, 1.0f, true);
        bus.setLabel(ids_.hit[h], kHitLabels[h]);
        ids_.env[h] = bus.declare(name + "Env");
        bus.setLabel(ids_.env[h], std::string(kHitLabels[h]) + ", as an envelope");
    }
    ids_.bass = bus.declare("response.bass");
    bus.setLabel(ids_.bass, "bass level (30-150 Hz, fast)");
    ids_.level = bus.declare("response.level");
    bus.setLabel(ids_.level, "level (fast)");
    ids_.transient = bus.declare("response.transient");
    bus.setLabel(ids_.transient, "attack energy (fast minus slow level)");
    ids_.sustain = bus.declare("response.sustain");
    bus.setLabel(ids_.sustain, "sustained energy (slow, not percussive)");
    ids_.flux = bus.declare("response.flux");
    bus.setLabel(ids_.flux, "spectral change");
    ids_.hatRate = bus.declare("response.hatRate");
    bus.setLabel(ids_.hatRate, "hat activity (hats a second)");
    ids_.melodic = bus.declare("response.melodic");
    bus.setLabel(ids_.melodic, "melodic activity (pitch moves; MIDI single notes)");
    ids_.pitch = bus.declare("response.pitch");
    bus.setLabel(ids_.pitch, "pitch of the latest note or confident pitch (0 = C1, 1 = C8)");
    ids_.intensity = bus.declare("response.intensity");
    bus.setLabel(ids_.intensity, "intensity (level over 12 s: the macro dynamics)");
}

void ResponseModel::step(const TimbreFeatures& f, double dt, const ResponseControls& c, const ResponseSettings& s) {
    const analysis::CausalOnsets& o = f.causal;
    const float hitS = hitSensitivity(c);
    const float levelS = levelSensitivity(c);
    const double att = std::clamp(static_cast<double>(c.attack), 0.25, 4.0);
    const double rel = std::clamp(static_cast<double>(c.release), 0.25, 4.0);
    if (!pickersReady_) {
        for (std::size_t k = 0; k < pickers_.size(); ++k) {
            pickers_[k] = analysis::defaultHitPicker(static_cast<analysis::HitClass>(k));
        }
        pickersReady_ = true;
    }
    // ---- hits: the causal ratios, picked at a threshold the sensitivity moves ----
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        env_[h] *= static_cast<float>(std::exp(-dt / (static_cast<double>(s.hitRelease[h]) * rel)));
    }
    for (std::size_t i = 0; i < kAudioHits.size(); ++i) {
        const auto k = static_cast<std::size_t>(kAudioHits[i]);
        analysis::HitPicker& p = pickers_[k];
        const float base = analysis::defaultHitPicker(kAudioHits[i]).fire;
        p.fire = base * std::exp2((0.5f - hitS) * 1.2f); // 0.66x .. 1.5x the default threshold
        const float ratio = o.valid ? o.ratio[k] : 0.0f;
        const float strength = hitS > 0.0f ? p.step(ratio, dt) : 0.0f;
        if (strength > 0.0f) {
            const float shaped = curve(strength, hitS) * presence(hitS);
            const std::size_t h = i; // kAudioHits are in ResponseHit order
            pending_[h] = true;
            pendingStrength_[h] = std::max(pendingStrength_[h], shaped);
            env_[h] = std::max(env_[h], shaped);
            ++count_[h];
            if (h == static_cast<std::size_t>(ResponseHit::Hat)) {
                hatRate_ += 1.0f;
            }
        }
    }
    hatRate_ *= static_cast<float>(std::exp(-dt / std::max(0.1, static_cast<double>(s.hatRateSeconds))));

    // ---- levels: fixed dB ranges, the moved floor, the curve, the followers ----
    if (!o.valid) {
        return;
    }
    const float bassN = conditionLevel(o.bassDb, levelS, s);
    const float levelN = conditionLevel(o.levelDb, levelS, s);
    follow(bass_, bassN, dt, s.bassAttack * att, s.bassRelease * rel);
    follow(level_, levelN, dt, s.levelAttack * att, s.levelRelease * rel);
    // The transient designer: a fast follower minus a slow one, in dB, 18 dB for 1.
    if (!primed_) {
        fastDb_ = slowDb_ = o.levelDb;
        primed_ = true;
    }
    follow(fastDb_, o.levelDb, dt, 0.003 * att, 0.04 * rel);
    follow(slowDb_, o.levelDb, dt, 0.04 * att, 0.4 * rel);
    const float rise = clamp01((fastDb_ - slowDb_) / 18.0f);
    transient_ = curve(rise, hitS) * presence(hitS) * (o.levelDb > s.floorDb - 24.0f ? 1.0f : 0.0f);
    follow(percussive_, rise, dt, 0.05, 0.6);
    // Sustained: the slow level, less the share that is attacks.
    follow(sustain_, levelN * (1.0f - clamp01(percussive_ * 2.5f)), dt, s.sustainAttack * att, s.sustainRelease * rel);
    // Change: the broadband ratio over its own background, 0.5..6.5 -> 0..1.
    const float fluxN = curve(clamp01((o.ratio[static_cast<std::size_t>(analysis::HitClass::Onset)] - 0.5f) / 6.0f),
                              levelS) * presence(levelS);
    follow(flux_, fluxN, dt, s.fluxAttack * att, s.fluxRelease * rel);
    follow(intensity_, levelN, dt, s.intensitySeconds, s.intensitySeconds);
    // Melodic activity from the audio: confident pitch moves of half a semitone or more, weighted by size (7 = 1).
    float move = 0.0f;
    if (f.f0Hz > 0.0f && f.pitchConfidence >= 0.5f) {
        const float semitone = 69.0f + 12.0f * std::log2(f.f0Hz / 440.0f);
        if (lastSemitone_ >= 0.0f) {
            const float d = std::fabs(semitone - lastSemitone_);
            if (d >= 0.5f && d <= 24.0f) {
                move = std::min(d, 7.0f) / 7.0f;
            }
        }
        lastSemitone_ = semitone;
        audioPitch_ = clamp01((semitone - 24.0f) / 84.0f);
    }
    melodic_ = melodic_ * static_cast<float>(std::exp(-dt / 1.0)) + move;
}

void ResponseModel::publish(signals::SignalBus& bus, const NoteTrack& notes, double seconds, bool noteOn,
                            float noteVelocity, float midiMelodic, float midiPitch, const ResponseControls& c,
                            const ResponseSettings& s) {
    if (ids_.bass == signals::kInvalidSignal) {
        return;
    }
    const float hitS = hitSensitivity(c);
    const double rel = std::clamp(static_cast<double>(c.release), 0.25, 4.0);
    // ---- the note: an event through the hit chain, and an envelope that is a pure function of the track ----
    constexpr auto kNote = static_cast<std::size_t>(ResponseHit::Note);
    if (noteOn && hitS > 0.0f) {
        pending_[kNote] = true;
        pendingStrength_[kNote] = std::max(pendingStrength_[kNote], curve(noteVelocity, hitS) * presence(hitS));
        ++count_[kNote];
    }
    {
        const double tau = static_cast<double>(s.hitRelease[kNote]) * rel;
        float env = 0.0f;
        const auto& n = notes.notes;
        auto it = std::upper_bound(n.begin(), n.end(), seconds,
                                   [](double t, const NoteEvent& e) { return t < e.start; });
        while (it != n.begin()) {
            --it;
            const double age = seconds - it->start;
            if (age > 8.0 * tau) {
                break;
            }
            env = std::max(env, curve(it->velocity, hitS) * presence(hitS) * static_cast<float>(std::exp(-age / tau)));
        }
        env_[kNote] = env;
    }
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        bus.setEvent(ids_.hit[h], pending_[h], pendingStrength_[h]);
        bus.set(ids_.env[h], env_[h]);
        pending_[h] = false;
        pendingStrength_[h] = 0.0f;
    }
    bus.set(ids_.bass, bass_);
    bus.set(ids_.level, level_);
    bus.set(ids_.transient, transient_);
    bus.set(ids_.sustain, sustain_);
    bus.set(ids_.flux, flux_);
    bus.set(ids_.hatRate, clamp01(hatRate_ / std::max(s.hatRateSeconds, 0.1f) / s.hatRateFull));
    bus.set(ids_.melodic, std::max(clamp01(melodic_ / s.melodicFull), clamp01(midiMelodic)));
    bus.set(ids_.pitch, midiPitch >= 0.0f ? midiPitch : std::max(audioPitch_, 0.0f));
    bus.set(ids_.intensity, intensity_);
}

void ResponseModel::publishZeros(signals::SignalBus& bus) {
    if (ids_.bass == signals::kInvalidSignal) {
        return;
    }
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        bus.setEvent(ids_.hit[h], false);
        bus.set(ids_.env[h], 0.0f);
    }
    for (const signals::SignalId id : {ids_.bass, ids_.level, ids_.transient, ids_.sustain, ids_.flux, ids_.hatRate,
                                       ids_.melodic, ids_.pitch, ids_.intensity}) {
        bus.set(id, 0.0f);
    }
}

} // namespace avgen::sonic
