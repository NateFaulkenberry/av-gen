// Fresnel (Effect Library Wave 2, catalog-stylization.md): a view-angle rim of light on the owner.
//
// **What it is.** The artistic Fresnel term, `color * (scale * (1 - N.V)^power + bias)`, added to the
// owner's emission before fog and into the emission target. It is the SAME rim lane Glow's rim uses
// (entity_fx.hpp lane 3, and the bias through the added-emission lane 2), so the two share it by the
// §7 rule for rims -- they sum, and the falloff is the strength-weighted mean -- and a Fresnel adds
// nothing to the record but its numbers.
//
// **Not here.** The catalog's Opacity mode (edges opaque, the centre see-through: the x-ray look)
// needs the owner re-routed to the blended pipeline, which is the Hologram re-route the shared
// infrastructure describes and no slice has built yet. This is Additive only; the X-Ray preset is
// therefore not offered rather than offered as a lie.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr glm::vec3 kColor{0.55f, 0.85f, 1.0f};
constexpr float kScale = 3.0f;
constexpr float kPower = 3.0f;
constexpr float kBias = 0.0f;

constexpr EffectField kFields[] = {
    storedColor("color", "Colour", kColor).main(),
    storedFloat("scale", "Strength", kScale, 0.0f, 200.0f, 0.0f, 20.0f).main().floorAt(0.0f)
        .tooltip("How bright the edge is where the surface turns fully away from the camera."),
    storedFloat("power", "Tightness", kPower, 0.25f, 16.0f, 0.5f, 8.0f).main().floorAt(0.05f)
        .tooltip("How close to the outline the light sits. Low is a wide soft sheen, high a\n"
                 "thin line."),
    storedFloat("bias", "Base", kBias, 0.0f, 50.0f, 0.0f, 2.0f).floorAt(0.0f)
        .tooltip("Light added evenly across the face as well, in the same colour."),
};

void set(E& e, glm::vec3 color, float scale, float power, float bias) {
    e.values.setColor("fresnel/color", color);
    e.values.setFloat("fresnel/scale", scale);
    e.values.setFloat("fresnel/power", power);
    e.values.setFloat("fresnel/bias", bias);
}

constexpr const char* kStyleNames[] = {"Soft Edge", "Ghost", "Ice"};

void softEdge(E& e) {
    set(e, kColor, kScale, kPower, kBias);
    e.style = kStyleNames[0];
}
void ghost(E& e) {
    set(e, {0.8f, 0.95f, 1.0f}, 2.0f, 1.4f, 0.15f);
    e.style = kStyleNames[1];
}
void ice(E& e) {
    set(e, {0.45f, 0.75f, 1.0f}, 6.0f, 5.0f, 0.0f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], softEdge},
    {kStyleNames[1], ghost},
    {kStyleNames[2], ice},
};

// The treble catches the edges.
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "scale", 4.0f, 15.0f, 250.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Fresnel;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.5;
    set(e, kColor, kScale, kPower, kBias);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const glm::vec3 color = glm::max(v.getColor("fresnel/color", kColor), glm::vec3(0.0f));
    c.rim = color * std::max(v.getFloat("fresnel/scale", kScale), 0.0f);
    c.rimPower = std::max(v.getFloat("fresnel/power", kPower), 0.05f);
    c.add = color * std::max(v.getFloat("fresnel/bias", kBias), 0.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Fresnel;
    s.key = "fresnel";
    s.enumName = "Fresnel";
    s.displayName = "Fresnel";
    s.description = "A soft coloured light along the entity's silhouette, strongest where its surface turns "
                    "away from the camera, and consistent as the camera moves. Blooms as light.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Fresnel";
    s.addTip = "A coloured edge light that follows this entity's outline from any angle.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "scale";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& fresnelSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
