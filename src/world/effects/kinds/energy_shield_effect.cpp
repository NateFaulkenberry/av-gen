// Energy Shield (Effect Library Wave 3, catalog-energy.md "Energy Shield"): a protective shell around
// its owner. Mostly invisible; a glowing rim, a cell pattern that shows at the rim and around
// impacts, bright rings travelling out from each hit, and a bright line where the shell cuts the
// ground.
//
// **How it is drawn.** SHELL: a sphere, an ellipsoid fitted to the owner's bounds, or a dome
// standing on the owner's base (world/effects/shell_frame, shaders/shell.wgsl `fs_shield`), both
// faces, the far side dimmer:
//   rim * pow(1 - |N.V|, p)  +  cells * reveal  +  sum_i ring(dist_i - speed * age_i) * decay
//   +  ground line (the shell's depth against the scene's linear depth)  +  a faint sky sheen.
// Additive, depth-tested, no depth write, fogged, blooms, casts no shadow.
//
// **Hits.** TRIGGER: the last `maxHits` event times of the instance's activation (`effectEventTimes`
// -- the beats, onsets, music events, markers, schedule or near passes of a Trigger activation; the
// window's start and every `repeatSeconds` after it otherwise). Each hit lands at a direction hashed
// from the instance's seed and the event's own time, so the same hit lands in the same place in a
// play and after a seek; its age is `t - t0`. No state.
//
// **Presence.** The activation's window and envelope, like every type. Under a Trigger activation
// the window opens at the first event: the shield comes up with the first hit, and a `lifetime`
// makes it a reactive shield that shows only for that long after each one.
//
// **Not here.** The catalogue's Contour mode (an inflated hull around the mesh) needs REDRAW, a Wave 4
// primitive; the optional refraction needs a DF proxy, another slice's files. Hits aimed at a named
// entity are not built: they land at hashed directions.

#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/kinds/shell_kind.hpp"
#include "world/effects/shell_frame.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kShapes[] = {"Sphere", "Ellipsoid", "Dome"};
enum Shape : int { Sphere = 0, Ellipsoid = 1, Dome = 2 };
constexpr const char* kPatterns[] = {"None", "Hex", "Cells"};

constexpr EffectField kFields[] = {
    // ---- Main ------------------------------------------------------------------------------------
    storedChoice("shape", "Shape", 0, kShapes).main()
        .tooltip("Sphere: round, around the owner.\nEllipsoid: stretched to the owner's proportions.\n"
                 "Dome: a half-sphere standing on the owner's base."),
    storedFloat("radius", "Radius", 0.0f, 0.0f, 1000.0f, 0.0f, 30.0f).fmt("%.2f m").main().floorAt(0.0f)
        .tooltip("The shell's radius in metres. 0 fits it round the owner (plus the padding)."),
    storedFloat("padding", "Padding", 0.2f, 0.0f, 4.0f, 0.0f, 1.5f).main().floorAt(0.0f)
        .tooltip("How far past the owner a fitted shell stands, as a fraction of the owner's size."),
    storedColor("color", "Colour", glm::vec3(0.3f, 0.75f, 1.0f)).main().sec("Look"),
    storedFloat("intensity", "Intensity", 1.0f, 0.0f, 50.0f, 0.0f, 6.0f).main().floorAt(0.0f)
        .tooltip("Scales everything the shell shows. Above about 1 its bright parts bloom."),
    storedFloat("rimIntensity", "Rim", 2.5f, 0.0f, 100.0f, 0.0f, 10.0f).main().floorAt(0.0f)
        .tooltip("The glow where the shell turns away from the camera."),
    storedFloat("rimPower", "Rim tightness", 3.0f, 0.5f, 16.0f, 1.0f, 8.0f).main().clampTo(0.5f, 16.0f)
        .tooltip("Higher keeps the rim to a thin edge."),
    storedChoice("pattern", "Pattern", 1, kPatterns).main()
        .tooltip("The cells that show at the rim and where the shell is hit."),
    storedFloat("patternScale", "Cell size", 0.9f, 0.02f, 50.0f, 0.1f, 4.0f).fmt("%.2f m").main().log().floorAt(0.02f),
    storedFloat("idleReveal", "Idle pattern", 0.15f, 0.0f, 1.0f, 0.0f, 1.0f).main().clampTo(0.0f, 1.0f)
        .tooltip("How much of the pattern shows when nothing is hitting the shell."),
    storedFloat("hitIntensity", "Hit brightness", 6.0f, 0.0f, 200.0f, 0.0f, 20.0f).main().sec("Hits").floorAt(0.0f)
        .tooltip("HDR brightness of the rings and flashes a hit makes. Hits come from the activation:\n"
                 "a Trigger activation's events (beats, onsets, drops, markers, near passes)."),
    storedColor("hitColor", "Hit colour", glm::vec3(0.75f, 0.95f, 1.0f)).main(),
    // ---- Advanced --------------------------------------------------------------------------------
    storedFloat("rippleSpeed", "Ring speed", 5.0f, 0.05f, 200.0f, 0.5f, 20.0f).fmt("%.1f m/s").sec("Hits").floorAt(0.05f)
        .tooltip("How fast a hit's ring travels across the shell."),
    storedFloat("rippleWidth", "Ring width", 0.35f, 0.01f, 20.0f, 0.05f, 2.0f).fmt("%.2f m").floorAt(0.01f),
    storedFloat("hitDecay", "Hit decay", 1.4f, 0.05f, 20.0f, 0.2f, 6.0f).fmt("%.2f /s").floorAt(0.05f)
        .tooltip("How fast a hit fades: its ring is at a fifth of its brightness after 1/decay seconds."),
    storedFloat("maxHits", "Hits shown", 6.0f, 1.0f, 8.0f, 1.0f, 8.0f).fmt("%.0f").clampTo(1.0f, 8.0f)
        .tooltip("How many recent hits are drawn at once; the oldest is the one left out."),
    storedFloat("patternIntensity", "Pattern brightness", 1.2f, 0.0f, 50.0f, 0.0f, 6.0f).sec("Look").floorAt(0.0f),
    storedFloat("flicker", "Cell flicker", 0.25f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("Cells shimmer independently."),
    storedFloat("backFace", "Far side", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How bright the far side of the shell is, seen through the near side."),
    storedFloat("reflection", "Sheen", 0.2f, 0.0f, 2.0f, 0.0f, 1.0f).floorAt(0.0f)
        .tooltip("A glassy reflection of the sky, strongest at grazing angles."),
    storedFloat("intersectWidth", "Ground line width", 0.35f, 0.0f, 20.0f, 0.0f, 2.0f).fmt("%.2f m").sec("Ground line").floorAt(0.0f)
        .tooltip("The bright line where the shell cuts the ground or anything else. 0 is none.\n"
                 "Needs the depth prepass, which runs with AO, contact shadows or water."),
    storedFloat("intersectIntensity", "Ground line", 3.0f, 0.0f, 100.0f, 0.0f, 12.0f).floorAt(0.0f),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the shell is centred (a dome: its base). On an entity: an offset\n"
                 "from the owner, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"energyShield", kFields};

// ---- styles ---------------------------------------------------------------------------------------

Trigger repeat(double period) {
    Trigger t;
    t.source = TriggerSource::Repeat;
    t.period = period;
    return t;
}
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
    int shape, pattern;
    glm::vec3 color;
    float intensity, rim, rimPower, cell, idle, hit;
    glm::vec3 hitColor;
    float speed, width, decay, patternIntensity, flicker, backFace, reflection, lineWidth, line;
    Trigger trigger;
};

// Every look writes every look row and what fires its hits (radius, padding and placement are the
// author's).
const Look kLooks[] = {
    // The sci-fi bubble: a clean cyan sphere, a soft rim, hexes only where it is struck -- on onsets.
    {"Sci-Fi Bubble Shield", Sphere, 1, {0.3f, 0.75f, 1.0f}, 1.0f, 2.5f, 4.0f, 0.9f, 0.15f, 6.0f,
     {0.75f, 0.95f, 1.0f}, 5.0f, 0.35f, 1.4f, 1.2f, 0.2f, 0.35f, 0.25f, 0.35f, 3.0f, onsets(1.4f)},
    // The hex barrier: an amber dome whose hexes always show, flaring on every second beat.
    {"Hex Barrier", Dome, 1, {1.0f, 0.62f, 0.18f}, 1.2f, 1.6f, 2.5f, 0.6f, 0.35f, 5.0f,
     {1.0f, 0.85f, 0.5f}, 6.0f, 0.45f, 1.2f, 2.0f, 0.35f, 0.5f, 0.1f, 0.5f, 4.0f, beats(2)},
    // A ward: a violet ellipsoid of organic cells, pulsed on a steady schedule.
    {"Arcane Ward", Ellipsoid, 2, {0.7f, 0.35f, 1.0f}, 1.0f, 2.0f, 2.2f, 0.45f, 0.2f, 4.0f,
     {0.95f, 0.7f, 1.0f}, 3.0f, 0.4f, 1.0f, 1.8f, 0.3f, 0.4f, 0.15f, 0.3f, 2.5f, repeat(1.8)},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "shape", static_cast<float>(l.shape));
    kRows.set(e, "pattern", static_cast<float>(l.pattern));
    kRows.setRgb(e, "color", l.color);
    kRows.set(e, "intensity", l.intensity);
    kRows.set(e, "rimIntensity", l.rim);
    kRows.set(e, "rimPower", l.rimPower);
    kRows.set(e, "patternScale", l.cell);
    kRows.set(e, "idleReveal", l.idle);
    kRows.set(e, "hitIntensity", l.hit);
    kRows.setRgb(e, "hitColor", l.hitColor);
    kRows.set(e, "rippleSpeed", l.speed);
    kRows.set(e, "rippleWidth", l.width);
    kRows.set(e, "hitDecay", l.decay);
    kRows.set(e, "patternIntensity", l.patternIntensity);
    kRows.set(e, "flicker", l.flicker);
    kRows.set(e, "backFace", l.backFace);
    kRows.set(e, "reflection", l.reflection);
    kRows.set(e, "intersectWidth", l.lineWidth);
    kRows.set(e, "intersectIntensity", l.line);
    // The shield comes up with its first hit and stays: no fade to restart on every event.
    e.activation = Activation::Trigger;
    e.timing.trigger = l.trigger;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.lifetime = 0.0;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {"Sci-Fi Bubble Shield", style0}, {"Hex Barrier", style1}, {"Arcane Ward", style2},
};

// Continuous modulation (hits are the trigger's): the owner's speed brightens the rim -- a craft's
// shield strains as it dashes -- and the level brings the pattern up.
constexpr EffectRoute kRoutes[] = {
    {"owner.speed", "rimIntensity", 0.08f, 60.0f, 600.0f},
    {"audio.rms", "idleReveal", 0.6f, 20.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::EnergyShield;
    e.timing = Timing{};
    applyLook(e, kLooks[0]);
    // The plain type is visible anywhere, audio or none: a hit on a steady schedule. The styles
    // switch to the music.
    e.timing.trigger = repeat(1.6);
    e.style.clear();
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

struct Placed {
    glm::vec3 centre{0.0f};
    glm::vec3 half{1.0f}; // the shell's semi-axes
    int shape = Sphere;
};

bool place(const E& e, const EffectContext& ctx, Placed& out) {
    kinds::ShellAnchor anchor;
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    if (!kinds::shellAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    out.shape = kRows.choice(e, "shape");
    const float authored = kRows.f(e, "radius");
    const float pad = 1.0f + std::max(kRows.f(e, "padding"), 0.0f);
    glm::vec3 fit = anchor.halfExtents;
    if (out.shape == Dome) {
        // A dome stands on the owner's base and has to clear its whole height.
        fit.y = 2.0f * anchor.halfExtents.y;
    }
    // A sphere through the box's corners clears it (its half-diagonal). An ellipsoid is fitted by
    // sqrt(2): through the corners of the box's middle section, which is what a round owner -- a
    // saucer, a dome of a tree -- fills, and the padding covers the rest.
    const float corner = std::sqrt(2.0f);
    if (authored > 0.0f) {
        out.half = glm::vec3(authored);
    } else if (out.shape == Sphere) {
        // Round in plan: a sphere through the rim of the owner's widest horizontal circle and its top.
        // Exact for a round owner (a saucer, a tree's crown, a creature seen from above); a boxy one
        // pokes its corners out by at most sqrt(2), which the padding covers.
        const glm::vec3 h = anchor.halfExtents;
        const float plan = std::max(h.x, h.z);
        out.half = glm::vec3(std::max(std::sqrt(plan * plan + h.y * h.y), 0.25f) * pad);
    } else {
        out.half = glm::max(fit * (corner * pad), glm::vec3(0.25f));
    }
    out.centre = anchor.centre;
    if (out.shape == Dome) {
        out.centre.y = anchor.entity ? anchor.baseY : anchor.centre.y;
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
    const float seed = kinds::seedOf(e.id);
    const float decay = std::max(kRows.f(e, "hitDecay"), 0.05f);

    // The hits: newest first, each a direction on the unit shell and an age.
    std::array<double, kMaxShellExtraPerShell> times{};
    const std::size_t want = static_cast<std::size_t>(std::clamp(kRows.f(e, "maxHits"), 1.0f, 8.0f) + 0.5f);
    const std::size_t fired = effectEventTimes(e, ctx, std::span(times).first(std::min(want, times.size())));
    std::array<glm::vec4, kMaxShellExtraPerShell> hits{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < fired; ++i) {
        const double age = ctx.seconds - times[i];
        // A hit faded below one percent is not drawn: ln(100) / decay seconds.
        if (age < 0.0 || age * static_cast<double>(decay) > 4.6) {
            continue;
        }
        glm::vec3 dir = shellHitDirection(seed, times[i]);
        if (placed.shape == Dome) {
            dir.y = std::abs(dir.y); // a dome is struck from above the ground
        }
        hits[n++] = glm::vec4(dir, static_cast<float>(age));
    }

    const float meanRadius = (placed.half.x + placed.half.y + placed.half.z) / 3.0f;
    ShellInstance shell;
    shell.model = kinds::shellModel(placed.centre, placed.half);
    shell.params[0] = glm::vec4(kRows.rgb(e, "color"), std::max(kRows.f(e, "intensity"), 0.0f) * live.envelope);
    shell.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(std::max(kRows.f(e, "rimIntensity"), 0.0f), std::clamp(kRows.f(e, "rimPower"), 0.5f, 16.0f),
                                std::clamp(kRows.f(e, "backFace"), 0.0f, 1.0f), std::clamp(kRows.f(e, "idleReveal"), 0.0f, 1.0f));
    shell.params[3] = glm::vec4(static_cast<float>(kRows.choice(e, "pattern")), std::max(kRows.f(e, "patternScale"), 0.02f),
                                std::max(kRows.f(e, "patternIntensity"), 0.0f), meanRadius);
    shell.params[4] = glm::vec4(std::max(kRows.f(e, "hitIntensity"), 0.0f), std::max(kRows.f(e, "rippleSpeed"), 0.05f),
                                std::max(kRows.f(e, "rippleWidth"), 0.01f), decay);
    shell.params[5] = glm::vec4(std::max(kRows.f(e, "intersectWidth"), 0.0f), std::max(kRows.f(e, "intersectIntensity"), 0.0f),
                                placed.shape == Dome ? 1.0f : 0.0f, std::max(kRows.f(e, "reflection"), 0.0f));
    shell.params[6] = glm::vec4(kRows.rgb(e, "hitColor"), std::clamp(kRows.f(e, "flicker"), 0.0f, 1.0f));
    if (!sink.append(ShellShading::Shield, ShellMesh::Sphere, shell, std::span<const glm::vec4>(hits.data(), n))) {
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
    s.kind = EffectKind::EnergyShield;
    s.key = "energyShield";
    s.enumName = "EnergyShield";
    s.displayName = "Energy Shield";
    s.description = "A protective shell around its owner: nearly invisible, with a glowing rim, a cell "
                    "pattern that shows at the rim and where it is struck, bright rings racing out from "
                    "each hit, and a bright line where it cuts the ground. Hits come from its trigger -- "
                    "beats, onsets, drops, markers or something coming near.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Energy Shield";
    s.addTip = "A force bubble around this entity that ripples where it is hit.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "rimIntensity";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    registerShellProducer(EffectKind::EnergyShield, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& energyShieldSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
