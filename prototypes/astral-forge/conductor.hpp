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
    float warpAdvect = 1.0f;   // 1: bound matter is carried through the warp (iteration 2); 0: re-attracted only (iteration 1)
    float sharpSpread = 0.6f;  // 1: only the anatomy's centre becomes precise; 0: everywhere (material studies)
    float bloom = 0.035f;
    float metaRadius = 22.0f;  // how far the unbound dust field extends
    float metaFace = 0.0f;     // the dust condenses into a giant ghost mask (0..1)
    float tendonWeight = 0.22f; // density weight of tendon matter: low, so tendons read as streams of flakes, thickening only where dense
    float tendonFlow = 1.0f;   // speed of matter streaming along the tendons (scaled by the mid band in TEST 06)
    float appendWeight = 1.0f; // density weight of appendage matter (below ~0.4: strands of dust, not tubes)
    glm::vec3 eye{0.0f, 0.0f, 25.0f}, target{0.0f};
    float fovDeg = 30.0f;
    float exposure = 0.85f, haze = 0.12f;
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
    s.appendWeight = 0.15f;
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
    s.fold0.w = 0.6f * sstep(7.0f, 9.0f, t) * restore;            // spiral implosion: folds inward (injective)
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
    s.appendWeight = 0.15f;
    s.metaRadius = 90.0f;
    s.metaFace = 1.0f;
    s.sharpSpread = 0.0f;
    s.C = 1.0f;
    defaults(s);
    s.temper = 0.35f;
    s.flow = 0.9f;
    s.shimmer = 0.35f;
    s.rigPhase = t * 0.3f;
    // the mouth opens into a portal while the camera is far away, so the dive can pass through it
    s.fold1.y = 1.1f * sstep(6.0f, 10.0f, t);
    const glm::vec3 eyeRing{-0.76f + 0.22f, 0.72f + 0.08f, 0.6f + 0.3f};
    const glm::vec3 inner{0.0f, -1.45f, 0.62f - 0.35f};
    float logd;
    glm::vec3 focus;
    if (t < 7.0f) {
        const float u = sstep(0.0f, 7.0f, t);
        logd = std::log(0.22f) + (std::log(95.0f) - std::log(0.22f)) * u;
        focus = glm::mix(eyeRing, glm::vec3(0.76f * 8.0f, -0.72f * 8.0f, 0.0f) * 0.85f, sstep(1.5f, 7.0f, t));
    } else if (t < 8.5f) {
        logd = std::log(95.0f);
        focus = glm::vec3(0.76f * 8.0f, -0.72f * 8.0f, 0.0f) * 0.85f;
    } else {
        const float u = sstep(8.5f, 16.0f, t);
        logd = std::log(95.0f) + (std::log(0.55f) - std::log(95.0f)) * u;
        focus = glm::mix(glm::vec3(0.76f * 8.0f, -0.72f * 8.0f, 0.0f) * 0.85f, inner, sstep(8.5f, 13.5f, t));
    }
    const float d = std::exp(logd);
    // head-on for the dive (the slit must be in front of the lens), drifting while far
    const float az = t < 8.5f ? -20.0f + 30.0f * std::sin(t * 0.25f) : glm::mix(-20.0f + 30.0f * std::sin(8.5f * 0.25f), 0.0f, sstep(8.5f, 12.0f, t));
    const float el = t < 8.5f ? 6.0f : glm::mix(6.0f, 0.0f, sstep(8.5f, 12.0f, t));
    s.eye = orbit(d, az, el, focus);
    s.target = focus;
    s.fovDeg = 30.0f;
    s.label = t < 3.0f ? "MICRO" : (t < 8.5f ? "REVEAL" : (t < 13.0f ? "INTERNAL" : "MICRO"));
    return s;
}

// ---- TEST 07 (iteration 2): THE TWO GODS ---------------------------------------------------------------
// The Machine God assembles under an orbiting camera (precise, faceted, inside its gyro orbits), collapses, and the
// Chimera forms from its dust while the camera keeps circling: three faces, no front.
inline State test07(float t) {
    State s;
    const bool chimera = t >= 8.5f;
    s.archA = s.archB = chimera ? kChimera : kMachine;
    const Curve C{{{0.0f, 0.25f}, {4.0f, 0.97f}, {8.2f, 1.0f}, {8.3f, 0.12f}, {9.0f, 0.18f}, {13.5f, 0.98f}, {17.0f, 1.0f}}};
    s.C = C(t);
    defaults(s);
    s.temper = chimera ? 0.35f + 0.4f * sstep(9.0f, 15.0f, t) : 0.15f + 0.15f * sstep(0.0f, 8.0f, t);
    s.warmth = chimera ? 0.4f : 0.05f;
    s.gratingUm = chimera ? 1.6f : 1.2f;
    s.flow = 1.0f;
    s.shimmer = 0.45f;
    s.blast = 18.0f;
    s.strobe = 0.6f * pulse(t, 8.25f, 0.045f);
    s.rigPhase = t * 0.4f;
    s.sharpSpread = 0.3f;
    // one continuous orbit: 400 degrees over the test, a slow dolly in, a low-to-level pass
    const float az = -40.0f + 400.0f * sstep(0.0f, 17.0f, t);
    // the Chimera is seen closer: its three faces must be read as the orbit discovers them
    const float d = glm::mix(17.0f - 3.0f * std::sin(t * 0.37f), 11.0f, sstep(8.5f, 11.0f, t));
    const float el = 12.0f * std::sin(t * 0.25f);
    s.eye = orbit(d, az, el, {0.0f, 0.2f, 0.0f});
    s.target = {0.0f, 0.1f, 0.0f};
    s.label = t < 8.2f ? "ORBIT" : (t < 9.5f ? "COLLISION" : "ORBIT");
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
    std::vector<bool> sectionWithhold; // a break: chaos is held, only the eyes bind, the god nearly arrives at its end
    double beatSeconds = 0.6;
};

// Iteration 2. Each kind of section (function label + repetition group, in order of first appearance) owns a
// PAIR of gods that alternate by occurrence: the same music returns with the same god's other face. On Trench
// the second verse brings the Machine God and the second chorus the Chimera.
inline Score buildScore(const SongAnalysis& song) {
    Score sc;
    const std::vector<std::pair<int, int>> pairs{{kSeraph, kMachine}, {kAbyss, kAbyss}, {kChoir, kChimera}, {kMask, kMachine}, {kChimera, kSeraph}};
    std::vector<std::string> keys;
    std::vector<int> occurrences;
    for (std::size_t i = 0; i < song.sections.size(); ++i) {
        const auto& se = song.sections[i];
        const std::string key = se.label + "/" + std::to_string(se.group);
        auto it = std::find(keys.begin(), keys.end(), key);
        if (it == keys.end()) { keys.push_back(key); occurrences.push_back(0); it = keys.end() - 1; }
        const std::size_t k = static_cast<std::size_t>(it - keys.begin());
        const auto& pr = pairs[k % pairs.size()];
        sc.sectionArch.push_back(occurrences[k]++ % 2 == 0 ? pr.first : pr.second);
        static const char* kQuiet[] = {"other", "break", "breakdown", "intro", "outro", "bridge"};
        bool w = false;
        for (const char* q : kQuiet) w = w || se.label == q;
        sc.sectionWithhold.push_back(w || (se.density < 0.3f && se.energy < 0.6f));
    }
    if (song.tempoBpm > 1.0f) sc.beatSeconds = 60.0 / song.tempoBpm;
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

inline bool withholdAt(const Score& sc, const Phrase& ph) {
    return ph.section >= 0 && static_cast<std::size_t>(ph.section) < sc.sectionWithhold.size() && sc.sectionWithhold[static_cast<std::size_t>(ph.section)];
}

inline float phraseCoherence(const Score& sc, const Phrase& ph, double t, const SongAnalysis* song = nullptr) {
    const float u = static_cast<float>((t - ph.start) / std::max(ph.end - ph.start, 1e-3));
    if (withholdAt(sc, ph)) {
        // WITHHOLD: chaos held, the eyes bind and drift (C ~ 0.2, where only eye matter binds); in the last four
        // beats of the section the god surges to nearly complete, so the next downbeat has something to destroy
        float C = 0.2f + 0.06f * std::sin(u * 12.566f);
        if (song) {
            const double secEnd = song->sections[static_cast<std::size_t>(ph.section)].end;
            C = glm::mix(C, 0.96f, sstep(static_cast<float>(secEnd - 4.0 * sc.beatSeconds), static_cast<float>(secEnd - 0.15), static_cast<float>(t)));
        }
        return C;
    }
    const bool firstOfSection = ph.index == 0 || sc.phrases[static_cast<std::size_t>(ph.index - 1)].section != ph.section;
    // a kick that destroys the form always starts a rebuild (otherwise the collapse is a 90 ms flinch)
    const bool build = (ph.index % 2 == 0) || firstOfSection || ph.kickOpens;
    // a phrase opened by a strong kick builds from near chaos (the previous form was destroyed); one that
    // opens softly only half-dissolves and rebuilds from there
    const float base = ph.kickOpens ? 0.08f : 0.45f;
    if (build) return base + (0.98f - base) * std::pow(sstep(0.0f, 0.82f, u), 1.25f);
    return 0.93f + 0.06f * std::sin(u * 6.28f);
}

inline State test06(float tl, double songT0, const SongAnalysis& song, const Score& sc) {
    State s;
    const double t = songT0 + tl;
    const AudioAtT a = sampleSong(song, t);
    std::size_t pi = 0;
    while (pi + 1 < sc.phrases.size() && t >= sc.phrases[pi + 1].start) ++pi;
    const Phrase& ph = sc.phrases.empty() ? Phrase{0, 1e9, 0, 0, false} : sc.phrases[pi];
    const float u = static_cast<float>((t - ph.start) / std::max(ph.end - ph.start, 1e-3));
    const int sec = std::max(ph.section, 0);
    const int arch = sc.sectionArch.empty() ? kSeraph : sc.sectionArch[static_cast<std::size_t>(sec) % sc.sectionArch.size()];
    const bool withhold = withholdAt(sc, ph);
    s.archA = s.archB = static_cast<float>(arch);

    float C = phraseCoherence(sc, ph, t, &song);
    const bool collapseOpen = ph.kickOpens && pi > 0;
    if (collapseOpen) {
        const Phrase& prev = sc.phrases[pi - 1];
        const float cPrevEnd = phraseCoherence(sc, prev, prev.end - 1e-3, &song);
        const float since = static_cast<float>(t - ph.start);
        if (since < 0.09f) C = cPrevEnd + (0.08f - cPrevEnd) * sstep(0.0f, 0.09f, since);
        // iteration 2: the strobe decays within ~0.15 s (the Critic read 0.4 s holds as overexposure)
        s.strobe = 0.6f * pulse(since, 0.0f, 0.045f);
    }
    if (!withhold) C += 0.10f * (a.env0.z - 0.5f);
    s.C = std::clamp(C, 0.0f, 1.0f);
    defaults(s);
    const float sinceSnare = static_cast<float>(t - a.lastSnare);
    // snares flash the face while it is forming; in a withhold the flash is the ONLY time the face appears
    const float flashGain = withhold ? 0.6f : 0.38f;
    s.flash = (s.C > 0.1f && s.C < 0.85f) ? flashGain * a.lastSnareS * std::exp(-sinceSnare / 0.13f) : 0.0f;
    s.mass = 1.0f + 0.3f * a.kickEnv;
    s.breath = 2.0f * a.env0.y - 1.0f + 0.6f * a.kickEnv;
    s.flow = 0.4f + 1.8f * a.env0.z;
    s.tendonFlow = 0.6f + 1.4f * a.env0.z;
    s.shimmer = std::clamp(0.2f + 0.9f * a.env0.w + 0.4f * a.hatEnv, 0.0f, 1.2f);
    s.flicker = 0.25f * a.hatEnv;
    const float secEnergy = song.sections.empty() ? 0.5f : song.sections[static_cast<std::size_t>(sec) % song.sections.size()].energy;
    s.temper = std::clamp(0.15f + 0.9f * (a.centroid - 0.35f) + 0.25f * secEnergy + 0.25f * s.C, 0.05f, 0.95f);
    s.blast = 14.0f + 10.0f * a.lastKickS;
    s.heatInject = 1.0f + 0.8f * secEnergy;
    const bool hold = !withhold && phraseCoherence(sc, ph, t, &song) > 0.9f;
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
    s.rigPhase = static_cast<float>(t) * 0.3f + static_cast<float>(ph.index) * 0.7f;
    s.sweep = std::clamp(-1.0f + 2.0f * sinceSnare / 0.35f, -1.0f, 1.0f);
    s.sweepStrength = std::exp(-sinceSnare / 0.3f) * a.lastSnareS;
    s.warmth = arch == kChoir ? 0.65f : (arch == kAbyss ? 0.45f : (arch == kMachine ? 0.1f : 0.25f));
    if (arch == kMachine) s.gratingUm = 1.2f; // the most regular grooves: the most holographic god

    // CAMERA VOCABULARY (iteration 2). A behaviour per phrase from its family, never the same twice in a row,
    // and the side alternates. Build phrases: OBSERVER, PROFILE, LOW. Held phrases: DESCENT, ORBIT, MICRO.
    // Withheld sections: HOVER on the eyes. A new section REVEALs; a kick-opened phrase begins in COLLISION.
    auto camAt = [&](double tc, glm::vec3& eye, glm::vec3& tgt, std::string& lab) {
        std::size_t k = 0;
        while (k + 1 < sc.phrases.size() && tc >= sc.phrases[k + 1].start) ++k;
        const Phrase& p = sc.phrases.empty() ? Phrase{0, 1e9, 0, 0, false} : sc.phrases[k];
        const float v = static_cast<float>((tc - p.start) / std::max(p.end - p.start, 1e-3));
        const bool first = p.index == 0 || sc.phrases[static_cast<std::size_t>(p.index - 1)].section != p.section;
        const bool wh = withholdAt(sc, p);
        // the behaviour belongs to the PHRASE (its type), so it never switches mid-phrase
        const bool build = p.index == 0 || first || p.kickOpens || p.index % 2 == 0;
        const float side = (p.index % 2 == 0) ? 1.0f : -1.0f;
        const int choice = (p.index * 7 + std::max(p.section, 0) * 5) % 3;
        glm::vec3 focus{0.0f, 0.2f, 0.0f};
        float d = 20.0f, el = 6.0f, az = 0.0f;
        tgt = {0.0f, -0.1f, 0.0f};
        if (wh) {
            const glm::vec3 eyes{0.0f, 0.72f, 0.6f};
            d = 8.5f - 2.0f * v; el = 3.0f; az = side * (12.0f - 10.0f * v); focus = eyes; tgt = eyes; lab = "HOVER";
        } else if (build) {
            if (choice == 0) { d = 30.0f - 13.0f * v; el = 6.0f; az = side * (20.0f - 8.0f * v); lab = "OBSERVER"; }
            else if (choice == 1) { d = 19.0f - 5.0f * v; el = 0.0f; az = side * (78.0f - 18.0f * v); lab = "PROFILE"; }
            else { d = 22.0f - 7.0f * v; el = -28.0f + 20.0f * v; az = side * (25.0f - 10.0f * v); lab = "LOW"; }
        } else {
            if (choice == 0) { d = 15.0f - 4.0f * v; el = 32.0f - 26.0f * v; az = side * 18.0f; lab = "DESCENT"; }
            else if (choice == 1) { d = 13.0f; el = 10.0f; az = side * (-55.0f + 110.0f * v); lab = "ORBIT"; }
            else {
                const glm::vec3 eye{side * 0.76f, 0.72f, 0.6f};
                d = 7.5f - 1.8f * v; el = 4.0f; az = side * 14.0f; focus = eye; tgt = eye; lab = "MICRO";
            }
        }
        const float since = static_cast<float>(tc - p.start);
        if (first && since < 4.5f && !wh) { d += 24.0f * std::sin(glm::pi<float>() * since / 4.5f); el += 6.0f; lab = "REVEAL"; }
        if (p.kickOpens && since < 1.4f) { d -= 5.0f * (1.0f - since / 1.4f); lab = "COLLISION"; }
        eye = orbit(d, az, el, focus);
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
    if (collapseOpen) {
        const float since = static_cast<float>(t - ph.start);
        const float sh = 0.25f * std::exp(-since / 0.25f);
        s.eye += sh * glm::vec3(std::sin(since * 71.0f), std::sin(since * 53.0f + 1.0f), 0.0f);
    }
    return s;
}

} // namespace astral
