// Dissolve (Effect Library Wave 2, catalog-stylization.md): the owner disintegrates -- or
// materialises -- through a noisy threshold with a glowing, charring edge.
//
// **What it is.** FXL's clip sub-block in Noise mode (world/effects/entity_fx.hpp). Every fragment of
// the owner has a keep-value k: a 3-octave value noise in the owner's own space (so the holes ride
// the owner as it moves), optionally swept by height. A fragment below the threshold is discarded;
// just above it the edge burns in the edge colour, and a little further up the surface chars dark.
//
// **Every pass.** The same test runs in `fs_depth` -- the depth prepass AND every shadow map -- and in
// the procedural depth entry, so a half-dissolved owner's shadow has holes where the owner does. A
// program-driven dissolve cannot do that (`fs_depth` runs no material programs), which is why this is
// an entity lane and not a material op.
//
// **Progress.** `Progress` is the keyable heart of it: 0 whole, 1 gone. With a `Duration`, the
// progress ramps from 0 to `Progress` over that many seconds after the activation window opens, so a
// Window (or, when it lands, a TRIGGER) activation plays the dissolve without a keyframe. `Reverse`
// runs it backwards: Materialise. The activation envelope fades the whole clip towards "nothing
// hidden", so a dissolve whose window closes comes back (as a fade-in of the owner through the noise).
//
// **Stacking.** Dissolve and Growth share the clip: two Dissolves on one owner take the larger
// progress (rendering-architecture §7, "clip takes the max"); a Dissolve and a Growth together are
// two different fronts, and the second is Dropped by name. The edge sums with any Glow.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kDirectionNames[] = {"Everywhere", "Upward", "Downward"};

constexpr float kProgress = 0.5f;
constexpr float kScale = 4.0f;
constexpr float kEdgeWidth = 0.06f;
constexpr glm::vec3 kEdgeColor{1.0f, 0.42f, 0.1f};
constexpr float kEdgeEmission = 14.0f;
constexpr float kBias = 0.5f;
constexpr float kDuration = 0.0f;

constexpr EffectField kFields[] = {
    storedFloat("progress", "Progress", kProgress, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How much of the entity has dissolved: 0 whole, 1 gone. Key it, route it,\n"
                 "or give it a Duration and a Window activation to play it."),
    storedColor("edgeColor", "Edge colour", kEdgeColor).main(),
    storedFloat("edgeEmission", "Edge brightness", kEdgeEmission, 0.0f, 500.0f, 0.0f, 60.0f).main().floorAt(0.0f)
        .tooltip("The burning edge's light. Blooms as a light source."),
    storedFloat("scale", "Hole size", kScale, 0.25f, 64.0f, 0.5f, 16.0f).fmt("%.1f")
        .tooltip("How fine the holes are: noise cells across the entity. Higher is finer."),
    storedFloat("edgeWidth", "Edge width", kEdgeWidth, 0.002f, 0.5f, 0.01f, 0.2f).fmt("%.3f").floorAt(0.001f)
        .tooltip("How wide the burning band around each hole is."),
    storedChoice("direction", "Direction", 0, kDirectionNames).sec("Sweep")
        .tooltip("Everywhere: holes open all over at once. Upward: it goes from the bottom\n"
                 "up. Downward: from the top down."),
    storedFloat("directionBias", "Sweep strength", kBias, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How much the sweep direction wins over the noise. 1 is a clean line."),
    storedFloat("duration", "Duration", kDuration, 0.0f, 120.0f, 0.0f, 10.0f).fmt("%.1f s").sec("Timing")
        .tooltip("0: Progress is used as it is. Above 0: after the activation window\n"
                 "opens, the progress climbs from 0 to Progress over this many seconds."),
    storedBool("reverse", "Materialise (reverse)", false)
        .tooltip("Run it backwards: the entity starts gone and forms out of the noise."),
};

void set(E& e, float progress, glm::vec3 edge, float emission, float scale, float width, int direction, float bias,
         bool reverse) {
    e.values.setFloat("dissolve/progress", progress);
    e.values.setColor("dissolve/edgeColor", edge);
    e.values.setFloat("dissolve/edgeEmission", emission);
    e.values.setFloat("dissolve/scale", scale);
    e.values.setFloat("dissolve/edgeWidth", width);
    e.values.setFloat("dissolve/direction", static_cast<float>(direction));
    e.values.setFloat("dissolve/directionBias", bias);
    e.values.setBool("dissolve/reverse", reverse);
}

constexpr const char* kStyleNames[] = {"Burn Away", "Teleport Out", "Materialise"};

void burnAway(E& e) {
    set(e, kProgress, kEdgeColor, kEdgeEmission, kScale, kEdgeWidth, 0, kBias, false);
    e.style = kStyleNames[0];
}
void teleportOut(E& e) {
    set(e, kProgress, {0.35f, 0.75f, 1.0f}, 24.0f, 9.0f, 0.04f, 1, 0.7f, false);
    e.style = kStyleNames[1];
}
void materialise(E& e) {
    set(e, kProgress, {0.6f, 1.0f, 0.85f}, 10.0f, 6.0f, 0.05f, 0, kBias, true);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], burnAway},
    {kStyleNames[1], teleportOut},
    {kStyleNames[2], materialise},
};

// The edge flares with the music's loudness.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "edgeEmission", 20.0f, 20.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Dissolve;
    e.activation = Activation::Always;
    // A dissolve's envelope is the dissolve: no fade on top of it.
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    set(e, kProgress, kEdgeColor, kEdgeEmission, kScale, kEdgeWidth, 0, kBias, false);
    e.values.setFloat("dissolve/duration", kDuration);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double local, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    float progress = std::clamp(v.getFloat("dissolve/progress", kProgress), 0.0f, 1.0f);
    const float duration = std::max(v.getFloat("dissolve/duration", kDuration), 0.0f);
    if (duration > 0.0f) {
        const float x = std::clamp(static_cast<float>(local) / duration, 0.0f, 1.0f);
        progress *= x * x * (3.0f - 2.0f * x);
    }
    const bool reverse = v.getBool("dissolve/reverse", false);
    const int direction = std::clamp(static_cast<int>(v.getFloat("dissolve/direction", 0.0f) + 0.5f), 0, 2);
    const float bias = std::clamp(v.getFloat("dissolve/directionBias", kBias), 0.0f, 1.0f);
    c.hasClip = true;
    c.clipMode = EntityFxClipMode::Noise;
    c.clipHidden = reverse ? 1.0f - progress : progress;
    c.clipNoiseScale = std::max(v.getFloat("dissolve/scale", kScale), 0.01f);
    c.clipEdgeWidth = std::max(v.getFloat("dissolve/edgeWidth", kEdgeWidth), 0.001f);
    c.clipEdge = glm::max(v.getColor("dissolve/edgeColor", kEdgeColor), glm::vec3(0.0f)) *
                 std::max(v.getFloat("dissolve/edgeEmission", kEdgeEmission), 0.0f);
    // Upward: the bottom goes first (a sweep by h); Downward: the top (by 1 - h, a negative bias).
    c.clipBreakup = direction == 1 ? bias : direction == 2 ? -bias : 0.0f;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Dissolve;
    s.key = "dissolve";
    s.enumName = "Dissolve";
    s.displayName = "Dissolve";
    s.description = "The entity disintegrates through a noisy threshold with a burning, charring edge -- or, "
                    "reversed, forms out of it. The holes are real: its depth and its shadow dissolve with it.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Dissolve";
    s.addTip = "This entity burns away through holes in a noise pattern, edges glowing -- or\n"
               "materialises out of it. Its shadow dissolves too. Key Progress to animate it.";
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

const EffectSchema& dissolveSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
