// Lightning (Effect Library Wave 3, catalog-energy.md "Lightning"): a discrete strike -- a branching
// channel from the sky (or a thing) to the ground (or a thing), with the temporal structure a real
// strike has, lighting the world while it lasts.
//
// **The strike, as a function of its age** (`age = t - t0`, t0 the event that fired it; no state):
//   1. the stepped LEADER: a dim channel growing from the cloud end over `leader` seconds -- an
//      arc-length reveal of the whole branching bolt, so branches appear as the leader passes them;
//   2. the RETURN STROKE: the whole channel at full brightness, then decaying in ~45 ms;
//   3. RESTRIKES: `restrikes` more strokes over `strokeDuration`, each dimmer, down the MAIN channel
//      only (the branches barely relight, as in a real flash), with dark gaps between;
//   4. the AFTERGLOW: a faint channel fading over `afterglow` seconds.
// A LIGHTMOD flash (a pool point light a third of the way up the channel) follows the same curve, so
// the valley flickers with the strokes.
//
// **Shape.** BOLT (world/effects/bolt_path): midpoint displacement with branches, keyed by (the
// instance's seed, the strike's own instant) -- so a strike scrubbed to is the strike played to, bit
// for bit -- placed between the endpoints. Level of detail by the strike's size on screen.
//
// **Where.** The Source is what is struck (the owner by default). Without a Target the bolt comes
// down from `height` metres above it, leaning up to `lean` metres, scattered per strike over
// `scatter` metres; with a Target it runs from the Source to the Target (an entity smiting another,
// a sprite shooting upward). A body endpoint is met at its bounds, not its centre.
//
// **Drawn** through RIBBON as two additive strips per strike (a Gaussian glow, a hot core clamped to
// 1.5 px), depth-tested, fogged, into the emission target so it blooms; camera-only velocity.
//
// **Not here** (catalog items that need hooks this slice does not own): the flash casting shadows
// (pool lights are unshadowed), a sky/ambient exposure flash, and snapping a target to the terrain.

#include "world/effects/effect_lights.hpp"
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
    // ---- the bolt ----
    storedFloat("jaggedness", "Jaggedness", 0.26f, 0.0f, 1.0f, 0.05f, 0.5f).main().sec("Bolt")
        .tooltip("How far the channel zig-zags: each segment's midpoint is pushed sideways by about\n"
                 "this fraction of the segment, at every scale."),
    storedFloat("branchProbability", "Branching", 0.55f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How readily the channel forks. Forks are commoner near the cloud."),
    storedFloat("branchDecay", "Branch length", 0.5f, 0.1f, 0.9f, 0.2f, 0.8f).main()
        .tooltip("A branch's length and brightness relative to what it leaves."),
    storedFloat("depth", "Detail", 7.0f, 2.0f, 8.0f, 3.0f, 8.0f).fmt("%.0f").main().clampTo(2.0f, 8.0f)
        .tooltip("Levels of midpoint refinement: 7 is 128 segments on the main channel. Lowered\n"
                 "automatically for a strike that is small on screen."),
    storedFloat("height", "Height", 90.0f, 1.0f, 5000.0f, 10.0f, 400.0f).fmt("%.0f m").log().main().floorAt(1.0f)
        .tooltip("Without a Target: how far above the Source the strike comes from. Negative\n"
                 "directions are a Target's job (a Red Sprite shoots up)."),
    storedFloat("lean", "Lean", 18.0f, 0.0f, 5000.0f, 0.0f, 200.0f).fmt("%.0f m").main().floorAt(0.0f)
        .tooltip("Without a Target: how far sideways the cloud end may sit, a different way per strike."),
    storedFloat("scatter", "Scatter", 0.0f, 0.0f, 5000.0f, 0.0f, 300.0f).fmt("%.0f m").main().floorAt(0.0f)
        .tooltip("How far each strike lands from the Source: 0 always hits it, 200 wanders a storm\n"
                 "over a valley."),
    storedColor("coreColor", "Core colour", glm::vec3(0.86f, 0.9f, 1.0f)).main().sec("Light"),
    storedFloat("coreIntensity", "Core brightness", 60.0f, 0.0f, 1000.0f, 0.0f, 200.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness of the white-hot channel (20-200 is lightning)."),
    storedFloat("coreWidth", "Core width", 0.3f, 0.005f, 20.0f, 0.02f, 2.0f).fmt("%.2f m").log().main().floorAt(0.005f)
        .tooltip("Never drawn thinner than 1.5 pixels, so a distant strike is a thread, not dashes."),
    storedColor("glowColor", "Glow colour", glm::vec3(0.42f, 0.58f, 1.0f)).main(),
    storedFloat("glowIntensity", "Glow brightness", 1.5f, 0.0f, 200.0f, 0.0f, 20.0f).main().floorAt(0.0f),
    storedFloat("glowWidth", "Glow width", 5.0f, 0.05f, 200.0f, 0.2f, 20.0f).fmt("%.1f m").log().main().floorAt(0.05f),
    storedFloat("flashIntensity", "Flash", 20000.0f, 0.0f, 10000000.0f, 0.0f, 200000.0f).main().floorAt(0.0f)
        .tooltip("The strike's light on the world (candela, a pool point light a third of the way up\n"
                 "the channel). 0 draws the bolt without lighting anything."),
    storedFloat("flashRange", "Flash reach", 220.0f, 1.0f, 10000.0f, 20.0f, 1000.0f).fmt("%.0f m").log().sec("Flash").floorAt(1.0f),
    storedFloat("flashFog", "Flash in fog", 0.6f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How much the flash lights the volumetric fog."),
    // ---- the strike's timing ----
    storedFloat("leader", "Leader", 0.07f, 0.0f, 2.0f, 0.0f, 0.3f).fmt("%.2f s").sec("Strike").floorAt(0.0f)
        .tooltip("How long the dim leader takes to grow down before the stroke."),
    storedFloat("restrikes", "Restrikes", 2.0f, 0.0f, 4.0f, 0.0f, 4.0f).fmt("%.0f").clampTo(0.0f, 4.0f)
        .tooltip("Strokes after the first, each down the main channel again after a dark gap."),
    storedFloat("strokeDuration", "Flicker span", 0.32f, 0.02f, 3.0f, 0.05f, 1.0f).fmt("%.2f s").floorAt(0.02f)
        .tooltip("Seconds from the first stroke to the last restrike."),
    storedFloat("afterglow", "Afterglow", 0.35f, 0.0f, 5.0f, 0.0f, 1.5f).fmt("%.2f s").floorAt(0.0f)
        .tooltip("How long the channel's faint glow lingers after the last stroke."),
    storedFloat("seed", "Seed", 1.0f, 0.0f, 100000.0f, 0.0f, 100.0f).fmt("%.0f").floorAt(0.0f)
        .tooltip("A different seed is a different storm; the same seed is the same strikes, always."),
};

constexpr kinds::StoredRows kRows{"lightning", kFields};

// The stroke time constant: a stroke is gone in about four of these.
constexpr float kStrokeTau = 0.045f;
constexpr std::size_t kMaxStrikes = 2; // a new strike may land while the last one's afterglow lingers

// ---- one strike, as a function of its age ----------------------------------------------------------

struct Strike {
    double t0 = 0.0;
    float brightness = 0.0f; // 1 = the return stroke's peak
    float branchGain = 1.0f; // how lit the branches are relative to the main channel
    float reveal = 1e30f;    // the leader's reach, as a fraction of the bolt's full arc length
    bool live = false;
};

Strike strikeAt(const E& e, double t0, double seconds) {
    Strike s;
    s.t0 = t0;
    const float age = static_cast<float>(seconds - t0);
    if (age < 0.0f) {
        return s;
    }
    const float leader = std::max(kRows.f(e, "leader"), 0.0f);
    const int restrikes = std::clamp(static_cast<int>(kRows.f(e, "restrikes") + 0.5f), 0, 4);
    const float span = std::max(kRows.f(e, "strokeDuration"), 0.02f);
    const float afterglow = std::max(kRows.f(e, "afterglow"), 0.0f);
    if (age < leader) {
        const float x = age / leader;
        s.reveal = x * x * (3.0f - 2.0f * x) * 0.999f + 0.001f;
        s.brightness = 0.12f + 0.1f * x;
        s.branchGain = 1.0f;
        s.live = true;
        return s;
    }
    const float a = age - leader;
    const float gap = restrikes > 0 ? span / static_cast<float>(restrikes) : 0.0f;
    const float last = gap * static_cast<float>(restrikes);
    float first = std::exp(-a / kStrokeTau);
    float total = first;
    for (int k = 1; k <= restrikes; ++k) {
        const float tk = gap * static_cast<float>(k);
        if (a < tk) {
            break;
        }
        // Each restrike a little weaker than the last, by a per-strike amount.
        const float amp = 0.8f * std::pow(0.82f, static_cast<float>(k - 1)) *
                          (0.75f + 0.25f * boltHash(0x51u, boltEventIndex(t0), static_cast<std::uint32_t>(k), 0u, 1u));
        total += amp * std::exp(-(a - tk) / kStrokeTau);
    }
    // The faint channel between strokes and after the last one.
    float glow = 0.0f;
    if (a <= last) {
        glow = 0.08f;
    } else if (afterglow > 0.0f && a < last + afterglow) {
        const float x = 1.0f - (a - last) / afterglow;
        glow = 0.08f * x * x;
    }
    s.brightness = std::max(total, glow);
    if (s.brightness < 2e-3f) {
        return s;
    }
    // Branches take the first stroke fully and a quarter of the restrikes.
    s.branchGain = std::clamp((first + 0.25f * (total - first) + 0.5f * glow) / std::max(s.brightness, 1e-6f), 0.0f, 1.0f);
    s.live = true;
    return s;
}

// The strikes in the air now, newest first, and the instance's envelope.
std::size_t liveStrikes(const E& e, const EffectContext& ctx, std::array<Strike, kMaxStrikes>& out, float& envelope) {
    if (!e.enabled || (e.owner.kind != EffectTarget::World && e.owner.kind != EffectTarget::Entity)) {
        return 0;
    }
    if (!windowEnvelope(e, ctx, envelope)) {
        return 0;
    }
    std::array<double, kMaxStrikes> times{};
    const std::size_t n = effectEventTimes(e, ctx, times);
    std::size_t live = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Strike s = strikeAt(e, times[i], ctx.seconds);
        if (s.live) {
            out[live++] = s;
        }
    }
    return live;
}

// Where strike `index` runs: from its cloud end to the point it lands.
bool placeStrike(const E& e, const EffectContext& ctx, std::uint64_t seed, std::uint32_t index, BoltPlacement& out) {
    Resolved src;
    if (!resolveEndpoint(e, e.wave.source, ctx, src)) {
        return false;
    }
    const float scatter = std::max(kRows.f(e, "scatter"), 0.0f);
    const auto disc = [&](std::uint32_t channel) {
        // A point in the unit disc (square-root radius: even coverage), from the strike's own hashes.
        const float r = std::sqrt(boltHash(seed, index, 0x5ca7u, channel, 0u));
        glm::vec2 d(boltHash(seed, index, 0x5ca7u, channel, 1u) * 2.0f - 1.0f,
                    boltHash(seed, index, 0x5ca7u, channel, 2u) * 2.0f - 1.0f);
        const float l = glm::length(d);
        d = l > 0.05f ? d / l : glm::vec2(1.0f, 0.0f);
        return d * r;
    };
    glm::vec3 top;
    glm::vec3 bottom;
    if (e.wave.hasTarget) {
        Resolved dst;
        if (!resolveEndpoint(e, e.wave.target, ctx, dst)) {
            return false;
        }
        const glm::vec2 j = disc(1u) * scatter;
        top = bodyExit(src, dst.point);
        bottom = bodyExit(dst, src.point) + glm::vec3(j.x, 0.0f, j.y);
    } else {
        const glm::vec2 j = disc(1u) * scatter;
        const glm::vec2 l = disc(2u) * std::max(kRows.f(e, "lean"), 0.0f);
        const float height = std::max(kRows.f(e, "height"), 1.0f);
        const glm::vec3 landing = src.point + glm::vec3(j.x, 0.0f, j.y);
        top = landing + glm::vec3(l.x, height, l.y);
        Resolved struck = src;
        struck.point = landing;
        struck.lo += glm::vec3(j.x, 0.0f, j.y);
        struck.hi += glm::vec3(j.x, 0.0f, j.y);
        bottom = bodyExit(struck, top);
    }
    out.start = top;
    out.end = bottom;
    out.bow = glm::vec3(0.0f);
    return glm::length(out.end - out.start) > 1e-3f;
}

BoltParams shapeOf(const E& e) {
    BoltParams p;
    p.depth = std::clamp(static_cast<int>(kRows.f(e, "depth") + 0.5f), 2, kBoltMaxDepth);
    p.jaggedness = std::clamp(kRows.f(e, "jaggedness"), 0.0f, 1.0f);
    p.branchProbability = std::clamp(kRows.f(e, "branchProbability"), 0.0f, 1.0f);
    p.branchDecay = std::clamp(kRows.f(e, "branchDecay"), 0.1f, 0.9f);
    p.generations = 2;
    return p;
}

// ---- the producer -----------------------------------------------------------------------------------

EffectStatus emit(const E& e, const EffectContext& ctx, const HistoryBank&, RibbonSink& sink, std::string& reason) {
    std::array<Strike, kMaxStrikes> strikes{};
    float envelope = 0.0f;
    const std::size_t n = liveStrikes(e, ctx, strikes, envelope);
    if (n == 0) {
        if (const char* waiting = effectTriggerDormancy(e, ctx); waiting != nullptr && reason.empty()) {
            reason.assign(waiting);
        }
        return EffectStatus::Dormant;
    }
    static thread_local std::vector<RibbonPoint> scratch;
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const BoltParams authored = shapeOf(e);
    const glm::vec3 core = glm::max(kRows.rgb(e, "coreColor"), glm::vec3(0.0f)) * std::max(kRows.f(e, "coreIntensity"), 0.0f);
    const glm::vec3 glow = glm::max(kRows.rgb(e, "glowColor"), glm::vec3(0.0f)) * std::max(kRows.f(e, "glowIntensity"), 0.0f);
    std::size_t drawn = 0;
    std::size_t reduced = 0;
    std::size_t refused = 0;
    // Oldest first, so the newest strike is drawn last (additive: the order changes nothing on screen,
    // but a full budget then refuses the older one).
    for (std::size_t k = n; k-- > 0;) {
        const Strike& s = strikes[k];
        const std::uint32_t index = boltEventIndex(s.t0);
        BoltPlacement placement;
        if (!placeStrike(e, ctx, seed, index, placement)) {
            continue;
        }
        BoltParams params = authored;
        params.depth = boltLodDepth(authored.depth, placement.length(),
                                    chordDistance(placement.start, placement.end, ctx.cameraPosition));
        const BoltPath& path = boltCache().get(params, seed, index);
        BoltLook look;
        look.core = core * s.brightness;
        look.glow = glow * s.brightness;
        look.coreWidth = std::max(kRows.f(e, "coreWidth"), 0.005f);
        look.glowWidth = std::max(kRows.f(e, "glowWidth"), look.coreWidth);
        look.opacity = envelope;
        look.eye = ctx.cameraPosition;
        look.hasEye = true;
        look.branchGain = s.branchGain;
        look.reveal = s.reveal < 1e29f ? s.reveal * path.maxS : 1e30f;
        drawBolt(sink, params, seed, index, placement, look, scratch, drawn, reduced, refused);
    }
    return ribbonStatus(drawn, reduced, refused, reason);
}

std::size_t records(const E& e, const EffectContext& ctx) {
    std::array<Strike, kMaxStrikes> strikes{};
    float envelope = 0.0f;
    const std::size_t n = liveStrikes(e, ctx, strikes, envelope);
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    std::size_t placed = 0;
    for (std::size_t k = 0; k < n; ++k) {
        BoltPlacement p;
        placed += placeStrike(e, ctx, seed, boltEventIndex(strikes[k].t0), p) ? 1u : 0u;
    }
    return std::min<std::size_t>(placed, 1u); // one record: the instance's strip pair
}

// LIGHTMOD: the flash, a third of the way up the brightest live strike's channel.
bool light(const E& e, const EffectContext& ctx, EffectLight& out) {
    const float intensity = std::max(kRows.f(e, "flashIntensity"), 0.0f);
    if (!(intensity > 0.0f)) {
        return false;
    }
    std::array<Strike, kMaxStrikes> strikes{};
    float envelope = 0.0f;
    const std::size_t n = liveStrikes(e, ctx, strikes, envelope);
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    float best = 0.0f;
    for (std::size_t k = 0; k < n; ++k) {
        BoltPlacement p;
        if (!placeStrike(e, ctx, seed, boltEventIndex(strikes[k].t0), p)) {
            continue;
        }
        // The leader barely lights anything; the strokes do.
        const float b = strikes[k].reveal < 1e29f ? 0.02f : strikes[k].brightness;
        if (b > best) {
            best = b;
            out.position = glm::mix(p.start, p.end, 0.67f);
        }
    }
    if (!(best > 1e-3f)) {
        return false;
    }
    out.intensity = intensity * best * envelope;
    out.color = glm::max(glm::mix(kRows.rgb(e, "glowColor"), kRows.rgb(e, "coreColor"), 0.6f), glm::vec3(0.0f));
    out.range = std::max(kRows.f(e, "flashRange"), 1.0f);
    out.volumetric = std::clamp(kRows.f(e, "flashFog"), 0.0f, 1.0f);
    return true;
}

// ---- presets ------------------------------------------------------------------------------------------

Trigger repeat(double period, double phase) {
    Trigger t;
    t.source = TriggerSource::Repeat;
    t.period = period;
    t.phase = phase;
    return t;
}
Trigger music(const char* name) {
    Trigger t;
    t.source = TriggerSource::MusicEvent;
    t.name = name;
    return t;
}

struct Look {
    const char* name;
    float jag, branching, decay, height, lean, scatter;
    glm::vec3 coreColor;
    float coreIntensity, coreWidth;
    glm::vec3 glowColor;
    float glowIntensity, glowWidth, flash, leader, restrikes, span, afterglow;
    Trigger trigger;
};

const Look kLooks[] = {
    // A storm over a valley: tall, heavily branched, wandering, blue-white, flickering three times.
    {"Storm Strike", 0.26f, 0.6f, 0.5f, 140.0f, 30.0f, 120.0f, {0.86f, 0.9f, 1.0f}, 70.0f, 0.4f,
     {0.42f, 0.58f, 1.0f}, 1.4f, 5.0f, 25000.0f, 0.08f, 2.0f, 0.34f, 0.4f, repeat(2.7, 0.4)},
    // On the music's drops: a single hard stroke and a quick release.
    {"Beat Lightning", 0.22f, 0.45f, 0.45f, 90.0f, 14.0f, 40.0f, {0.9f, 0.92f, 1.0f}, 80.0f, 0.3f,
     {0.55f, 0.5f, 1.0f}, 1.8f, 5.0f, 25000.0f, 0.04f, 1.0f, 0.14f, 0.25f, music("drop")},
    // Gold judgement from above onto the owner: straight-ish, few branches, slow to fade.
    {"Divine Smite", 0.14f, 0.3f, 0.4f, 60.0f, 4.0f, 0.0f, {1.0f, 0.93f, 0.7f}, 90.0f, 0.5f,
     {1.0f, 0.68f, 0.22f}, 2.0f, 6.0f, 25000.0f, 0.12f, 0.0f, 0.1f, 0.9f, repeat(4.0, 0.5)},
    // A red sprite: slow, soft and red, a tall jagged tendril (use a Target above to shoot it up).
    {"Red Sprite", 0.34f, 0.7f, 0.6f, 200.0f, 50.0f, 60.0f, {1.0f, 0.55f, 0.5f}, 25.0f, 0.8f,
     {1.0f, 0.12f, 0.18f}, 1.5f, 14.0f, 8000.0f, 0.3f, 0.0f, 0.2f, 1.4f, repeat(5.0, 1.0)},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "jaggedness", l.jag);
    kRows.set(e, "branchProbability", l.branching);
    kRows.set(e, "branchDecay", l.decay);
    kRows.set(e, "height", l.height);
    kRows.set(e, "lean", l.lean);
    kRows.set(e, "scatter", l.scatter);
    kRows.setRgb(e, "coreColor", l.coreColor);
    kRows.set(e, "coreIntensity", l.coreIntensity);
    kRows.set(e, "coreWidth", l.coreWidth);
    kRows.setRgb(e, "glowColor", l.glowColor);
    kRows.set(e, "glowIntensity", l.glowIntensity);
    kRows.set(e, "glowWidth", l.glowWidth);
    kRows.set(e, "flashIntensity", l.flash);
    kRows.set(e, "leader", l.leader);
    kRows.set(e, "restrikes", l.restrikes);
    kRows.set(e, "strokeDuration", l.span);
    kRows.set(e, "afterglow", l.afterglow);
    e.activation = Activation::Trigger;
    e.timing.trigger = l.trigger;
    e.timing.lifetime = 0.0;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {"Storm Strike", style0},
    {"Beat Lightning", style1},
    {"Divine Smite", style2},
    {"Red Sprite", style3},
};

// What fires a strike is its trigger; the music shapes it. Loudness brightens the stroke; the treble
// roughens the channel (catalog 2.7).
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "coreIntensity", 60.0f, 10.0f, 400.0f},
    {"audio.treble", "jaggedness", 0.15f, 20.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Lightning;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.wave.source = EffectEndpoint{};
    e.wave.source.kind = SourceKind::Owner;
    e.wave.hasTarget = false;
    e.wave.target = EffectEndpoint{};
    // The plain type: every two and a half seconds, onto its owner from 90 m up.
    e.activation = Activation::Trigger;
    e.timing.trigger = repeat(2.5, 0.25);
    return e;
}

Result<void> readExtra(E& e, const nlohmann::json& block) { return readEndpoints(e, block); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Lightning;
    s.key = "lightning";
    s.enumName = "Lightning";
    s.displayName = "Lightning";
    s.description =
        "A lightning strike: a branching channel from the sky down to its target, with a leader, a "
        "blinding return stroke, flickering restrikes and a fading afterglow, flashing light over the "
        "world. Fires on a trigger -- a beat, a drop, a marker, a schedule -- and every strike is the "
        "same strike however the timeline reaches it.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCpu | CostVertex | CostFragment;
    s.addLabel = "Lightning";
    s.addTip = "Lightning strikes onto this entity (or a point), on the beat, a drop or a schedule.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.priority = 0;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "coreIntensity";
    s.factory = make;
    s.getSource = getSource;
    s.setSource = setSource;
    s.hasTarget = hasTarget;
    s.getTarget = getTarget;
    s.setTarget = setTarget;
    s.writeExtra = writeEndpoints;
    s.readExtra = readExtra;
    s.validate = validateEndpoints;
    s.resolve.bucket = EffectBucket::Ribbon;
    s.resolve.records = records;
    s.resolve.light = light;
    registerRibbonProducer(EffectKind::Lightning, RibbonProducer{emit, nullptr});
    return s;
}

} // namespace

const EffectSchema& lightningSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
