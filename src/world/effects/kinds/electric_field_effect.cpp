// Electric Field (Effect Library Wave 3, catalog-energy.md "Electric Field"): electricity crawling
// over its owner's SURFACE, in two parts, either or both on:
//
// **Surface Crackle** -- a flickering blue-white web over the owner, drawn by FXL in the lit pass: the
// vein sub-block (Worley F2 - F1 cell edges, domain-warped), so the web is part of the lit surface --
// fogged, bloomed, depth-exact, riding the owner. It CRAWLS because the domain warp's amount is
// driven by a smooth hashed function of time (the edges slide as the warp changes), SPARKS as pulses
// of light run out through it from the centre, and STUTTERS by a hashed brightness per 1/18 s. The
// crackle is a secondary lane term (`EffectResolve::lanes` on a Ribbon-bucket type): the vein block is
// exclusive per owner, so an owner already carrying Pulsing Veins keeps them and this reports Partial
// with the holder's name.
//
// **Crawling Arcs** -- short bolts hopping between points on the owner: each of `arcCount` slots picks
// a pair of points `hopDistance` apart from (seed, slot, floor(t * arcRate)), flashes an arc bowed out
// from the surface between them, and lets it die before the next hop. BOLT and RIBBON draw them.
//
// **The surface is the bounds' ellipsoid.** The catalog samples points from the owner's mesh; this
// engine gives an effect its owner's DRAWN view (a world matrix and world bounds), not its triangles,
// so the points lie on the ellipsoid inscribed in those bounds, turning with the owner. For a saucer,
// an orb or a body that fills its box this reads as its hull; for a spindly one the arcs stand off it.
// Documented, not hidden.
//
// Stateless: every hop, flicker and crawl is a function of the transport second.

#include "world/effects/entity_fx.hpp"
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
    storedFloat("crackle", "Crackle", 4.0f, 0.0f, 200.0f, 0.0f, 20.0f).main().sec("Surface crackle").floorAt(0.0f)
        .tooltip("HDR brightness of the web over the surface. 0 turns the crackle off."),
    storedFloat("crackleScale", "Web size", 5.0f, 0.5f, 40.0f, 1.0f, 15.0f).main().floorAt(0.5f)
        .tooltip("Cells across the owner: more is a finer web."),
    storedFloat("crackleSpeed", "Crawl", 1.5f, 0.0f, 20.0f, 0.0f, 6.0f).main().floorAt(0.0f)
        .tooltip("How fast the web slides and sparks run through it."),
    storedFloat("coverage", "Coverage", 0.45f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How thick the web's lines are and how much of it glows between sparks."),
    storedFloat("arcCount", "Arcs", 4.0f, 0.0f, 8.0f, 0.0f, 8.0f).fmt("%.0f").main().sec("Crawling arcs").clampTo(0.0f, 8.0f)
        .tooltip("How many arcs hop over the surface at once. 0 turns them off."),
    storedFloat("arcRate", "Hop rate", 5.0f, 0.2f, 40.0f, 0.5f, 15.0f).fmt("%.1f Hz").main().floorAt(0.2f)
        .tooltip("How many times a second each arc jumps to a new place."),
    storedFloat("hopDistance", "Hop distance", 0.8f, 0.05f, 2.0f, 0.1f, 1.5f).main()
        .tooltip("How far an arc jumps across the owner, as a fraction of its size."),
    storedColor("color", "Colour", glm::vec3(0.5f, 0.75f, 1.0f)).main().sec("Light"),
    storedFloat("intensity", "Arc brightness", 25.0f, 0.0f, 500.0f, 0.0f, 100.0f).main().floorAt(0.0f),
    storedFloat("coreWidth", "Arc width", 0.03f, 0.002f, 5.0f, 0.005f, 0.3f).fmt("%.3f m").log().sec("Arc shape").floorAt(0.002f),
    storedFloat("glowWidth", "Arc glow", 0.35f, 0.01f, 20.0f, 0.05f, 2.0f).fmt("%.2f m").log().floorAt(0.01f),
    storedFloat("glowIntensity", "Arc glow brightness", 1.2f, 0.0f, 100.0f, 0.0f, 12.0f).floorAt(0.0f),
    storedFloat("seed", "Seed", 1.0f, 0.0f, 100000.0f, 0.0f, 100.0f).fmt("%.0f").floorAt(0.0f),
};

constexpr kinds::StoredRows kRows{"electricField", kFields};

bool live(const E& e, const EffectContext& ctx, float& envelope, NodeView& view) {
    if (!e.enabled || e.owner.kind != EffectTarget::Entity || ctx.scene == nullptr) {
        return false;
    }
    if (!windowEnvelope(e, ctx, envelope)) {
        return false;
    }
    return ctx.scene->nodeView(e.owner.name, view);
}

// A smooth function of x in [0, 1): hashed values at the integers, smoothstepped between.
float smoothHash(std::uint64_t seed, double x, std::uint32_t channel) {
    const double k = std::floor(x);
    const auto i = static_cast<std::uint32_t>(static_cast<std::int64_t>(k));
    const float f = static_cast<float>(x - k);
    const float s = f * f * (3.0f - 2.0f * f);
    const float a = boltHash(seed, i, channel, 0xc7au, 0u);
    const float b = boltHash(seed, i + 1u, channel, 0xc7au, 0u);
    return a + (b - a) * s;
}

// FXL: the crackle, as the vein sub-block. The builder applies the envelope.
bool lanes(const E& e, const EffectContext& ctx, const NodeView&, double, EntityLaneContribution& c) {
    const float crackle = std::max(kRows.f(e, "crackle"), 0.0f);
    if (!(crackle > 0.0f)) {
        return false;
    }
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const float speed = std::max(kRows.f(e, "crackleSpeed"), 0.0f);
    const float coverage = std::clamp(kRows.f(e, "coverage"), 0.0f, 1.0f);
    // The stutter: a new brightness 18 times a second.
    const double stutter = ctx.seconds * 18.0;
    const float flick = 0.45f + 0.55f * boltHash(seed, static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(stutter))), 0x57au, 0u, 0u);
    const glm::vec3 color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f));
    c.hasVeins = true;
    c.veinsScale = std::max(kRows.f(e, "crackleScale"), 0.5f);
    c.veinsWidth = 0.02f + 0.1f * coverage;
    // The crawl: the warp's amount wanders smoothly, which slides the cell edges over the surface.
    c.veinsNoise = 0.35f + 0.6f * smoothHash(seed, ctx.seconds * static_cast<double>(speed) * 0.8, 1u);
    c.veinsCoordinate = 1.0f; // sparks run outward from the centre
    c.veinsNear = (color * 0.6f + glm::vec3(0.4f)) * crackle * flick;
    c.veinsFar = color * crackle * flick;
    c.veinsPulseSpeed = 0.6f * speed + 0.2f;
    c.veinsPulseWidth = 0.05f;
    c.veinsPulseInterval = 0.3f;
    c.veinsBaseline = 0.15f + 0.45f * coverage;
    return true;
}

// A point on the ellipsoid inscribed in the owner's drawn bounds, in a direction fixed to the owner.
glm::vec3 onSurface(const NodeView& view, const glm::vec3& local) {
    const glm::vec3 centre = view.hasBounds ? 0.5f * (view.boundsMin + view.boundsMax) : glm::vec3(view.world[3]);
    const glm::vec3 half = view.hasBounds ? 0.5f * (view.boundsMax - view.boundsMin) : glm::vec3(0.5f);
    const glm::mat3 r(glm::normalize(glm::vec3(view.world[0])), glm::normalize(glm::vec3(view.world[1])),
                      glm::normalize(glm::vec3(view.world[2])));
    glm::vec3 d = r * local;
    const float l = glm::length(d);
    d = l > 1e-6f ? d / l : glm::vec3(0.0f, 1.0f, 0.0f);
    return centre + d * half;
}

glm::vec3 unitFrom(std::uint64_t seed, std::uint32_t slot, std::uint32_t k, std::uint32_t channel) {
    // A direction on the sphere: a hashed point in the cube, normalised (the cube's corners bias it
    // slightly, which a hop pattern does not show).
    glm::vec3 d(boltHash(seed, slot, k, channel, 0u) * 2.0f - 1.0f, boltHash(seed, slot, k, channel, 1u) * 2.0f - 1.0f,
                boltHash(seed, slot, k, channel, 2u) * 2.0f - 1.0f);
    const float l = glm::length(d);
    return l > 0.05f ? d / l : glm::vec3(0.0f, 1.0f, 0.0f);
}

EffectStatus emit(const E& e, const EffectContext& ctx, const HistoryBank&, RibbonSink& sink, std::string& reason) {
    float envelope = 0.0f;
    NodeView view;
    if (!live(e, ctx, envelope, view)) {
        if (const char* waiting = effectTriggerDormancy(e, ctx); waiting != nullptr && reason.empty()) {
            reason.assign(waiting);
        }
        return EffectStatus::Dormant;
    }
    const bool crackle = kRows.f(e, "crackle") > 0.0f && (view.entityCount + view.proceduralCount) > 0;
    const int slots = std::clamp(static_cast<int>(kRows.f(e, "arcCount") + 0.5f), 0, 8);
    static thread_local std::vector<RibbonPoint> scratch;
    const std::uint64_t seed = instanceSeed(e, kRows.f(e, "seed"));
    const double rate = std::max(static_cast<double>(kRows.f(e, "arcRate")), 0.2);
    const float hop = std::clamp(kRows.f(e, "hopDistance"), 0.05f, 2.0f);
    const glm::vec3 color = glm::max(kRows.rgb(e, "color"), glm::vec3(0.0f));
    const glm::vec3 centre = view.hasBounds ? 0.5f * (view.boundsMin + view.boundsMax) : glm::vec3(view.world[3]);
    BoltParams params;
    params.depth = 4;
    params.jaggedness = 0.28f;
    params.branchProbability = 0.3f;
    params.branchDecay = 0.4f;
    params.generations = 1;
    std::size_t drawn = 0;
    std::size_t reduced = 0;
    std::size_t refused = 0;
    for (int j = 0; j < slots; ++j) {
        const auto slot = static_cast<std::uint32_t>(j);
        const double x = ctx.seconds * rate + static_cast<double>(boltHash(seed, slot, 0x9a5u, 0u, 0u));
        const double kf = std::floor(x);
        const auto k = static_cast<std::uint32_t>(static_cast<std::int64_t>(kf));
        const float frac = static_cast<float>(x - kf);
        // A hop flashes on within a few percent of its step and is gone by 60% of it.
        const float on = kinds::smooth01(0.0f, 0.06f, frac) * (1.0f - kinds::smooth01(0.2f, 0.6f, frac));
        const float strength = on * (0.55f + 0.45f * boltHash(seed, slot, k, 0xb71u, 0u));
        if (strength <= 1e-3f) {
            continue;
        }
        const glm::vec3 from = unitFrom(seed, slot, k, 1u);
        // The landing: across from `from` by `hop` of a radian-ish, a hashed way round it.
        glm::vec3 side = glm::cross(from, unitFrom(seed, slot, k, 2u));
        const float sl = glm::length(side);
        side = sl > 1e-3f ? side / sl : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 to = glm::normalize(from + side * hop);
        BoltPlacement placement;
        placement.start = onSurface(view, from);
        placement.end = onSurface(view, to);
        const float chord = glm::length(placement.end - placement.start);
        if (chord < 1e-3f) {
            continue;
        }
        glm::vec3 out = 0.5f * (placement.start + placement.end) - centre;
        const float ol = glm::length(out);
        out = ol > 1e-5f ? out / ol : glm::vec3(0.0f, 1.0f, 0.0f);
        placement.bow = out * (0.45f * chord);
        BoltParams p = params;
        p.depth = boltLodDepth(params.depth, chord, chordDistance(placement.start, placement.end, ctx.cameraPosition));
        BoltLook look;
        look.core = (color * 0.4f + glm::vec3(0.6f)) * std::max(kRows.f(e, "intensity"), 0.0f) * strength;
        look.glow = color * std::max(kRows.f(e, "glowIntensity"), 0.0f) * strength;
        look.coreWidth = std::max(kRows.f(e, "coreWidth"), 0.002f);
        look.glowWidth = std::max(kRows.f(e, "glowWidth"), look.coreWidth);
        look.opacity = envelope;
        look.eye = ctx.cameraPosition;
        look.hasEye = true;
        drawBolt(sink, p, seed ^ (0xE1Eull << 32), (k << 3) | slot, placement, look, scratch, drawn, reduced, refused);
    }
    const EffectStatus arcs = ribbonStatus(drawn, reduced, refused, reason);
    if (arcs == EffectStatus::Dormant && crackle) {
        // The crackle is the part that drew (in the lit pass): Drawn, unless the FXL builder refused it
        // this frame and left its reason.
        return reason.empty() ? EffectStatus::Drawn : EffectStatus::Partial;
    }
    return arcs;
}

std::size_t records(const E& e, const EffectContext& ctx) {
    float envelope = 0.0f;
    NodeView view;
    return live(e, ctx, envelope, view) ? 1u : 0u;
}

// ---- presets ------------------------------------------------------------------------------------------

struct Look {
    const char* name;
    float crackle, scale, speed, coverage, arcs, rate, hop, intensity;
    glm::vec3 color;
};

const Look kLooks[] = {
    // Overcharged: a bright, busy blue-white web and many quick arcs.
    {"Overcharged", 6.0f, 6.0f, 2.5f, 0.5f, 6.0f, 8.0f, 0.7f, 35.0f, {0.5f, 0.75f, 1.0f}},
    // EMP'd: a sparse, stuttering violet web and a few slow, long hops.
    {"EMP'd", 3.0f, 3.5f, 0.8f, 0.25f, 3.0f, 2.5f, 1.2f, 20.0f, {0.72f, 0.5f, 1.0f}},
    // Storm Golem: a dense slow web and big arcs, a stone body full of weather.
    {"Storm Golem", 4.0f, 9.0f, 1.0f, 0.7f, 5.0f, 4.0f, 1.0f, 45.0f, {0.4f, 0.65f, 1.0f}},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "crackle", l.crackle);
    kRows.set(e, "crackleScale", l.scale);
    kRows.set(e, "crackleSpeed", l.speed);
    kRows.set(e, "coverage", l.coverage);
    kRows.set(e, "arcCount", l.arcs);
    kRows.set(e, "arcRate", l.rate);
    kRows.set(e, "hopDistance", l.hop);
    kRows.set(e, "intensity", l.intensity);
    kRows.setRgb(e, "color", l.color);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {"Overcharged", style0},
    {"EMP'd", style1},
    {"Storm Golem", style2},
};

// The treble crackles the web; onsets make the arcs hop faster (catalog 2.7).
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "crackle", 6.0f, 15.0f, 250.0f},
    {"audio.onset", "arcRate", 8.0f, 5.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::ElectricField;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.3;
    e.timing.fadeOut = 0.5;
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::ElectricField;
    s.key = "electricField";
    s.enumName = "ElectricField";
    s.displayName = "Electric Field";
    s.description =
        "Electricity crawling over the owner: a flickering web of light over its surface that slides "
        "and sparks, and short arcs jumping from one place on it to another. Either part can be "
        "switched off.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCpu | CostVertex | CostFragment;
    s.addLabel = "Electric Field";
    s.addTip = "A crackling web of electricity over this entity, with arcs hopping across it.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "crackle";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Ribbon;
    s.resolve.records = records;
    s.resolve.lanes = lanes; // the crackle, a secondary FXL term (see the header)
    registerRibbonProducer(EffectKind::ElectricField, RibbonProducer{emit, nullptr});
    return s;
}

} // namespace

const EffectSchema& electricFieldSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
