// Motion Smear (Effect Library Wave 2, catalog-motion.md): the trailing side of a fast owner stretches
// back along its path, as in a 2D animator's smear frame.
//
// **What it is.** FXL's smear sub-block (entity_fx.hpp). The owner's measured velocity v -- from HIST,
// `EffectSceneQuery::nodeVelocity`, a backward difference on the fixed step grid, so it is the same
// played or scrubbed and not a frame-rate-dependent one-frame delta -- becomes one smear vector
// s = -v * seconds * amount (limited to `Max stretch`, faded in above `Min speed`). The vertex stage
// moves each vertex by `s * saturate(N . s^)^sharpness`: the back half, whose normals face away from
// the motion, is pulled towards where the owner was; the front stays crisp. It is not motion blur
// (post already has that); it is geometry, so it is lit, shadowed and depth-correct, in every pass.
//
// **Velocity target.** The previous frame's position is displaced by the SAME smear (the catalog's
// "or accept a small error"): the velocity target ignores the smear's own change over one frame. The
// smear changes as fast as the owner accelerates, which for a craft that is dashing and stopping is a
// one-frame error of a fraction of the stretch.
//
// **Stacking.** One smear per owner; it sums with a Breathing and an Organic Pulsation.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr float kAmount = 1.0f;
constexpr float kSeconds = 0.08f;
constexpr float kSharpness = 1.5f;
constexpr float kMinSpeed = 2.0f;
constexpr float kMaxStretch = 3.0f;

constexpr EffectField kFields[] = {
    storedFloat("amount", "Amount", kAmount, 0.0f, 10.0f, 0.0f, 3.0f).main().floorAt(0.0f)
        .tooltip("How much of the smear to draw. 0 is none."),
    storedFloat("smearSeconds", "Smear length", kSeconds, 0.0f, 2.0f, 0.0f, 0.4f).fmt("%.2f s").main()
        .tooltip("How far back the trailing side reaches, in seconds of the entity's travel."),
    storedFloat("sharpness", "Sharpness", kSharpness, 0.1f, 16.0f, 0.25f, 6.0f).floorAt(0.05f).main()
        .tooltip("How much of the back is pulled. Low stretches the whole back half; high\n"
                 "only the very tail."),
    storedFloat("minSpeed", "Min speed", kMinSpeed, 0.0f, 200.0f, 0.0f, 20.0f).fmt("%.1f m/s").sec("Limits")
        .tooltip("Below this speed there is no smear, so a drifting entity stays crisp."),
    storedFloat("maxStretch", "Max stretch", kMaxStretch, 0.0f, 100.0f, 0.0f, 20.0f).fmt("%.1f m")
        .tooltip("The longest the smear may get, in metres, however fast the entity goes."),
};

void set(E& e, float amount, float seconds, float sharpness, float minSpeed, float maxStretch) {
    e.values.setFloat("motionSmear/amount", amount);
    e.values.setFloat("motionSmear/smearSeconds", seconds);
    e.values.setFloat("motionSmear/sharpness", sharpness);
    e.values.setFloat("motionSmear/minSpeed", minSpeed);
    e.values.setFloat("motionSmear/maxStretch", maxStretch);
}

constexpr const char* kStyleNames[] = {"Cartoon Smear", "Speedster", "Subtle"};

void cartoon(E& e) {
    set(e, kAmount, kSeconds, kSharpness, kMinSpeed, kMaxStretch);
    e.style = kStyleNames[0];
}
void speedster(E& e) {
    set(e, 1.0f, 0.15f, 0.8f, 5.0f, 8.0f);
    e.style = kStyleNames[1];
}
void subtle(E& e) {
    set(e, 0.6f, 0.05f, 2.5f, 3.0f, 1.0f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], cartoon},
    {kStyleNames[1], speedster},
    {kStyleNames[2], subtle},
};

// An onset snaps an extra stretch in.
constexpr EffectRoute kRoutes[] = {
    {"audio.onset", "amount", 0.6f, 5.0f, 200.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::MotionSmear;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.25;
    set(e, kAmount, kSeconds, kSharpness, kMinSpeed, kMaxStretch);
    return e;
}

bool lanes(const E& e, const EffectContext& ctx, const NodeView&, double, EntityLaneContribution& c) {
    glm::vec3 velocity(0.0f);
    if (ctx.scene == nullptr || !ctx.scene->nodeVelocity(e.owner.name, velocity)) {
        return false; // no recorded motion yet: nothing to smear, and Dormant says so
    }
    const EffectValueStore& v = e.values;
    const float speed = glm::length(velocity);
    const float minSpeed = std::max(v.getFloat("motionSmear/minSpeed", kMinSpeed), 0.0f);
    // Faded in over the band from the minimum to twice it, so crossing the threshold does not pop.
    const float x = std::clamp((speed - minSpeed) / std::max(minSpeed, 0.5f), 0.0f, 1.0f);
    const float fade = x * x * (3.0f - 2.0f * x);
    glm::vec3 smear = -velocity * std::max(v.getFloat("motionSmear/smearSeconds", kSeconds), 0.0f) *
                      std::max(v.getFloat("motionSmear/amount", kAmount), 0.0f) * fade;
    const float maxStretch = std::max(v.getFloat("motionSmear/maxStretch", kMaxStretch), 0.0f);
    const float length = glm::length(smear);
    if (length > maxStretch && length > 0.0f) {
        smear *= maxStretch / length;
    }
    c.hasSmear = true;
    c.smear = smear;
    c.smearSharpness = std::max(v.getFloat("motionSmear/sharpness", kSharpness), 0.05f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::MotionSmear;
    s.key = "motionSmear";
    s.enumName = "MotionSmear";
    s.displayName = "Motion Smear";
    s.description = "When the entity moves fast, its trailing side stretches back along its path like an "
                    "animator's smear frame, and snaps back when it stops. Real geometry, not a blur.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostVertex;
    s.addLabel = "Motion Smear";
    s.addTip = "This entity's back half stretches out behind it when it moves fast -- a cartoon\n"
               "smear frame -- and snaps back when it stops.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "amount";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& motionSmearSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
