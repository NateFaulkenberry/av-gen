// Growth (Effect Library Wave 2, catalog-organic.md): the owner grows into existence.
//
// **What it is.** FXL's clip sub-block as a reveal FRONT (world/effects/entity_fx.hpp): a threshold
// rises through the owner from its root -- up its height, or out from its node's origin -- and ahead
// of the front there is nothing, at it a bright thin band, and behind it solid, lit, shadowed
// geometry. Noise can break the front up (Crystal Grow). The catalog's reading (b), which is the
// default for arbitrary meshes; reading (a), scale-in, is an XFORM job, and (c), arc-length
// extrusion of a tube, needs `makeTube` to carry arc length and is not done here.
//
// **Every pass.** The discard also runs in the depth prepass and the shadow maps (`fs_depth`), so the
// shadow grows with the owner instead of standing there fully grown.
//
// **Progress.** 0 is nothing, 1 is fully grown, and a fully grown owner is untouched (no clip, no
// edge). With a `Duration` the growth runs 0 -> `Progress` over that many seconds after the
// activation window opens, so a Window activation (or TRIGGER, when it lands) plays it. When the
// effect is not live the owner is simply there, fully grown -- which is the neutral state.
//
// **Stacking.** Growth and Dissolve share the clip sub-block: two Growths of the same mode take the
// larger hidden fraction; a Growth and a Dissolve on one owner are two different fronts and the
// second is Dropped by name.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kModeNames[] = {"Upward", "Outward", "Scattered"};

constexpr float kProgress = 0.6f;
constexpr float kEdgeWidth = 0.035f;
constexpr glm::vec3 kEdgeColor{0.45f, 1.0f, 0.62f};
constexpr float kEdgeEmission = 8.0f;
constexpr float kBreakup = 0.35f;
constexpr float kScale = 5.0f;
constexpr float kDuration = 0.0f;

constexpr EffectField kFields[] = {
    storedFloat("progress", "Progress", kProgress, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How far it has grown: 0 nothing, 1 fully grown. Key it, route it, or give\n"
                 "it a Duration and a Window activation to play it."),
    storedChoice("mode", "Grows", 0, kModeNames).main()
        .tooltip("Upward: from the bottom of the entity to the top.\n"
                 "Outward: from its origin (a tree's root, a pod's centre) out.\n"
                 "Scattered: patches appear all over, like crystals forming."),
    storedColor("edgeColor", "Edge colour", kEdgeColor).main(),
    storedFloat("edgeEmission", "Edge brightness", kEdgeEmission, 0.0f, 500.0f, 0.0f, 60.0f).main().floorAt(0.0f)
        .tooltip("The growing front's light. 0 is a clean reveal with no glow."),
    storedFloat("edgeWidth", "Edge width", kEdgeWidth, 0.002f, 0.5f, 0.005f, 0.2f).fmt("%.3f").floorAt(0.001f)
        .tooltip("How thick the glowing band at the front is, as a fraction of the entity."),
    storedFloat("noiseBreakup", "Ragged front", kBreakup, 0.0f, 1.0f, 0.0f, 1.0f).sec("Front")
        .tooltip("How much noise breaks up the front. 0 is a clean level line."),
    storedFloat("scale", "Raggedness scale", kScale, 0.25f, 64.0f, 0.5f, 16.0f).fmt("%.1f")
        .tooltip("How fine the front's raggedness (and the Scattered patches) are."),
    storedFloat("duration", "Duration", kDuration, 0.0f, 120.0f, 0.0f, 10.0f).fmt("%.1f s").sec("Timing")
        .tooltip("0: Progress is used as it is. Above 0: after the activation window\n"
                 "opens, it grows from nothing to Progress over this many seconds."),
};

void set(E& e, float progress, int mode, glm::vec3 edge, float emission, float width, float breakup, float scale) {
    e.values.setFloat("growth/progress", progress);
    e.values.setFloat("growth/mode", static_cast<float>(mode));
    e.values.setColor("growth/edgeColor", edge);
    e.values.setFloat("growth/edgeEmission", emission);
    e.values.setFloat("growth/edgeWidth", width);
    e.values.setFloat("growth/noiseBreakup", breakup);
    e.values.setFloat("growth/scale", scale);
}

constexpr const char* kStyleNames[] = {"Sprout", "Bloom Out", "Crystal Grow"};

void sprout(E& e) {
    set(e, kProgress, 0, kEdgeColor, kEdgeEmission, kEdgeWidth, kBreakup, kScale);
    e.style = kStyleNames[0];
}
void bloomOut(E& e) {
    set(e, kProgress, 1, {1.0f, 0.55f, 0.85f}, 10.0f, 0.05f, 0.5f, 4.0f);
    e.style = kStyleNames[1];
}
void crystalGrow(E& e) {
    set(e, kProgress, 2, {0.55f, 0.85f, 1.0f}, 18.0f, 0.012f, 0.0f, 7.0f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], sprout},
    {kStyleNames[1], bloomOut},
    {kStyleNames[2], crystalGrow},
};

// Growth that follows the music's build (the catalog's `audio.rms -> progress`): a loud passage
// pushes a partly grown owner further on, and it settles back as the music falls.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "progress", 0.15f, 200.0f, 1200.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Growth;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    set(e, kProgress, 0, kEdgeColor, kEdgeEmission, kEdgeWidth, kBreakup, kScale);
    e.values.setFloat("growth/duration", kDuration);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double local, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    float grown = std::clamp(v.getFloat("growth/progress", kProgress), 0.0f, 1.0f);
    const float duration = std::max(v.getFloat("growth/duration", kDuration), 0.0f);
    if (duration > 0.0f) {
        const float x = std::clamp(static_cast<float>(local) / duration, 0.0f, 1.0f);
        grown *= x * x * (3.0f - 2.0f * x);
    }
    const int mode = std::clamp(static_cast<int>(v.getFloat("growth/mode", 0.0f) + 0.5f), 0, 2);
    c.hasClip = true;
    c.clipMode = mode == 0 ? EntityFxClipMode::Height : mode == 1 ? EntityFxClipMode::Radial : EntityFxClipMode::Noise;
    c.clipHidden = 1.0f - grown;
    c.clipNoiseScale = std::max(v.getFloat("growth/scale", kScale), 0.01f);
    c.clipEdgeWidth = std::max(v.getFloat("growth/edgeWidth", kEdgeWidth), 0.001f);
    c.clipEdge = glm::max(v.getColor("growth/edgeColor", kEdgeColor), glm::vec3(0.0f)) *
                 std::max(v.getFloat("growth/edgeEmission", kEdgeEmission), 0.0f);
    c.clipBreakup = mode == 2 ? 0.0f : std::clamp(v.getFloat("growth/noiseBreakup", kBreakup), 0.0f, 1.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Growth;
    s.key = "growth";
    s.enumName = "Growth";
    s.displayName = "Growth";
    s.description = "The entity grows into existence: a front rises from its root -- up, out, or in scattered "
                    "patches -- with a glowing band at the growing edge. Its shadow grows with it.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Growth";
    s.addTip = "This entity grows in from its root with a glowing front, shadow and all.\n"
               "Key Progress, or set a Duration and a Window activation.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "edgeEmission";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& growthSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
