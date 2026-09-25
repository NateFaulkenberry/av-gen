// Bioluminescence (Effect Library Wave 2, catalog-organic.md): a living light pattern on an organism.
//
// **What it is.** FXL's bioluminescent-pattern sub-block (the extension record, entity_fx.hpp):
// photophores (Worley F1 spots), stripes, or glowing cell walls (Worley F2 - F1) in the owner's own
// space, so the pattern rides the creature. Every cell breathes on its own phase (asynchronous, as
// real photophores are), its hue jitters a little per cell, and every `Wave interval` seconds a band of
// brightness rolls up the body. Added to the owner's emission before fog and into the emission
// target, so it is fogged, depth-exact and blooms as light; it sums with a Glow and a Pulse on the
// same owner multiplies it.
//
// **Distance.** A cell smaller than about a pixel fades to the pattern's mean coverage, so a distant
// glowing thing glows evenly instead of sparkling (the pattern branch is uniform per draw and takes
// no derivatives: the footprint comes from the view depth and the projection).
//
// **Not here.** The catalog's flare (a touch/proximity response) needs TRIGGER, which lands in
// parallel; its spill joins the ecology light aggregation, which is a World-preset job. Neither is
// faked: this is the surface pattern, and a Glow with its spill light on the same owner lights the
// ground if that is wanted.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kPatternNames[] = {"Spots", "Stripes", "Cells"};

constexpr float kScale = 14.0f;
constexpr float kCoverage = 0.4f;
constexpr glm::vec3 kColor{0.15f, 1.0f, 0.78f};
constexpr float kVariation = 0.12f;
constexpr float kIntensity = 5.0f;
constexpr float kBreatheRate = 0.18f;
constexpr float kBreatheDepth = 0.65f;
constexpr float kWaveSpeed = 0.4f;
constexpr float kWaveInterval = 7.0f;
constexpr float kWaveGain = 1.5f;

constexpr EffectField kFields[] = {
    storedChoice("pattern", "Pattern", 0, kPatternNames).main()
        .tooltip("Spots: scattered photophores. Stripes: bands round the body, like a comb\n"
                 "jelly's rows. Cells: glowing cell walls."),
    storedColor("color", "Colour", kColor).main(),
    storedFloat("intensity", "Brightness", kIntensity, 0.0f, 500.0f, 0.0f, 30.0f).main().floorAt(0.0f)
        .tooltip("The pattern's light. Blooms as a light source."),
    storedFloat("scale", "Scale", kScale, 0.25f, 128.0f, 1.0f, 30.0f).fmt("%.1f").main()
        .tooltip("How many cells across the entity. Higher is finer."),
    storedFloat("coverage", "Coverage", kCoverage, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("How much of the surface the pattern covers: spot size, stripe or wall width."),
    storedFloat("colorVariation", "Colour variation", kVariation, 0.0f, 1.0f, 0.0f, 0.5f).sec("Life")
        .tooltip("How far each cell's hue wanders from the colour."),
    storedFloat("breatheRate", "Breathe rate", kBreatheRate, 0.0f, 10.0f, 0.0f, 2.0f).fmt("%.2f Hz")
        .tooltip("How fast each cell brightens and dims, each on its own phase."),
    storedFloat("breatheDepth", "Breathe depth", kBreatheDepth, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How far a cell dims between breaths. 0 is a steady glow."),
    storedFloat("waveSpeed", "Wave speed", kWaveSpeed, 0.0f, 10.0f, 0.0f, 2.0f).fmt("%.2f /s")
        .tooltip("How fast the occasional bright wave rolls up the body, in lengths per second."),
    storedFloat("waveInterval", "Wave interval", kWaveInterval, 0.5f, 120.0f, 1.0f, 30.0f).fmt("%.1f s")
        .tooltip("Seconds between waves."),
    storedFloat("waveGain", "Wave brightness", kWaveGain, 0.0f, 20.0f, 0.0f, 5.0f)
        .tooltip("How much brighter the pattern is where the wave passes. 0: no wave."),
};

void set(E& e, int pattern, glm::vec3 color, float intensity, float scale, float coverage, float variation,
         float breatheRate, float breatheDepth, float waveGain) {
    e.values.setFloat("bioluminescence/pattern", static_cast<float>(pattern));
    e.values.setColor("bioluminescence/color", color);
    e.values.setFloat("bioluminescence/intensity", intensity);
    e.values.setFloat("bioluminescence/scale", scale);
    e.values.setFloat("bioluminescence/coverage", coverage);
    e.values.setFloat("bioluminescence/colorVariation", variation);
    e.values.setFloat("bioluminescence/breatheRate", breatheRate);
    e.values.setFloat("bioluminescence/breatheDepth", breatheDepth);
    e.values.setFloat("bioluminescence/waveGain", waveGain);
}

constexpr const char* kStyleNames[] = {"Glowmere Creature", "Deep-Sea Photophores", "Jelly Rows"};

void glowmereCreature(E& e) {
    set(e, 0, kColor, kIntensity, kScale, kCoverage, kVariation, kBreatheRate, kBreatheDepth, kWaveGain);
    e.style = kStyleNames[0];
}
void deepSea(E& e) {
    set(e, 0, {0.25f, 0.55f, 1.0f}, 9.0f, 26.0f, 0.2f, 0.08f, 0.35f, 0.85f, 2.5f);
    e.style = kStyleNames[1];
}
void jellyRows(E& e) {
    set(e, 1, {0.7f, 0.45f, 1.0f}, 6.0f, 8.0f, 0.3f, 0.45f, 0.5f, 0.5f, 1.0f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], glowmereCreature},
    {kStyleNames[1], deepSea},
    {kStyleNames[2], jellyRows},
};

// The mids brighten it.
constexpr EffectRoute kRoutes[] = {
    {"audio.mid", "intensity", 6.0f, 40.0f, 500.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Bioluminescence;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.0;
    set(e, 0, kColor, kIntensity, kScale, kCoverage, kVariation, kBreatheRate, kBreatheDepth, kWaveGain);
    e.values.setFloat("bioluminescence/waveSpeed", kWaveSpeed);
    e.values.setFloat("bioluminescence/waveInterval", kWaveInterval);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const int pattern = std::clamp(static_cast<int>(v.getFloat("bioluminescence/pattern", 0.0f) + 0.5f), 0, 2);
    c.hasBio = true;
    c.bioPattern = static_cast<EntityFxBioPattern>(pattern + 1);
    c.bioColor = glm::max(v.getColor("bioluminescence/color", kColor), glm::vec3(0.0f)) *
                 std::max(v.getFloat("bioluminescence/intensity", kIntensity), 0.0f);
    c.bioScale = std::max(v.getFloat("bioluminescence/scale", kScale), 0.01f);
    c.bioCoverage = std::clamp(v.getFloat("bioluminescence/coverage", kCoverage), 0.0f, 1.0f);
    c.bioVariation = std::max(v.getFloat("bioluminescence/colorVariation", kVariation), 0.0f);
    c.bioBreatheRate = std::max(v.getFloat("bioluminescence/breatheRate", kBreatheRate), 0.0f);
    c.bioBreatheDepth = std::clamp(v.getFloat("bioluminescence/breatheDepth", kBreatheDepth), 0.0f, 1.0f);
    c.bioWaveSpeed = std::max(v.getFloat("bioluminescence/waveSpeed", kWaveSpeed), 0.0f);
    c.bioWaveInterval = std::max(v.getFloat("bioluminescence/waveInterval", kWaveInterval), 0.1f);
    c.bioWaveGain = std::max(v.getFloat("bioluminescence/waveGain", kWaveGain), 0.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Bioluminescence;
    s.key = "bioluminescence";
    s.enumName = "Bioluminescence";
    s.displayName = "Bioluminescence";
    s.description = "A living light pattern on the entity: photophores, stripes or glowing cell walls that "
                    "breathe out of step with each other, with a slow wave rolling up the body now and then. "
                    "Blooms as light; fades evenly with distance.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Bioluminescence";
    s.addTip = "Living light on this entity: glowing spots, stripes or cells that breathe\n"
               "and ripple, like a deep-sea creature or a Glowmere plant.";
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

const EffectSchema& bioluminescenceSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
