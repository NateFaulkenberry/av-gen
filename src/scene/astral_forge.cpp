#include "scene/astral_forge.hpp"

#include "spatial/audio_history.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

Result<void> refuseUnknown(const json& j, std::initializer_list<std::string_view> known, std::string_view where) {
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find(known.begin(), known.end(), std::string_view(it.key())) == known.end()) {
            return fail("{}: unknown key '{}'", where, it.key());
        }
    }
    return {};
}

const char* modeName(AstralConductorMode m) {
    switch (m) {
    case AstralConductorMode::Song: return "song";
    case AstralConductorMode::Live: return "live";
    default: return "auto";
    }
}

} // namespace

// ---- JSON ---------------------------------------------------------------------------------------------------------

Result<AstralForge> astralFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'astral' must be an object");
    }
    if (auto ok = refuseUnknown(j, {"enabled", "particles", "grid", "gridSize", "conductor", "camera", "preroll", "controls"},
                                "astral");
        !ok) {
        return std::unexpected(ok.error());
    }
    AstralForge a;
    a.enabled = j.value("enabled", true);
    const auto particles = j.value("particles", static_cast<std::int64_t>(a.particles));
    if (particles < 65536 || particles > (8 << 20)) {
        return fail("astral.particles must be in 65536..8388608 (got {})", particles);
    }
    a.particles = static_cast<std::uint32_t>(particles / 256 * 256);
    a.gridRes = j.value("grid", a.gridRes);
    if (a.gridRes < 64 || a.gridRes > 320 || a.gridRes % 8 != 0) {
        return fail("astral.grid must be a multiple of 8 in 64..320 (got {})", a.gridRes);
    }
    a.gridSize = j.value("gridSize", a.gridSize);
    if (!(a.gridSize > 4.0f && a.gridSize < 200.0f)) {
        return fail("astral.gridSize must be in (4, 200)");
    }
    const std::string mode = j.value("conductor", std::string("auto"));
    if (mode == "auto") a.conductor = AstralConductorMode::Auto;
    else if (mode == "song") a.conductor = AstralConductorMode::Song;
    else if (mode == "live") a.conductor = AstralConductorMode::Live;
    else return fail("astral.conductor must be auto, song or live (got '{}')", mode);
    a.driveCamera = j.value("camera", a.driveCamera);
    a.preroll = std::clamp(j.value("preroll", a.preroll), 0.0f, 30.0f);
    if (j.contains("controls")) {
        const json& c = j.at("controls");
        if (!c.is_object()) {
            return fail("astral.controls must be an object");
        }
        if (auto ok = refuseUnknown(c, {"summon", "hold", "intensity", "palette", "light", "atmosphere", "godRays",
                                        "legibility", "zoom", "exposure", "god", "intro", "ending"},
                                    "astral.controls");
            !ok) {
            return std::unexpected(ok.error());
        }
        astral::Controls& k = a.controls;
        k.summon = c.value("summon", k.summon);
        k.hold = c.value("hold", k.hold);
        k.intensity = c.value("intensity", k.intensity);
        k.palette = c.value("palette", k.palette);
        k.light = c.value("light", k.light);
        k.atmosphere = c.value("atmosphere", k.atmosphere);
        k.godRays = c.value("godRays", k.godRays);
        k.legibility = c.value("legibility", k.legibility);
        k.zoom = c.value("zoom", k.zoom);
        k.exposure = c.value("exposure", k.exposure);
        k.god = c.value("god", k.god);
        k.intro = c.value("intro", k.intro);
        k.ending = c.value("ending", k.ending);
        if (k.god < -1 || k.god > 6) {
            return fail("astral.controls.god must be -1 (the score) or 0..6");
        }
    }
    a.live = a.controls;
    a.cameraDrive = a.driveCamera ? 1.0f : 0.0f;
    return a;
}

json astralToJson(const AstralForge& a) {
    const astral::Controls& k = a.controls;
    return json{{"enabled", a.enabled},
                {"particles", a.particles},
                {"grid", a.gridRes},
                {"gridSize", a.gridSize},
                {"conductor", modeName(a.conductor)},
                {"camera", a.driveCamera},
                {"preroll", a.preroll},
                {"controls",
                 {{"summon", k.summon}, {"hold", k.hold}, {"intensity", k.intensity}, {"palette", k.palette},
                  {"light", k.light}, {"atmosphere", k.atmosphere}, {"godRays", k.godRays},
                  {"legibility", k.legibility}, {"zoom", k.zoom}, {"exposure", k.exposure}, {"god", k.god},
                  {"intro", k.intro}, {"ending", k.ending}}}};
}

// ---- parameters -------------------------------------------------------------------------------------------------

namespace {

struct Registrar {
    params::ParameterSet& params;
    AstralParameters& out;

    void f(const char* rel, const char* label, float def, float lo, float hi) {
        params::ParamDesc<float> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = "astral";
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = lo;
        d.softMax = hi;
        out.all.push_back(&params.add(std::move(d)));
    }
    void i(const char* rel, const char* label, int def, int lo, int hi) {
        params::ParamDesc<int> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = "astral";
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = lo;
        d.softMax = hi;
        out.all.push_back(&params.add(std::move(d)));
    }
};

params::IParameter* find(const AstralParameters& p, std::string_view leaf) {
    const std::size_t n = p.prefix.size();
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == n + leaf.size() && path.compare(n, leaf.size(), leaf) == 0) {
            return ip;
        }
    }
    return nullptr;
}
float fval(const AstralParameters& p, std::string_view leaf, float fallback) {
    if (auto* fp = dynamic_cast<params::Parameter<float>*>(find(p, leaf))) {
        return fp->value();
    }
    return fallback;
}
int ival(const AstralParameters& p, std::string_view leaf, int fallback) {
    if (auto* ip = dynamic_cast<params::Parameter<int>*>(find(p, leaf))) {
        return ip->value();
    }
    return fallback;
}

} // namespace

AstralParameters registerAstralParameters(params::ParameterSet& params, const AstralForge& rest,
                                          const std::string& prefix) {
    AstralParameters p;
    p.prefix = prefix;
    Registrar r{params, p};
    const astral::Controls& k = rest.controls;
    r.f("summon", "summon (coherence floor)", k.summon, 0.0f, 1.0f);
    r.f("hold", "hold the formed face", k.hold, 0.0f, 1.0f);
    r.f("collapse", "collapse (trigger: fires on rising past 0.5)", 0.0f, 0.0f, 1.0f);
    r.i("god", "god (-1 the score; 0 mask, 1 seraph, 2 abyss, 3 chimera, 4 machine, 5 choir, 6 horns)", k.god, -1, 6);
    r.f("intensity", "intensity (matter energy)", k.intensity, 0.0f, 2.0f);
    r.f("palette", "per-god palette", k.palette, 0.0f, 1.0f);
    r.f("light", "raking key, rim and eye glow", k.light, 0.0f, 1.0f);
    r.f("atmosphere", "void glow", k.atmosphere, 0.0f, 1.0f);
    r.f("godRays", "god rays", k.godRays, 0.0f, 1.0f);
    r.f("legibility", "face legibility at a formed peak", k.legibility, 0.0f, 1.0f);
    r.f("zoom", "camera zoom (distance divisor)", k.zoom, 0.25f, 4.0f);
    r.f("exposure", "exposure (multiplier)", k.exposure, 0.0f, 4.0f);
    r.f("camera", "conductor drives the camera (0 = the scene's camera)", rest.driveCamera ? 1.0f : 0.0f, 0.0f, 1.0f);
    r.f("density", "particles simulated (fraction)", 1.0f, 0.05f, 1.0f);
    return p;
}

float applyAstralParameters(const AstralParameters& p, const AstralForge& rest, AstralForge& live) {
    astral::Controls k = rest.controls;
    k.summon = fval(p, "summon", k.summon);
    k.hold = fval(p, "hold", k.hold);
    k.god = ival(p, "god", k.god);
    k.intensity = fval(p, "intensity", k.intensity);
    k.palette = fval(p, "palette", k.palette);
    k.light = fval(p, "light", k.light);
    k.atmosphere = fval(p, "atmosphere", k.atmosphere);
    k.godRays = fval(p, "godRays", k.godRays);
    k.legibility = fval(p, "legibility", k.legibility);
    k.zoom = fval(p, "zoom", k.zoom);
    k.exposure = fval(p, "exposure", k.exposure);
    live.live = k;
    live.cameraDrive = fval(p, "camera", rest.driveCamera ? 1.0f : 0.0f);
    live.density = fval(p, "density", 1.0f);
    return fval(p, "collapse", 0.0f);
}

// ---- the live conductor ----------------------------------------------------------------------------------------

void AstralLiveConductor::reset() { *this = AstralLiveConductor{}; }

void AstralLiveConductor::ingest(const spatial::AudioHistory& audio, double now) {
    using astral::SongAnalysis;
    constexpr int B = SongAnalysis::kBins;
    static_assert(B == spatial::kAudioBins, "the history's spectrogram is the conductor's");
    const std::int64_t newest = audio.newestRow(now);
    if (newest < 0) {
        return;
    }
    const double rate = audio.rowRate();
    if (nextRow_ < 0 || nextRow_ > newest + 1 || newest - nextRow_ > static_cast<std::int64_t>(rate * 4.0)) {
        nextRow_ = std::max<std::int64_t>(0, newest - static_cast<std::int64_t>(rate)); // (re)start one second back
    }
    if (song_.hops == 0) {
        song_.hopRate = static_cast<float>(rate);
        song_.t0 = static_cast<float>(now - static_cast<double>(newest - nextRow_) / rate);
    }
    // The prototype's envelopes (buildSong), one row at a time: band attack 12 ms / release 220 ms, a slow centroid.
    const float att = 1.0f - std::exp(-1.0f / (0.012f * song_.hopRate));
    const float rel = 1.0f - std::exp(-1.0f / (0.22f * song_.hopRate));
    const float cen = 1.0f - std::exp(-1.0f / (1.5f * song_.hopRate));
    auto binOf = [&](float hz) { return std::clamp(static_cast<int>(std::log(hz / 32.0f) / std::log(16000.0f / 32.0f) * B), 0, B); };
    const int cuts[6] = {0, binOf(150), binOf(400), binOf(2000), binOf(6000), B};
    for (; nextRow_ <= newest; ++nextRow_) {
        float row[B];
        double wsum = 0.0, csum = 0.0, energy = 0.0;
        for (int k = 0; k < B; ++k) {
            row[k] = audio.value(nextRow_, k);
            wsum += row[k];
            csum += row[k] * (k + 0.5) / B;
            energy += row[k] * row[k];
        }
        centroid_ += (static_cast<float>(wsum > 1e-6 ? csum / wsum : 0.5) - centroid_) * cen;
        for (int b = 0; b < 5; ++b) {
            float m = 0.0f;
            for (int k = cuts[b]; k < cuts[b + 1]; ++k) m += row[k];
            m /= static_cast<float>(std::max(1, cuts[b + 1] - cuts[b]));
            env_[b] += (m - env_[b]) * (m > env_[b] ? att : rel);
        }
        const float rms = static_cast<float>(std::sqrt(energy / B));
        rmsPeak_ = std::max(rmsPeak_ * 0.99995f, rms);
        song_.env0.push_back(glm::vec4(env_[0], env_[1], env_[2], env_[3]));
        song_.env1.push_back(glm::vec4(env_[4], rms, 0.0f, rms / rmsPeak_));
        song_.centroid.push_back(centroid_);
        ++song_.hops;
    }
    // Onsets: kicks (low), snares (mid), hats (high) and beats, appended as the live analysis reports them.
    auto take = [&](spatial::OnsetSource src, std::vector<float>* T, std::vector<float>* S, std::vector<double>* beats) {
        const auto& list = audio.onsets(src);
        const double last = T != nullptr ? (T->empty() ? -1e9 : static_cast<double>(T->back()))
                                         : (beats->empty() ? -1e9 : beats->back());
        for (const spatial::AudioOnset& o : list) {
            if (o.time <= last + 1e-6 || o.time > now + 1e-6) continue;
            if (T != nullptr) {
                T->push_back(static_cast<float>(o.time));
                S->push_back(std::max(0.25f, o.strength));
            } else {
                beats->push_back(o.time);
            }
        }
    };
    take(spatial::OnsetSource::Low, &song_.kickT, &song_.kickS, nullptr);
    take(spatial::OnsetSource::Mid, &song_.snareT, &song_.snareS, nullptr);
    take(spatial::OnsetSource::High, &song_.hatT, &song_.hatS, nullptr);
    take(spatial::OnsetSource::Beat, nullptr, nullptr, &song_.beats);
}

void AstralLiveConductor::advancePhrases(double now, bool collapse) {
    using astral::Phrase;
    const auto& beats = song_.beats;
    // the beat period: the median of the last eight intervals (0.5 s until there are some)
    double beatSec = 0.5;
    if (beats.size() >= 3) {
        std::vector<double> d;
        for (std::size_t i = beats.size() - std::min<std::size_t>(beats.size() - 1, 8); i < beats.size(); ++i) {
            d.push_back(beats[i] - beats[i - 1]);
        }
        std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
        beatSec = std::clamp(d[d.size() / 2], 0.25, 1.5);
    }
    score_.beatSeconds = beatSec;
    song_.tempoBpm = static_cast<float>(60.0 / beatSec);
    auto kickNear = [&](double t) {
        for (std::size_t k = song_.kickT.size(); k-- > 0;) {
            if (song_.kickT[k] < t - 0.07) break;
            if (std::abs(song_.kickT[k] - t) < 0.07 && song_.kickS[k] > 0.5f) return true;
        }
        return false;
    };
    // Virtual sections of eight phrases: each chooses the next god of the cycle (a performer's `astral/god` wins).
    constexpr int kGods[] = {astral::kSeraph, astral::kMachine, astral::kChoir, astral::kChimera, astral::kAbyss};
    auto open = [&](double start, std::size_t beat, bool kick) {
        const int index = static_cast<int>(score_.phrases.size());
        if (index % 8 == 0) {
            if (!song_.sections.empty()) song_.sections.back().end = start;
            astral::Section se;
            se.start = start;
            se.end = 1e9;
            se.group = static_cast<int>(song_.sections.size());
            se.energy = 0.6f;
            se.density = 0.6f;
            se.label = "live";
            song_.sections.push_back(se);
            score_.sectionArch.push_back(kGods[godCycle_++ % 5]);
            score_.sectionWithhold.push_back(false);
        }
        score_.phrases.push_back(Phrase{start, start + 16.0 * beatSec, index, static_cast<int>(song_.sections.size()) - 1, kick});
        phraseStartBeat_ = beat;
    };
    if (score_.phrases.empty()) {
        if (beats.empty() && !collapse) return;
        open(beats.empty() ? now : beats.front(), 0, collapse);
        if (collapse) return;
    }
    if (collapse) {
        score_.phrases.back().end = now;
        open(now, beats.size(), true);
        return;
    }
    // close the open phrase on its sixteenth beat; the next opens there
    while (beats.size() > phraseStartBeat_ + 16) {
        const double at = beats[phraseStartBeat_ + 16];
        score_.phrases.back().end = at;
        open(at, phraseStartBeat_ + 16, kickNear(at));
    }
    Phrase& cur = score_.phrases.back();
    cur.end = std::max(cur.start + 16.0 * beatSec, now + beatSec);
    if (!cur.kickOpens && now - cur.start < 0.2) cur.kickOpens = kickNear(cur.start);
}

astral::State AstralLiveConductor::update(const spatial::AudioHistory& audio, double now, const astral::Controls& controls,
                                          bool collapse) {
    if (now < lastNow_ - 0.5) {
        reset(); // the history's clock went back: a new input, or a restarted file
    }
    lastNow_ = now;
    ingest(audio, now);
    song_.duration = now;
    advancePhrases(now, collapse);
    astral::Controls c = controls;
    c.intro = c.ending = false;
    astral::State s = astral::test06(static_cast<float>(now), 0.0, song_, score_, c.god);
    astral::applyControls(s, c);
    return s;
}

} // namespace avgen::scene
