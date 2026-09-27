#include "directing/reactivity_proposer.hpp"

#include "directing/plan_route.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace avgen::directing {
namespace {

using json = nlohmann::json;
using params::ModOp;

float round3(float v) {
    return std::round(v * 1000.0f) / 1000.0f;
}

// A route's shape for one musical layer: the signal, how it moves a target, and how it breathes.
struct Template {
    std::string layer;
    std::string signal;
    ModOp op = ModOp::Add;
    float amount = 0.0f; // Add: the offset, as a fraction of the target's base
    float inMin = 0.0f;  // Multiply: the remap (the rest maps to 1)
    float inMax = 1.0f;
    float outMin = 1.0f;
    float outMax = 1.0f;
    float attackMs = 0.0f;
    float decayMs = 0.0f;
    bool beat = true;    // answers a beat, so its depth follows the section
    std::string what;    // "a heartbeat on each kick"
};

Template kick() { return {"kick", "audio.onsetLow", ModOp::Add, 0.60f, 0, 1, 1, 1, 5.0f, 300.0f, true, "a heartbeat on each kick"}; }
Template clap() { return {"clap", "audio.onsetMid", ModOp::Add, 0.50f, 0, 1, 1, 1, 5.0f, 180.0f, true, "a short flare on each clap"}; }
Template bar() { return {"bar", "music.downbeat", ModOp::Add, 0.45f, 0, 1, 1, 1, 80.0f, 1200.0f, true, "a slow swell on each bar's downbeat"}; }
Template breath(const std::string& signal) {
    return {"breath", signal, ModOp::Multiply, 1.0f, 0.0f, 1.0f, 0.88f, 1.15f, 0.0f, 0.0f, true, "a breath across every two bars"};
}
Template phrase() { return {"phrase", "music.phrase", ModOp::Add, 0.70f, 0, 1, 1, 1, 250.0f, 3000.0f, true, "a swell at each phrase turn that settles back"}; }
// The lead and the bass: a band's steady level (ADR-897), remapped over the window the song's own
// sections span in that band, so the response uses its whole range whatever the mix.
Template lead(float lo, float hi) {
    return {"lead", "audio.midLevel", ModOp::Multiply, 1.0f, lo, hi, 0.92f, 1.22f, 300.0f, 1200.0f, true, "brightening with the lead"};
}
Template bass(float lo, float hi) {
    return {"bass", "audio.bassLevel", ModOp::Multiply, 1.0f, lo, hi, 0.92f, 1.20f, 200.0f, 900.0f, true, "swelling with the bass"};
}
Template gusts(const std::string& signal) {
    return {"breath", signal, ModOp::Multiply, 1.0f, 0.0f, 1.0f, 0.80f, 1.35f, 0.0f, 0.0f, false, "gusts every two bars"};
}
Template sectionFlare() {
    return {"section", "section.change", ModOp::Add, 0.80f, 0, 1, 1, 1, 40.0f, 1800.0f, true, "relighting at each new section"};
}
Template hats() { return {"hats", "audio.onsetHigh", ModOp::Add, 0.18f, 0, 1, 1, 1, 8.0f, 200.0f, true, "a flicker on the hats"}; }

// The same template as an echo on a Multiply target (a light, spores): the event lifts it from 1.
Template asMultiply(Template t, float lift) {
    if (t.op == ModOp::Add) {
        t.op = ModOp::Multiply;
        t.inMin = 0.0f;
        t.inMax = 1.0f;
        t.outMin = 1.0f;
        t.outMax = 1.0f + lift;
    }
    return t;
}

// A slow arc on the section: the section's energy, remapped over the song's own range.
Template arc(float eLo, float eHi, float outLo, float outHi, float attackMs, float decayMs, std::string what) {
    Template t{"section energy", "section.energy", ModOp::Multiply, 1.0f, eLo, eHi, outLo, outHi, attackMs, decayMs, false,
               std::move(what)};
    return t;
}

bool quietType(std::string_view type) {
    static const std::set<std::string_view> kQuiet{"pause", "break", "breakdown", "suspense", "suspension",
                                                   "intro", "outro", "interlude", "ambient"};
    return kQuiet.contains(type);
}
bool arrivalType(std::string_view type) {
    static const std::set<std::string_view> kArrival{"peak", "chorus", "drop", "climax", "hook"};
    return kArrival.contains(type);
}

std::string shortName(const ReactiveTarget& t) {
    switch (t.group) {
    case ReactiveGroup::ScatterGlow:
    case ReactiveGroup::ScatterHue:
    case ReactiveGroup::ScatterWave: {
        const auto slash = t.owner.rfind('/');
        return slash == std::string::npos ? t.owner : t.owner.substr(slash + 1);
    }
    case ReactiveGroup::Particles: return t.kind == ReactiveKind::Density ? t.owner + "-density" : t.owner;
    case ReactiveGroup::EcologyLight: return "ecology-light";
    case ReactiveGroup::Atmosphere: return t.kind == ReactiveKind::Density ? "fog" : "fog-glow";
    case ReactiveGroup::Wind: return t.path.substr(t.path.rfind('/') + 1);
    case ReactiveGroup::Water:
    case ReactiveGroup::WaterTears: return "water-" + t.path.substr(t.path.rfind('/') + 1);
    default: return t.owner;
    }
}

struct Builder {
    const ReactiveCatalog& catalog;
    const ReactivityOptions& options;
    std::string depthSource;
    float depthMin = 0.0f;
    float depthMax = 1.0f;
    Plan plan;
    std::set<std::string> keys;
    std::vector<std::string> notes;
    std::set<std::string> used; // targets already given a route

    std::string key(const std::string& base) {
        std::string k = base;
        for (int n = 2; keys.contains(k); ++n) {
            k = fmt::format("{}-{}", base, n);
        }
        keys.insert(k);
        return k;
    }

    // Whether the target may be given a route: not already given one, not one a character or a
    // scenario drives, and not one the author routes.
    bool free(const ReactiveTarget& t) {
        if (used.contains(t.path)) {
            return false;
        }
        if (t.scripted) {
            notes.push_back(fmt::format("{} ({}) is left alone: a character or a staging scenario drives it", t.label,
                                        t.path));
            used.insert(t.path);
            return false;
        }
        if (options.skipAuthored && !t.drivenBy.empty()) {
            std::string sources;
            for (const std::string& s : t.drivenBy) {
                sources += (sources.empty() ? "" : ", ") + s;
            }
            notes.push_back(fmt::format("{} ({}) is left alone: the project already routes {} into it", t.label, t.path,
                                        sources));
            used.insert(t.path);
            return false;
        }
        return true;
    }

    void route(const ReactiveTarget& t, const Template& tp, float factor, float delayMs, ReactiveLevel level,
               const std::string& owner, std::string reason) {
        PlanRoute item;
        item.key = key(tp.layer == "section energy" ? "section." + shortName(t) : tp.layer + "." + shortName(t));
        item.level = level;
        item.group = reactiveGroupName(t.group);
        item.owner = owner;
        item.layer = tp.layer;
        item.reason = std::move(reason);
        params::ModRoute& r = item.route;
        r.source = tp.signal;
        r.target = t.path;
        r.component = t.component;
        r.op = tp.op;
        r.enabled = true;
        r.chain.delayMs = std::round(std::clamp(delayMs, 0.0f, params::ProcessorChain::kMaxDelayMs));
        r.chain.attackMs = tp.attackMs;
        r.chain.decayMs = tp.decayMs;
        if (tp.op == ModOp::Add) {
            r.amount = round3(tp.amount * factor * (t.base > 0.0f ? t.base : 1.0f));
        } else {
            r.amount = 1.0f;
            r.chain.remapEnabled = true;
            r.chain.remapInMin = round3(tp.inMin);
            r.chain.remapInMax = round3(tp.inMax);
            r.chain.remapOutMin = round3(1.0f - (1.0f - tp.outMin) * factor);
            r.chain.remapOutMax = round3(1.0f + (tp.outMax - 1.0f) * factor);
            r.chain.clampEnabled = tp.inMin > 0.0f || tp.inMax < 1.0f; // a level outside the remap's window holds its end
            r.chain.clampMin = tp.inMin;
            r.chain.clampMax = tp.inMax;
        }
        if (tp.beat && !depthSource.empty()) {
            r.depthSource = depthSource;
            r.depthMin = depthMin;
            r.depthMax = depthMax;
        }
        used.insert(t.path);
        plan.routes.push_back(std::move(item));
    }
};

double beatSeconds(const MusicalContext& music) {
    if (music.tempoBpm > 0.0) {
        return 60.0 / music.tempoBpm;
    }
    if (music.beatTimes.size() >= 2) {
        std::vector<double> gaps;
        for (std::size_t i = 1; i < music.beatTimes.size(); ++i) {
            gaps.push_back(music.beatTimes[i] - music.beatTimes[i - 1]);
        }
        std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
        return gaps[gaps.size() / 2];
    }
    return 0.0;
}

float weightedMean(const MusicalContext& music, float analysis::SpanProfile::*field) {
    double sum = 0.0;
    double weight = 0.0;
    for (const SectionRun& s : music.sections) {
        if (s.audio.measured()) {
            const double w = std::max(0.0, s.endSeconds - s.startSeconds);
            sum += w * static_cast<double>(s.audio.*field);
            weight += w;
        }
    }
    return weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
}

// The window a band's level spans across the measured sections (min, max), widened to at least 0.1.
std::pair<float, float> bandWindow(const MusicalContext& music, std::size_t band) {
    float lo = 1.0f;
    float hi = 0.0f;
    for (const SectionRun& s : music.sections) {
        if (s.audio.measured() && band < s.audio.bandCount) {
            lo = std::min(lo, s.audio.bandLevels[band]);
            hi = std::max(hi, s.audio.bandLevels[band]);
        }
    }
    if (hi < lo) {
        return {0.3f, 0.7f};
    }
    if (hi - lo < 0.1f) {
        const float mid = 0.5f * (lo + hi);
        lo = mid - 0.05f;
        hi = mid + 0.05f;
    }
    return {round3(lo), round3(hi)};
}

float largest(const MusicalContext& music, float analysis::SpanProfile::*field) {
    float out = 0.0f;
    for (const SectionRun& s : music.sections) {
        if (s.audio.measured()) {
            out = std::max(out, s.audio.*field);
        }
    }
    return out;
}

} // namespace

std::vector<MusicalLayer> musicalLayers(const MusicalContext& music) {
    const bool measured = std::any_of(music.sections.begin(), music.sections.end(),
                                      [](const SectionRun& s) { return s.audio.measured(); });
    const double beat = beatSeconds(music);
    std::set<float> energies;
    for (const SectionRun& s : music.sections) {
        energies.insert(round3(s.energy));
    }
    const auto rate = [&](float analysis::SpanProfile::*field, float threshold, const char* noun) {
        MusicalLayer l;
        if (!measured) {
            l.evidence = "no measured audio under the sections";
            return l;
        }
        const float mean = weightedMean(music, field);
        const float most = largest(music, field);
        l.present = mean >= threshold || most >= threshold * 2.0f;
        l.evidence = fmt::format("{} {:.1f}/s on average, {:.1f}/s at most", noun, mean, most);
        return l;
    };
    std::vector<MusicalLayer> out;
    MusicalLayer k = rate(&analysis::SpanProfile::kickRate, 0.5f, "kicks");
    k.name = "kick";
    k.signal = "audio.onsetLow";
    out.push_back(k);
    MusicalLayer c = rate(&analysis::SpanProfile::snareRate, 0.5f, "snares/claps");
    c.name = "clap";
    c.signal = "audio.onsetMid";
    out.push_back(c);
    MusicalLayer h = rate(&analysis::SpanProfile::hatRate, 0.8f, "hats");
    h.name = "hats";
    h.signal = "audio.onsetHigh";
    out.push_back(h);
    const std::string grid = beat > 0.0 ? fmt::format("a beat of {:.3f} s ({:.1f} BPM)", beat, 60.0 / beat) : "no beat grid";
    out.push_back({"bar", "music.downbeat", beat > 0.0, grid});
    out.push_back({"breath", "", beat > 0.0, beat > 0.0 ? fmt::format("two bars = {:.2f} s", 8.0 * beat) : grid});
    out.push_back({"phrase", "music.phrase", beat > 0.0,
                   beat > 0.0 ? fmt::format("{}-bar phrases", std::max(1, music.phraseBars)) : grid});
    out.push_back({"lead", "audio.midLevel", measured, measured ? "the mid band's steady level" : "no measured audio"});
    out.push_back({"bass", "audio.bassLevel", measured, measured ? "the bass band's steady level" : "no measured audio"});
    out.push_back({"section", "section.energy", energies.size() >= 2,
                   fmt::format("{} section(s), {} distinct energies ({})", music.sections.size(), energies.size(),
                               music.sectionSource.empty() ? "none" : music.sectionSource)});
    return out;
}

json ReactivityProposal::summaryJson() const {
    json byLevel = json::object();
    json byGroup = json::object();
    json bySource = json::object();
    json byOwner = json::object();
    for (const PlanRoute& r : plan.routes) {
        byLevel[reactiveLevelName(r.level)] = byLevel.value(reactiveLevelName(r.level), 0) + 1;
        byGroup[r.group] = byGroup.value(r.group, 0) + 1;
        bySource[r.route.source] = bySource.value(r.route.source, 0) + 1;
        byOwner[r.owner] = byOwner.value(r.owner, 0) + 1;
    }
    json layersJson = json::array();
    for (const MusicalLayer& l : layers) {
        layersJson.push_back({{"name", l.name}, {"signal", l.signal}, {"present", l.present}, {"evidence", l.evidence}});
    }
    return {{"routes", plan.routes.size()},
            {"sources", plan.sources.size()},
            {"byLevel", std::move(byLevel)},
            {"byGroup", std::move(byGroup)},
            {"bySource", std::move(bySource)},
            {"byOwner", std::move(byOwner)},
            {"layers", std::move(layersJson)},
            {"depth", depthSource.empty() ? json(nullptr)
                                          : json{{"source", depthSource}, {"min", depthMin}, {"max", depthMax}}},
            {"notes", notes}};
}

ReactivityProposal proposeReactivity(const ReactiveCatalog& catalog, const MusicalContext& music,
                                     const ReactivityOptions& options) {
    ReactivityProposal out;
    out.layers = musicalLayers(music);
    const auto layer = [&](std::string_view name) -> const MusicalLayer& {
        return *std::find_if(out.layers.begin(), out.layers.end(), [&](const MusicalLayer& l) { return l.name == name; });
    };
    const bool measured = layer("lead").present;
    const double beat = beatSeconds(music);
    const float beatMs = beat > 0.0 ? static_cast<float>(beat * 1000.0) : 500.0f;

    Builder b{catalog, options, {}, 0.0f, 1.0f, {}, {}, {}, {}};
    b.plan.id = options.planId;
    b.plan.title = options.title;
    b.plan.tier = Tier::Baked;
    b.plan.provenance = Provenance{"script", "avgen reactivity proposer (ADR-927)",
                                   "the default reactivity proposal: micro, meso and macro routes from the reactive "
                                   "catalogue and the music's layers"};

    // ---- depth follows the section ----------------------------------------------------------------
    float eLo = 1.0f;
    float eHi = 0.0f;
    for (const SectionRun& s : music.sections) {
        eLo = std::min(eLo, s.energy);
        eHi = std::max(eHi, s.energy);
    }
    const bool sections = layer("section").present && eHi - eLo >= 0.05f;
    if (sections) {
        // The quietest section keeps `quietestDepth` of the response and the loudest answers in full,
        // whatever scale the energies were written on -- never below 0 anywhere on 0..1.
        float slope = (1.0f - options.quietestDepth) / (eHi - eLo);
        float lo = options.quietestDepth - slope * eLo;
        if (lo < 0.0f) {
            lo = 0.0f;
            slope = 1.0f / eHi;
        }
        b.depthSource = "section.energy";
        b.depthMin = round3(lo);
        b.depthMax = round3(lo + slope);
    } else if (measured) {
        b.depthSource = "audio.energy";
        b.depthMin = options.quietestDepth;
        b.depthMax = 1.0f;
    }
    out.depthSource = b.depthSource;
    out.depthMin = b.depthMin;
    out.depthMax = b.depthMax;
    const auto depthWords = [&]() -> std::string {
        if (b.depthSource == "section.energy") {
            return fmt::format(" Its depth follows the section: {:.0f}% in the quietest, in full in the loudest.",
                               options.quietestDepth * 100.0f);
        }
        if (b.depthSource == "audio.energy") {
            return " Its depth follows the music's measured energy.";
        }
        return {};
    };

    // ---- the sources the plan makes ------------------------------------------------------------------
    const std::string breathName = "two-bar-breath";
    bool breathUsed = false;
    const auto breathTemplate = [&]() {
        breathUsed = true;
        return breath("lfo." + breathName);
    };

    // ---- meso: the heroes ----------------------------------------------------------------------------
    std::vector<const ReactiveHero*> heroes;
    for (const ReactiveHero& h : catalog.heroes) {
        const bool emits = std::any_of(catalog.targets.begin(), catalog.targets.end(), [&](const ReactiveTarget& t) {
            return t.group == ReactiveGroup::HeroEmission && t.hero == h.name;
        });
        if (emits) {
            heroes.push_back(&h);
        }
    }
    std::stable_sort(heroes.begin(), heroes.end(), [](const ReactiveHero* a, const ReactiveHero* c) {
        return a->importance != c->importance ? a->importance > c->importance : a->name < c->name;
    });
    std::vector<Template> rotation;
    if (layer("kick").present) rotation.push_back(kick());
    if (layer("clap").present) rotation.push_back(clap());
    if (layer("bar").present) rotation.push_back(bar());
    if (layer("breath").present) rotation.push_back(breath("lfo." + breathName));
    if (layer("phrase").present) rotation.push_back(phrase());
    const auto [midLo, midHi] = bandWindow(music, 2);   // the analyzer's mid band
    const auto [bassLo, bassHi] = bandWindow(music, 0); // and its bass band
    if (measured) {
        rotation.push_back(lead(midLo, midHi));
        rotation.push_back(bass(bassLo, bassHi));
    }
    std::string leadHero;
    glm::vec3 leadPosition{0.0f};
    for (std::size_t i = 0; i < heroes.size(); ++i) {
        const ReactiveHero& hero = *heroes[i];
        const float factor = std::max(0.55f, 1.0f - 0.07f * static_cast<float>(i));
        float heroDelay = 0.0f;
        Template tp;
        if (i < rotation.size()) {
            tp = rotation[i];
        } else if (sections) {
            tp = sectionFlare();
            // The heroes beyond the layers relight on the section change, outward from the first.
            heroDelay = glm::length(glm::vec2(hero.position.x - leadPosition.x, hero.position.z - leadPosition.z)) /
                        options.propagationMetresPerSecond * 1000.0f;
        } else if (!rotation.empty()) {
            tp = rotation[i % rotation.size()];
            heroDelay = 0.25f * beatMs * static_cast<float>(i / rotation.size());
        } else {
            out.notes.push_back(fmt::format("hero '{}' has nothing in the music to answer: no beat grid, no measured "
                                            "audio, no sections",
                                            hero.name));
            continue;
        }
        if (tp.layer == "breath") {
            tp = breathTemplate();
        }
        if (i == 0) {
            leadHero = hero.name;
            leadPosition = hero.position;
        }
        // The hero's glowing parts: those a program lights (where a hero's light comes from, ADR-179),
        // brightest first, three at most; a hero with none answers through its brightest plain part.
        std::vector<const ReactiveTarget*> parts;
        for (const ReactiveTarget& t : catalog.targets) {
            if (t.group == ReactiveGroup::HeroEmission && t.hero == hero.name) {
                parts.push_back(&t);
            }
        }
        std::stable_sort(parts.begin(), parts.end(), [](const ReactiveTarget* a, const ReactiveTarget* c) {
            if (a->programLit != c->programLit) {
                return a->programLit;
            }
            return a->emission != c->emission ? a->emission > c->emission : a->path < c->path;
        });
        const bool anyLit = !parts.empty() && parts.front()->programLit;
        std::erase_if(parts, [&](const ReactiveTarget* t) { return anyLit && !t->programLit; });
        for (std::size_t p = anyLit ? 3 : 1; parts.size() > p;) {
            out.notes.push_back(fmt::format("{} is left alone: {} answers through its brighter parts", parts.back()->path,
                                            hero.name));
            parts.pop_back();
        }
        const std::string when = i < rotation.size()
                                     ? fmt::format("{} is the {} owner", hero.name, tp.layer)
                                     : fmt::format("{} relights {:.0f} ms after the section changes, {:.0f} m from {}",
                                                   hero.name, heroDelay, heroDelay * options.propagationMetresPerSecond / 1000.0f,
                                                   leadHero);
        std::size_t j = 0;
        for (const ReactiveTarget* part : parts) {
            if (!b.free(*part)) {
                continue;
            }
            const float delay = heroDelay + 35.0f * static_cast<float>(j++);
            b.route(*part, tp, factor, delay, ReactiveLevel::Meso, hero.name,
                    fmt::format("{}: {} in {}'s glow, at {:.0f}% of the lead hero's amplitude; its parts answer 35 ms "
                                "apart, so the hero reads as one body.{}",
                                when, tp.what, part->owner, factor * 100.0f, tp.beat ? depthWords() : std::string()));
        }
        // What stands beside the hero answers with it, a moment later.
        for (const ReactiveTarget& t : catalog.targets) {
            if (t.hero != hero.name || !b.free(t)) {
                continue;
            }
            if (t.group == ReactiveGroup::Light && !t.global) {
                b.route(t, asMultiply(tp, 0.30f * factor), 1.0f, heroDelay + 50.0f, ReactiveLevel::Meso, hero.name,
                        fmt::format("The light beside {} echoes it 50 ms later: the ground answers {}.{}", hero.name, tp.what,
                                    tp.beat ? depthWords() : std::string()));
            } else if (t.group == ReactiveGroup::Particles && t.kind == ReactiveKind::Luminance) {
                Template echo = asMultiply(tp, 0.25f * factor);
                echo.decayMs = std::max(echo.decayMs, 400.0f);
                b.route(t, echo, 1.0f, heroDelay + 90.0f, ReactiveLevel::Micro, hero.name,
                        fmt::format("{}'s spores catch its pulse 90 ms later.{}", hero.name,
                                    tp.beat ? depthWords() : std::string()));
            }
        }
    }
    if (heroes.empty()) {
        out.notes.push_back("no hero emits light of its own (nodes/<part>/emissiveBoost), so no hero answers a layer");
    }

    // ---- the glowing scatter layers: micro fast layers, and the kick's echo -----------------------------
    // Only the layers that read as lights: a quarter of the brightest glowing layer's emission or more.
    // A faint glow in the vegetation (ferns, grass, the trees' fireflies at a few percent of the
    // mushrooms') pulsing on a beat reads as flicker, not as the valley listening.
    float brightest = 0.0f;
    for (const ReactiveTarget& t : catalog.targets) {
        if (t.group == ReactiveGroup::ScatterGlow) {
            brightest = std::max(brightest, t.emission);
        }
    }
    const auto salient = [&](const ReactiveTarget& t) { return t.emission >= 0.25f * brightest; };
    std::vector<const ReactiveTarget*> glows;
    std::string faint;
    for (const ReactiveTarget& t : catalog.targets) {
        if (t.group == ReactiveGroup::ScatterGlow) {
            if (salient(t)) {
                glows.push_back(&t);
            } else {
                faint += fmt::format("{}{} ({:.0f}%)", faint.empty() ? "" : ", ", shortName(t),
                                     brightest > 0.0f ? 100.0f * t.emission / brightest : 0.0f);
            }
        }
    }
    if (!faint.empty()) {
        out.notes.push_back(fmt::format("faintly glowing layers are left alone -- {} of the brightest layer's glow: a "
                                        "beat there reads as flicker in the vegetation",
                                        faint));
    }
    std::stable_sort(glows.begin(), glows.end(), [](const ReactiveTarget* a, const ReactiveTarget* c) {
        return a->size != c->size ? a->size < c->size : a->path < c->path;
    });
    std::size_t small = 0;
    std::size_t flickers = 0;
    std::size_t lamps = 0;
    for (const ReactiveTarget* t : glows) {
        if (!b.free(*t)) {
            continue;
        }
        const std::string name = shortName(*t);
        if (t->size >= 1.0f) {
            Template tp = layer("clap").present ? clap() : (layer("bar").present ? bar() : sectionFlare());
            tp.amount = 0.30f;
            tp.attackMs = std::max(tp.attackMs, 8.0f);
            const float delay = 60.0f + 30.0f * static_cast<float>(lamps++);
            b.route(*t, tp, 1.0f, delay, ReactiveLevel::Micro, t->owner,
                    fmt::format("The {} ({:.1f} m, lamps among the small lights) flare on the {} {:.0f} ms late, their "
                                "own colour against the heroes'.{}",
                                name, t->size, tp.layer, delay, depthWords()));
            continue;
        }
        if (small == 0 && (layer("kick").present || layer("bar").present)) {
            Template tp = layer("kick").present ? kick() : bar();
            tp.amount = 0.30f;
            tp.decayMs = 320.0f;
            b.route(*t, tp, 1.0f, 90.0f, ReactiveLevel::Meso, t->owner,
                    fmt::format("The {} (the smallest glowing layer, {:.2f} m) answer the {} 90 ms after {}: the valley's "
                                "small lights echo its heartbeat, +30%.{}",
                                name, t->size, tp.layer, leadHero.empty() ? std::string("the heroes") : leadHero,
                                depthWords()));
        } else {
            Template tp = layer("hats").present ? hats() : (layer("clap").present ? clap() : bar());
            tp.amount = tp.layer == "hats" ? 0.18f : 0.20f;
            const float delay = 30.0f + 40.0f * static_cast<float>(flickers++);
            b.route(*t, tp, 1.0f, delay, ReactiveLevel::Micro, t->owner,
                    fmt::format("The {} ({:.2f} m) flicker on the {}, {:.0f} ms behind the beat, +{:.0f}%: small, fast and "
                                "local.{}",
                                name, t->size, tp.layer, delay, tp.amount * 100.0f, depthWords()));
        }
        ++small;
    }

    // ---- meso: waves through the glowing layers along the bar (ADR-905/906) ---------------------------------
    bool anyWave = false;
    for (const ReactiveTarget& t : catalog.targets) {
        if (t.group != ReactiveGroup::ScatterWave && t.group != ReactiveGroup::NodeWave) {
            continue;
        }
        anyWave = true;
        if (!t.fieldTriggered) {
            out.notes.push_back(fmt::format("field '{}' (the {}) runs on the transport clock, so its wave leaves once "
                                            "at 0 s: give it a trigger (ADR-906), e.g. every bar, to send one along "
                                            "the bar",
                                            t.field, t.label));
            continue;
        }
        if (!sections || !b.free(t)) {
            continue;
        }
        Template tp = arc(eLo, eHi, 0.35f, 1.15f, 2000.0f, 3000.0f, "");
        if (t.op == ModOp::Add) {
            tp.op = ModOp::Add;
            tp.amount = 8.0f; // "8 is clear" (the emission stream), scaled by the section below
        }
        tp.layer = "section energy";
        b.route(t, tp, 1.0f, 0.0f, ReactiveLevel::Meso, t.owner,
                fmt::format("The light wave of field '{}' through the {} is faint in the quiet sections and full in "
                            "the loud ones: the wave keeps its trigger's rhythm, the section sets how far it reads.",
                            t.field, shortName(t)));
    }
    if (!anyWave && !glows.empty()) {
        out.notes.push_back("no glowing layer names a wave field: add a wave field with a trigger (every bar: "
                            "{\"source\": \"beat\", \"everyN\": 4}) and name it in the layer's emissiveField (ADR-905, "
                            "ADR-906) to send light through the mushrooms along the bar; the next proposal routes its depth");
    }

    // ---- macro: the glowing layers' colour, keyed by section ------------------------------------------------
    std::vector<const ReactiveTarget*> hues;
    for (const ReactiveTarget& t : catalog.targets) {
        const ReactiveTarget* glow = t.group == ReactiveGroup::ScatterHue ? catalog.find(t.path.substr(0, t.path.rfind('/')) + "/emissionGain")
                                                                          : nullptr;
        if (glow != nullptr && salient(*glow)) {
            hues.push_back(&t); // the lights' colour, not the vegetation's: the frame's gamut stays the plateau's
        }
    }
    std::stable_sort(hues.begin(), hues.end(), [](const ReactiveTarget* a, const ReactiveTarget* c) {
        return a->size != c->size ? a->size < c->size : a->path < c->path;
    });
    if (!hues.empty() && music.sections.size() >= 2) {
        // Cooler in the quiet sections (+0.06 of a turn), a little cooler before the song arrives (+0.03),
        // as authored from its arrival, warmer in the drop (-0.08): "the light rebuilt".
        std::size_t arrival = music.sections.size();
        for (std::size_t i = 0; i < music.sections.size() && arrival == music.sections.size(); ++i) {
            if (arrivalType(music.sections[i].type)) {
                arrival = i;
            }
        }
        const float span = std::max(eHi - eLo, 1e-3f);
        for (std::size_t i = 0; i < music.sections.size() && arrival == music.sections.size(); ++i) {
            if ((music.sections[i].energy - eLo) / span >= 0.85f) {
                arrival = i;
            }
        }
        bool hasDrop = std::any_of(music.sections.begin(), music.sections.end(),
                                   [](const SectionRun& s) { return s.type == "drop"; });
        std::size_t climax = music.sections.size();
        if (!hasDrop) {
            float best = -1.0f;
            for (std::size_t i = music.sections.size() / 2; i < music.sections.size(); ++i) {
                if (music.sections[i].energy > best) {
                    best = music.sections[i].energy;
                    climax = i;
                }
            }
        }
        std::vector<float> value(music.sections.size(), 0.0f);
        for (std::size_t i = 0; i < music.sections.size(); ++i) {
            const SectionRun& s = music.sections[i];
            const float norm = (s.energy - eLo) / span;
            if (s.type == "drop" || i == climax) {
                value[i] = -0.08f;
            } else if (quietType(s.type) || norm < 0.3f) {
                value[i] = 0.06f;
            } else if (i < arrival) {
                value[i] = 0.03f;
            }
        }
        PlanSource hue;
        hue.key = b.key("source.glowing-plants-hue-by-section");
        hue.kind = "timeline";
        hue.name = "glowing-plants-hue-by-section";
        json keysJson = json::array();
        keysJson.push_back({{"time", 0.0}, {"value", value[0]}, {"interp", "linear"}});
        std::string arcText;
        for (std::size_t i = 0; i < music.sections.size(); ++i) {
            const SectionRun& s = music.sections[i];
            arcText += fmt::format("{}{} {:+.2f}", arcText.empty() ? "" : ", ", s.type, value[i]);
            if (i == 0 || value[i] == value[i - 1]) {
                continue;
            }
            const double glide = std::min(beat > 0.0 ? 4.0 * beat : 2.0, 0.5 * (s.endSeconds - s.startSeconds));
            keysJson.push_back({{"time", s.startSeconds}, {"value", value[i - 1]}, {"interp", "linear"}});
            keysJson.push_back({{"time", s.startSeconds + glide}, {"value", value[i]}, {"interp", "linear"}});
        }
        hue.settings = {{"keys", keysJson}, {"loopLength", 0.0}, {"mode", "value"}};
        hue.reason = fmt::format("The glowing layers' colour follows the song's sections and never its audio, gliding "
                                 "over a bar into each: {} (turns of hue).",
                                 arcText);
        b.plan.sources.push_back(hue);
        for (std::size_t k = 0; k < hues.size(); ++k) {
            const ReactiveTarget& t = *hues[k];
            if (!b.free(t)) {
                continue;
            }
            Template tp{"sections", "timeline." + hue.name, ModOp::Add, 1.0f, 0, 1, 1, 1, 0.0f, 0.0f, false, ""};
            const float scale = std::max(0.6f, 1.0f - 0.15f * static_cast<float>(k));
            // Add of the timeline's value: the amount is the scale itself, not a fraction of a base of 0.
            b.route(t, tp, 1.0f, 250.0f * static_cast<float>(k), ReactiveLevel::Macro, t.owner,
                    fmt::format("The {}' colour turns with the sections ({:.0f}% of the arc, {:.0f} ms after the smaller "
                                "layers): cooler in the quiet sections, warmer in the drop -- keyed, never driven by the "
                                "audio.",
                                shortName(t), scale * 100.0f, 250.0f * static_cast<float>(k)));
            b.plan.routes.back().route.amount = round3(scale);
        }
    }

    // ---- micro: free particle systems glint on the hats; macro: their density follows the section ---------
    std::size_t glints = 0;
    for (const ReactiveTarget& t : catalog.targets) {
        if (t.group != ReactiveGroup::Particles || !t.hero.empty()) {
            continue;
        }
        if (t.kind == ReactiveKind::Luminance) {
            if (!b.free(t)) {
                continue;
            }
            Template tp = layer("hats").present ? hats() : (layer("clap").present ? clap() : bar());
            tp = asMultiply(tp, 0.25f + 0.03f * static_cast<float>(glints % 3));
            tp.attackMs = 5.0f;
            tp.decayMs = 250.0f;
            const float delay = 40.0f * static_cast<float>(glints % 4);
            ++glints;
            b.route(t, tp, 1.0f, delay, ReactiveLevel::Micro, t.owner,
                    fmt::format("The {} glint on the {}, {:.0f} ms apart from the other swarms: small, fast, local.{}",
                                t.owner, tp.layer, delay, depthWords()));
        } else if (t.kind == ReactiveKind::Density && sections && b.free(t)) {
            b.route(t, arc(eLo, eHi, 0.75f, 1.35f, 2000.0f, 3000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, t.owner,
                    fmt::format("More {} in the loud sections and fewer in the quiet: the air fills with the song.", t.owner));
        }
    }

    // ---- macro: the world's light, air and wind follow the section ----------------------------------------
    for (const ReactiveTarget& t : catalog.targets) {
        switch (t.group) {
        case ReactiveGroup::EcologyLight:
            if (sections && b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.65f, 1.35f, 1500.0f, 3000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, "world",
                        "The light the glowing plants and fungi cast on the ground rises and falls with the song's arc, "
                        "gliding over seconds.");
            } else if (!sections && measured && b.free(t)) {
                b.route(t, Template{"energy", "audio.energy", ModOp::Multiply, 1.0f, 0.30f, 0.60f, 0.80f, 1.25f, 1500.0f,
                                    3000.0f, false, ""},
                        1.0f, 0.0f, ReactiveLevel::Macro, "world",
                        "The light the glowing plants cast follows the music's measured energy (the song has no sections "
                        "to follow).");
            }
            break;
        case ReactiveGroup::Atmosphere:
            if (sections && t.kind == ReactiveKind::Density && b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.85f, 1.20f, 3000.0f, 5000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, "world",
                        "The air thickens in the loud sections and clears in the quiet ones, slowly.");
            }
            break;
        case ReactiveGroup::Wind:
            if (t.path == "scene/windSpeed" && sections && b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.80f, 1.30f, 3000.0f, 5000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, "world",
                        "The wind's strength follows the section: a breeze in the quiet, livelier in the drop -- never a "
                        "storm.");
            } else if (t.path == "scene/wind/gustAmount" && layer("breath").present && b.free(t)) {
                breathUsed = true;
                b.route(t, gusts("lfo." + breathName), 1.0f, beatMs, ReactiveLevel::Macro, "world",
                        "Gusts every two bars, a beat after the heroes breathe.");
            } else if (t.path == "scene/wind/turbulence" && sections && catalog.find("scene/windSpeed") == nullptr &&
                       b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.80f, 1.40f, 3000.0f, 5000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, "world",
                        "The wind's turbulence follows the section (its strength is a phase rate in this scene, so the "
                        "arc moves turbulence instead).");
            }
            break;
        case ReactiveGroup::Effect:
            if (t.level == ReactiveLevel::Macro && measured && b.free(t)) {
                Template tp = lead(midLo, midHi);
                tp.outMin = 0.94f;
                tp.outMax = 1.15f;
                tp.attackMs = 400.0f;
                tp.decayMs = 1600.0f;
                tp.beat = false; // a continuous answer to the lead: no beat to scale
                b.route(t, tp, 1.0f, 0.0f, ReactiveLevel::Macro, t.owner,
                        fmt::format("The {} answers the lead: its intensity lifts with the mid band's steady level, "
                                    "slowly, never on the beat.",
                                    t.label));
            }
            break;
        case ReactiveGroup::Water: {
            const std::string leaf = t.path.substr(t.path.rfind('/') + 1);
            if (leaf == "sparkle" && t.op == ModOp::Multiply && b.free(t)) {
                Template tp = asMultiply(layer("hats").present ? hats() : bar(), 0.30f);
                tp.attackMs = 10.0f;
                tp.decayMs = 300.0f;
                b.route(t, tp, 1.0f, 150.0f, ReactiveLevel::Micro, t.owner,
                        fmt::format("The water's sparkle catches the {} 150 ms late.{}", tp.layer, depthWords()));
            } else if (leaf == "swell" && t.op == ModOp::Multiply && layer("breath").present && b.free(t)) {
                b.route(t, breathTemplate(), 1.0f, 2.0f * beatMs, ReactiveLevel::Meso, t.owner,
                        "The water breathes two beats after the heroes, with the wind between them.");
            } else if (leaf == "glow" && t.op == ModOp::Multiply && sections && b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.85f, 1.20f, 2000.0f, 4000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, t.owner,
                        "The water's glow follows the section.");
            }
            break;
        }
        case ReactiveGroup::WaterTears:
            if (t.path.ends_with("/tears") && sections && b.free(t)) {
                b.route(t, arc(eLo, eHi, 0.70f, 1.30f, 3000.0f, 5000.0f, ""), 1.0f, 0.0f, ReactiveLevel::Macro, t.owner,
                        "The water's tears catch more of the wind in the loud sections.");
            }
            break;
        default: break;
        }
    }

    // ---- what the proposal leaves alone, said out loud -------------------------------------------------
    for (const ReactiveTarget& t : catalog.targets) {
        if (t.group == ReactiveGroup::Light && t.global) {
            out.notes.push_back(fmt::format("the {} ({}) is left alone: moving the key or the ambient changes the whole "
                                            "frame's look, the style shift the owner ruled out",
                                            t.label, t.path));
        } else if (t.group == ReactiveGroup::MaterialEmission) {
            out.notes.push_back(fmt::format("{} is left alone: a route on it moves every surface drawn with it ({}) in "
                                            "lockstep; the per-node and per-layer lanes are the handles",
                                            t.path, t.sharedBy.size()));
        }
    }
    if (breathUsed) {
        PlanSource lfo;
        lfo.key = b.key("source." + breathName);
        lfo.kind = "lfo";
        lfo.name = breathName;
        lfo.settings = {{"shape", "sine"}};
        lfo.parameters = {{"beatSync", 1.0f}, {"beatsPerCycle", 8.0f}};
        lfo.reason = "A slow sine locked to the bar grid, one cycle every two bars (8 beats), for the breath the heroes, "
                     "the wind's gusts and the water share -- each at its own delay.";
        b.plan.sources.insert(b.plan.sources.begin(), lfo);
    }
    out.plan = std::move(b.plan);
    out.notes.insert(out.notes.begin(), b.notes.begin(), b.notes.end());
    return out;
}

} // namespace avgen::directing
