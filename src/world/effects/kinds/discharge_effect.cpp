// Discharge (Effect Library Wave 3, catalog-energy.md "Discharge"): the release -- an instant flash,
// a burst of bolts radiating from the owner, and sparks sprayed out, all gone within about half a
// second. Often paired with a Shockwave or a Charge-Up on the same trigger.
//
// **Everything is a function of the event's age** (`age = t - t0`, TRIGGER): the bolts' directions
// are hashed from (seed, the event's instant, k) and drawn at full brightness on the event, stuttering
// and fading over `boltDuration`; the flash (LIGHTMOD) decays in ~70 ms; the sparks are BALLISTIC --
// each a streak along `p(age) = origin + v age + g age^2 / 2` from where the owner was at the event
// (HIST), with a hashed velocity and life. So a scrub into the middle of a discharge lands on the
// same bolts, the same flash and the same sparks a play shows.
//
// **Why the sparks are not EMIT.** The catalog puts them in the particle system, and names the cost:
// a particle burst is a frame event, the one part of a discharge that would not survive a seek. Drawn
// as ribbon streaks they are exact under scrub, cost one strip for the whole spray, need no hook into
// the particle builder, and a spark's short life is what a streak is best at. What they give up is
// the particle renderer's soft-particle depth fade, which a thread-thin streak does not need.
//
// **Where.** From the Source (the owner by default; a world point for a World owner). The bolts leave
// the owner's bounds and end `radius` metres out, tilted down by `downward` (a discharge that grounds).

#include "world/effects/effect_lights.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/kinds/bolt_rows.hpp"
#include "world/effects/kinds/stored_rows.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace bolt_rows;

constexpr EffectField kFields[] = {
    storedFloat("bolts", "Bolts", 6.0f, 1.0f, 8.0f, 1.0f, 8.0f).fmt("%.0f").main().sec("Burst").clampTo(1.0f, 8.0f)
        .tooltip("How many bolts leave the owner at each discharge."),
    storedFloat("radius", "Reach", 9.0f, 0.2f, 2000.0f, 1.0f, 60.0f).fmt("%.1f m").log().main().floorAt(0.2f)
        .tooltip("How far from the owner's centre the bolts end."),
    storedFloat("downward", "Grounding", 0.3f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How strongly the bolts bend towards the ground: 0 radiates evenly, 1 mostly strikes\n"
                 "down."),
    storedFloat("boltDuration", "Bolt life", 0.4f, 0.03f, 5.0f, 0.1f, 1.5f).fmt("%.2f s").main().floorAt(0.03f),
    storedFloat("jaggedness", "Jaggedness", 0.3f, 0.0f, 1.0f, 0.05f, 0.5f).main(),
    storedFloat("branchProbability", "Branching", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f).main(),
    storedColor("color", "Colour", glm::vec3(0.5f, 0.72f, 1.0f)).main().sec("Light"),
    storedFloat("intensity", "Bolt brightness", 50.0f, 0.0f, 1000.0f, 0.0f, 150.0f).main().floorAt(0.0f),
    storedFloat("flashIntensity", "Flash", 1200.0f, 0.0f, 10000000.0f, 0.0f, 100000.0f).main().floorAt(0.0f)
        .tooltip("The discharge's light on its surroundings (candela; 0 = none)."),
    storedFloat("coreWidth", "Bolt width", 0.08f, 0.002f, 10.0f, 0.01f, 0.5f).fmt("%.3f m").log().sec("Bolt shape").floorAt(0.002f),
    storedFloat("glowIntensity", "Glow brightness", 1.2f, 0.0f, 200.0f, 0.0f, 20.0f).floorAt(0.0f),
    storedFloat("glowWidth", "Glow width", 0.6f, 0.02f, 100.0f, 0.1f, 6.0f).fmt("%.2f m").log().floorAt(0.02f),
    storedFloat("flashRange", "Flash reach", 60.0f, 1.0f, 5000.0f, 5.0f, 300.0f).fmt("%.0f m").log().floorAt(1.0f),
    storedFloat("sparkCount", "Sparks", 24.0f, 0.0f, 64.0f, 0.0f, 64.0f).fmt("%.0f").main().sec("Sparks").clampTo(0.0f, 64.0f),
    storedFloat("sparkSpeed", "Spark speed", 12.0f, 0.0f, 200.0f, 0.0f, 40.0f).fmt("%.1f m/s").main().floorAt(0.0f),
    storedFloat("sparkLife", "Spark life", 0.7f, 0.05f, 5.0f, 0.1f, 2.0f).fmt("%.2f s").sec("Spark shape").floorAt(0.05f),
    storedFloat("sparkIntensity", "Spark brightness", 14.0f, 0.0f, 500.0f, 0.0f, 50.0f).floorAt(0.0f),
    storedFloat("seed", "Seed", 1.0f, 0.0f, 100000.0f, 0.0f, 100.0f).fmt("%.0f").floorAt(0.0f),
};

constexpr kinds::StoredRows kRows{"discharge", kFields};

constexpr std::size_t kMaxEvents = 2;
constexpr float kGravity = 9.81f;
constexpr float kFlashTau = 0.07f;
constexpr float kStreakSeconds = 0.035f; // a spark's streak: where it was this long ago to where it is

float life(const E& e) {
    return std::max(std::max(kRows.f(e, "boltDuration"), 0.03f),
                    kRows.f(e, "sparkCount") >= 0.5f ? std::max(kRows.f(e, "sparkLife"), 0.05f) : 0.0f);
}

// The discharges in the air now (newest first), and the instance's envelope.
std::size_t liveEvents(const E& e, const EffectContext& ctx, std::array<double, kMaxEvents>& out, float& envelope) {
    if (!e.enabled || (e.owner.kind != EffectTarget::World && e.owner.kind != EffectTarget::Entity)) {
        return 0;
    }
    if (!windowEnvelope(e, ctx, envelope)) {
        return 0;
    }
    std::array<double, kMaxEvents> times{};
    const std::size_t n = effectEventTimes(e, ctx, times);
    const double span = static_cast<double>(life(e));
    std::size_t kept = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double age = ctx.seconds - times[i];
        if (age >= 0.0 && age < span) {
            out[kept++] = times[i];
        }
    }
    return kept;
}

glm::vec3 unitFrom(std::uint64_t seed, std::uint32_t event, std::uint32_t k, std::uint32_t channel) {
    glm::vec3 d(boltHash(seed, event, k, channel, 0u) * 2.0f - 1.0f, boltHash(seed, event, k, channel, 1u) * 2.0f - 1.0f,
                boltHash(seed, event, k, channel, 2u) * 2.0f - 1.0f);
    const float l = glm::length(d);
    return l > 0.05f ? d / l : glm::vec3(0.0f, 1.0f, 0.0f);
}

EffectStatus emit(const E& e, const EffectContext& ctx, const HistoryBank&, RibbonSink& sink, std::string& reason) {
    std::array<double, kMaxEvents> events{};
    float envelope = 0.0f;
    const std::size_t n = liveEvents(e, ctx, events, envelope);
    Resolved src;
    if (n == 0 || !resolveEndpoint(e, e.wave.source, ctx, src)) {
        if (const char* waiting = effectTriggerDormancy(e, ctx); waiting != nullptr && reason.empty()) {
            reason.assign(waiting);
        }
        return EffectStatus::Dormant;
    }
    static thread_local std::vector<RibbonPoint> scratch;
    static thread_local std::vector<BoltStreak> sparks;
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const glm::vec3 color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f));
    const int bolts = std::clamp(static_cast<int>(kRows.f(e, "bolts") + 0.5f), 1, 8);
    const float radius = std::max(kRows.f(e, "radius"), 0.2f);
    const float down = std::clamp(kRows.f(e, "downward"), 0.0f, 1.0f);
    const float duration = std::max(kRows.f(e, "boltDuration"), 0.03f);
    BoltParams params;
    params.depth = 5;
    params.jaggedness = std::clamp(kRows.f(e, "jaggedness"), 0.0f, 1.0f);
    params.branchProbability = std::clamp(kRows.f(e, "branchProbability"), 0.0f, 1.0f);
    params.branchDecay = 0.45f;
    params.generations = 1;
    std::size_t drawn = 0;
    std::size_t reduced = 0;
    std::size_t refused = 0;
    for (std::size_t i = n; i-- > 0;) {
        const double t0 = events[i];
        const float age = static_cast<float>(ctx.seconds - t0);
        const std::uint32_t event = boltEventIndex(t0);
        // ---- the bolts ----
        if (age < duration) {
            const float x = age / duration;
            // A stutter 30 times a second, fading out.
            const auto tick = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(age * 30.0f)));
            for (int k = 0; k < bolts; ++k) {
                const auto kk = static_cast<std::uint32_t>(k);
                glm::vec3 d = unitFrom(seed, event, kk, 1u) + glm::vec3(0.0f, -1.6f * down, 0.0f);
                const float dl = glm::length(d);
                d = dl > 1e-4f ? d / dl : glm::vec3(0.0f, -1.0f, 0.0f);
                BoltPlacement placement;
                placement.start = bodyExit(src, src.point + d * 1e4f);
                const float reachOut = std::max(radius * (0.7f + 0.3f * boltHash(seed, event, kk, 3u, 0u)),
                                                glm::length(placement.start - src.point) + 0.3f);
                placement.end = src.point + d * reachOut;
                const float flick = 0.55f + 0.45f * boltHash(seed, event, kk, 4u, tick);
                const float brightness = (1.0f - x) * (1.0f - x) * flick;
                BoltParams p = params;
                p.depth = boltLodDepth(params.depth, placement.length(),
                                       chordDistance(placement.start, placement.end, ctx.cameraPosition));
                const BoltPath& path = boltCache().get(p, seed, (event * 8u) + kk);
                BoltLook look;
                look.core = (color * 0.35f + glm::vec3(0.65f)) * std::max(kRows.f(e, "intensity"), 0.0f) * brightness;
                look.glow = color * std::max(kRows.f(e, "glowIntensity"), 0.0f) * brightness;
                look.coreWidth = std::max(kRows.f(e, "coreWidth"), 0.002f);
                look.glowWidth = std::max(kRows.f(e, "glowWidth"), look.coreWidth);
                look.opacity = envelope;
        look.eye = ctx.cameraPosition;
        look.hasEye = true;
                // The bolts leap out over the first 40 ms.
                look.reveal = path.maxS * std::clamp(age / 0.04f, 0.02f, 1.0f) + (age >= 0.04f ? 1e29f : 0.0f);
                drawBolt(sink, p, seed, (event * 8u) + kk, placement, look, scratch, drawn, reduced, refused);
            }
        }
        // ---- the sparks ----
        const int count = std::clamp(static_cast<int>(kRows.f(e, "sparkCount") + 0.5f), 0, 64);
        const float sparkLife = std::max(kRows.f(e, "sparkLife"), 0.05f);
        if (count == 0 || age >= sparkLife) {
            continue;
        }
        // Released where the owner WAS at the event (HIST), so a moving owner leaves its sparks behind.
        Resolved origin = src;
        if (e.owner.kind == EffectTarget::Entity && e.wave.source.kind == SourceKind::Owner && ctx.scene != nullptr) {
            NodeView view;
            glm::vec3 then;
            if (ctx.scene->nodeView(e.owner.name, view) && ctx.scene->nodeDrawnPosition(e.owner.name, t0, then)) {
                const glm::vec3 shift = then - glm::vec3(view.world[3]);
                origin.point += shift;
                origin.lo += shift;
                origin.hi += shift;
            }
        }
        const float speed = std::max(kRows.f(e, "sparkSpeed"), 0.0f);
        const float sparkI = std::max(kRows.f(e, "sparkIntensity"), 0.0f);
        const glm::vec3 g(0.0f, -kGravity, 0.0f);
        sparks.clear();
        for (int s = 0; s < count; ++s) {
            const auto ss = static_cast<std::uint32_t>(s) + 100u;
            const float own = sparkLife * (0.45f + 0.55f * boltHash(seed, event, ss, 5u, 0u));
            if (age >= own) {
                continue;
            }
            glm::vec3 d = unitFrom(seed, event, ss, 6u);
            d.y = std::abs(d.y) * 0.8f + 0.2f; // sprayed up and out; gravity brings them down
            d = glm::normalize(d);
            const glm::vec3 v = d * (speed * (0.35f + 0.65f * boltHash(seed, event, ss, 7u, 0u)));
            const glm::vec3 o = bodyExit(origin, origin.point + d * 1e4f);
            const float a1 = age;
            const float a0 = std::max(age - kStreakSeconds, 0.0f);
            BoltStreak streak;
            streak.head = o + v * a1 + 0.5f * g * a1 * a1;
            streak.tail = o + v * a0 + 0.5f * g * a0 * a0;
            const float y = age / own;
            streak.color = glm::mix(glm::vec3(1.0f, 0.97f, 0.9f), color, std::min(y * 1.5f, 1.0f)) * sparkI;
            streak.opacity = std::pow(1.0f - y, 1.5f) * envelope;
            streak.width = 0.05f;
            sparks.push_back(streak);
        }
        switch (appendStreaks(sink, sparks, scratch)) {
        case BoltFit::Written: ++drawn; break;
        case BoltFit::CoreOnly: ++reduced; break;
        case BoltFit::NoRoom: ++refused; break;
        case BoltFit::Nothing: break;
        }
    }
    return ribbonStatus(drawn, reduced, refused, reason);
}

std::size_t records(const E& e, const EffectContext& ctx) {
    std::array<double, kMaxEvents> events{};
    float envelope = 0.0f;
    Resolved src;
    return liveEvents(e, ctx, events, envelope) > 0 && resolveEndpoint(e, e.wave.source, ctx, src) ? 1u : 0u;
}

bool light(const E& e, const EffectContext& ctx, EffectLight& out) {
    const float intensity = std::max(kRows.f(e, "flashIntensity"), 0.0f);
    std::array<double, kMaxEvents> events{};
    float envelope = 0.0f;
    Resolved src;
    if (!(intensity > 0.0f) || liveEvents(e, ctx, events, envelope) == 0 || !resolveEndpoint(e, e.wave.source, ctx, src)) {
        return false;
    }
    const float age = static_cast<float>(ctx.seconds - events[0]);
    const float duration = std::max(kRows.f(e, "boltDuration"), 0.03f);
    // The flash, then the bolts' own light while they last.
    const float x = std::clamp(age / duration, 0.0f, 1.0f);
    const float level = std::exp(-age / kFlashTau) + 0.25f * (1.0f - x) * (1.0f - x);
    if (level < 1e-3f) {
        return false;
    }
    out.position = src.point;
    out.intensity = intensity * level * envelope;
    out.color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f)) * 0.6f + glm::vec3(0.4f);
    out.range = std::max(kRows.f(e, "flashRange"), 1.0f);
    out.volumetric = 0.6f;
    return true;
}

// The spark origins read the owner's past position (HIST): as far back as a spark lives.
float historySeconds(const E& e) {
    return e.owner.kind == EffectTarget::Entity ? life(e) + 0.1f : 0.0f;
}

// ---- presets ------------------------------------------------------------------------------------------

Trigger onsets(float threshold) {
    Trigger t;
    t.source = TriggerSource::Onset;
    t.threshold = threshold;
    return t;
}
Trigger beats(int everyN) {
    Trigger t;
    t.source = TriggerSource::Beat;
    t.everyN = everyN;
    return t;
}

struct Look {
    const char* name;
    float bolts, radius, down, duration, intensity, sparks, speed, flash;
    glm::vec3 color;
    Trigger trigger;
};

const Look kLooks[] = {
    // Overload: everything at once -- eight long bolts, a big flash, a fountain of sparks.
    {"Overload", 8.0f, 14.0f, 0.25f, 0.5f, 70.0f, 48.0f, 16.0f, 1500.0f, {0.5f, 0.72f, 1.0f}, beats(4)},
    // On the bass: bolts that ground themselves, a heavy flash, few sparks.
    {"Bass Discharge", 5.0f, 10.0f, 0.8f, 0.35f, 55.0f, 16.0f, 8.0f, 1200.0f, {0.62f, 0.5f, 1.0f}, onsets(1.4f)},
    // A static pop: tiny, quick, white.
    {"Static Pop", 3.0f, 2.5f, 0.1f, 0.15f, 30.0f, 12.0f, 5.0f, 400.0f, {0.8f, 0.88f, 1.0f}, onsets(1.0f)},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "bolts", l.bolts);
    kRows.set(e, "radius", l.radius);
    kRows.set(e, "downward", l.down);
    kRows.set(e, "boltDuration", l.duration);
    kRows.set(e, "intensity", l.intensity);
    kRows.set(e, "sparkCount", l.sparks);
    kRows.set(e, "sparkSpeed", l.speed);
    kRows.set(e, "flashIntensity", l.flash);
    kRows.setRgb(e, "color", l.color);
    e.activation = Activation::Trigger;
    e.timing.trigger = l.trigger;
    e.timing.lifetime = 0.0;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {"Overload", style0},
    {"Bass Discharge", style1},
    {"Static Pop", style2},
};

// What fires it is the trigger; how hard is the level at that moment, and a sharp attack throws more
// sparks (catalog 2.7).
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "intensity", 50.0f, 10.0f, 500.0f},
    {"audio.onset", "sparkCount", 16.0f, 5.0f, 400.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Discharge;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.wave.source = EffectEndpoint{};
    e.wave.source.kind = SourceKind::Owner;
    e.wave.hasTarget = false;
    // The plain type: every fourth beat.
    e.activation = Activation::Trigger;
    e.timing.trigger = beats(4);
    return e;
}

// A discharge has a Source (where it bursts from) and no Target; the schema declares the target
// hooks all-or-none, so it declares none and writes only the source.
void writeExtra(const E& e, nlohmann::json& block) { block["source"] = waveEndpointToJson(e.wave.source); }
Result<void> readExtra(E& e, const nlohmann::json& block) {
    if (block.contains("source")) {
        auto src = waveEndpointFromJson(block.at("source"));
        if (!src) {
            return fail("source: {}", src.error().message);
        }
        e.wave.source = *src;
    }
    e.wave.hasTarget = false;
    return {};
}
Result<void> validate(const E& e) { return e.wave.source.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Discharge;
    s.key = "discharge";
    s.enumName = "Discharge";
    s.displayName = "Discharge";
    s.description =
        "A release of stored energy: on a trigger, a flash of light, a burst of bolts radiating from "
        "the owner (or bending down to ground) and a spray of sparks, all gone within about half a "
        "second. Every discharge is the same however the timeline reaches it.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCpu | CostVertex | CostFragment;
    s.addLabel = "Discharge";
    s.addTip = "A burst of bolts, sparks and a flash from this entity, on the beat or an onset.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "intensity";
    s.factory = make;
    s.getSource = bolt_rows::getSource;
    s.setSource = bolt_rows::setSource;
    s.writeExtra = writeExtra;
    s.readExtra = readExtra;
    s.validate = validate;
    s.resolve.bucket = EffectBucket::Ribbon;
    s.resolve.records = records;
    s.resolve.light = light;
    registerRibbonProducer(EffectKind::Discharge, RibbonProducer{emit, historySeconds});
    return s;
}

} // namespace

const EffectSchema& dischargeSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
