// Reality Tear (Effect Library Wave 3, catalog-distortion.md "Reality Tear"): a jagged crack in space.
// The image is sheared apart along a fractal line, with white-hot edges and a void -- or another
// world -- inside. Compare Portal, a round and controlled opening.
//
// **How it is drawn.** The crack's outline is a BOLT path (world/effects/bolt_path): midpoint
// displacement with no branches, so it always advances along its length and never folds back, keyed
// by the instance's id and the instant its activation opened -- every tear differs, and a scrub draws
// the tear a play draws. Its 32 points are packed as half floats beside the record (SHELL's side data,
// eight vec4) and a quad in the tear's plane, shading `Tear` (shaders/shell_fx.wgsl `fs_tear`), finds
// each fragment's distance to the polyline and where along it it is. The opening's half-width is
// Width x Open x a lens-shaped taper (pointed ends), roughened by noise; inside it the interior is
// drawn -- Void (black, faint parallax stars), Other World (drifting two-colour clouds behind the plane)
// or Glitch (row-displaced colour blocks) -- and at its edge a thin white-hot line falls off to the
// edge colour over the edge width, split into offset colour channels by Chroma. It is composited OVER
// the frame and WRITES DEPTH (the interior is a surface: what is behind it is hidden, the fog and DF
// that follow stop there). Outside the edge band nothing is drawn; the bloom haloes the edge.
//
// **Around it.** A chain of DF Warp proxies along the crack (`EffectResolve::distortion`) pushes the
// image apart on either side, with a chromatic split; EMIT (`EffectResolve::particles`) throws shards
// off it, most as it opens. The edge flickers: `hash(floor(t * rate))`, a pure function of time.
//
// **Open and close.** `open` is a row (key it) times the activation envelope, eased; with the Trigger
// or Window activation the fade-in is the opening. Owners: the World (placed, aimed by Heading and
// Roll) or an Entity (at its centre plus the offset). The catalog's Camera form (a full-screen tear
// transition through FXPOST) is not built.

#include "scene/particles.hpp"
#include "world/effects/bolt_path.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_fx_kind.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kInteriors[] = {"Void", "Other World", "Glitch"};
constexpr std::size_t kPoints = 32; // eight vec4 of side data, four half-float points each
constexpr std::size_t kShearProxies = 6;

constexpr EffectField kFields[] = {
    storedFloat("length", "Length", 6.0f, 0.1f, 1000.0f, 0.5f, 40.0f).fmt("%.2f m").main().log().floorAt(0.1f)
        .tooltip("How long the crack is."),
    storedFloat("open", "Open", 1.0f, 0.0f, 1.0f, 0.0f, 1.0f).main().clampTo(0.0f, 1.0f)
        .tooltip("How far the tear has opened. Key it, or use the Trigger or Window activation: the\n"
                 "fade-in is the opening."),
    storedFloat("width", "Width", 0.45f, 0.0f, 100.0f, 0.0f, 3.0f).fmt("%.2f m").main().floorAt(0.0f)
        .tooltip("The half-width of the gap at its widest, fully open. 0 is a hairline crack."),
    storedFloat("jaggedness", "Jaggedness", 0.22f, 0.0f, 0.6f, 0.0f, 0.45f).main().clampTo(0.0f, 0.6f)
        .tooltip("How far the crack zig-zags from a straight line."),
    storedColor("edgeColor", "Edge colour", glm::vec3(0.72f, 0.4f, 1.0f)).main().sec("Edge"),
    storedFloat("edgeEmission", "Edge brightness", 3.0f, 0.0f, 200.0f, 0.0f, 12.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness of the white-hot line along the edge. The colour falls off from it."),
    storedFloat("edgeWidth", "Edge width", 0.12f, 0.005f, 20.0f, 0.02f, 1.0f).fmt("%.2f m").sec("Edge").floorAt(0.005f)
        .tooltip("How far the edge colour reaches from the gap."),
    storedFloat("coreWidth", "Hot line width", 0.02f, 0.001f, 5.0f, 0.005f, 0.2f).fmt("%.3f m").floorAt(0.001f),
    storedFloat("flicker", "Flicker", 0.3f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How much the edge stutters."),
    storedFloat("flickerRate", "Flicker rate", 12.0f, 0.0f, 60.0f, 0.0f, 30.0f).fmt("%.1f Hz").floorAt(0.0f),
    storedChoice("interior", "Interior", 0, kInteriors).main().sec("Interior")
        .tooltip("Void: black, with faint distant stars. Other World: drifting clouds behind the crack.\n"
                 "Glitch: shifted colour blocks."),
    storedColor("interiorColor", "Interior colour", glm::vec3(0.35f, 0.12f, 0.6f)).main(),
    storedColor("interiorColor2", "Second colour", glm::vec3(0.1f, 0.45f, 0.9f)).sec("Interior"),
    storedFloat("interiorBrightness", "Interior brightness", 0.5f, 0.0f, 20.0f, 0.0f, 2.0f).floorAt(0.0f),
    storedFloat("shear", "Shear", 0.2f, 0.0f, 10.0f, 0.0f, 1.0f).fmt("%.2f m").sec("Distortion").floorAt(0.0f)
        .tooltip("How far the image on either side is pushed apart."),
    storedFloat("shearBand", "Shear reach", 1.2f, 0.05f, 50.0f, 0.2f, 6.0f).fmt("%.2f m").floorAt(0.05f)
        .tooltip("How far from the crack the image is bent."),
    storedFloat("chroma", "Chroma", 0.4f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("The colour split at the edge and in the bent image."),
    storedFloat("shardRate", "Shards", 30.0f, 0.0f, 2000.0f, 0.0f, 300.0f).fmt("%.0f /s").sec("Shards").floorAt(0.0f)
        .tooltip("Glowing splinters thrown off the crack, per second; more while it opens."),
    storedFloat("yaw", "Heading", 0.0f, -360.0f, 360.0f, -180.0f, 180.0f).fmt("%.1f deg").sec("Placement")
        .tooltip("Which way the tear's plane faces, about the vertical."),
    storedFloat("roll", "Roll", 0.0f, -180.0f, 180.0f, -90.0f, 90.0f).fmt("%.1f deg")
        .tooltip("0 runs the crack vertically; 90 horizontally."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m")
        .tooltip("On the World: the tear's centre. On an entity: an offset from its centre."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"realityTear", kFields};

struct Look {
    const char* name;
    int interior;
    glm::vec3 edge;
    float emission, width, edgeWidth, shear, chroma, flicker;
    glm::vec3 inside, inside2;
    float brightness;
};

constexpr Look kLooks[] = {
    // A void rift: violet edges round black, a moderate shear.
    {"Void Rift", 0, {0.72f, 0.4f, 1.0f}, 3.0f, 0.45f, 0.12f, 0.2f, 0.4f, 0.3f, {0.35f, 0.12f, 0.6f}, {0.1f, 0.45f, 0.9f}, 0.5f},
    // A glitch tear: cyan-white edges split into colour, shifted blocks inside, heavy flicker.
    {"Glitch Tear", 2, {0.4f, 0.95f, 1.0f}, 3.5f, 0.35f, 0.1f, 0.25f, 1.0f, 0.7f, {1.0f, 0.1f, 0.5f}, {0.1f, 0.9f, 1.0f}, 0.7f},
    // A crystal crack: a hairline, white edges, no gap to speak of, a strong shear.
    {"Crystal Crack", 0, {0.85f, 0.92f, 1.0f}, 3.0f, 0.04f, 0.08f, 0.6f, 0.6f, 0.1f, {0.2f, 0.25f, 0.35f}, {0.5f, 0.6f, 0.8f}, 0.3f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "interior", static_cast<float>(l.interior));
    kRows.setRgb(e, "edgeColor", l.edge);
    kRows.set(e, "edgeEmission", l.emission);
    kRows.set(e, "width", l.width);
    kRows.set(e, "edgeWidth", l.edgeWidth);
    kRows.set(e, "shear", l.shear);
    kRows.set(e, "chroma", l.chroma);
    kRows.set(e, "flicker", l.flicker);
    kRows.setRgb(e, "interiorColor", l.inside);
    kRows.setRgb(e, "interiorColor2", l.inside2);
    kRows.set(e, "interiorBrightness", l.brightness);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalogue's modulation: onsets make it stutter.
constexpr EffectRoute kRoutes[] = {
    {"audio.onset", "flicker", 0.6f, 5.0f, 180.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::RealityTear;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.6;
    applyLook(e, kLooks[0]);
    return e;
}

// ---- the one state every hook reads ---------------------------------------------------------------

struct State {
    glm::vec3 centre{0.0f};
    glm::vec3 along{0.0f, 1.0f, 0.0f};  // the crack's direction
    glm::vec3 across{1.0f, 0.0f, 0.0f}; // in the plane, across it
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    float length = 1.0f;
    float open = 1.0f;     // 0..1, eased
    float envelope = 1.0f;
    double start = 0.0;    // when this opening began (keys the crack's shape)
    std::array<glm::vec2, kPoints> points{}; // metres in the plane: (across, along)
    float maxAcross = 0.0f;
};

std::uint64_t crackSeed(const E& e, double start) {
    const auto id = static_cast<std::uint32_t>(kinds::seedOf(e.id));
    return (static_cast<std::uint64_t>(id) << 32) | kinds::fxMillis(start);
}

bool stateOf(const E& e, const EffectContext& ctx, State& out) {
    kinds::FxLive live;
    if (e.owner.kind == EffectTarget::Light || !kinds::fxLive(e, ctx, live)) {
        return false;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    kinds::FxAnchor anchor;
    if (!kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    out.envelope = live.envelope;
    out.open = std::clamp(kRows.f(e, "open"), 0.0f, 1.0f) * kinds::smooth01(0.0f, 1.0f, live.envelope);
    if (!(out.open > 1e-3f)) {
        return false;
    }
    out.start = live.start;
    out.centre = anchor.centre;
    out.length = std::max(kRows.f(e, "length"), 0.1f);
    out.normal = kinds::fxHeading(kRows.f(e, "yaw"), 0.0f, false);
    glm::vec3 right;
    glm::vec3 up;
    kinds::fxPlaneAxes(out.normal, right, up);
    const float roll = glm::radians(kRows.f(e, "roll"));
    out.along = up * std::cos(roll) + right * std::sin(roll);
    out.across = glm::normalize(glm::cross(out.along, out.normal));

    // The outline: a BOLT main channel with no branches (it never folds back), resampled to 32 points.
    BoltParams params;
    params.depth = 5;
    params.jaggedness = std::clamp(kRows.f(e, "jaggedness"), 0.0f, 0.6f);
    params.branchProbability = 0.0f;
    params.generations = 0;
    const BoltPath& path = boltCache().get(params, crackSeed(e, live.start), 0u);
    const std::span<const BoltVertex> main = path.path(0);
    out.maxAcross = 0.0f;
    for (std::size_t k = 0; k < kPoints; ++k) {
        const std::size_t at = main.empty() ? 0 : std::min(main.size() - 1, (k * (main.size() - 1) + (kPoints - 1) / 2) / (kPoints - 1));
        const glm::vec3 c = main.empty() ? glm::vec3(0.0f, 0.0f, static_cast<float>(k) / (kPoints - 1)) : main[at].position;
        out.points[k] = glm::vec2(c.x * out.length, (c.z - 0.5f) * out.length);
        out.maxAcross = std::max(out.maxAcross, std::abs(out.points[k].x));
    }
    return true;
}

glm::vec3 worldOf(const State& s, glm::vec2 p) { return s.centre + s.across * p.x + s.along * p.y; }

std::size_t records(const E& e, const EffectContext& ctx) {
    State s;
    return stateOf(e, ctx, s) ? 1u : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    const bool lost = !reason.empty();
    State s;
    if (!stateOf(e, ctx, s)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    const float width = std::max(kRows.f(e, "width"), 0.0f) * s.open;
    const float edge = std::max(kRows.f(e, "edgeWidth"), 0.005f);
    const float halfW = s.maxAcross + width * 1.3f + edge * 4.0f + 0.05f;
    const float halfL = 0.5f * s.length + edge * 2.0f + 0.05f;

    // The flicker: one hashed step per 1/rate seconds, a pure function of time.
    const float seed = kinds::seedOf(e.id);
    const float rate = std::max(kRows.f(e, "flickerRate"), 0.0f);
    const float amount = std::clamp(kRows.f(e, "flicker"), 0.0f, 1.0f);
    float flick = 1.0f;
    if (rate > 0.0f && amount > 0.0f) {
        const auto step = static_cast<std::uint32_t>(std::floor(ctx.seconds * static_cast<double>(rate)) + 1.0e6);
        const float h = kinds::fxHash(static_cast<std::uint32_t>(seed), step);
        flick = 1.0f - amount * (h < 0.22f ? 0.85f : 0.35f * h);
    }

    std::array<glm::vec4, kPoints / 4> packed{};
    for (std::size_t k = 0; k < kPoints; ++k) {
        const std::uint32_t bits = glm::packHalf2x16(s.points[k]);
        packed[k / 4][static_cast<glm::length_t>(k % 4)] = std::bit_cast<float>(bits);
    }

    ShellInstance shell;
    shell.model = kinds::fxModel(s.centre, s.across * halfW, s.along * halfL, s.normal * halfW);
    shell.params[0] = glm::vec4(kRows.rgb(e, "edgeColor"), std::max(kRows.f(e, "edgeEmission"), 0.0f) * flick);
    shell.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(kRows.rgb(e, "interiorColor"), std::max(kRows.f(e, "interiorBrightness"), 0.0f));
    shell.params[3] = glm::vec4(width, edge * (0.4f + 0.6f * s.open), std::max(kRows.f(e, "coreWidth"), 0.001f),
                                std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f));
    shell.params[4] = glm::vec4(static_cast<float>(kRows.choice(e, "interior")), static_cast<float>(kPoints), s.length, s.open);
    shell.params[5] = glm::vec4(halfW, halfL, 8.0f, kinds::fxHash(static_cast<std::uint32_t>(seed), kinds::fxMillis(s.start)) * 100.0f);
    shell.params[6] = glm::vec4(kRows.rgb(e, "interiorColor2"), s.envelope);
    if (!sink.append(ShellShading::Tear, ShellMesh::Quad, shell, packed)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    return kinds::fxDrawn(lost);
}

// DF: the image on either side pushed away from the crack. A chain of DF Wake segments along the
// outline (the Wave 2 field: a displacement across a tube, cos^2-windowed along it so neighbours sum
// to one), phased so the displacement never changes sign -- every pixel near the crack samples from
// nearer the crack, which is the two sides drawn apart -- with a light colour split.
std::size_t distortion(const E& e, const EffectContext& ctx, std::span<DistortionProxy> out) {
    State s;
    const float shear = std::max(kRows.f(e, "shear"), 0.0f);
    if (shear <= 0.0f || !stateOf(e, ctx, s)) {
        return 0;
    }
    const float band = std::max(kRows.f(e, "shearBand"), 0.05f);
    const float chroma = std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f);
    const std::size_t n = std::min(kShearProxies, out.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t a = i * (kPoints - 1) / n;
        const std::size_t b = (i + 1) * (kPoints - 1) / n;
        const glm::vec3 pa = worldOf(s, s.points[a]);
        const glm::vec3 pb = worldOf(s, s.points[b]);
        glm::vec3 dir = pb - pa;
        const float len = glm::length(dir);
        dir = len > 1e-5f ? dir / len : s.along;
        const glm::vec3 side = glm::normalize(glm::cross(dir, s.normal));
        DistortionProxy p;
        // The lens plane a little behind the tear: its own glowing edge is in front of it, so no tap
        // smears the edge into the bend.
        p.centre = glm::vec4(0.5f * (pa + pb), 0.3f * band);
        // The window reaches one segment either way, so each point is covered by two segments whose
        // weights sum to one.
        p.axis0 = glm::vec4(dir * len, static_cast<float>(DistortionShape::Ellipsoid));
        p.axis1 = glm::vec4(side * band, static_cast<float>(DistortionField::Wake));
        p.axis2 = glm::vec4(s.normal * band, 0.5f * band);
        p.terms = glm::vec4(1.0f, 0.0f, 0.0f, shear * s.open);
        // A quarter cycle across the radius, a quarter-cycle phase: -cos, one sign all the way out.
        p.shape = glm::vec4(0.25f, 0.35f, 0.0f, 0.0f);
        p.noise = glm::vec4(0.25f, 0.12f * chroma, 0.0f, kinds::seedOf(e.id) + static_cast<float>(i));
        out[i] = p;
    }
    return n;
}

// EMIT: glowing shards thrown off the crack, most while it opens.
bool particles(const E& e, const EffectContext& ctx, scene::ParticleSystem& p) {
    p.capacity = 4096;
    p.shape = scene::EmitterShape::Box;
    p.spawnRate = 0.0f;
    p.burst = 0.0f;
    p.spread = 0.7f;
    p.speedMin = 0.6f;
    p.speedMax = 2.6f;
    p.gravity = glm::vec3(0.0f, -1.2f, 0.0f);
    p.drag = 0.5f;
    p.turbulence = 0.3f;
    p.lifetimeMin = 0.6f;
    p.lifetimeMax = 1.6f;
    p.sizeStart = 0.03f;
    p.sizeEnd = 0.0f;
    p.blend = scene::ParticleBlend::Additive;
    p.velocityStretch = 0.8f;
    p.stretchMax = 0.25f;
    p.softness = 0.2f;
    p.fogCoupling = 1.0f;
    State s;
    if (!stateOf(e, ctx, s)) {
        return false;
    }
    const glm::vec3 edge = kRows.rgb(e, "edgeColor");
    p.colorStart = glm::vec4(glm::mix(edge, glm::vec3(1.0f), 0.6f), 1.0f);
    p.colorEnd = glm::vec4(edge, 0.0f);
    p.emissive = 2.5f;
    p.position = s.centre;
    // The crack's own box: thin across the plane's normal, its length along it.
    p.extent = glm::abs(s.along) * (0.45f * s.length) + glm::abs(s.across) * (s.maxAcross + 0.05f) +
               glm::abs(s.normal) * 0.05f;
    p.direction = s.normal;
    // Most while it opens: the rate follows how fast it is opening, and a trickle once it is open.
    const float opening = 1.0f - s.envelope;
    p.spawnRate = std::max(kRows.f(e, "shardRate"), 0.0f) * s.open * (0.4f + 3.0f * opening);
    p.seed = static_cast<std::uint32_t>(kinds::seedOf(e.id)) + 47u;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::RealityTear;
    s.key = "realityTear";
    s.enumName = "RealityTear";
    s.displayName = "Reality Tear";
    s.description = "A jagged crack in space: white-hot edges falling off to colour round a void or another "
                    "world, the image on either side pushed apart with a colour split, shards thrown off, "
                    "a stuttering flicker. It tears open with its activation or a keyed Open, and every "
                    "opening is a new crack.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Reality Tear";
    s.addTip = "A crack in space placed in the world, or tearing round this entity.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "flicker";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    s.resolve.distortion = distortion;
    s.resolve.particles = particles;
    registerShellProducer(EffectKind::RealityTear, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& realityTearSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
