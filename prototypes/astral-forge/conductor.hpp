// THE ASTRAL FORGE -- the CONDUCTOR: the entity state as a pure function of t (and, for TEST 06, of the
// whole-song analysis). docs/prototypes/astral-forge/03-parameter-state-model.md is its specification.
#pragma once

#include "astral_audio.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace astral {

enum Arch { kMask = 0, kSeraph = 1, kAbyss = 2, kChimera = 3, kMachine = 4, kChoir = 5, kHorns = 6 };

struct State {
    float C = 0.0f, S = 0.0f, temper = 0.2f;
    float archA = 0, archB = 0, morph = 0.0f, breath = 0.0f;
    float mass = 1.0f, flash = 0.0f, flow = 0.8f, shimmer = 0.3f;
    float blast = 12.0f, heatInject = 1.0f, chaos = 2.0f, smoothK = 0.6f;
    glm::vec4 fold0{0.0f}, fold1{0.0f}; // twist, eyeDepth, tunnel, inversion | bend, -, choirSync, choirMerge
    glm::vec3 centre{0.0f};
    float scale = 1.0f;
    float fall = 0.0f, escape = 0.15f, strobe = 0.0f;
    float rigPhase = 0.0f, sweep = 0.0f, sweepStrength = 0.0f, flicker = 0.0f;
    float bandGain = 1.0f, warmth = 0.3f;
    float gratingUm = 1.6f;
    float sharpSpread = 0.6f;  // 1: only the anatomy's centre becomes precise; 0: everywhere (material studies)
    float bloom = 0.035f;
    float appendWeight = 1.0f; // density weight of appendage matter (below ~0.4: strands of dust, not tubes)
    glm::vec3 eye{0.0f, 0.0f, 25.0f}, target{0.0f};
    float fovDeg = 30.0f;
    float exposure = 0.85f, haze = 0.25f;
    std::string label; // camera behaviour / phase, for the log
};

// ---- small curve helpers ---------------------------------------------------------------------------
inline float smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
inline float sstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
struct Curve {
    std::vector<std::pair<float, float>> k;
    float operator()(float t) const {
        if (k.empty()) return 0.0f;
        if (t <= k.front().first) return k.front().second;
        for (std::size_t i = 1; i < k.size(); ++i)
            if (t <= k[i].first) {
                const float u = (t - k[i - 1].first) / std::max(k[i].first - k[i - 1].first, 1e-6f);
                return k[i - 1].second + (k[i].second - k[i - 1].second) * smooth01(u);
            }
        return k.back().second;
    }
};
inline float pulse(float t, float at, float width) { const float x = (t - at) / width; return (t >= at) ? std::exp(-x) : 0.0f; }
inline glm::vec3 orbit(float dist, float azDeg, float elDeg, glm::vec3 c) {
    const float az = glm::radians(azDeg), el = glm::radians(elDeg);
    return c + dist * glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
}
inline void defaults(State& s) {
    s.S = sstep(0.55f, 1.0f, s.C);
    s.chaos = 2.4f * (1.0f - 0.75f * s.C);
    s.smoothK = 1.3f - 1.05f * s.C;
}

// ---- TEST 01: CHAOS -> FACE ------------------------------------------------------------------------
inline State test01(float t) {
    State s;
    s.archA = s.archB = kMask;
    s.appendWeight = 0.3f;
    const Curve C{{{0.0f, 0.0f}, {1.5f, 0.02f}, {3.0f, 0.17f}, {5.0f, 0.33f}, {7.0f, 0.52f}, {8.6f, 0.72f}, {9.9f, 0.92f},
                   {10.7f, 1.0f}, {11.5f, 1.0f}, {11.6f, 0.2f}, {14.0f, 0.06f}}};
    s.C = C(t);
    defaults(s);
    s.flash = 0.35f * pulse(t, 4.3f, 0.12f) + 0.35f * pulse(t, 6.2f, 0.12f);
    s.temper = 0.12f + 0.55f * s.C * s.C;
    s.flow = 0.5f + 0.9f * s.C;
    s.shimmer = 0.35f;
    s.blast = 16.0f;
    s.heatInject = 0.8f;
    s.strobe = 0.35f * pulse(t, 11.55f, 0.06f);
    s.breath = 0.5f * std::sin(t * 1.3f);
    s.rigPhase = t * 0.35f;
    s.fovDeg = 26.0f;
    const float d = 22.0f - 9.0f * sstep(0.0f, 11.5f, t) - 2.5f * sstep(11.5f, 12.5f, t);
    s.eye = orbit(d, -8.0f + 10.0f * t / 14.0f, 4.0f, {0.0f, 0.2f, 0.0f});
    s.target = {0.0f, -0.1f, 0.0f};
    s.label = t < 11.5f ? "OBSERVER" : "COLLISION";
    return s;
}

// ---- TEST 02: METALLIC FIELD (no face) ---------------------------------------------------------------
inline State test02(float t) {
    State s;
    s.archA = s.archB = kHorns;
    s.sharpSpread = 0.0f;
    s.C = std::min(0.97f, 0.7f + 0.27f * sstep(0.0f, 2.0f, t)) + 0.03f * std::sin(t * 1.7f);
    defaults(s);
    s.temper = 0.3f + 0.45f * sstep(1.0f, 11.0f, t);
    s.flow = 1.2f;
    s.shimmer = 0.5f;
    s.rigPhase = t * 0.9f;
    s.sweep = std::fmod(t * 0.35f, 2.0f) - 1.0f;
    s.sweepStrength = 1.0f;
    s.gratingUm = 1.3f;
    s.breath = 0.6f * std::sin(t * 0.9f);
    const float u = sstep(0.0f, 12.0f, t);
    const float d = 15.0f - 8.0f * u - 4.2f * sstep(8.5f, 12.0f, t);
    s.eye = orbit(d, 20.0f + 75.0f * u, 18.0f - 22.0f * u, {0.0f, 0.3f, 0.0f});
    s.target = glm::mix(glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(1.2f, 1.6f, 1.0f), sstep(7.0f, 12.0f, t));
    s.fovDeg = 32.0f;
    s.label = t < 8.5f ? "OBSERVER" : "MICRO";
    return s;
}

// ---- TEST 03: DIMENSIONAL FOLD ------------------------------------------------------------------------
inline State test03(float t) {
    State s;
    s.archA = kSeraph;
    s.archB = kAbyss;
    s.C = 0.96f - 0.12f * pulse(t, 7.0f, 0.5f);
    defaults(s);
    const float restore = 1.0f - sstep(10.0f, 12.5f, t);
    s.fold0.x = 0.75f * sstep(2.0f, 4.5f, t) * restore;           // twist (wide wings tear above ~0.8: their tips outrun the matter)
    s.fold0.y = 1.0f * sstep(3.5f, 6.0f, t) * restore;            // the left eye recedes through depth
    s.fold0.z = 1.0f * sstep(5.0f, 7.5f, t) * restore;            // the mouth becomes a tunnel
    s.fold0.w = 0.42f * sstep(7.0f, 9.0f, t) * restore;           // sphere inversion: folds inward
    s.fold1.x = 1.2f * sstep(6.0f, 8.5f, t) * restore;            // bend
    s.morph = sstep(8.0f, 9.8f, t) * (1.0f - sstep(10.5f, 12.5f, t)); // unfolds as something else
    s.temper = 0.25f + 0.45f * s.morph + 0.2f * s.fold0.w;
    s.flow = 1.0f;
    s.shimmer = 0.4f;
    s.rigPhase = t * 0.4f;
    s.eye = orbit(14.0f - 2.5f * sstep(0.0f, 14.0f, t), -25.0f + 50.0f * sstep(0.0f, 14.0f, t), 6.0f, {0.0f, 0.3f, 0.0f});
    s.target = {0.0f, 0.4f, 0.0f};
    s.fovDeg = 30.0f;
    s.exposure = 1.1f;
    s.label = "IMPOSSIBLE";
    return s;
}

// ---- TEST 04: CHOIR -------------------------------------------------------------------------------------
inline State test04(float t) {
    State s;
    s.archA = s.archB = kChoir;
    const Curve C{{{0.0f, 0.55f}, {1.5f, 0.92f}, {12.4f, 1.0f}, {12.5f, 0.15f}, {15.0f, 0.05f}}};
    s.C = C(t);
    defaults(s);
    s.fold1.z = sstep(4.5f, 8.0f, t);   // sync
    s.fold1.w = sstep(8.5f, 11.0f, t);  // merge into one face
    s.temper = 0.12f + 0.25f * s.fold1.z + 0.35f * s.fold1.w;
    s.flow = 0.9f;
    s.shimmer = 0.45f;
    s.blast = 18.0f;
    s.strobe = 0.6f * pulse(t, 12.45f, 0.08f);
    s.rigPhase = t * 0.4f;
    s.warmth = 0.6f;
    s.sharpSpread = 0.0f; // every small face must be precise, not only the centre
    // close on a few faces (each its own), pulling back to reveal what they compose, then onto the one face
    const float d = 7.0f + 9.0f * sstep(1.5f, 8.0f, t) - 2.5f * sstep(9.0f, 12.0f, t);
    const glm::vec3 near{0.9f, 0.6f, 0.0f};
    const glm::vec3 focus = glm::mix(near, glm::vec3(0.0f, 0.1f, 0.0f), sstep(1.5f, 7.0f, t));
    s.eye = orbit(d, 28.0f - 28.0f * sstep(1.0f, 9.0f, t), 10.0f - 8.0f * sstep(2.0f, 9.0f, t), focus);
    s.target = focus;
    s.label = t < 7.0f ? "REVEAL" : (t < 12.4f ? "DESCENT" : "COLLISION");
    return s;
}

// ---- TEST 05: SCALE RECURSION ------------------------------------------------------------------------
// The camera moves from the engraving of an eye ring out to the meta field and back in, through the mouth,
// to the smaller face inside it, and to its engraving: no cut.
inline State test05(float t) {
    State s;
    s.archA = s.archB = kMask;
    s.appendWeight = 0.3f;
    s.C = 1.0f;
    defaults(s);
    s.temper = 0.35f;
    s.flow = 0.9f;
    s.shimmer = 0.35f;
    s.rigPhase = t * 0.3f;
    const glm::vec3 eyeRing{-0.86f + 0.25f, 0.52f + 0.1f, 0.62f + 0.33f};
    const glm::vec3 inner{0.0f, -1.32f, 0.66f - 0.35f};
    // log-distance: 0.18 -> 70 (t 0..7), hold, then 70 -> 0.12 at the inner face (t 9..16)
    float logd;
    glm::vec3 focus;
    if (t < 7.0f) {
        const float u = sstep(0.0f, 7.0f, t);
        logd = std::log(0.18f) + (std::log(70.0f) - std::log(0.18f)) * u;
        focus = glm::mix(eyeRing, glm::vec3(0.0f), sstep(0.5f, 5.0f, t));
    } else if (t < 9.0f) {
        logd = std::log(70.0f);
        focus = glm::vec3(0.0f);
    } else {
        const float u = sstep(9.0f, 16.0f, t);
        logd = std::log(70.0f) + (std::log(0.12f) - std::log(70.0f)) * u;
        focus = glm::mix(glm::vec3(0.0f), inner, sstep(10.0f, 14.5f, t));
    }
    const float d = std::exp(logd);
    const float az = -20.0f + 30.0f * std::sin(t * 0.25f);
    s.eye = orbit(d, az, 6.0f, focus);
    s.target = focus;
    s.fovDeg = 30.0f;
    s.label = t < 3.0f ? "MICRO" : (t < 9.0f ? "REVEAL" : (t < 13.0f ? "INTERNAL" : "MICRO"));
    return s;
}

// ---- TEST 06: AUDIO ----------------------------------------------------------------------------------------
// Musical structure, not amplitude: sections choose the god (repetition groups summon the same one), 16-beat
// phrases are coherence cycles (even phrases build from chaos, odd ones hold and fold), a kick on a phrase
// downbeat collapses the form, snares flash the face (eyes and mouth over-bind for ~250 ms), the mid band sets
// filament flow, the highs shimmer, and a section's spectral brightness tempers the metal.
struct Phrase { double start, end; int index; int section; bool kickOpens; };

struct Score {
    std::vector<Phrase> phrases;
    std::vector<int> sectionArch;
};

inline Score buildScore(const SongAnalysis& song) {
    Score sc;
    const std::vector<int> palette{kSeraph, kAbyss, kChoir, kMachine, kChimera, kMask};
    // The same music summons the same god: key on (function label, repetition group), in order of first
    // appearance. (On Fireballs the detector puts every section in one group; the labels still differ.)
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < song.sections.size(); ++i) {
        const std::string key = song.sections[i].label + "/" + std::to_string(song.sections[i].group);
        auto it = std::find(keys.begin(), keys.end(), key);
        if (it == keys.end()) { keys.push_back(key); it = keys.end() - 1; }
        sc.sectionArch.push_back(palette[static_cast<std::size_t>(it - keys.begin()) % palette.size()]);
    }
    auto sectionAt = [&](double t) {
        for (std::size_t i = 0; i < song.sections.size(); ++i) if (t < song.sections[i].end) return static_cast<int>(i);
        return static_cast<int>(song.sections.size()) - 1;
    };
    auto kickNear = [&](double t) {
        for (std::size_t k = 0; k < song.kickT.size(); ++k) if (std::abs(song.kickT[k] - t) < 0.07 && song.kickS[k] > 0.5f) return true;
        return false;
    };
    // phrases of 16 beats, restarted at every section boundary (a section always opens a phrase)
    const auto& B = song.beats;
    int idx = 0;
    std::size_t b = 0;
    while (b + 1 < B.size()) {
        const int sec = sectionAt(B[b] + 1e-3);
        const double secEnd = sec >= 0 ? song.sections[static_cast<std::size_t>(sec)].end : 1e9;
        std::size_t e = std::min(b + 16, B.size() - 1);
        while (e > b + 1 && B[e] > secEnd + 0.05) --e;
        sc.phrases.push_back({B[b], B[e], idx++, sec, kickNear(B[b])});
        b = e;
    }
    return sc;
}

inline float phraseCoherence(const Score& sc, const Phrase& ph, double t) {
    const float u = static_cast<float>((t - ph.start) / std::max(ph.end - ph.start, 1e-3));
    const bool firstOfSection = ph.index == 0 || sc.phrases[static_cast<std::size_t>(ph.index - 1)].section != ph.section;
    const bool build = (ph.index % 2 == 0) || firstOfSection;
    if (build) return 0.08f + 0.9f * std::pow(sstep(0.0f, 0.82f, u), 1.25f);
    return 0.93f + 0.06f * std::sin(u * 6.28f);
}

inline State test06(float tl, double songT0, const SongAnalysis& song, const Score& sc) {
    State s;
    const double t = songT0 + tl;
    const AudioAtT a = sampleSong(song, t);
    // the phrase we are in
    std::size_t pi = 0;
    while (pi + 1 < sc.phrases.size() && t >= sc.phrases[pi + 1].start) ++pi;
    const Phrase& ph = sc.phrases.empty() ? Phrase{0, 1e9, 0, 0, false} : sc.phrases[pi];
    const float u = static_cast<float>((t - ph.start) / std::max(ph.end - ph.start, 1e-3));
    const int sec = std::max(ph.section, 0);
    const int arch = sc.sectionArch.empty() ? kSeraph : sc.sectionArch[static_cast<std::size_t>(sec) % sc.sectionArch.size()];
    s.archA = s.archB = static_cast<float>(arch);

    // coherence: the phrase cycle, nudged by the guitars' density (mid), and a violent drop when the next
    // phrase opens on a kick: the last 60 ms of the previous phrase are its final, highest note.
    float C = phraseCoherence(sc, ph, t);
    const bool collapseOpen = ph.kickOpens && pi > 0;
    if (collapseOpen) {
        const Phrase& prev = sc.phrases[pi - 1];
        const float cPrevEnd = phraseCoherence(sc, prev, prev.end - 1e-3);
        const float since = static_cast<float>(t - ph.start);
        // the collapse: from the previous phrase's held form to near chaos in 90 ms, then the new build
        if (since < 0.09f) C = cPrevEnd + (0.12f - cPrevEnd) * sstep(0.0f, 0.09f, since);
        s.strobe = 0.7f * pulse(since, 0.0f, 0.07f);
    }
    C += 0.10f * (a.env0.z - 0.5f);
    s.C = std::clamp(C, 0.0f, 1.0f);
    defaults(s);
    // snares flash the face while it is forming: the "that's a face" moment lands on the backbeat
    const float sinceSnare = static_cast<float>(t - a.lastSnare);
    s.flash = (s.C > 0.1f && s.C < 0.85f) ? 0.38f * a.lastSnareS * std::exp(-sinceSnare / 0.13f) : 0.0f;
    // structure in the low end: kicks give weight and breath, not size
    s.mass = 1.0f + 0.3f * a.kickEnv;
    s.breath = 2.0f * a.env0.y - 1.0f + 0.6f * a.kickEnv;
    s.flow = 0.4f + 1.8f * a.env0.z;
    s.shimmer = std::clamp(0.2f + 0.9f * a.env0.w + 0.4f * a.hatEnv, 0.0f, 1.2f);
    s.flicker = 0.25f * a.hatEnv;
    const float secEnergy = song.sections.empty() ? 0.5f : song.sections[static_cast<std::size_t>(sec) % song.sections.size()].energy;
    s.temper = std::clamp(0.15f + 0.9f * (a.centroid - 0.35f) + 0.25f * secEnergy + 0.25f * s.C, 0.05f, 0.95f);
    s.blast = 14.0f + 10.0f * a.lastKickS;
    s.heatInject = 1.0f + 0.8f * secEnergy;
    // odd phrases hold and FOLD
    const bool hold = phraseCoherence(sc, ph, t) > 0.9f;
    if (hold) {
        const float f = std::sin(u * glm::pi<float>());
        s.fold0.x = 0.9f * f * (0.6f + a.env0.w);
        s.fold0.y = 0.8f * sstep(0.45f, 0.8f, u) * (1.0f - sstep(0.9f, 1.0f, u));
        s.fold0.z = 0.8f * a.env0.y * f;
        s.fold1.x = 0.7f * f;
        s.fold0.w = 0.6f * sstep(0.8f, 0.98f, u) * (ph.index % 4 == 3 ? 1.0f : 0.0f);
    }
    if (arch == kChoir) { s.fold1.z = sstep(0.3f, 0.7f, u); s.fold1.w = hold ? sstep(0.0f, 0.5f, u) : 0.0f; }
    if (arch == kAbyss) s.fall = 1.5f;
    // light: the rig turns with the phrase, a snare sweeps a band across the entity
    s.rigPhase = static_cast<float>(t) * 0.3f + static_cast<float>(ph.index) * 0.7f;
    s.sweep = std::clamp(-1.0f + 2.0f * sinceSnare / 0.35f, -1.0f, 1.0f);
    s.sweepStrength = std::exp(-sinceSnare / 0.3f) * a.lastSnareS;
    s.warmth = arch == kChoir ? 0.65f : (arch == kAbyss ? 0.45f : 0.25f);

    // camera behaviour from musical state, smoothed by a pure window over t (no history)
    auto camAt = [&](double tc, glm::vec3& eye, glm::vec3& tgt, std::string& lab) {
        std::size_t k = 0;
        while (k + 1 < sc.phrases.size() && tc >= sc.phrases[k + 1].start) ++k;
        const Phrase& p = sc.phrases.empty() ? Phrase{0, 1e9, 0, 0, false} : sc.phrases[k];
        const float v = static_cast<float>((tc - p.start) / std::max(p.end - p.start, 1e-3));
        const bool first = p.index == 0 || sc.phrases[static_cast<std::size_t>(p.index - 1)].section != p.section;
        const bool build = phraseCoherence(sc, p, tc) < 0.9f;
        const float az = -30.0f + 60.0f * static_cast<float>(std::fmod(p.index * 0.618, 1.0)) + 12.0f * v;
        float d, el;
        if (first) { d = 13.0f + 30.0f * sstep(0.0f, 0.6f, v); el = 10.0f; lab = "REVEAL"; }
        else if (build) { d = 30.0f - 13.0f * v; el = 6.0f; lab = "OBSERVER"; }
        else { d = 15.0f - 4.0f * v; el = 22.0f - 18.0f * v; lab = "DESCENT"; }
        if (p.kickOpens && (tc - p.start) < 1.4) { d -= 5.0f * (1.0f - static_cast<float>(tc - p.start) / 1.4f); lab = "COLLISION"; }
        eye = orbit(d, az, el, {0.0f, 0.2f, 0.0f});
        tgt = {0.0f, -0.1f, 0.0f};
    };
    glm::vec3 eyeAcc{0.0f}, tgtAcc{0.0f};
    float wsum = 0.0f;
    for (int k = 0; k < 16; ++k) {
        const double tc = t - 0.9 * k / 15.0;
        glm::vec3 e, g;
        std::string lab;
        camAt(tc, e, g, lab);
        const float w = 1.0f - k / 16.0f;
        eyeAcc += e * w; tgtAcc += g * w; wsum += w;
        if (k == 0) s.label = lab;
    }
    s.eye = eyeAcc / wsum;
    s.target = tgtAcc / wsum;
    // a collapse shakes the lens
    if (collapseOpen) {
        const float since = static_cast<float>(t - ph.start);
        const float sh = 0.25f * std::exp(-since / 0.25f);
        s.eye += sh * glm::vec3(std::sin(since * 71.0f), std::sin(since * 53.0f + 1.0f), 0.0f);
    }
    return s;
}

} // namespace astral
