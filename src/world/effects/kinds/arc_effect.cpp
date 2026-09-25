// Arc (Effect Library Wave 3, catalog-energy.md "Arc"): a CONTINUOUS electric arc between two
// endpoints that stays alive and crawls -- a Tesla coil's discharge, a link between two craft, a
// saucer tethered to the ground by a writhing thread.
//
// **How it moves.** Lightning's generator (BOLT) with no strike sequence. Each strand is re-seeded
// `rate` times a second (`k = floor(t * rate)`), and between re-seeds its main channel is CROSS-FADED
// from the shape of seed k to that of seed k+1 -- displacement blended so its size holds, so the arc
// writhes rather than pops -- while k's branches fade out and k+1's fade in. Its brightness flickers
// by a hashed amount per re-seed. Everything is a function of the transport second: the arc a scrub
// lands on is the arc a play shows.
//
// **Where.** From the Source (the owner by default) to the Target (a world point by default; a node,
// a hero, the camera) or, without one, straight down `reach` metres. A body endpoint is met at its
// bounds. `sag` bows the arc downward like a hanging cable; strands spread a little at their ends so
// two or three read as separate threads.
//
// **Drawn** through RIBBON (a glow strip and a core strip per strand), with an optional LIGHTMOD point
// light at the middle, flickering with it.

#include "world/effects/effect_lights.hpp"
#include "world/effects/kinds/bolt_rows.hpp"
#include "world/effects/kinds/stored_rows.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace bolt_rows;

constexpr EffectField kFields[] = {
    storedFloat("strands", "Strands", 2.0f, 1.0f, 4.0f, 1.0f, 4.0f).fmt("%.0f").main().sec("Arc").clampTo(1.0f, 4.0f)
        .tooltip("How many separate threads run between the ends."),
    storedFloat("rate", "Crawl rate", 7.0f, 0.1f, 60.0f, 0.5f, 25.0f).fmt("%.1f Hz").main().floorAt(0.1f)
        .tooltip("How many times a second each strand re-forms. It flows from one shape to the next\n"
                 "rather than jumping."),
    storedFloat("jaggedness", "Jaggedness", 0.2f, 0.0f, 1.0f, 0.03f, 0.5f).main(),
    storedFloat("sag", "Sag", 0.06f, -1.0f, 1.0f, -0.3f, 0.3f).main()
        .tooltip("How far the arc bows down in the middle, as a fraction of its length (negative\n"
                 "bows it up)."),
    storedFloat("branchProbability", "Branching", 0.18f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Short side-branches thrown off the arc."),
    storedFloat("depth", "Detail", 6.0f, 2.0f, 8.0f, 3.0f, 8.0f).fmt("%.0f").sec("Shape").clampTo(2.0f, 8.0f)
        .tooltip("Levels of midpoint refinement. Lowered automatically for an arc small on screen."),
    storedFloat("reach", "Reach", 6.0f, 0.1f, 5000.0f, 0.5f, 60.0f).fmt("%.1f m").log().floorAt(0.1f)
        .tooltip("Without a Target: how far straight down from the Source the arc runs."),
    storedColor("color", "Colour", glm::vec3(0.45f, 0.72f, 1.0f)).main().sec("Light"),
    storedFloat("intensity", "Core brightness", 30.0f, 0.0f, 1000.0f, 0.0f, 120.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness of the core (it blooms above about 1)."),
    storedFloat("coreWidth", "Core width", 0.06f, 0.002f, 10.0f, 0.01f, 0.5f).fmt("%.3f m").log().main().floorAt(0.002f),
    storedFloat("glowIntensity", "Glow brightness", 1.5f, 0.0f, 200.0f, 0.0f, 20.0f).main().floorAt(0.0f),
    storedFloat("glowWidth", "Glow width", 0.9f, 0.02f, 100.0f, 0.1f, 6.0f).fmt("%.2f m").log().main().floorAt(0.02f),
    storedFloat("flicker", "Flicker", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How much the brightness jumps between re-forms: 0 is a steady arc, 1 stutters."),
    storedFloat("light", "Light", 0.0f, 0.0f, 1000000.0f, 0.0f, 20000.0f).sec("Arc light").floorAt(0.0f)
        .tooltip("A real point light at the middle of the arc (candela; 0 = none). It takes one of\n"
                 "the 16 effect lights."),
    storedFloat("lightRange", "Light reach", 25.0f, 0.5f, 5000.0f, 2.0f, 200.0f).fmt("%.0f m").log().floorAt(0.5f),
    storedFloat("seed", "Seed", 1.0f, 0.0f, 100000.0f, 0.0f, 100.0f).fmt("%.0f").floorAt(0.0f),
};

constexpr kinds::StoredRows kRows{"arc", kFields};

// Where the arc runs this frame. False when an end cannot be found.
bool ends(const E& e, const EffectContext& ctx, glm::vec3& a, glm::vec3& b) {
    Resolved src;
    if (!resolveEndpoint(e, e.wave.source, ctx, src)) {
        return false;
    }
    if (e.wave.hasTarget) {
        Resolved dst;
        if (!resolveEndpoint(e, e.wave.target, ctx, dst)) {
            return false;
        }
        a = bodyExit(src, dst.point);
        b = bodyExit(dst, src.point);
    } else {
        const glm::vec3 below = src.point - glm::vec3(0.0f, std::max(kRows.f(e, "reach"), 0.1f), 0.0f);
        a = bodyExit(src, below - glm::vec3(0.0f, 1e3f, 0.0f));
        b = a - glm::vec3(0.0f, std::max(kRows.f(e, "reach"), 0.1f), 0.0f);
    }
    return glm::length(b - a) > 1e-3f;
}

bool live(const E& e, const EffectContext& ctx, float& envelope) {
    if (!e.enabled || (e.owner.kind != EffectTarget::World && e.owner.kind != EffectTarget::Entity)) {
        return false;
    }
    return windowEnvelope(e, ctx, envelope) && kRows.f(e, "intensity") + kRows.f(e, "glowIntensity") > 0.0f;
}

// One re-form step: which two seeds this instant lies between, how far, and the flicker.
struct Step {
    std::uint32_t k = 0;
    float f = 0.0f;
    float brightness = 1.0f;
};

Step stepOf(const E& e, double seconds, std::uint64_t seed, std::uint32_t strand) {
    const double rate = std::max(static_cast<double>(kRows.f(e, "rate")), 0.1);
    // Each strand on its own phase, so the strands do not re-form in lock step.
    const double x = seconds * rate + static_cast<double>(boltHash(seed, strand, 0xa2cu, 0u, 0u));
    const double k = std::floor(x);
    Step s;
    s.k = static_cast<std::uint32_t>(static_cast<std::int64_t>(k));
    const float frac = static_cast<float>(x - k);
    s.f = frac * frac * (3.0f - 2.0f * frac);
    // Brightness per re-form, jumping over the first quarter of the step (a stutter, not a pulse).
    const float flicker = std::clamp(kRows.f(e, "flicker"), 0.0f, 1.0f);
    const float a = 1.0f - flicker * boltHash(seed, strand, s.k, 0xf1cu, 0u);
    const float b = 1.0f - flicker * boltHash(seed, strand, s.k + 1u, 0xf1cu, 0u);
    const float jump = std::clamp(frac * 4.0f, 0.0f, 1.0f);
    s.brightness = a + (b - a) * jump * jump * (3.0f - 2.0f * jump);
    return s;
}

BoltParams shapeOf(const E& e) {
    BoltParams p;
    p.depth = std::clamp(static_cast<int>(kRows.f(e, "depth") + 0.5f), 2, kBoltMaxDepth);
    p.jaggedness = std::clamp(kRows.f(e, "jaggedness"), 0.0f, 1.0f);
    p.branchProbability = std::clamp(kRows.f(e, "branchProbability"), 0.0f, 1.0f);
    p.branchDecay = 0.35f;
    p.generations = 1;
    return p;
}

EffectStatus emit(const E& e, const EffectContext& ctx, const HistoryBank&, RibbonSink& sink, std::string& reason) {
    float envelope = 0.0f;
    glm::vec3 a;
    glm::vec3 b;
    if (!live(e, ctx, envelope) || !ends(e, ctx, a, b)) {
        if (const char* waiting = effectTriggerDormancy(e, ctx); waiting != nullptr && reason.empty()) {
            reason.assign(waiting);
        }
        return EffectStatus::Dormant;
    }
    static thread_local std::vector<RibbonPoint> scratch;
    static thread_local BoltPath blended;
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const int strands = std::clamp(static_cast<int>(kRows.f(e, "strands") + 0.5f), 1, 4);
    const float length = glm::length(b - a);
    BoltParams params = shapeOf(e);
    params.depth = boltLodDepth(params.depth, length, chordDistance(a, b, ctx.cameraPosition));
    const glm::vec3 color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f));
    const float sag = kRows.f(e, "sag");
    std::size_t drawn = 0;
    std::size_t reduced = 0;
    std::size_t refused = 0;
    for (int strand = 0; strand < strands; ++strand) {
        const auto sid = static_cast<std::uint32_t>(strand);
        const Step step = stepOf(e, ctx.seconds, seed, sid);
        const std::uint64_t strandSeed = seed ^ (0x9E3779B97F4A7C15ull * (sid + 1u));
        // Two cached shapes, then their blend. The cache holds both (it evicts the least recently
        // used, never the one just returned), and `blended` is this thread's scratch.
        const BoltPath& from = boltCache().get(params, strandSeed, step.k);
        const BoltPath& to = boltCache().get(params, strandSeed, step.k + 1u);
        blendBolts(from, to, step.f, blended);
        // Strands part a little at their ends (a few percent of the length, a hashed way each).
        const auto spread = [&](std::uint32_t end) {
            if (strands == 1) {
                return glm::vec3(0.0f);
            }
            glm::vec3 d(boltHash(seed, sid, end, 0x5b7u, 0u) - 0.5f, boltHash(seed, sid, end, 0x5b7u, 1u) - 0.5f,
                        boltHash(seed, sid, end, 0x5b7u, 2u) - 0.5f);
            return d * (0.06f * length);
        };
        BoltPlacement placement;
        placement.start = a + spread(0u) * 0.3f;
        placement.end = b + spread(1u);
        placement.bow = glm::vec3(0.0f, -sag * length, 0.0f);
        BoltLook look;
        look.core = color * 0.35f + glm::vec3(0.65f);
        look.core *= std::max(kRows.f(e, "intensity"), 0.0f) * step.brightness;
        look.glow = color * std::max(kRows.f(e, "glowIntensity"), 0.0f) * step.brightness;
        look.coreWidth = std::max(kRows.f(e, "coreWidth"), 0.002f);
        look.glowWidth = std::max(kRows.f(e, "glowWidth"), look.coreWidth);
        look.opacity = envelope;
        look.eye = ctx.cameraPosition;
        look.hasEye = true;
        switch (appendBoltStrips(sink, blended, placement, look, scratch)) {
        case BoltFit::Written: ++drawn; break;
        case BoltFit::CoreOnly: ++reduced; break;
        case BoltFit::NoRoom: ++refused; break;
        case BoltFit::Nothing: break;
        }
    }
    return ribbonStatus(drawn, reduced, refused, reason);
}

std::size_t records(const E& e, const EffectContext& ctx) {
    float envelope = 0.0f;
    glm::vec3 a;
    glm::vec3 b;
    return live(e, ctx, envelope) && ends(e, ctx, a, b) ? 1u : 0u;
}

bool light(const E& e, const EffectContext& ctx, EffectLight& out) {
    const float intensity = std::max(kRows.f(e, "light"), 0.0f);
    float envelope = 0.0f;
    glm::vec3 a;
    glm::vec3 b;
    if (!(intensity > 0.0f) || !live(e, ctx, envelope) || !ends(e, ctx, a, b)) {
        return false;
    }
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const Step step = stepOf(e, ctx.seconds, seed, 0u);
    const float length = glm::length(b - a);
    out.position = 0.5f * (a + b) - glm::vec3(0.0f, kRows.f(e, "sag") * length, 0.0f);
    out.intensity = intensity * step.brightness * envelope;
    out.color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f)) * 0.7f + glm::vec3(0.3f);
    out.range = std::max(kRows.f(e, "lightRange"), 0.5f);
    out.volumetric = 0.5f;
    return true;
}

// ---- presets ------------------------------------------------------------------------------------------

struct Look {
    const char* name;
    float strands, rate, jag, sag, branching, intensity, coreWidth, glowIntensity, glowWidth, flicker, light;
    glm::vec3 color;
};

const Look kLooks[] = {
    // A Tesla coil: three fast, jagged, violet-white threads that stutter.
    {"Tesla Coil", 3.0f, 12.0f, 0.3f, 0.0f, 0.35f, 40.0f, 0.04f, 1.6f, 0.6f, 0.55f, 600.0f, {0.62f, 0.55f, 1.0f}},
    // A tether between two craft (or a craft and the ground): two calm cyan strands with a gentle sag.
    {"UFO Link", 2.0f, 5.0f, 0.16f, 0.05f, 0.12f, 30.0f, 0.08f, 1.5f, 1.2f, 0.2f, 900.0f, {0.35f, 0.9f, 1.0f}},
    // Barely holding together: one thick orange-white strand, slow, heavily branched, lurching.
    {"Unstable Conduit", 1.0f, 3.0f, 0.34f, 0.12f, 0.5f, 45.0f, 0.12f, 2.0f, 1.6f, 0.8f, 700.0f, {1.0f, 0.55f, 0.25f}},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "strands", l.strands);
    kRows.set(e, "rate", l.rate);
    kRows.set(e, "jaggedness", l.jag);
    kRows.set(e, "sag", l.sag);
    kRows.set(e, "branchProbability", l.branching);
    kRows.set(e, "intensity", l.intensity);
    kRows.set(e, "coreWidth", l.coreWidth);
    kRows.set(e, "glowIntensity", l.glowIntensity);
    kRows.set(e, "glowWidth", l.glowWidth);
    kRows.set(e, "flicker", l.flicker);
    kRows.set(e, "light", l.light);
    kRows.setRgb(e, "color", l.color);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {"Tesla Coil", style0},
    {"UFO Link", style1},
    {"Unstable Conduit", style2},
};

// The treble quickens the crawl; the bass surges the current (catalog 2.7).
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "rate", 10.0f, 20.0f, 300.0f},
    {"audio.bass", "intensity", 30.0f, 15.0f, 350.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Arc;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.3;
    e.timing.fadeOut = 0.4;
    e.wave.source = EffectEndpoint{};
    e.wave.source.kind = SourceKind::Owner;
    // The plain type hangs `reach` metres straight down from its owner; aim it at something (a node,
    // a hero, a position) in the Target picker.
    e.wave.hasTarget = false;
    e.wave.target = EffectEndpoint{};
    e.wave.target.kind = SourceKind::World;
    return e;
}

Result<void> readExtra(E& e, const nlohmann::json& block) { return readEndpoints(e, block); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Arc;
    s.key = "arc";
    s.enumName = "Arc";
    s.displayName = "Arc";
    s.description =
        "A continuous electric arc between two things -- the owner and another entity, a point on the "
        "ground, the camera: one to four writhing threads that re-form several times a second, flow "
        "from shape to shape, flicker and throw short side-branches.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCpu | CostVertex | CostFragment;
    s.addLabel = "Arc";
    s.addTip = "A writhing electric arc from this entity to another thing or a point.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "intensity";
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
    registerRibbonProducer(EffectKind::Arc, RibbonProducer{emit, nullptr});
    return s;
}

} // namespace

const EffectSchema& arcSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
