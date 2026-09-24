// Glow (Effect Library, catalog-light.md): the owner emits light from its surface.
//
// **What it is.** A uniform emissive boost on ONE entity -- one saucer of three, not every user of
// its material -- with an optional view-dependent rim and an optional spill light, so a glowing
// thing also lights its neighbours. It reaches the picture through FXL (world/effects/entity_fx.hpp):
//
//   * `Brightness` multiplies ALL of the owner's emission: its material's own light, and what this
//     and any other lane effect add. Two Glows on one owner multiply (rendering-architecture §7).
//   * `Self-glow` adds radiance in the glow's colour over the whole surface, so a Glow on a thing
//     with no emissive material still glows. Added emission from several Glows sums.
//   * `Rim` adds radiance at grazing angles, `pow(1 - N.V, tightness)`; rims sum.
//   * The spill is a real point light (LIGHTMOD's pool, 16 slots, unshadowed) at the owner's centre
//     in the glow's colour, reaching `Spill reach` times the owner's radius. When the pool is full
//     the glow still draws and reports `Partial`, saying so.
//
// Everything is added BEFORE fog (a glow down the valley is seen through the air) and into the
// emission target, so selective bloom (`post/bloom/emissionWeight`) blooms it as a light source.
//
// **What it deliberately is not.** "Every user of this material glows" is a material-program
// Emission op and needs no effect. A Glow on a LIGHT is that light's intensity, which is Pulse's or
// Flicker's job; the type targets Entity only.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

// Defaults, shared by the rows and the reader so the two cannot disagree about an absent value.
constexpr float kGain = 2.0f;
constexpr glm::vec3 kTint{0.35f, 0.85f, 1.0f};
constexpr float kSelfGlow = 1.5f;
constexpr float kRim = 2.0f;
constexpr glm::vec3 kRimColor{0.6f, 0.95f, 1.0f};
constexpr float kRimPower = 3.0f;
constexpr float kRecolour = 0.0f;
constexpr float kSpillIntensity = 12.0f;
constexpr float kSpillRange = 3.0f;
constexpr float kSpillFog = 0.0f;

constexpr EffectField kFields[] = {
    storedFloat("gain", "Brightness", kGain, 0.0f, 50.0f, 0.0f, 12.0f).main().floorAt(0.0f)
        .tooltip("Multiplies all of this entity's light: its material's own emission and\n"
                 "the glow added below. 1 leaves the material's emission as it is. Two Glows\n"
                 "on one entity multiply; a Pulse on it multiplies this too."),
    storedColor("tint", "Colour", kTint).main(),
    storedFloat("glow", "Self-glow", kSelfGlow, 0.0f, 100.0f, 0.0f, 12.0f).main().floorAt(0.0f)
        .tooltip("Light added over the whole surface in the glow's colour, so a Glow on\n"
                 "something with no emissive material still glows. Bloom sees it as a light."),
    storedFloat("rim", "Rim", kRim, 0.0f, 100.0f, 0.0f, 12.0f).main().floorAt(0.0f)
        .tooltip("Extra light where the surface turns away from the camera -- the edge of\n"
                 "a glowing object. 0 is a flat glow."),
    storedBool("spill", "Light the surroundings", false).main(),
    storedColor("rimColor", "Rim colour", kRimColor).sec("Rim"),
    storedFloat("rimPower", "Rim tightness", kRimPower, 0.5f, 16.0f, 0.5f, 8.0f).floorAt(0.05f)
        .tooltip("How close to the silhouette the rim sits. Low is a wide halo across the\n"
                 "body, high is a thin line on the outline."),
    storedFloat("recolour", "Tint own light", kRecolour, 0.0f, 1.0f, 0.0f, 1.0f).sec("Own emission")
        .tooltip("How much the entity's own emissive light takes the glow's colour. It\n"
                 "multiplies: a red light tinted blue gets darker, not purple."),
    storedFloat("spillIntensity", "Spill brightness", kSpillIntensity, 0.0f, 2000.0f, 0.0f, 150.0f)
        .sec("Spill light").floorAt(0.0f)
        .tooltip("The spill light's intensity at Brightness 1 (candela). It follows the\n"
                 "entity's brightness, so a pulsing glow pulses the light it throws.\n"
                 "One of the 16 effect lights; when they are all taken the glow still\n"
                 "draws and its status says Partial."),
    storedFloat("spillRange", "Spill reach", kSpillRange, 0.5f, 20.0f, 0.5f, 8.0f).fmt("%.1f x size")
        .floorAt(0.1f)
        .tooltip("How far the spill reaches, in multiples of the entity's own radius."),
    storedFloat("spillFog", "Spill in fog", kSpillFog, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How much the spill light lights the volumetric fog around the entity."),
};

// ---- styles ------------------------------------------------------------------------------------

void set(E& e, float gain, glm::vec3 tint, float glow, float rim, glm::vec3 rimColor, float rimPower,
         bool spill, float spillIntensity) {
    e.values.setFloat("glow/gain", gain);
    e.values.setColor("glow/tint", tint);
    e.values.setFloat("glow/glow", glow);
    e.values.setFloat("glow/rim", rim);
    e.values.setColor("glow/rimColor", rimColor);
    e.values.setFloat("glow/rimPower", rimPower);
    e.values.setBool("glow/spill", spill);
    e.values.setFloat("glow/spillIntensity", spillIntensity);
}

constexpr const char* kStyleNames[] = {"Soft", "Neon", "Bioluminescent", "Hot Core", "Radioactive"};

void soft(E& e) {
    set(e, 1.5f, {1.0f, 0.82f, 0.58f}, 1.0f, 0.6f, {1.0f, 0.9f, 0.7f}, 2.0f, false, 8.0f);
    e.style = kStyleNames[0];
}
void neon(E& e) {
    set(e, 2.0f, {1.0f, 0.18f, 0.78f}, 0.5f, 8.0f, {1.0f, 0.35f, 0.9f}, 4.5f, false, 10.0f);
    e.style = kStyleNames[1];
}
void bioluminescent(E& e) {
    set(e, 1.2f, {0.18f, 0.95f, 0.78f}, 0.9f, 1.6f, {0.5f, 1.0f, 0.9f}, 2.5f, true, 6.0f);
    e.style = kStyleNames[2];
}
void hotCore(E& e) {
    set(e, 4.0f, {1.0f, 0.55f, 0.2f}, 4.0f, 1.0f, {1.0f, 0.8f, 0.5f}, 1.5f, true, 24.0f);
    e.style = kStyleNames[3];
}
void radioactive(E& e) {
    set(e, 2.5f, {0.45f, 1.0f, 0.18f}, 2.5f, 3.0f, {0.7f, 1.0f, 0.4f}, 3.0f, true, 14.0f);
    e.style = kStyleNames[4];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], soft},        {kStyleNames[1], neon},        {kStyleNames[2], bioluminescent},
    {kStyleNames[3], hotCore},     {kStyleNames[4], radioactive},
};

// The catalog's default: the glow breathes with the beat. Brightness is a multiplier, so the route
// lifts it by up to one whole step on the kick and lets it fall back over a quarter second.
constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "gain", 1.0f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Glow;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.5;
    set(e, kGain, kTint, kSelfGlow, kRim, kRimColor, kRimPower, false, kSpillIntensity);
    return e;
}

// ---- resolve -----------------------------------------------------------------------------------

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const glm::vec3 tint = glm::max(v.getColor("glow/tint", kTint), glm::vec3(0.0f));
    c.gain = std::max(v.getFloat("glow/gain", kGain), 0.0f);
    c.add = tint * std::max(v.getFloat("glow/glow", kSelfGlow), 0.0f);
    c.rim = glm::max(v.getColor("glow/rimColor", kRimColor), glm::vec3(0.0f)) *
            std::max(v.getFloat("glow/rim", kRim), 0.0f);
    c.rimPower = std::max(v.getFloat("glow/rimPower", kRimPower), 0.05f);
    // A multiply towards the colour normalised to its brightest channel, so "tint fully" keeps the
    // light's strongest channel and does not also dim it.
    const float peak = std::max({tint.r, tint.g, tint.b, 1e-4f});
    const float recolour = std::clamp(v.getFloat("glow/recolour", kRecolour), 0.0f, 1.0f);
    c.ownTint = glm::vec3(1.0f) + (tint / peak - glm::vec3(1.0f)) * recolour;
    c.spill = v.getBool("glow/spill", false);
    c.spillColor = tint / peak;
    c.spillIntensity = std::max(v.getFloat("glow/spillIntensity", kSpillIntensity), 0.0f);
    c.spillRange = std::max(v.getFloat("glow/spillRange", kSpillRange), 0.1f);
    c.spillFog = std::clamp(v.getFloat("glow/spillFog", kSpillFog), 0.0f, 1.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Glow;
    s.key = "glow";
    s.enumName = "Glow";
    s.displayName = "Glow";
    s.description = "The entity emits light from its surface: a brightness on its own emission, an added "
                    "glow in a colour, an optional rim at its silhouette, and an optional spill light "
                    "that lights what is around it. Blooms as a light source.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Glow";
    s.addTip = "This entity emits light: an added glow, a rim at its outline, and optionally a\n"
               "real light on its surroundings. Only this entity -- not every user of its material.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "gain";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& glowSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
