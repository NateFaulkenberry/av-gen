// Bloom Source (Effect Library, catalog-light.md): the owner blooms without being made brighter.
//
// **What it is.** Selective bloom (ADR-039, `post/bloom/emissionWeight`) blooms a pixel in
// proportion to how much of its radiance the emission target says is LIGHT. A lit surface writes
// only its emissive part there, so a bright but non-emissive saucer does not bloom. Bloom Source
// raises the owner's emission target towards its whole radiance -- `Bloom share` of the way -- and
// leaves its colour exactly as it was. The surface looks the same; the halo around it grows.
//
// Through FXL's bloom-share lane (`fxA.y`, world/effects/entity_fx.hpp); several Bloom Sources on one
// owner take the maximum (rendering-architecture §7).
//
// **When it does nothing, said out loud.** With `post/bloom/emissionWeight` at 0 the bloom does not
// consult the emission target at all, so every pixel over the threshold blooms whatever this says;
// and a pixel below the bloom threshold does not bloom at any share. Both are in the tooltip, because
// a control that silently does nothing teaches an artist that the system is broken (ADR-421).
//
// **Not implemented:** the catalog's "threshold bias". The threshold is applied in post to the
// finished image, not per object, and moving it per object would need the id target in the bloom
// prefilter -- a post change, outside this package.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr float kWeight = 0.6f;

constexpr EffectField kFields[] = {
    storedFloat("weight", "Bloom share", kWeight, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How much of this entity's light the bloom treats as a light source, without\n"
                 "making it brighter. 1 blooms it like a lamp; 0 like any lit surface.\n"
                 "Needs selective bloom: post/bloom/emissionWeight above 0 (at 0 the bloom\n"
                 "does not look at the emission at all). A pixel below the bloom threshold\n"
                 "does not bloom at any share."),
};

constexpr const char* kStyleNames[] = {"Subtle", "Hot"};
void subtle(E& e) {
    e.values.setFloat("bloomSource/weight", 0.3f);
    e.style = kStyleNames[0];
}
void hot(E& e) {
    e.values.setFloat("bloomSource/weight", 1.0f);
    e.style = kStyleNames[1];
}
constexpr EffectStyle kStyles[] = {{kStyleNames[0], subtle}, {kStyleNames[1], hot}};

constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "weight", 0.35f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::BloomSource;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.25;
    e.timing.fadeOut = 0.5;
    e.values.setFloat("bloomSource/weight", kWeight);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    c.bloomShare = std::clamp(e.values.getFloat("bloomSource/weight", kWeight), 0.0f, 1.0f);
    return c.bloomShare > 0.0f;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::BloomSource;
    s.key = "bloomSource";
    s.enumName = "BloomSource";
    s.displayName = "Bloom Source";
    s.description = "The entity blooms like a light source without being made any brighter: selective "
                    "bloom sees more of its light. Needs post/bloom/emissionWeight above 0.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Bloom Source";
    s.addTip = "This entity blooms like a lamp without changing how it looks.\n"
               "Needs selective bloom (post/bloom/emissionWeight above 0).";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "weight";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& bloomSourceSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
