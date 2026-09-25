// Force Field (Effect Library Wave 3, catalog-energy.md "Force Field"): a barrier REGION placed in the
// world -- a wall, a box, a cylinder or a dome -- rather than a shell round an entity. Faint until
// something comes near it; bright where things approach or pass through it, along its edges, and
// where it meets the ground.
//
// **How it is drawn.** SHELL (world/effects/shell_frame, shaders/shell.wgsl `fs_barrier`): a quad, a
// box, an open cylinder or a sphere cut at its equator, with a cell pattern (hexes, organic cells or
// a grid) in METRES on its surface -- a cylinder is unrolled so its cells do not stretch round the
// side -- scrolling upward. Its brightness is
//   pattern * (idle + sum_j smoothstep(R, 0, |p - e_j|) + ground)  +  edges  +  ground line  +  rim,
// where e_j are up to eight revealer positions the CPU supplies: the camera (a row), the owner, and
// the scene's heroes nearest the barrier. Additive, depth-tested, no depth write, fogged, blooms.
//
// **Determinism.** Stateless: the scroll and the cell flicker are functions of the transport second,
// the revealers are this frame's drawn positions.
//
// **Owners.** The World (primary: placed at `position`, its base on that point) and an Entity (a
// projected wall that moves with its owner: based under the owner's bounds, plus the offset).
//
// **Not here.** The optional DF refraction (another slice's files).

#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_kind.hpp"
#include "world/effects/shell_frame.hpp"
#include "world/hero.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kShapes[] = {"Wall", "Box", "Cylinder", "Dome"};
enum Shape : int { Wall = 0, Box = 1, Cylinder = 2, Dome = 3 };
constexpr const char* kPatterns[] = {"Hex", "Cells", "Grid"};

constexpr EffectField kFields[] = {
    // ---- Main ------------------------------------------------------------------------------------
    storedChoice("shape", "Shape", 0, kShapes).main()
        .tooltip("Wall: a flat sheet (width x height).\nBox: width x height x depth.\n"
                 "Cylinder: a round wall (diameter x height).\nDome: a half-sphere (diameter x height)."),
    storedFloat("sizeX", "Width", 8.0f, 0.1f, 10000.0f, 0.5f, 60.0f).fmt("%.1f m").main().log().floorAt(0.1f),
    storedFloat("sizeY", "Height", 4.0f, 0.1f, 10000.0f, 0.5f, 40.0f).fmt("%.1f m").main().log().floorAt(0.1f),
    storedFloat("sizeZ", "Depth", 8.0f, 0.1f, 10000.0f, 0.5f, 60.0f).fmt("%.1f m").main().log().floorAt(0.1f)
        .tooltip("A box's depth. A wall has none; a cylinder and a dome use the width."),
    storedFloat("yaw", "Turn", 0.0f, -360.0f, 360.0f, -180.0f, 180.0f).fmt("%.0f deg").main()
        .tooltip("Turns the barrier about the vertical."),
    storedColor("color", "Colour", glm::vec3(0.25f, 0.85f, 1.0f)).main().sec("Look"),
    storedFloat("intensity", "Intensity", 1.2f, 0.0f, 50.0f, 0.0f, 6.0f).main().floorAt(0.0f)
        .tooltip("Scales everything the barrier shows. Above about 1 its bright parts bloom."),
    storedChoice("pattern", "Pattern", 0, kPatterns).main(),
    storedFloat("patternScale", "Cell size", 0.6f, 0.02f, 100.0f, 0.1f, 4.0f).fmt("%.2f m").main().log().floorAt(0.02f),
    storedFloat("scrollSpeed", "Scroll", 0.4f, -50.0f, 50.0f, -3.0f, 3.0f).fmt("%.2f m/s").main()
        .tooltip("How fast the pattern climbs the barrier (negative: falls)."),
    storedFloat("idle", "Idle", 0.12f, 0.0f, 4.0f, 0.0f, 1.0f).main().floorAt(0.0f)
        .tooltip("How much of the pattern shows with nothing near it."),
    storedFloat("revealRadius", "Reveal radius", 4.0f, 0.05f, 1000.0f, 0.5f, 20.0f).fmt("%.1f m").main().sec("Reveal").floorAt(0.05f)
        .tooltip("How close something has to come before the barrier lights up around it."),
    // ---- Advanced --------------------------------------------------------------------------------
    storedFloat("revealGain", "Reveal strength", 1.0f, 0.0f, 10.0f, 0.0f, 3.0f).sec("Reveal").floorAt(0.0f),
    storedBool("revealCamera", "Camera reveals", true)
        .tooltip("The camera lights the barrier as it comes near, as a character would."),
    storedFloat("revealHeroes", "Heroes that reveal", 4.0f, 0.0f, 6.0f, 0.0f, 6.0f).fmt("%.0f").clampTo(0.0f, 6.0f)
        .tooltip("How many of the scene's heroes -- the nearest to the barrier -- light it as they\n"
                 "approach. An entity owner always does."),
    storedFloat("intersectWidth", "Ground line width", 0.4f, 0.0f, 20.0f, 0.0f, 2.0f).fmt("%.2f m").sec("Ground line").floorAt(0.0f)
        .tooltip("The bright line where the barrier meets the ground or anything passing through it.\n"
                 "Needs the depth prepass, which runs with AO, contact shadows or water."),
    storedFloat("intersectIntensity", "Ground line", 3.0f, 0.0f, 100.0f, 0.0f, 12.0f).floorAt(0.0f),
    storedFloat("edgeGlow", "Edge glow", 1.5f, 0.0f, 50.0f, 0.0f, 6.0f).sec("Look").floorAt(0.0f)
        .tooltip("A glow along the barrier's free edges: a wall's frame, a box's edges, a cylinder's\n"
                 "rims, a dome's base."),
    storedFloat("edgeWidth", "Edge width", 0.25f, 0.01f, 20.0f, 0.05f, 2.0f).fmt("%.2f m").floorAt(0.01f),
    storedFloat("lineWidth", "Line width", 0.05f, 0.005f, 0.45f, 0.01f, 0.3f).clampTo(0.005f, 0.45f)
        .tooltip("The pattern's lines, as a fraction of a cell."),
    storedFloat("fill", "Fill", 0.08f, 0.0f, 2.0f, 0.0f, 0.5f).floorAt(0.0f)
        .tooltip("A faint sheet of colour between the lines, where the barrier is revealed."),
    storedFloat("rim", "Rim", 0.4f, 0.0f, 20.0f, 0.0f, 3.0f).floorAt(0.0f)
        .tooltip("A glow where a curved barrier turns away from the camera."),
    storedFloat("flicker", "Cell flicker", 0.2f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f),
    storedFloat("backFace", "Far side", 0.6f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How bright a box's, cylinder's or dome's far side is, seen through the near side."),
    storedFloat("positionX", "Position X", 0.0f, -100000.0f, 100000.0f, -100.0f, 100.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: the middle of the barrier's base. On an entity: an offset from under\n"
                 "the owner, in world metres."),
    storedFloat("positionY", "Position Y", 0.0f, -100000.0f, 100000.0f, -100.0f, 100.0f).fmt("%.2f m"),
    storedFloat("positionZ", "Position Z", 0.0f, -100000.0f, 100000.0f, -100.0f, 100.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"forceField", kFields};

// ---- styles ---------------------------------------------------------------------------------------

struct Look {
    const char* name;
    int shape, pattern;
    glm::vec3 color;
    float intensity, cell, scroll, idle, line, edge, fill, rim, flicker;
};

// Every look writes every look row and its shape (size and placement are the author's).
constexpr Look kLooks[] = {
    // A laser grid: a red wall of fine square lines, a hard frame, no scroll.
    {"Laser Grid", Wall, 2, {1.0f, 0.12f, 0.08f}, 1.6f, 0.45f, 0.0f, 0.35f, 0.04f, 2.5f, 0.04f, 0.0f, 0.05f},
    // A containment dome: cyan hexes climbing a half-sphere, faint until approached.
    {"Containment Dome", Dome, 0, {0.25f, 0.85f, 1.0f}, 1.2f, 0.8f, 0.3f, 0.1f, 0.05f, 1.5f, 0.08f, 0.6f, 0.2f},
    // Glowmere's ward: organic green-teal cells drifting up a round wall, breathing gently.
    {"Glowmere Ward", Cylinder, 1, {0.3f, 1.0f, 0.65f}, 1.0f, 0.9f, 0.25f, 0.18f, 0.06f, 1.0f, 0.1f, 0.3f, 0.35f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "shape", static_cast<float>(l.shape));
    kRows.set(e, "pattern", static_cast<float>(l.pattern));
    kRows.setRgb(e, "color", l.color);
    kRows.set(e, "intensity", l.intensity);
    kRows.set(e, "patternScale", l.cell);
    kRows.set(e, "scrollSpeed", l.scroll);
    kRows.set(e, "idle", l.idle);
    kRows.set(e, "lineWidth", l.line);
    kRows.set(e, "edgeGlow", l.edge);
    kRows.set(e, "fill", l.fill);
    kRows.set(e, "rim", l.rim);
    kRows.set(e, "flicker", l.flicker);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2},
};

// The catalogue's modulation: the level brings the barrier up (the beat response drives the scroll).
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "idle", 0.6f, 20.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::ForceField;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.8;
    e.timing.fadeOut = 0.8;
    // The plain type: a cyan hex wall, as the rows' defaults say.
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

struct Placed {
    glm::vec3 centre{0.0f};
    glm::vec3 half{1.0f};
    float yaw = 0.0f;
    int shape = Wall;
    ShellMesh mesh = ShellMesh::Quad;
};

bool place(const E& e, const EffectContext& ctx, Placed& out) {
    kinds::ShellAnchor anchor;
    const glm::vec3 position(kRows.f(e, "positionX"), kRows.f(e, "positionY"), kRows.f(e, "positionZ"));
    if (!kinds::shellAnchor(e, ctx, position, anchor)) {
        return false;
    }
    out.shape = kRows.choice(e, "shape");
    const glm::vec3 size(std::max(kRows.f(e, "sizeX"), 0.1f), std::max(kRows.f(e, "sizeY"), 0.1f),
                         std::max(kRows.f(e, "sizeZ"), 0.1f));
    out.yaw = glm::radians(kRows.f(e, "yaw"));
    // The base: a World barrier stands on its position; an entity's under the owner.
    const glm::vec3 base(anchor.centre.x, anchor.entity ? anchor.baseY : anchor.centre.y, anchor.centre.z);
    switch (out.shape) {
    case Wall:
        out.mesh = ShellMesh::Quad;
        out.half = glm::vec3(size.x * 0.5f, size.y * 0.5f, 1.0f);
        out.centre = base + glm::vec3(0.0f, out.half.y, 0.0f);
        break;
    case Box:
        out.mesh = ShellMesh::Box;
        out.half = size * 0.5f;
        out.centre = base + glm::vec3(0.0f, out.half.y, 0.0f);
        break;
    case Cylinder:
        out.mesh = ShellMesh::Cylinder;
        out.half = glm::vec3(size.x * 0.5f, size.y * 0.5f, size.x * 0.5f);
        out.centre = base + glm::vec3(0.0f, out.half.y, 0.0f);
        break;
    default: // Dome: a sphere whose lower half the shader cuts away
        out.mesh = ShellMesh::Sphere;
        out.half = glm::vec3(size.x * 0.5f, size.y, size.x * 0.5f);
        out.centre = base;
        break;
    }
    return kinds::finite3(out.centre);
}

std::size_t records(const E& e, const EffectContext& ctx) {
    kinds::ShellLive live;
    Placed placed;
    return kinds::shellLive(e, ctx, live) && place(e, ctx, placed) ? 1u : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    kinds::ShellLive live;
    if (!kinds::shellLive(e, ctx, live)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    Placed placed;
    if (!place(e, ctx, placed)) {
        return EffectStatus::Dormant;
    }

    // The revealers: the camera, the owner, then the heroes nearest the barrier (ties by list order,
    // so the choice is a pure function of the frame).
    std::array<glm::vec4, kMaxShellExtraPerShell> revealers{};
    std::size_t n = 0;
    if (kRows.f(e, "revealCamera") > 0.5f) {
        revealers[n++] = glm::vec4(ctx.cameraPosition, 1.0f);
    }
    if (e.owner.kind == EffectTarget::Entity && ctx.scene != nullptr) {
        NodeView view;
        if (ctx.scene->nodeView(e.owner.name, view)) {
            const glm::vec3 at = view.hasBounds ? 0.5f * (view.boundsMin + view.boundsMax) : glm::vec3(view.world[3]);
            revealers[n++] = glm::vec4(at, 1.0f);
        }
    }
    const std::size_t heroes = std::min<std::size_t>(static_cast<std::size_t>(kRows.f(e, "revealHeroes") + 0.5f),
                                                     revealers.size() - n);
    // Selection by (distance, list index), each pick the least pair greater than the last: the k
    // nearest, in order, with no scratch and no sort.
    float lastD = -1.0f;
    std::size_t lastH = 0;
    for (std::size_t k = 0; k < heroes; ++k) {
        std::size_t best = ctx.heroes.size();
        float bestD = 0.0f;
        for (std::size_t h = 0; h < ctx.heroes.size(); ++h) {
            if (e.owner.kind == EffectTarget::Entity && ctx.heroes[h].name == e.owner.name) {
                continue; // already there as the owner
            }
            const float d = glm::length(ctx.heroes[h].position - placed.centre);
            const bool after = d > lastD || (d == lastD && h > lastH);
            if (after && (best == ctx.heroes.size() || d < bestD)) {
                best = h;
                bestD = d;
            }
        }
        if (best == ctx.heroes.size()) {
            break;
        }
        revealers[n++] = glm::vec4(ctx.heroes[best].position, 1.0f);
        lastD = bestD;
        lastH = best;
    }

    const float scroll = static_cast<float>(ctx.seconds * static_cast<double>(kRows.f(e, "scrollSpeed")));
    ShellInstance shell;
    shell.model = kinds::shellModel(placed.centre, placed.half, placed.yaw);
    shell.params[0] = glm::vec4(kRows.rgb(e, "color"), std::max(kRows.f(e, "intensity"), 0.0f) * live.envelope);
    shell.params[1] = glm::vec4(kinds::seedOf(e.id), static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(static_cast<float>(kRows.choice(e, "pattern") + 1), std::max(kRows.f(e, "patternScale"), 0.02f),
                                scroll, std::max(kRows.f(e, "idle"), 0.0f));
    shell.params[3] = glm::vec4(std::max(kRows.f(e, "revealRadius"), 0.05f), std::max(kRows.f(e, "revealGain"), 0.0f),
                                std::max(kRows.f(e, "intersectWidth"), 0.0f), std::max(kRows.f(e, "intersectIntensity"), 0.0f));
    shell.params[4] = glm::vec4(static_cast<float>(placed.shape), std::max(kRows.f(e, "edgeGlow"), 0.0f),
                                std::max(kRows.f(e, "edgeWidth"), 0.01f), std::max(kRows.f(e, "rim"), 0.0f));
    shell.params[5] = glm::vec4(std::clamp(kRows.f(e, "lineWidth"), 0.005f, 0.45f), std::clamp(kRows.f(e, "flicker"), 0.0f, 1.0f),
                                std::clamp(kRows.f(e, "backFace"), 0.0f, 1.0f), std::max(kRows.f(e, "fill"), 0.0f));
    if (!sink.append(ShellShading::Barrier, placed.mesh, shell, std::span<const glm::vec4>(revealers.data(), n))) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    if (kRows.f(e, "intersectWidth") > 0.0f && kRows.f(e, "intersectIntensity") > 0.0f) {
        sink.usesDepth();
    }
    return EffectStatus::Drawn;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::ForceField;
    s.key = "forceField";
    s.enumName = "ForceField";
    s.displayName = "Force Field";
    s.description = "A barrier of energy placed in the world -- a wall, a box, a round wall or a dome -- "
                    "with a pattern climbing its surface. Faint until something comes near it; bright "
                    "where the camera or a character approaches, along its edges, and where it meets "
                    "the ground.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Force Field";
    s.addTip = "An energy barrier -- wall, box, cylinder or dome -- that lights up as things approach.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "scrollSpeed";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    registerShellProducer(EffectKind::ForceField, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& forceFieldSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
