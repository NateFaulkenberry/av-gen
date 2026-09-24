// Travel Beam (ADR-207, ADR-702): a front that travels ahead of the camera towards where it is going.
//
// Before ADR-702 this was the "Camera Travel Beam", a record in the scene-wide `worldEffects` list.
// It is now an ordinary effect type. Its source is the camera and its target is whichever hero the
// cut is handing off to, so its natural owners are the World (one beam for the whole film) and a
// Camera (a beam that belongs to one camera of a multicam rig).

#include "world/effects/kinds/wave_rows.hpp"

#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kStyleNames[] = {"Bioluminescent", "Rainbow", "Energy", "Magical", "Subtle"};

void applyStyle(E& e, const char* name) {
    if (applyBeamStyle(e.wave, name)) {
        e.style = name;
    }
}
void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }
void style4(E& e) { applyStyle(e, kStyleNames[4]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], style0}, {kStyleNames[1], style1}, {kStyleNames[2], style2},
    {kStyleNames[3], style3}, {kStyleNames[4], style4},
};

constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "intensity", 0.8f, 60.0f, 420.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::TravelBeam;
    e.wave.source.kind = SourceKind::Camera;
    // The beam is *going somewhere*: the subject the shot is travelling to. `FocusHero` resolves to
    // whichever hero the schedule names, which keeps this free of any scene's coordinates.
    e.wave.hasTarget = true;
    e.wave.target.kind = SourceKind::FocusHero;
    e.activation = Activation::CameraTravel;
    e.wave.propagation.kind = PropagationKind::DirectionalWave;
    // Blended, because none of the three alone is right: forward alone points at where the operator
    // is looking rather than where the camera is going, velocity alone flails on a bowed path, and
    // camera-to-target alone ignores the arc entirely (ADR-207).
    e.wave.propagation.direction = DirectionMode::Blended;
    e.wave.propagation.forwardWeight = 1.0f;
    e.wave.propagation.velocityWeight = 0.6f;
    e.wave.propagation.targetWeight = 1.2f;
    e.wave.propagation.speed = 95.0f;
    e.wave.propagation.range = 320.0f;
    e.wave.propagation.startOffset = 12.0f;
    e.wave.propagation.verticalExtent = 90.0f;
    e.wave.propagation.verticalGrowth = 0.30f;
    e.wave.propagation.ringCount = 0.0f;
    e.timing.delay = 0.15;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.1;
    e.timing.repeatSeconds = 3.2;
    applyStyle(e, "Bioluminescent");
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s = wave_rows::baseSchema();
    s.kind = EffectKind::TravelBeam;
    s.key = "travelBeam";
    s.enumName = "TravelBeam";
    s.displayName = "Travel Beam";
    s.description = "A front of light sweeping ahead of the camera towards the subject it is travelling to.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Travel Beam";
    s.addTip = "A front that sweeps ahead of the camera towards the subject it is travelling to.\n"
               "Fires while the director's cut is moving between subjects.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Camera);
    s.styles = kStyles;
    s.routes = kRoutes;
    s.factory = make;
    return s;
}

} // namespace

const EffectSchema& travelBeamSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
