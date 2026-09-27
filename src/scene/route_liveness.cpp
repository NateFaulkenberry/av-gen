#include "scene/route_liveness.hpp"

#include "analysis/analysis_track.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"
#include "scene/sdf_object.hpp"
#include "seq/sequence.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/wave_effect.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace avgen::scene {

using params::liveness::Finding;
using params::liveness::Verdict;

namespace {

// The GPU's material program table (rendering::kMaxGpuMaterialPrograms, material_programs.hpp): a
// program past this many is never uploaded, and its surfaces shade as though they had none. Mirrored
// here because avgen_core does not see the renderer; a GPU test holds the two equal.
constexpr std::size_t kGpuProgramSlots = 8;

std::vector<std::string_view> splitPath(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) {
            out.push_back(path.substr(start));
            break;
        }
        out.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    return out;
}

bool matches(std::span<const std::string_view> pattern, std::span<const std::string_view> path) {
    if (pattern.size() != path.size()) {
        return false;
    }
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != "*" && pattern[i] != path[i]) {
            return false;
        }
    }
    return true;
}

Finding make(std::string_view rule, Verdict verdict, std::string reason) {
    return Finding{std::string(rule), verdict, std::move(reason)};
}

bool colourIsBlack(const glm::vec3& c) {
    return c.x <= 0.0f && c.y <= 0.0f && c.z <= 0.0f;
}

// ADR-916. Whether the water whose tear controls sit under `prefix` (".../water/tears/") draws tears
// at some moment of the project. A water whose amount is 0 is drawn by the pipeline with the tear code
// compiled out, so every other tear setting of it is read only while something lifts the amount: an
// enabled route with a nonzero amount, or an enabled track with a key above 0. Unknown when the
// routes and tracks are not to hand (a bare facts object), so the rules built on this can decline to
// guess in either direction.
enum class TearsShow : std::uint8_t { Yes, Never, Unknown };

TearsShow tearsShow(const LivenessInputs& in, const std::string& prefix) {
    const std::string amount = prefix + "amount";
    if (in.params != nullptr) {
        if (const params::IParameter* p = in.params->find(amount); p != nullptr && p->baseComponent(0) > 0.0f) {
            return TearsShow::Yes;
        }
    }
    if (in.modulator == nullptr || in.timeline == nullptr) {
        return TearsShow::Unknown;
    }
    for (const params::ModRoute& route : in.modulator->routes()) {
        if (route.enabled && route.target == amount && route.amount != 0.0f) {
            return TearsShow::Yes;
        }
    }
    for (const params::Track& track : in.timeline->tracks()) {
        if (track.enabled && track.target == amount &&
            std::any_of(track.keys.begin(), track.keys.end(), [](const params::Key& k) { return k.value[0] > 0.0f; })) {
            return TearsShow::Yes;
        }
    }
    return TearsShow::Never;
}

// ADR-916: a water whose tears show, follow the wind and drift -- the one case in which the wind's
// direction is a drifting lattice's direction, and so a phase rate.
bool driftingTearsFollowTheWind(const LivenessInputs& in) {
    if (in.params == nullptr) {
        return false;
    }
    constexpr std::string_view kLeaf = "followWind";
    constexpr std::string_view kTail = "/water/tears/followWind";
    for (const params::IParameter* p : in.params->ordered()) {
        const std::string& name = p->path();
        if (name.size() <= kTail.size() || name.compare(name.size() - kTail.size(), kTail.size(), kTail) != 0 ||
            p->baseComponent(0) < 0.5f) {
            continue;
        }
        const std::string prefix = name.substr(0, name.size() - kLeaf.size());
        const params::IParameter* drift = in.params->find(prefix + "drift");
        if (drift != nullptr && drift->baseComponent(0) != 0.0f && tearsShow(in, prefix) == TearsShow::Yes) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---- the phase-rate table --------------------------------------------------------------------------
//
// Every modulatable parameter found whose phase is (absolute time x its value), by reading every
// shader and every CPU use of the render clock (ADR-902 records how, and the rate-like parameters
// that were checked and are safe: integrated over dt, differenced, or used as an amplitude). Not in
// the table because they depend on authored content: a material program op whose constant multiplies
// the Time input, and a user shader layer input the layer multiplies by `sys.time`.
namespace {
// clang-format off
constexpr PhaseRateEntry kPhaseRates[] = {
    // signal sources
    {"sources/*/rate", "lfo", "source.cpp LfoSource: position = renderTime * rate + phase"},
    {"sources/*/beatsPerCycle", "lfo", "source.cpp LfoSource (beatSync): position = beats / beatsPerCycle"},
    {"sources/*/rate", "noise", "source.cpp NoiseSource::evaluate: lattice at time * rate"},
    {"sources/*/scale", "timeline", "source.cpp TimelineSource: source time = (renderTime + offset) * scale"},
    // scene, camera, environment
    {"scene/wind/gustSpeed", "", "core/wind.cpp, wind_field.wgsl: gust phase kg * (a - t * gustSpeed)"},
    {"scene/wind/gustScale", "", "core/wind.cpp, wind_field.wgsl: kg = 1 / gustScale multiplies t * gustSpeed"},
    {"scene/wind/turbulenceSpeed", "", "core/wind.cpp, wind_field.wgsl: turbulence phase t * turbulenceSpeed * kt"},
    {"scene/wind/turbulenceScale", "", "core/wind.cpp, wind_field.wgsl: kt = 1 / turbulenceScale multiplies t * turbulenceSpeed"},
    {"scene/wind/regionDrift", "", "core/wind.cpp, wind_field.wgsl: region waves move by t * regionDrift"},
    {"scene/windSpeed", "", "common.wgsl: wind-body leaf flutter sin(phase + t * (5.5 + 3 * strength)), strength = windSpeed x region", {}, true},
    {"scene/volumeNoiseSpeed", "", "volume.wgsl: noise offset = renderTime * volumeNoiseSpeed"},
    {"camera/shake/frequency", "", "camera.cpp: shake noise at seconds * frequency"},
    {"env/dayNight/cycleSeconds", "", "day_night.cpp: day phase = seconds / cycleSeconds"},
    // nodes
    {"nodes/*/energy/pulseSpeed", "", "common.wgsl: tree energy pulse at t * pulseSpeed"},
    {"nodes/*/energy/propagation", "", "common.wgsl: fract(h - t * propagation)"},
    {"nodes/*/energy/shimmerSpeed", "", "common.wgsl: shimmer at t * shimmerSpeed"},
    // ADR-914 bounded the water's advection, so a change jumps its fields by at most 8 s of travel rather
    // than by the whole elapsed time -- still a jump.
    {"nodes/*/water/flowSpeed", "", "water.wgsl flowPhases (ADR-914): each field travels (phase x period) * flowSpeed, up to kFlowPeriod = 8 s of it"},
    {"nodes/*/water/rippleScale", "", "water.wgsl: ripple lattice scale of a flow-advected coordinate", "flowSpeed"},
    // ADR-916: the tears' seam lattice drifts rigidly at t * drift, and its step size, spacing, stretch
    // and direction scale or turn that drifting coordinate. None of these is modulatable -- a route to
    // one is refused -- so what meets these rows is a timeline track that keys one.
    {"nodes/*/water/tears/drift", "", "water.wgsl tearAt: lattice = (dot(p, along) - drift * t, dot(p, across)) / cell"},
    {"nodes/*/water/tears/cell", "", "water.wgsl tearAt: lattice cell of a drifting coordinate", "drift"},
    {"nodes/*/water/tears/spacing", "", "water.wgsl tearAt: seam spacing of a drifting coordinate", "drift"},
    {"nodes/*/water/tears/stretch", "", "water.wgsl tearAt: seam stretch of a drifting coordinate", "drift"},
    {"nodes/*/water/tears/direction", "", "water.wgsl tearAt: direction of a drifting coordinate", "drift"},
    {"scene/windDirection", "", "water.wgsl tearAt (ADR-916): seams that follow the wind drift along it by drift * t", {}, false, true},
    // particles
    {"particles/*/pulseRate", "", "particles.wgsl pulseGain: phase = renderTime * pulseRate"},
    {"particles/*/pauseRate", "", "particles.wgsl: fract(t * pauseRate + ...)"},
    {"particles/*/turbulenceSpeed", "", "particles.wgsl: turbulence at renderTime * turbulenceSpeed"},
    // procedural, fields, SDF
    {"procedural/*/deform/*/speed", "", "procedural.wgsl: twist/sine/noise deformers at t * speed"},
    {"field/*/speed", "", "spatial/field.cpp, fields.wgsl: tau = speed * t + phase"},
    {"field/*/waveSpeed", "", "fields.wgsl: s = dist - waveOrigin - waveSpeed * t"},
    {"field/*/wavelength", "", "fields.wgsl: wave shape scales a front that moves with t * waveSpeed", "waveSpeed"},
    {"sdf/*/node/*/speed", "", "sdf.wgsl: displace noise/wave at t * speed"},
    // entity behaviours
    {"entity/*/hover/rate", "", "behaviors.cpp: slowNoise(time * rate)"},
    {"entity/*/drift/rate", "", "behaviors.cpp: drift noise at time * rate"},
    {"entity/*/liveliness/swayRate", "", "behaviors.cpp: sway at time * swayRate"},
    // effects: sky and media
    {"fx/*/speed", "comet", "comet_effect.cpp: progress = elapsed * speed / travelSeconds"},
    {"fx/*/travelSeconds", "comet,meteors", "comet_effect.cpp, meteor_shower_effect.cpp: progress = elapsed / travelSeconds"},
    {"fx/*/flowSpeed", "comet,meteors,aurora", "atmospherics.cpp: flow phase = elapsed * flowSpeed"},
    {"fx/*/sparkleSpeed", "comet,meteors,groundPulse,travelBeam", "atmospherics.cpp, wave_effect.cpp: sparkle at elapsed * sparkleSpeed"},
    {"fx/*/rainbowSpeed", "comet,aurora,groundPulse,travelBeam", "atmospherics.cpp, wave_effect.cpp: hue at elapsed * rainbowSpeed"},
    {"fx/*/radiantDrift", "meteors", "meteor_shower_effect.cpp: radiant at elapsed * radiantDrift"},
    {"fx/*/driftSpeed", "aurora,fog", "atmospherics.cpp, fog.wgsl: drift at elapsed * driftSpeed"},
    {"fx/*/verticalSpeed", "aurora", "atmospherics.cpp: vertical phase at elapsed * verticalSpeed"},
    {"fx/*/repeat", "comet,meteors,aurora,groundPulse,travelBeam", "atmospherics.cpp, wave_effect.cpp: phase = fmod(elapsed, repeat)"},
    {"fx/*/rotationSpeed", "vortex", "vortex.wgsl, core/vortex.cpp: rotation at t * rotationSpeed"},
    {"fx/*/breathSpeed", "vortex", "vortex.wgsl, core/vortex.cpp: breath at t * breathSpeed"},
    {"fx/*/bandArms", "vortex", "vortex.wgsl: arm count scales a rotating angle", "rotationSpeed"},
    {"fx/*/swirl", "fog", "fog.wgsl, fog_field.cpp: swirl at t * swirl"},
    {"fx/*/swellSpeed", "fog", "fog.wgsl, fog_field.cpp: swell at t * swellSpeed"},
    {"fx/*/driftVertical", "fog", "fog.wgsl, fog_field.cpp: vertical drift at t * driftVertical"},
    {"fx/*/turbulenceSpeed", "fog,spaceWarp", "fog.wgsl, distortion.wgsl: turbulence at t * turbulenceSpeed"},
    {"fx/*/detailScale", "fog,tornado", "fog.wgsl, tornado.wgsl: detail scale of a time-advected coordinate", "driftSpeed,swirl,turbulenceSpeed,climbRate"},
    {"fx/*/turbulenceScale", "fog", "fog.wgsl: turbulence scale of a time-advected coordinate", "turbulenceSpeed"},
    {"fx/*/bankRotation", "fog", "fog.wgsl: bank rotation of a drifting coordinate", "driftSpeed,swirl"},
    {"fx/*/driftWind", "fog", "fog.wgsl: drift direction x t", "driftSpeed"},
    {"fx/*/rotationBottom", "tornado", "tornado.wgsl, core/tornado.cpp: rotation at t * rotationBottom"},
    {"fx/*/rotationTop", "tornado", "tornado.wgsl, core/tornado.cpp: rotation at t * rotationTop"},
    {"fx/*/rotationCurve", "tornado", "tornado.wgsl, core/tornado.cpp: rotation profile of t * rotation"},
    {"fx/*/suctionSpeed", "tornado", "tornado.wgsl, core/tornado.cpp: suction at t * suctionSpeed"},
    {"fx/*/climbRate", "tornado", "tornado.wgsl, core/tornado.cpp: climb at t * climbRate"},
    {"fx/*/wobbleSpeed", "tornado,bubble", "tornado.wgsl, shell_fx.wgsl: wobble at t * wobbleSpeed"},
    {"fx/*/stripeCount", "tornado", "tornado.wgsl: stripes of a climbing coordinate", "climbRate"},
    {"fx/*/suctionCount", "tornado", "tornado.wgsl: vortices of a rotating coordinate", "suctionSpeed"},
    // effects: surface waves
    {"fx/*/speed", "groundPulse,travelBeam", "wave_effect.cpp: front = speed * pass (bounded by repeat when repeat > 0)"},
    // effects: entity lanes
    {"fx/*/speed", "colorCycling", "pbr_shade.wgsl fxHueTurns: hue = renderTime * speed"},
    {"fx/*/rate", "pulse", "pulse_effect.cpp: cycle = local * rate + phase"},
    {"fx/*/rate", "breathing", "pbr_shade.wgsl: breath at t * rate"},
    {"fx/*/speed", "organicPulsation", "pbr_shade.wgsl: bulge travels at t * speed"},
    {"fx/*/interval", "organicPulsation", "pbr_shade.wgsl: bulge period of a moving front", "speed"},
    {"fx/*/breatheRate", "bioluminescence", "pbr_shade.wgsl: spots breathe at t * breatheRate"},
    {"fx/*/waveInterval", "bioluminescence", "pbr_shade.wgsl: fmod(t, waveInterval)"},
    {"fx/*/pulseSpeed", "pulsingVeins", "pbr_shade.wgsl: vein pulses at t * pulseSpeed"},
    {"fx/*/pulseInterval", "pulsingVeins", "pbr_shade.wgsl: pulse period of a moving front", "pulseSpeed"},
    {"fx/*/crackleSpeed", "electricField", "electric_field_effect.cpp -> pbr_shade.wgsl: crackle at t * crackleSpeed"},
    {"fx/*/arcRate", "electricField", "electric_field_effect.cpp: arcs at t * arcRate"},
    {"fx/*/rate", "arc", "arc_effect.cpp: arc phase at t * rate"},
    // effects: motion
    {"fx/*/period", "orbit,spiral,float,bounce", "xform_rows.hpp cycles(): seconds / period"},
    {"fx/*/frequency", "shake", "shake_effect.cpp: noise at seconds * frequency"},
    // effects: shells and distortion
    {"fx/*/speed", "plasma", "plasma_effect.cpp -> shell.wgsl: plasma at t * speed"},
    {"fx/*/scrollSpeed", "forceField", "force_field_effect.cpp -> shell.wgsl: scroll at t * scrollSpeed"},
    {"fx/*/patternScale", "forceField", "shell.wgsl: pattern scale of a scrolling coordinate", "scrollSpeed"},
    {"fx/*/drift", "lightBeam", "light_beam_effect.cpp -> shell_fx.wgsl: dust drift at t * drift"},
    {"fx/*/drainage", "bubble", "shell_fx.wgsl: film drainage at t * drainage"},
    {"fx/*/swirl", "portal", "portal_effect.cpp -> shell_fx.wgsl: swirl at t * swirl"},
    {"fx/*/flickerRate", "realityTear", "reality_tear_effect.cpp: floor(seconds * flickerRate)"},
    {"fx/*/rippleSpeed", "velocityDistortion", "velocity_distortion_effect.cpp -> distortion.wgsl: ripples at t * rippleSpeed"},
    {"fx/*/twinkleRate", "stars", "star_field.cpp -> skybox.wgsl: twinkle at (seconds mod 256) * twinkleRate"},
    {"fx/*/pulseRate", "particleEmitter", "particle_emitter_effect.cpp -> particles.wgsl: phase = renderTime * pulseRate"},
};
// clang-format on

std::vector<std::string_view> splitList(std::string_view list) {
    std::vector<std::string_view> out;
    for (std::string_view rest = list; !rest.empty();) {
        const std::size_t comma = rest.find(',');
        out.push_back(rest.substr(0, comma));
        if (comma == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(comma + 1);
    }
    return out;
}
} // namespace

std::span<const PhaseRateEntry> phaseRateTable() {
    return kPhaseRates;
}

// ---- the facts ---------------------------------------------------------------------------------

struct SceneLivenessFacts::Cache {
    struct Program {
        std::size_t index = 0;
        bool writesEmission = false;
        std::vector<std::string> emissiveLayers;
        bool used = false;
    };
    std::map<std::string, Program, std::less<>> programs;
    bool programsBuilt = false;

    // node name -> whether a surface it draws itself emits, and its parent
    struct Node {
        NodeKind kind = NodeKind::Gltf;
        std::string parent;
        bool emitsSelf = false;
        bool opaqueToRule = false; // a kind whose emission this rule cannot see (SDF): never flagged
    };
    std::map<std::string, Node, std::less<>> nodes;
    // node name -> its procedurals in scene order (the lead, then its parts)
    std::map<std::string, std::vector<const ProceduralGeometry*>, std::less<>> procedurals;
    bool nodesBuilt = false;

    std::map<std::string, std::optional<Finding>, std::less<>> effectFires; // by id, lazily
    std::unique_ptr<world::TriggerClock> triggers;
};

SceneLivenessFacts::SceneLivenessFacts(LivenessInputs inputs) : in_(inputs) {}
SceneLivenessFacts::~SceneLivenessFacts() = default;

const SceneLivenessFacts::Cache& SceneLivenessFacts::cache() const {
    if (!cache_) {
        cache_ = std::make_unique<Cache>();
    }
    return *cache_;
}

namespace {

const MaterialProgram* findProgram(const Scene& scene, std::string_view name) {
    for (const MaterialProgram& p : scene.materialPrograms) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

bool inRange(int reg) {
    return reg >= 0 && reg < kMaterialRegisters;
}

// Whether a surface with this material and style puts any light out: its program writes emission,
// or (no program emission) its own emissive is non-zero and not black, or it is drawn unlit.
bool surfaceEmits(const Scene& scene, const Material& m, MeshStyle style) {
    if (style != MeshStyle::Lit || m.unlit) {
        return true; // an unlit surface is all emission; this rule does not second-guess it
    }
    if (!m.program.empty()) {
        if (const MaterialProgram* p = findProgram(scene, m.program)) {
            if (inRange(p->emissionRegister)) {
                return true;
            }
            for (const MaterialLayer& layer : p->layers) {
                if (layer.enabled && inRange(layer.emissionRegister)) {
                    return true;
                }
            }
        }
    }
    return m.emissiveIntensity > 0.0f && (!colourIsBlack(m.emissiveColor) || m.emissiveTexture.valid());
}

} // namespace

params::liveness::SignalFacts SceneLivenessFacts::signal(std::string_view name) const {
    params::liveness::SignalFacts facts;
    if (in_.bus == nullptr) {
        return facts;
    }
    const auto id = in_.bus->find(name);
    if (!id) {
        return facts;
    }
    const signals::SignalInfo& info = in_.bus->info(*id);
    facts.exists = true;
    facts.isEvent = info.isEvent;
    facts.minValue = info.minValue;
    facts.maxValue = info.maxValue;

    const auto startsWith = [name](std::string_view prefix) { return name.substr(0, prefix.size()) == prefix; };
    if (in_.judgeSilence && !in_.hasAudio && (startsWith("audio.") || startsWith("music."))) {
        facts.silentBecause = fmt::format("the project has no analysed audio, so '{}' never moves", name);
    } else if (in_.judgeSilence && !in_.hasAudio && !in_.hasTempo && startsWith("beat.")) {
        facts.silentBecause = fmt::format("the project has no audio and no tempo, so '{}' never moves", name);
    }
    const auto median = [](std::vector<float>& v) {
        if (v.empty()) {
            return 0.0f;
        }
        const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
        std::nth_element(v.begin(), mid, v.end());
        return *mid;
    };
    if (name == "audio.onset" && in_.track != nullptr && !in_.track->empty()) {
        // The strength the bus gives an onset (AudioSignals::publish), over the piece's onsets.
        std::vector<float> strengths;
        for (const analysis::AnalysisFrame& f : in_.track->frames()) {
            if (f.onset) {
                strengths.push_back(std::min(1.0f, f.onsetStrength * 0.5f));
            }
        }
        facts.typicalEventStrength = median(strengths);
    }
    if (in_.offline && startsWith("control.")) {
        facts.liveOnlyBecause = fmt::format("'{}' moves only with live MIDI/OSC input, which a render does not "
                                            "have: it holds one value for the whole render",
                                            name);
    }
    if (startsWith("timeline.") && in_.sources != nullptr) {
        for (const auto& source : in_.sources->sources()) {
            if (source->kind() != "timeline" || "timeline." + source->name() != name) {
                continue;
            }
            const auto& timeline = static_cast<const signals::TimelineSource&>(*source);
            if (timeline.mode() == signals::TimelineMode::Event) {
                facts.isEvent = true;
                std::vector<float> hits;
                for (const signals::Keyframe& k : timeline.keys()) {
                    if (k.value > 0.0f) {
                        hits.push_back(k.value);
                    }
                }
                facts.typicalEventStrength = median(hits);
            } else if (in_.durationSeconds > 0.0) {
                facts.pulses = timeline.positiveSpans(0.0, in_.durationSeconds);
                // A curve that is up for long stretches is an arc, not a score of pulses.
                const bool pulseLike = std::any_of(facts.pulses.begin(), facts.pulses.end(),
                                                   [](const auto& s) { return s.second - s.first < 0.25; });
                if (!pulseLike) {
                    facts.pulses.clear();
                }
            }
        }
    }
    return facts;
}

const params::IParameter* SceneLivenessFacts::parameter(std::string_view path) const {
    return in_.params != nullptr ? in_.params->find(path) : nullptr;
}

std::optional<Finding> SceneLivenessFacts::deadTarget(std::string_view path, int component) const {
    static_cast<void>(component);
    const std::vector<std::string_view> seg = splitPath(path);
    if (seg.size() < 2) {
        return std::nullopt;
    }
    auto& c = const_cast<Cache&>(cache());
    const Composition* comp = in_.composition;

    // fx/<id>/... -- an effect that never fires moves nothing, and neither does a route to it.
    if (seg[0] == "fx") {
        for (const world::EffectInstance& e : in_.effects) {
            if (e.id == seg[1]) {
                if (auto why = effectNeverFires(e)) {
                    return make("effect-never-fires", Verdict::Dead,
                                fmt::format("effect '{}' never fires: {}", e.id, why->reason));
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    // nodes/<terrain>/water/tears/<setting> (ADR-916): read only while the water draws tears at all, and
    // the fixed direction only while the seams do not follow the wind. The amount itself is what turns
    // them on, so it is never unread.
    if (seg.size() >= 5 && seg[seg.size() - 5] == "nodes" && seg[seg.size() - 3] == "water" &&
        seg[seg.size() - 2] == "tears" && seg.back() != "amount") {
        const std::string prefix(path.substr(0, path.size() - seg.back().size()));
        if (tearsShow(in_, prefix) == TearsShow::Never) {
            return make("tear-setting-unread", Verdict::Dead,
                        fmt::format("'{}amount' is 0 and no route or track lifts it, so the water is drawn with "
                                    "the tear code compiled out and its '{}' is never read (ADR-916)",
                                    prefix, seg.back()));
        }
        if (seg.back() == "direction" && in_.params != nullptr) {
            if (const params::IParameter* follow = in_.params->find(prefix + "followWind");
                follow != nullptr && follow->baseComponent(0) >= 0.5f) {
                return make("tear-setting-unread", Verdict::Dead,
                            fmt::format("the seams follow the scene wind ('{}followWind' is on), so their fixed "
                                        "direction is never read (ADR-916)",
                                        prefix));
            }
        }
        return std::nullopt;
    }
    if (comp == nullptr) {
        return std::nullopt;
    }
    const Scene& scene = comp->scene();

    if (!c.programsBuilt) {
        c.programsBuilt = true;
        for (std::size_t i = 0; i < scene.materialPrograms.size(); ++i) {
            const MaterialProgram& p = scene.materialPrograms[i];
            Cache::Program info;
            info.index = i;
            info.writesEmission = inRange(p.emissionRegister);
            // Each emissive layer by the path its own intensity is registered at (ADR-905,
            // `registerMaterialProgramParameters`): `layer/<i>/<name>`, counted from 1.
            for (std::size_t li = 0; li < p.layers.size(); ++li) {
                const MaterialLayer& layer = p.layers[li];
                if (layer.enabled && inRange(layer.emissionRegister)) {
                    info.emissiveLayers.push_back("layer/" + std::to_string(li + 1) + "/" +
                                                  (layer.name.empty() ? std::string("layer") : layer.name));
                }
            }
            c.programs.emplace(p.name, std::move(info));
        }
        const auto use = [&c](const std::string& name) {
            if (auto it = c.programs.find(name); it != c.programs.end()) {
                it->second.used = true;
            }
        };
        for (const Entity& e : scene.entities) {
            use(e.material.program);
        }
        for (const ProceduralGeometry& p : scene.procedurals) {
            use(p.material.program);
        }
        for (const SdfObject& s : scene.sdfs) {
            use(s.material.program);
        }
    }

    // material/<program>/...
    if (seg[0] == "material" && seg.size() >= 3) {
        const auto it = c.programs.find(seg[1]);
        if (it == c.programs.end()) {
            return std::nullopt;
        }
        const Cache::Program& p = it->second;
        if (p.index >= kGpuProgramSlots) {
            return make("program-not-uploaded", Verdict::Dead,
                        fmt::format("program '{}' is number {} in the scene and the GPU table holds {}: it is "
                                    "never uploaded",
                                    seg[1], p.index + 1, kGpuProgramSlots));
        }
        if (!p.used) {
            return make("program-unused", Verdict::Dead,
                        fmt::format("no surface in the scene names program '{}'", seg[1]));
        }
        if (seg.size() == 3 && seg[2] == "emissionIntensity" && !p.writesEmission) {
            if (!p.emissiveLayers.empty()) {
                std::string layers;
                for (const std::string& l : p.emissiveLayers) {
                    layers += (layers.empty() ? "" : ", ") + fmt::format("material/{}/{}/emissionIntensity", seg[1], l);
                }
                return make("emission-lives-in-layer", Verdict::Dead,
                            fmt::format("program '{}' writes no base emission; its glow comes from its layers: key "
                                        "or route {} instead (ADR-905)",
                                        seg[1], layers));
            }
            return make("program-has-no-emission", Verdict::Dead,
                        fmt::format("program '{}' writes no emission register, so its emissionIntensity "
                                    "multiplies nothing",
                                    seg[1]));
        }
        return std::nullopt;
    }

    // nodes/<terrain>/scatter/<layer>/emissionGain|hueOffset|emissiveFieldAmount (ADR-905, amended by
    // ADR-926): every layer registers the three lanes, and on a layer that emits nothing -- no emissive
    // colour, no program that writes emission -- each multiplies zero. The light-wave depth also needs
    // a field to multiply: a layer that names none, or names one the scene does not have, moves nothing.
    if (seg[0] == "nodes" && seg.size() == 5 && seg[2] == "scatter") {
        const CompositionNode* terrain = comp->findNode(std::string(seg[1]));
        if (terrain == nullptr || terrain->kind != NodeKind::Terrain) {
            return std::nullopt;
        }
        for (const world::ScatterLayer& layer : terrain->ecology.layers) {
            if (layer.name != seg[3]) {
                continue;
            }
            bool emits = layer.emissiveIntensity > 0.0f && !colourIsBlack(layer.emissiveColor);
            if (!emits && !layer.materialProgram.empty()) {
                const MaterialProgram* p = findProgram(scene, layer.materialProgram);
                emits = p != nullptr && p->writesEmission();
            }
            if (!emits) {
                return make("layer-emits-nothing", Verdict::Dead,
                            fmt::format("scatter layer '{}' emits nothing (no emissive colour, no program that writes "
                                        "emission), so its {} multiplies zero",
                                        seg[3], seg[4]));
            }
            if (seg[4] == "emissiveFieldAmount") {
                if (layer.emissiveField.empty()) {
                    return make("no-field-named", Verdict::Dead,
                                fmt::format("scatter layer '{}' names no emissiveField, so its light-wave depth "
                                            "multiplies nothing",
                                            seg[3]));
                }
                if (scene.fields.find(layer.emissiveField) == nullptr) {
                    return make("no-field-named", Verdict::Dead,
                                fmt::format("scatter layer '{}' names field '{}', which the scene does not have", seg[3],
                                            layer.emissiveField));
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    // procedural/<n>/emissiveFieldAmount: the same question for a procedural node's own field.
    if (seg[0] == "procedural" && seg.size() == 3 && seg[2] == "emissiveFieldAmount") {
        const CompositionNode* node = comp->findNode(std::string(seg[1]));
        if (node == nullptr || node->kind != NodeKind::Procedural) {
            return std::nullopt;
        }
        if (node->procedural.emissiveField.empty()) {
            return make("no-field-named", Verdict::Dead,
                        fmt::format("node '{}' names no emissiveField, so its light-wave depth multiplies nothing", seg[1]));
        }
        if (scene.fields.find(node->procedural.emissiveField) == nullptr) {
            return make("no-field-named", Verdict::Dead,
                        fmt::format("node '{}' names field '{}', which the scene does not have", seg[1],
                                    node->procedural.emissiveField));
        }
        return std::nullopt;
    }

    if (seg[0] != "procedural" && seg[0] != "nodes") {
        return std::nullopt;
    }
    if (!c.nodesBuilt) {
        c.nodesBuilt = true;
        for (const auto& node : comp->nodes()) {
            Cache::Node info;
            info.kind = node->kind;
            info.parent = node->parent;
            info.opaqueToRule = node->kind == NodeKind::Sdf;
            c.nodes.emplace(node->name, std::move(info));
        }
        for (std::size_t i = 0; i < scene.entities.size(); ++i) {
            if (const CompositionNode* owner = comp->nodeForEntity(i)) {
                const Entity& e = scene.entities[i];
                if (surfaceEmits(scene, e.material, e.style)) {
                    c.nodes[owner->name].emitsSelf = true;
                }
            }
        }
        for (std::size_t i = 0; i < scene.procedurals.size(); ++i) {
            if (const CompositionNode* owner = comp->nodeForProcedural(i)) {
                const ProceduralGeometry& p = scene.procedurals[i];
                c.procedurals[owner->name].push_back(&p);
                if (surfaceEmits(scene, p.material, MeshStyle::Lit)) {
                    c.nodes[owner->name].emitsSelf = true;
                }
            }
        }
        // A particle node emits when its particles do (their own `emissive`).
        for (auto& [name, info] : c.nodes) {
            if (info.kind == NodeKind::Particles && in_.params != nullptr) {
                const params::IParameter* p = in_.params->find("particles/" + name + "/emissive");
                if (p == nullptr || p->finalComponent(0) > 0.0f) {
                    info.emitsSelf = true;
                }
            }
        }
    }

    // procedural/<node>/material/emissive|emissiveColor and procedural/<node>/parts/<k>/emissiveGain|
    // emissiveColor: a material's own emission, on a surface whose program writes emission (ADR-179).
    if (seg[0] == "procedural" && seg.size() >= 4) {
        const auto parts = c.procedurals.find(seg[1]);
        if (parts == c.procedurals.end() || parts->second.empty()) {
            return std::nullopt;
        }
        const ProceduralGeometry* surface = nullptr;
        std::string what;
        if (seg.size() == 4 && seg[2] == "material" && (seg[3] == "emissive" || seg[3] == "emissiveColor")) {
            surface = parts->second.front();
            what = fmt::format("the material's own {}", seg[3]);
        } else if (seg.size() == 5 && seg[2] == "parts" && (seg[4] == "emissiveGain" || seg[4] == "emissiveColor")) {
            std::size_t k = 0;
            const std::string index(seg[3]);
            try {
                k = static_cast<std::size_t>(std::stoul(index));
            } catch (...) {
                return std::nullopt;
            }
            if (k < parts->second.size()) {
                surface = parts->second[k];
                what = fmt::format("part {}'s {}", k, seg[4]);
            }
        }
        if (surface != nullptr && !surface->material.program.empty()) {
            if (const MaterialProgram* p = findProgram(scene, surface->material.program);
                p != nullptr && inRange(p->emissionRegister)) {
                return make("program-owns-emission", Verdict::Dead,
                            fmt::format("{} is replaced by program '{}', which writes emission (ADR-179)", what,
                                        p->name));
            }
        }
        return std::nullopt;
    }

    // nodes/<node>/emissiveBoost: a post-program multiplier on whatever the node emits (the
    // semantics agent/emission lands: every node kind, program-lit and procedural included), so it is
    // dead only on a node none of whose surfaces -- nor any descendant's -- emits at all.
    if (seg[0] == "nodes" && seg.size() == 3 && seg[2] == "emissiveBoost") {
        const auto it = c.nodes.find(seg[1]);
        if (it == c.nodes.end()) {
            return std::nullopt;
        }
        // The node and its descendants.
        std::set<std::string, std::less<>> subtree{std::string(seg[1])};
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& [name, info] : c.nodes) {
                if (!subtree.contains(name) && subtree.contains(info.parent)) {
                    subtree.insert(name);
                    grew = true;
                }
            }
        }
        for (const std::string& name : subtree) {
            const Cache::Node& n = c.nodes[name];
            if (n.emitsSelf || n.opaqueToRule) {
                return std::nullopt;
            }
        }
        return make("node-emits-nothing", Verdict::Dead,
                    fmt::format("no surface of node '{}' ({}){} emits anything, so a multiplier on its emission "
                                "multiplies zero",
                                seg[1], nodeKindName(it->second.kind),
                                subtree.size() > 1 ? fmt::format(" or of its {} descendant(s)", subtree.size() - 1)
                                                   : std::string()));
    }
    return std::nullopt;
}

std::optional<Finding> SceneLivenessFacts::phaseRate(std::string_view path) const {
    const std::vector<std::string_view> full = splitPath(path);
    for (const PhaseRateEntry& entry : kPhaseRates) {
        const std::vector<std::string_view> pattern = splitPath(entry.path);
        if (pattern.size() > full.size()) {
            continue;
        }
        // The pattern against the path's last segments: a nested composition prefixes nodes/<child>/.
        const std::span<const std::string_view> seg(full.data() + (full.size() - pattern.size()), pattern.size());
        if (!matches(pattern, seg)) {
            continue;
        }
        if (!entry.kinds.empty()) {
            const std::vector<std::string_view> kinds = splitList(entry.kinds);
            const auto listed = [&kinds](std::string_view k) {
                return std::find(kinds.begin(), kinds.end(), k) != kinds.end();
            };
            bool kindMatches = false;
            if (seg[0] == "sources" && in_.sources != nullptr) {
                for (const auto& s : in_.sources->sources()) {
                    kindMatches = kindMatches || (s->name() == seg[1] && listed(s->kind()));
                }
            } else if (seg[0] == "fx") {
                for (const world::EffectInstance& e : in_.effects) {
                    kindMatches = kindMatches || (e.id == seg[1] && listed(world::effectKindName(e.kind)));
                }
            }
            if (!kindMatches) {
                continue;
            }
        }
        if (!entry.whenNonZero.empty()) {
            // A secondary trap: only while one of the rates beside it is moving the coordinate.
            const std::string prefix(path.substr(0, path.size() - seg.back().size()));
            bool moving = false;
            for (std::string_view sibling : splitList(entry.whenNonZero)) {
                const params::IParameter* p = parameter(prefix + std::string(sibling));
                moving = moving || (p != nullptr && p->finalComponent(0) != 0.0f);
            }
            if (!moving) {
                continue;
            }
        }
        if (entry.windTears && !driftingTearsFollowTheWind(in_)) {
            continue;
        }
        if (entry.windBodies) {
            // Only a scene whose meshes carry a wind body (ADR-360) has the flutter this goes through.
            bool bodies = false;
            if (in_.params != nullptr) {
                for (const params::IParameter* p : in_.params->ordered()) {
                    const std::string& name = p->path();
                    if (name.size() > 14 && name.compare(name.size() - 14, 14, "/wind/strength") == 0 &&
                        name.rfind("nodes/", 0) == 0) {
                        bodies = true;
                        break;
                    }
                }
            }
            if (!bodies) {
                continue;
            }
        }
        return make("phase-rate", Verdict::Hazard,
                    fmt::format("'{}' is a rate whose phase is time x rate ({}): a change of d at t seconds jumps "
                                "the pattern by t x d -- key or route a phase instead",
                                path, entry.evidence));
    }
    return std::nullopt;
}

std::optional<Finding> SceneLivenessFacts::effectNeverFires(const world::EffectInstance& e) const {
    auto& c = const_cast<Cache&>(cache());
    if (const auto it = c.effectFires.find(e.id); it != c.effectFires.end()) {
        return it->second;
    }
    std::optional<Finding> result;
    const auto never = [&](std::string reason) { result = make("effect-never-fires", Verdict::Dead, std::move(reason)); };
    if (!e.enabled) {
        result = make("disabled", Verdict::Dead, "the effect is switched off");
    } else if (e.owner.kind == world::EffectTarget::Entity && in_.composition != nullptr) {
        const auto& nodes = in_.composition->nodes();
        const auto& heroes = in_.composition->heroes();
        const bool present = std::any_of(nodes.begin(), nodes.end(), [&](const auto& n) { return n->name == e.owner.name; }) ||
                             std::any_of(heroes.begin(), heroes.end(), [&](const auto& h) { return h.name == e.owner.name; });
        if (!present) {
            never(fmt::format("its owner '{}' is not a node or hero of this scene", e.owner.name));
        }
    }
    if (!result) {
        const double duration = in_.durationSeconds;
        switch (e.activation) {
        case world::Activation::Always:
            break;
        case world::Activation::Window:
            if (!(e.timing.windowSeconds > 0.0)) {
                never("its activation window is empty");
            } else if (duration > 0.0 && e.timing.windowStart >= duration) {
                never(fmt::format("its window opens at {:.2f} s, after the piece ends ({:.2f} s)", e.timing.windowStart,
                                  duration));
            } else if (e.timing.windowStart + e.timing.windowSeconds <= 0.0) {
                never("its window closes before the piece starts");
            }
            break;
        case world::Activation::CameraTravel:
            if (in_.shots.empty()) {
                never("camera travel needs the director's shot spans, and the project carries none "
                      "(cameraShotSpans)");
            } else if (std::none_of(in_.shots.begin(), in_.shots.end(), [](const world::ShotSpan& s) { return s.travel; })) {
                never(fmt::format("none of the project's {} shot span(s) is a camera travel", in_.shots.size()));
            }
            break;
        case world::Activation::HeroFocus: {
            if (in_.shots.empty()) {
                never("hero focus needs the director's shot spans, and the project carries none (cameraShotSpans)");
                break;
            }
            // Who the effect fires for: a wave whose source follows focus fires for any spotlit hero;
            // an entity-owned effect otherwise fires only for its own subject (wave_effect.cpp,
            // entity_fx.cpp).
            bool follows = e.owner.kind != world::EffectTarget::Entity;
            std::string subject = e.owner.kind == world::EffectTarget::Entity ? e.owner.name : std::string();
            if (e.kind == world::EffectKind::GroundPulse || e.kind == world::EffectKind::TravelBeam) {
                follows = e.wave.source.kind == world::SourceKind::FocusHero;
                subject = e.wave.source.kind == world::SourceKind::Owner
                              ? (e.owner.kind == world::EffectTarget::Entity ? e.owner.name : std::string())
                              : e.wave.source.name;
            }
            const bool fires = std::any_of(in_.shots.begin(), in_.shots.end(), [&](const world::ShotSpan& s) {
                return s.spotlight && (follows || subject.empty() || s.subject == subject);
            });
            if (!fires) {
                never(subject.empty() || follows
                          ? fmt::format("none of the project's {} shot span(s) spotlights a subject", in_.shots.size())
                          : fmt::format("none of the project's {} shot span(s) spotlights '{}'", in_.shots.size(), subject));
            }
            break;
        }
        case world::Activation::Trigger: {
            if (!c.triggers) {
                c.triggers = std::make_unique<world::TriggerClock>();
                c.triggers->bind(in_.track, in_.markers, in_.history,
                                 duration > 0.0 ? duration : 1e9, in_.meter);
            }
            const std::string_view owner =
                e.owner.kind == world::EffectTarget::Entity ? std::string_view(e.owner.name) : std::string_view();
            std::array<double, 1> last{};
            const double end = duration > 0.0 ? duration : 1e9;
            if (e.timing.trigger.source != world::TriggerSource::Proximity &&
                c.triggers->lastTriggers(e.timing.trigger, owner, end, last) == 0) {
                const char* why = c.triggers->silence(e.timing.trigger, owner);
                never(why != nullptr ? std::string(why)
                                     : fmt::format("its {} trigger has no event before the piece ends",
                                                   world::triggerSourceName(e.timing.trigger.source)));
            }
            break;
        }
        }
    }
    c.effectFires.emplace(e.id, result);
    return result;
}

// ---- the audit ---------------------------------------------------------------------------------

namespace {

const char* routeOrigin(const params::ModRoute& r) {
    if (r.fromEntity) {
        return "entity";
    }
    if (r.fromGraph) {
        return "graph";
    }
    if (r.fromMacro) {
        return "macro";
    }
    return "project";
}

void settle(AuditEntry& entry) {
    entry.verdict = params::liveness::verdictOf(entry.findings);
}

nlohmann::json routeDetail(const params::ModRoute& r, const SceneLivenessFacts& facts) {
    nlohmann::json j;
    j["source"] = r.source;
    j["target"] = r.target;
    j["component"] = r.component;
    j["op"] = std::string(params::modOpName(r.op));
    j["amount"] = static_cast<double>(r.amount);
    j["enabled"] = r.enabled;
    j["delayMs"] = static_cast<double>(r.chain.delayMs);
    j["attackMs"] = static_cast<double>(r.chain.attackMs);
    j["decayMs"] = static_cast<double>(r.chain.decayMs);
    if (!r.depthSource.empty()) {
        j["depthSource"] = r.depthSource;
        j["depthMin"] = static_cast<double>(r.depthMin);
        j["depthMax"] = static_cast<double>(r.depthMax);
    }
    const params::liveness::SignalFacts source = facts.signal(r.source);
    j["sourceIsEvent"] = source.isEvent;
    if (source.exists && source.isEvent) {
        // At the strength the event rule judged: the piece's typical one when known.
        const float full = source.maxValue > 0.0f ? source.maxValue : 1.0f;
        const float strength =
            source.typicalEventStrength > 0.0f && source.typicalEventStrength < full ? source.typicalEventStrength : full;
        if (source.typicalEventStrength > 0.0f) {
            j["typicalEventStrength"] = std::round(static_cast<double>(source.typicalEventStrength) * 10000.0) / 10000.0;
        }
        const auto pass = params::liveness::eventPassThrough(r.chain, r.polarity, strength, facts.frameRate());
        j["eventPassThrough"] = pass ? std::round(static_cast<double>(pass->ratio) * 10000.0) / 10000.0 : 0.0;
    }
    return j;
}

} // namespace

std::string auditReason(const AuditEntry& entry) {
    for (const Finding& f : entry.findings) {
        if (f.verdict == entry.verdict) {
            return f.reason;
        }
    }
    return "no rule found a way for it to miss the picture";
}

RouteAudit auditProject(const LivenessInputs& inputs, const params::Modulator& modulator,
                        const params::Timeline& timeline) {
    // The facts see the very routes and tracks being audited: a rule may ask what else drives a
    // parameter its target depends on (ADR-916: what lifts a water's tear amount).
    LivenessInputs in = inputs;
    in.modulator = in.modulator != nullptr ? in.modulator : &modulator;
    in.timeline = in.timeline != nullptr ? in.timeline : &timeline;
    const SceneLivenessFacts facts(in);
    const auto& registry = params::liveness::Registry::standard();
    RouteAudit audit;

    const std::vector<params::ModRoute>& routes = modulator.routes();
    const std::vector<params::Track>& tracks = timeline.tracks();
    const auto set = registry.checkSet(routes, tracks);

    for (std::size_t i = 0; i < routes.size(); ++i) {
        const params::ModRoute& r = routes[i];
        AuditEntry entry;
        entry.index = i;
        entry.label = fmt::format("{} -> {}", r.source, r.target);
        entry.detail = routeDetail(r, facts);
        entry.detail["origin"] = routeOrigin(r);
        entry.findings = registry.checkRoute(r, facts);
        entry.findings.insert(entry.findings.end(), set.routes[i].begin(), set.routes[i].end());
        settle(entry);
        audit.routes.push_back(std::move(entry));
    }

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const params::Track& t = tracks[i];
        AuditEntry entry;
        entry.index = i;
        entry.label = fmt::format("track -> {}", t.target);
        entry.detail = {{"target", t.target},
                        {"component", t.component},
                        {"mode", params::trackModeName(t.mode)},
                        {"timeBase", params::timeBaseName(t.timeBase)},
                        {"keys", t.keys.size()},
                        {"enabled", t.enabled}};
        if (!timeline.enabled) {
            entry.findings.push_back(make("disabled", Verdict::Dead, "the project's timeline is switched off"));
        }
        auto found = registry.checkTrack(t, facts);
        entry.findings.insert(entry.findings.end(), found.begin(), found.end());
        entry.findings.insert(entry.findings.end(), set.tracks[i].begin(), set.tracks[i].end());
        settle(entry);
        audit.tracks.push_back(std::move(entry));
    }

    for (std::size_t e = 0; e < inputs.effects.size(); ++e) {
        const world::EffectInstance& effect = inputs.effects[e];
        auto defaults = world::defaultEffectRoutes(effect);
        if (!defaults) {
            AuditEntry entry;
            entry.index = audit.effectDefaultRoutes.size();
            entry.label = fmt::format("default routes of effect '{}'", effect.id);
            entry.detail = {{"effect", effect.id}, {"effectType", world::effectKindName(effect.kind)}};
            entry.findings.push_back(make("unknown-source", Verdict::Dead, defaults.error().message));
            settle(entry);
            audit.effectDefaultRoutes.push_back(std::move(entry));
            continue;
        }
        for (const params::ModRoute& r : *defaults) {
            AuditEntry entry;
            entry.index = audit.effectDefaultRoutes.size();
            entry.label = fmt::format("default {} -> {}", r.source, r.target);
            entry.detail = routeDetail(r, facts);
            entry.detail["effect"] = effect.id;
            entry.detail["effectType"] = world::effectKindName(effect.kind);
            entry.detail["installed"] = std::any_of(routes.begin(), routes.end(), [&](const params::ModRoute& x) {
                return x.source == r.source && x.target == r.target && x.component == r.component;
            });
            entry.findings = registry.checkRoute(r, facts);
            settle(entry);
            audit.effectDefaultRoutes.push_back(std::move(entry));
        }
    }

    for (std::size_t e = 0; e < inputs.effects.size(); ++e) {
        const world::EffectInstance& effect = inputs.effects[e];
        AuditEntry entry;
        entry.index = e;
        entry.label = fmt::format("effect '{}'", effect.id);
        entry.detail = {{"id", effect.id},
                        {"type", world::effectKindName(effect.kind)},
                        {"owner", effect.owner.label()},
                        {"activation", world::activationName(effect.activation)},
                        {"enabled", effect.enabled}};
        if (auto why = facts.effectNeverFires(effect)) {
            entry.findings.push_back(std::move(*why));
        }
        settle(entry);
        audit.effects.push_back(std::move(entry));
    }
    return audit;
}

nlohmann::json routeAuditToJson(const RouteAudit& audit, const LivenessInputs& inputs, std::string_view project) {
    using nlohmann::json;
    json doc;
    doc["format"] = "avgen-route-audit";
    doc["version"] = 1;
    doc["project"] = std::string(project);
    doc["tier"] = "configured";
    doc["frameRate"] = inputs.frameRate;
    doc["durationSeconds"] = inputs.durationSeconds;
    doc["hasAudio"] = inputs.hasAudio;
    doc["verdicts"] = {
        {"live", "binds, and no rule can show it failing to reach the picture (connected; whether it visibly "
                 "moves anything is the behavioural tier's question)"},
        {"dead", "a rule shows it cannot reach the picture"},
        {"hazard", "it reaches the picture in a way that is almost certainly not what was meant"}};
    json rules = json::array();
    for (const auto& r : params::liveness::Registry::standard().rules()) {
        rules.push_back({{"id", std::string(r.id)},
                         {"appliesTo", std::string(r.appliesTo)},
                         {"verdicts", std::string(r.verdicts)},
                         {"summary", std::string(r.summary)}});
    }
    doc["rules"] = std::move(rules);

    std::map<std::string, int> byRule;
    const auto list = [&byRule](const std::vector<AuditEntry>& entries, json& summary) {
        json out = json::array();
        int live = 0;
        int dead = 0;
        int hazard = 0;
        for (const AuditEntry& e : entries) {
            json j = e.detail;
            j["index"] = e.index;
            j["verdict"] = params::liveness::verdictName(e.verdict);
            j["reason"] = auditReason(e);
            json findings = json::array();
            for (const Finding& f : e.findings) {
                findings.push_back({{"rule", f.rule}, {"verdict", params::liveness::verdictName(f.verdict)}, {"reason", f.reason}});
                ++byRule[f.rule];
            }
            j["findings"] = std::move(findings);
            out.push_back(std::move(j));
            (e.verdict == Verdict::Dead ? dead : e.verdict == Verdict::Hazard ? hazard : live) += 1;
        }
        summary = {{"total", entries.size()}, {"live", live}, {"dead", dead}, {"hazard", hazard}};
        return out;
    };
    json summary;
    json routesSummary;
    json tracksSummary;
    json defaultsSummary;
    json effectsSummary;
    doc["routes"] = list(audit.routes, routesSummary);
    doc["tracks"] = list(audit.tracks, tracksSummary);
    doc["effectDefaultRoutes"] = list(audit.effectDefaultRoutes, defaultsSummary);
    doc["effects"] = list(audit.effects, effectsSummary);
    summary["routes"] = routesSummary;
    summary["tracks"] = tracksSummary;
    summary["effectDefaultRoutes"] = defaultsSummary;
    summary["effects"] = effectsSummary;
    summary["findingsByRule"] = byRule;
    doc["summary"] = std::move(summary);
    return doc;
}

} // namespace avgen::scene
