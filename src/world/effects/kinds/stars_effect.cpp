// Stars (Effect Library Wave 2, catalog-particles.md "Stars" -- not a particle effect): the sky's
// star field, owned by an instance instead of fixed constants in shaders/skybox.wgsl. The field and
// why it is one per sky are in world/effects/star_field.hpp.

#include "world/effects/effect_registry.hpp"
#include "world/effects/star_field.hpp"

#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr StarField kDefault{};

constexpr EffectField kFields[] = {
    storedFloat("brightness", "Brightness", kDefault.brightness, 0.0f, 50.0f, 0.0f, 4.0f).main()
        .tooltip("Radiance of the brightest stars. Stars add to the sky, so raise this with the\n"
                 "sky's own intensity, and give them bloom with the scene's sky bloom."),
    storedFloat("density", "Density", kDefault.density, 0.0f, 0.2f, 0.0f, 0.06f)
        .tooltip("The fraction of the sky's cells that hold a star. 0.012 is a clear rural night;\n"
                 "0.04 is deep space."),
    storedFloat("magnitudeSlope", "Magnitude spread", kDefault.magnitudeSlope, 1.0f, 12.0f, 1.0f, 8.0f)
        .tooltip("1 makes every star equally bright. Higher leaves a few bright stars among many\n"
                 "faint ones, as a real sky is."),
    storedFloat("colorSpread", "Colour spread", kDefault.colorSpread, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("0: every star blue-white. 1: from blue-white to orange, by each star's temperature."),
    storedFloat("twinkle", "Twinkle", kDefault.twinkle, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("Scintillation depth. Strongest near the horizon, where the light crosses the most air."),
    storedFloat("twinkleRate", "Twinkle rate", kDefault.twinkleRate, 0.0f, 40.0f, 0.0f, 12.0f)
        .tooltip("Radians per second. Each star runs within 30% of it, so they never twinkle in step."),
    storedFloat("horizonFade", "Horizon fade", kDefault.horizonFade, 0.01f, 1.0f, 0.05f, 0.8f)
        .tooltip("How high above the horizon the stars reach full brightness (as the sine of the\n"
                 "elevation). Low values keep stars down to the horizon line."),
    storedFloat("band", "Galactic band", kDefault.band, 0.0f, 4.0f, 0.0f, 2.0f)
        .tooltip("A Milky Way: a soft band of glow across the sky, with more stars inside it. 0 = none."),
    storedFloat("bandTilt", "Band tilt", kDefault.bandTilt, -3.1416f, 3.1416f, -1.5708f, 1.5708f)
        .tooltip("Radians. The band's great circle tilted from lying along the horizon (0) toward\n"
                 "passing overhead (1.57)."),
    storedFloat("daylight", "Hide in daylight", kDefault.daylight, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How far a bright sky behind a star hides it. 1: stars vanish in daylight, as real\n"
                 "ones do. 0: they show through any sky (a stylised choice)."),
};

void set(E& e, const char* leaf, float v) { e.values.setFloat(std::string("stars/") + leaf, v); }

constexpr const char* kStyleNames[] = {"Clear Night", "Deep Space", "Twinkling Horizon"};
void clearNight(E& e) {
    set(e, "brightness", 2.0f);
    set(e, "density", 0.015f);
    set(e, "magnitudeSlope", 2.5f);
    set(e, "colorSpread", 0.5f);
    set(e, "twinkle", 0.25f);
    set(e, "twinkleRate", 3.0f);
    set(e, "horizonFade", 0.35f);
    set(e, "band", 0.0f);
    set(e, "bandTilt", 0.6f);
    set(e, "daylight", 1.0f);
    e.style = kStyleNames[0];
}
void deepSpace(E& e) {
    clearNight(e);
    set(e, "brightness", 2.5f);
    set(e, "density", 0.04f);
    set(e, "magnitudeSlope", 4.5f);
    set(e, "colorSpread", 0.8f);
    set(e, "twinkle", 0.0f);
    set(e, "horizonFade", 0.02f);
    set(e, "band", 1.2f);
    set(e, "daylight", 0.0f);
    e.style = kStyleNames[1];
}
void twinklingHorizon(E& e) {
    clearNight(e);
    set(e, "density", 0.02f);
    set(e, "twinkle", 0.8f);
    set(e, "twinkleRate", 6.0f);
    set(e, "horizonFade", 0.08f);
    e.style = kStyleNames[2];
}
constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], clearNight}, {kStyleNames[1], deepSpace}, {kStyleNames[2], twinklingHorizon}};

// The catalog's modulation: treble shimmers the field, the beat lifts it.
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "twinkle", 0.5f, 20.0f, 300.0f},
    {"beat.pulse", "brightness", 0.3f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Stars;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.5;
    clearNight(e);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Stars;
    s.key = "stars";
    s.enumName = "Stars";
    s.displayName = "Stars";
    s.description = "The sky's star field: density, a realistic spread of magnitudes and colours, "
                    "twinkle strongest near the horizon, and an optional galactic band. Replaces the "
                    "sky's fixed stars, on any sky.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Stars";
    s.addTip = "A controllable star field in the sky, in place of the fixed one.";
    s.targets = targetBit(EffectTarget::World);
    s.category = EffectCategory::Sky;
    s.stage = RenderStage::Sky;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "brightness";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Starfield;
    s.resolve.records = starFieldRecords;
    return s;
}

} // namespace

const EffectSchema& starsSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
