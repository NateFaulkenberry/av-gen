// Rim Light (Effect Library Wave 2, catalog-stylization.md): a cinematographer's kicker on the owner.
//
// **What it is.** A stylised back-light rim, lit only where the surface both turns away from the
// camera (grazing, `(1 - N.V)^power`) AND faces a rim direction -- `smoothstep` of N.L around a
// threshold, so a small softness gives a toon-crisp edge. Unlike Fresnel, which is view-only and
// lights the whole outline, this lights ONE side, as if a light sat behind and to the side. The
// direction is fixed in VIEW space (x right, y up, z away from the camera), so it frames the same way
// wherever the camera is: FXL's rim-light sub-block in the extension record (entity_fx.hpp).
//
// It is not a real light: it casts no shadow, costs no light slot, and lights nothing but its owner.
// Added before fog and into the emission target, so it blooms.
//
// **Not here.** The catalog's `source = Light` (the direction of a named Light endpoint) and a Light
// owner applying rim to entities in its radius are a later wave.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr glm::vec3 kColor{1.0f, 0.86f, 0.62f};
constexpr float kIntensity = 5.0f;
constexpr float kPower = 2.0f;
constexpr float kAzimuth = -40.0f;
constexpr float kElevation = 25.0f;
constexpr float kThreshold = 0.0f;
constexpr float kSoftness = 0.3f;

constexpr EffectField kFields[] = {
    storedColor("color", "Colour", kColor).main(),
    storedFloat("intensity", "Brightness", kIntensity, 0.0f, 500.0f, 0.0f, 30.0f).main().floorAt(0.0f),
    storedFloat("azimuth", "From the side", kAzimuth, -180.0f, 180.0f, -180.0f, 180.0f).fmt("%.0f deg").main()
        .tooltip("Where the kicker sits around the entity, seen from the camera: 0 directly\n"
                 "behind it, negative to the left, positive to the right."),
    storedFloat("elevation", "From above", kElevation, -89.0f, 89.0f, -89.0f, 89.0f).fmt("%.0f deg")
        .tooltip("How high the kicker sits, seen from the camera."),
    storedFloat("power", "Tightness", kPower, 0.25f, 16.0f, 0.5f, 8.0f).floorAt(0.05f)
        .tooltip("How close to the outline the rim sits."),
    storedFloat("threshold", "Threshold", kThreshold, -1.0f, 1.0f, -1.0f, 1.0f).sec("Edge")
        .tooltip("How far round the lit side reaches. Higher keeps the rim to the side that\n"
                 "faces the kicker most directly."),
    storedFloat("softness", "Softness", kSoftness, 0.001f, 1.0f, 0.005f, 1.0f).floorAt(0.001f)
        .tooltip("How soft the rim's end is. Small is a crisp, cel-shaded edge."),
};

void set(E& e, glm::vec3 color, float intensity, float azimuth, float elevation, float power, float threshold,
         float softness) {
    e.values.setColor("rimLight/color", color);
    e.values.setFloat("rimLight/intensity", intensity);
    e.values.setFloat("rimLight/azimuth", azimuth);
    e.values.setFloat("rimLight/elevation", elevation);
    e.values.setFloat("rimLight/power", power);
    e.values.setFloat("rimLight/threshold", threshold);
    e.values.setFloat("rimLight/softness", softness);
}

constexpr const char* kStyleNames[] = {"Kicker", "Neon Edge", "Toon Rim"};

void kicker(E& e) {
    set(e, kColor, kIntensity, kAzimuth, kElevation, kPower, kThreshold, kSoftness);
    e.style = kStyleNames[0];
}
void neonEdge(E& e) {
    set(e, {1.0f, 0.2f, 0.85f}, 12.0f, 60.0f, 10.0f, 3.5f, 0.0f, 0.2f);
    e.style = kStyleNames[1];
}
void toonRim(E& e) {
    set(e, {1.0f, 1.0f, 0.95f}, 4.0f, -60.0f, 30.0f, 1.2f, 0.25f, 0.01f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], kicker},
    {kStyleNames[1], neonEdge},
    {kStyleNames[2], toonRim},
};

// The catalog's pair: the kick flashes the kicker, and the bass holds it up between kicks.
constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "intensity", 6.0f, 10.0f, 260.0f},
    {"audio.bass", "intensity", 3.0f, 60.0f, 500.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::RimLight;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.5;
    set(e, kColor, kIntensity, kAzimuth, kElevation, kPower, kThreshold, kSoftness);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const float az = glm::radians(v.getFloat("rimLight/azimuth", kAzimuth));
    const float el = glm::radians(std::clamp(v.getFloat("rimLight/elevation", kElevation), -89.0f, 89.0f));
    c.hasRimLight = true;
    c.rimLight = glm::max(v.getColor("rimLight/color", kColor), glm::vec3(0.0f)) *
                 std::max(v.getFloat("rimLight/intensity", kIntensity), 0.0f);
    c.rimLightPower = std::max(v.getFloat("rimLight/power", kPower), 0.05f);
    // View space: x right, y up, z away from the camera. Azimuth 0 is straight behind the owner.
    c.rimLightDir = glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
    c.rimLightThreshold = std::clamp(v.getFloat("rimLight/threshold", kThreshold), -1.0f, 1.0f);
    c.rimLightSoftness = std::max(v.getFloat("rimLight/softness", kSoftness), 0.001f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::RimLight;
    s.key = "rimLight";
    s.enumName = "RimLight";
    s.displayName = "Rim Light";
    s.description = "A stylised back-light on one side of the entity's silhouette, as if a kicker light sat behind "
                    "and to the side of it. Fixed relative to the camera, so it frames well from anywhere.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Rim Light";
    s.addTip = "A bright edge on one side of this entity's outline, like a light behind it --\n"
               "soft and cinematic, or crisp and cel-shaded.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "intensity";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& rimLightSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
