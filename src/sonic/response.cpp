#include "sonic/response.hpp"

#include "sonic/notes.hpp"
#include "sonic/timbre.hpp"

#include "core/log.hpp"

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

double dbToPower(float db) {
    return std::pow(10.0, static_cast<double>(db) / 10.0);
}

float powerToDb(double p) {
    return p > 1e-12 ? static_cast<float>(10.0 * std::log10(p)) : -120.0f;
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

float conditionLevel(float db, float referenceDb, const ResponseSettings::LevelWindow& window, float sensitivity,
                     const ResponseSettings& s) {
    if (!(db > s.gateDb)) {
        return 0.0f;
    }
    const float span = std::max(window.spanDb, 1.0f);
    const float top = std::max(referenceDb, s.referenceMinDb) + window.topDb -
                      (sensitivity - 0.5f) * 2.0f * s.sensitivityShare * span;
    const float n = clamp01(1.0f - (top - db) / span);
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
    // ADR-1510 retired the fixed window (ADR-441: no aliases); a stale key is said once and ignored.
    for (const char* retired : {"floorDb", "rangeDb", "sensitivityDb"}) {
        if (j.contains(retired)) {
            log::warn("sonic.response.{} was retired by ADR-1510 (the levels read against an adaptive reference); "
                      "ignored",
                      retired);
        }
    }
    gateDb = j.value("gateDb", gateDb);
    referenceMinDb = j.value("referenceMinDb", referenceMinDb);
    referenceSeconds = std::max(0.05f, j.value("referenceSeconds", referenceSeconds));
    referenceRise = std::max(0.0f, j.value("referenceRise", referenceRise));
    referenceFall = std::max(0.0f, j.value("referenceFall", referenceFall));
    sensitivityShare = std::clamp(j.value("sensitivityShare", sensitivityShare), 0.0f, 1.0f);
    if (const auto w = j.find("windows"); w != j.end() && w->is_object()) {
        for (auto [name, window] : {std::pair{"bass", &bassWindow}, std::pair{"level", &levelWindow},
                                    std::pair{"sustain", &sustainWindow}, std::pair{"levelSlow", &levelSlowWindow},
                                    std::pair{"intensity", &intensityWindow}}) {
            const auto it = w->find(name);
            if (it == w->end()) {
                continue;
            }
            if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
                return fail("'sonic.response.windows.{}' must be [topDb, spanDb]", name);
            }
            window->topDb = (*it)[0].get<float>();
            window->spanDb = std::max(1.0f, (*it)[1].get<float>());
        }
    }
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
    levelSlowSeconds = std::max(0.05f, j.value("levelSlowSeconds", levelSlowSeconds));
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
    nlohmann::json windows;
    for (auto [name, window] : {std::pair{"bass", &bassWindow}, std::pair{"level", &levelWindow},
                                std::pair{"sustain", &sustainWindow}, std::pair{"levelSlow", &levelSlowWindow},
                                std::pair{"intensity", &intensityWindow}}) {
        windows[name] = {window->topDb, window->spanDb};
    }
    return {{"sensitivity", controls.sensitivity},
            {"transient", controls.transient},
            {"sustain", controls.sustain},
            {"attack", controls.attack},
            {"release", controls.release},
            {"gateDb", gateDb},
            {"referenceMinDb", referenceMinDb},
            {"referenceSeconds", referenceSeconds},
            {"referenceRise", referenceRise},
            {"referenceFall", referenceFall},
            {"sensitivityShare", sensitivityShare},
            {"windows", windows},
            {"hitRelease", releases}};
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
    bus.setLabel(ids_.bass, "bass level (30-150 Hz, the body: held between hits, against the music's loudness)");
    ids_.level = bus.declare("response.level");
    bus.setLabel(ids_.level, "level (the body: held between hits, against the music's loudness)");
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
    bus.setLabel(ids_.intensity, "intensity (level over 12 s against the last minute: the macro dynamics)");
    // ADR-1361: declared last, after every older id.
    ids_.levelSlow = bus.declare("response.levelSlow");
    bus.setLabel(ids_.levelSlow, "level, slow (over about 3 s: when a rest becomes silence)");
}

void ResponseModel::step(const TimbreFeatures& f, double dt, const ResponseControls& c, const ResponseSettings& s) {
    const analysis::CausalOnsets& o = f.causal;
    const float hitS = hitSensitivity(c);
    const float levelS = levelSensitivity(c);
    const double att = std::clamp(static_cast<double>(c.attack), 0.25, 4.0);
    const double rel = std::clamp(static_cast<double>(c.release), 0.25, 4.0);
    // ---- hits: the detector's decisions (ADR-1067), their strength shaped by the sensitivity. Below neutral the
    // weakest are dropped (at 0.25 those under 0.3); above it they are lifted, but no hit is made that the detector did
    // not hear: the detector's thresholds are measured, the performer's knob is how much they show. ----
    for (std::size_t h = 0; h < kResponseHitCount; ++h) {
        env_[h] *= static_cast<float>(std::exp(-dt / (static_cast<double>(s.hitRelease[h]) * rel)));
    }
    const float minStrength = std::max(0.0f, 0.5f - hitS) * 1.2f;
    for (std::size_t i = 0; i < kAudioHits.size(); ++i) {
        const auto k = static_cast<std::size_t>(kAudioHits[i]);
        float strength = hitS > 0.0f && o.valid && o.hit[k] ? o.strength[k] : 0.0f;
        if (strength < minStrength) {
            strength = 0.0f;
        }
        if (strength > 0.0f) {
            const float shaped = curve(strength, hitS) * presence(hitS);
            const std::size_t h = i; // kAudioHits are in ResponseHit order
            if (!pending_[h]) {
                envBefore_[h] = env_[h];
                pendingDrumness_[h] = 1.0f;
            }
            pending_[h] = true;
            pendingStrength_[h] = std::max(pendingStrength_[h], shaped);
            pendingDrumness_[h] = std::min(pendingDrumness_[h], o.drumness[k]);
            env_[h] = std::max(env_[h], shaped);
            ++count_[h];
            if (h == static_cast<std::size_t>(ResponseHit::Hat)) {
                hatRate_ += 1.0f;
            }
        }
    }
    hatRate_ *= static_cast<float>(std::exp(-dt / std::max(0.1, static_cast<double>(s.hatRateSeconds))));

    // ---- levels (ADR-1510): followers in dB, read against the adaptive loudness reference ----
    if (!o.valid) {
        return;
    }
    const float gate = s.gateDb;
    const float levelDb = std::max(o.levelDb, gate);
    const float bassDb = std::max(o.bassDb, gate);
    if (!levelsPrimed_) {
        bassDb_ = levelDb_ = referenceDb_ = gate;
        levelsPrimed_ = true;
    }
    const double power = dbToPower(levelDb);
    const bool heard = o.levelDb > gate;
    // The reference: the gated level's power over a few seconds -- until that much has been heard, the plain average
    // of what has (the character tiers' warm-up), so the first bars are read against themselves rather than against
    // silence -- after that followed up in seconds and down over a minute. Silence below the gate leaves it where it is.
    if (heard) {
        heardSeconds_ += dt;
        const double a = std::max(alpha(dt, static_cast<double>(s.referenceSeconds)), dt / heardSeconds_);
        referencePower_ += a * (power - referencePower_);
        const float source = powerToDb(referencePower_);
        if (heardSeconds_ <= 2.0 * static_cast<double>(s.referenceSeconds)) {
            referenceDb_ = source; // warming: the average of what has been heard, with no lag on top of it
        } else {
            follow(referenceDb_, source, dt, static_cast<double>(s.referenceRise), static_cast<double>(s.referenceFall));
        }
    }
    // The slow levels: power over levelSlowSeconds and intensitySeconds, from the first sound heard (the same warm-up),
    // silence included -- a rest becomes silence (ADR-1361).
    if (heard || sinceHeard_ > 0.0) {
        sinceHeard_ += dt;
        const double warm = dt / sinceHeard_;
        const double slowTau = static_cast<double>(s.levelSlowSeconds) * (power > levelSlowPower_ ? att : rel);
        levelSlowPower_ += std::max(alpha(dt, slowTau), warm) * (power - levelSlowPower_);
        intensityPower_ += std::max(alpha(dt, static_cast<double>(s.intensitySeconds)), warm) * (power - intensityPower_);
    }
    follow(bassDb_, bassDb, dt, static_cast<double>(s.bassAttack) * att, static_cast<double>(s.bassRelease) * rel);
    follow(levelDb_, levelDb, dt, static_cast<double>(s.levelAttack) * att,
           static_cast<double>(s.levelRelease) * rel);
    const float ref = referenceDb_;
    const float levelSlowDb = sinceHeard_ > 0.0 ? powerToDb(levelSlowPower_) : gate;
    const float intensityDb = sinceHeard_ > 0.0 ? powerToDb(intensityPower_) : gate;
    bass_ = conditionLevel(bassDb_, ref, s.bassWindow, levelS, s);
    level_ = conditionLevel(levelDb_, ref, s.levelWindow, levelS, s);
    levelSlow_ = conditionLevel(levelSlowDb, ref, s.levelSlowWindow, levelS, s);
    intensity_ = conditionLevel(intensityDb, ref, s.intensityWindow, levelS, s);
    // The transient designer: a fast follower minus a slow one, in dB, 18 dB for 1.
    if (!primed_) {
        fastDb_ = slowDb_ = o.levelDb;
        primed_ = true;
    }
    follow(fastDb_, o.levelDb, dt, 0.003 * att, 0.04 * rel);
    follow(slowDb_, o.levelDb, dt, 0.04 * att, 0.4 * rel);
    const float rise = clamp01((fastDb_ - slowDb_) / 18.0f);
    transient_ = curve(rise, hitS) * presence(hitS) * (o.levelDb > gate - 24.0f ? 1.0f : 0.0f);
    follow(percussive_, rise, dt, 0.05, 0.6);
    // Sustained: the level, less the share that is attacks, followed slowly.
    const float sustainN = conditionLevel(o.levelDb, ref, s.sustainWindow, levelS, s);
    follow(sustain_, sustainN * (1.0f - clamp01(percussive_ * 2.5f)), dt, s.sustainAttack * att,
           s.sustainRelease * rel);
    // Change: the broadband ratio over its own background, 0.5..6.5 -> 0..1.
    const float fluxN = curve(clamp01((o.ratio[static_cast<std::size_t>(analysis::HitClass::Onset)] - 0.5f) / 6.0f),
                              levelS) * presence(levelS);
    follow(flux_, fluxN, dt, s.fluxAttack * att, s.fluxRelease * rel);
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
    // ADR-1068: a snare or a hat that lands on a MIDI note-on (a note begun in the last 120 ms: the snare is decided
    // 43 ms after its attack, and a frame is up to one render frame late) is that note's attack unless it is clearly
    // noise: a pluck's or a stab's attack is broadband enough to pass the detector, and the note says what it was.
    {
        bool noteNear = false;
        const auto& n = notes.notes;
        for (auto it = std::upper_bound(n.begin(), n.end(), seconds,
                                        [](double t, const NoteEvent& e) { return t < e.start; });
             it != n.begin();) {
            --it;
            if (seconds - it->start > 0.12) {
                break;
            }
            noteNear = true;
            break;
        }
        if (noteNear) {
            for (const ResponseHit h : {ResponseHit::Snare, ResponseHit::Hat}) {
                const auto i = static_cast<std::size_t>(h);
                if (pending_[i] && pendingDrumness_[i] < s.midiDrumness) {
                    pending_[i] = false;
                    pendingStrength_[i] = 0.0f;
                    env_[i] = envBefore_[i];
                    ++demoted_[i];
                }
            }
        }
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
    bus.set(ids_.levelSlow, levelSlow_);
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
                                       ids_.melodic, ids_.pitch, ids_.intensity, ids_.levelSlow}) {
        bus.set(id, 0.0f);
    }
}

} // namespace avgen::sonic
