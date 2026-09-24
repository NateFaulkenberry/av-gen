// Ground Pulse (ADR-207, ADR-702): a ring spreading through the ground from where its source stands.
//
// Before ADR-702 this was the "Hero Pulse", one record in the scene-wide `worldEffects` list with a
// `FocusHero` source: a single pulse that followed whichever hero the director's cut was holding.
// It is now an ordinary effect type, and the canonical way to use it is ATTACHED TO AN ENTITY with
// an `Owner` source and `HeroFocus` activation: each hero carries its own instance, which fires when
// the cut holds that hero and spreads from where that hero stands. Sixteen such instances behave,
// together, exactly like the one pulse that followed focus -- the activation rule for a named
// subject is "fire only for my own subject" -- while each can now be tuned, disabled or modulated
// on its own. A World-owned pulse with a `FocusHero` source is still legal and still does what the
// old one did, for a scene that wants one shared look.

#include "world/effects/kinds/wave_rows.hpp"

#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kStyleNames[] = {"Water", "Bioluminescent", "Shockwave", "Magical"};

void applyStyle(E& e, const char* name) {
    if (applyPulseStyle(e.wave, name)) {
        e.style = name;
    }
}
void style0(E& e) { applyStyle(e, kStyleNames[0]); }
void style1(E& e) { applyStyle(e, kStyleNames[1]); }
void style2(E& e) { applyStyle(e, kStyleNames[2]); }
void style3(E& e) { applyStyle(e, kStyleNames[3]); }

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], style0}, {kStyleNames[1], style1}, {kStyleNames[2], style2}, {kStyleNames[3], style3},
};

// One ring per beat, felt rather than seen: the Beat response slider writes this same route, and a
// pulse added from the menu should answer the music before anybody finds the slider.
constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "intensity", 1.2f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::GroundPulse;
    // The owner. On an entity this is the entity; the validator refuses it on the World, and the
    // Add Effect menu gives a World-owned pulse `FocusHero` instead (see `makeFor` in
    // effect_stack.cpp), which is ADR-207's scene-wide pulse.
    e.wave.source.kind = SourceKind::Owner;
    // Down to the base. A hero's position is the centre of the object the camera is pointed at; a
    // ripple that starts in the air is a ring floating around a mushroom cap.
    e.wave.source.groundOffset = 0.0f;
    e.activation = Activation::HeroFocus;
    e.wave.propagation.kind = PropagationKind::RadialWave;
    e.wave.propagation.direction = DirectionMode::Explicit;
    e.wave.propagation.speed = 11.0f;
    e.wave.propagation.range = 46.0f;
    e.wave.propagation.verticalExtent = 4.5f;
    e.wave.propagation.verticalGrowth = 0.30f;
    e.timing.delay = 0.35;
    e.timing.fadeIn = 0.6;
    e.timing.fadeOut = 1.2;
    // One ring per two seconds by default; a route from `beat.pulse` onto `fx/<id>/intensity` is
    // what makes it musical.
    e.timing.repeatSeconds = 2.0;
    applyStyle(e, "Bioluminescent");
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s = wave_rows::baseSchema();
    s.kind = EffectKind::GroundPulse;
    s.key = "groundPulse";
    s.enumName = "GroundPulse";
    s.displayName = "Ground Pulse";
    s.addLabel = "Ground Pulse";
    s.addTip = "A ring spreading through the ground from where its owner stands.\n"
               "On an entity: fires when the director's cut holds that entity.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.styles = kStyles;
    s.routes = kRoutes;
    s.factory = make;
    return s;
}

} // namespace

const EffectSchema& groundPulseSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
