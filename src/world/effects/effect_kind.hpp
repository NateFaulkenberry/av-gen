#pragma once

// Every effect TYPE the engine declares (ADR-702). One enumeration for all of them: a sky phenomenon,
// a placed medium and a wave across the ground are types of one thing, an effect, and there is no
// second list for a second family to live in.
//
// The numeric values are load-bearing and append-only: `MediumSlot::kind` carries the value to
// `shaders/volume.wgsl` as a scalar, so a new type goes at the END. Everything else a type is -- its
// name, category, targets, render stage and parameters -- lives in its schema
// (`effect_registry.hpp`), which is where to look, not here.

#include <cstdint>
#include <optional>
#include <string_view>

namespace avgen::world {

enum class EffectKind : std::uint8_t {
    Comet,  // a bright head on a world-space curve, with a trail integrated along the view ray
    Aurora, // curtains on vertical shells, rising from a world height, shaped by the spectrum
    // ADR-387. A cosmic vortex, drawn by the volumetric pass rather than by `atmosphere_fx.wgsl` --
    // which pass rasterises an effect is its render stage, not its identity.
    Vortex,
    // ADR-500. The two types added to prove the registry: a shower is N records in the comet bucket,
    // a fog bank is a placed volumetric medium with no swirl.
    MeteorShower,
    VolumetricFog,
    // ADR-580. A tornado: a rotating column of dust and condensate standing IN the world, read from
    // the side. The first type to reach the march through a different density function, which is
    // what `MediumSlot::kind` exists to select.
    Tornado,
    // ADR-702. ADR-207's two surface waves, which before ADR-702 were a separate `worldEffects` list
    // with its own registrar, serialiser and panel. A pulse spreads from where its source stands --
    // an entity it is attached to, or whichever hero the cut is on; a beam travels ahead of the
    // camera towards where it is going.
    GroundPulse,
    TravelBeam,
    // ADR-703 (Effect Library, Wave 1) reserves the next values, written explicitly because the
    // Wave 1 types land on separate branches and the number a type serialises under must not depend
    // on merge order: Glow = 8, Pulse = 9, BloomSource = 10, Trail = 11, SpaceWarp = 12,
    // ParticleEmitter = 13. Each is declared here by the change that registers its schema.
    Glow = 8,        // FXL: the entity emits light, with an optional rim and spill light
    Pulse = 9,       // FXL: the entity's light swells and falls, whole or as a travelling band
    BloomSource = 10, // FXL: the entity blooms like a light without getting brighter
    // A strip through the owner's recent path, RIBBON over HIST (Light Trail is a style of it).
    Trail = 11,
    SpaceWarp = 12, // DF: the view bending around its owner (distortion_frame.hpp)
    ParticleEmitter = 13, // ADR-703: an effect-owned particle system riding its owner (EMIT)
    // Wave 2 reserves, by slice (explicit values, as Wave 1 did):
    //   motion (XFORM):   Orbit = 14, Spiral = 15, Float = 16, Shake = 17, Bounce = 18
    //   events (TRIGGER): Shockwave = 19, Ripple = 20, VelocityDistortion = 30
    //   surface (FXL 2):  Dissolve = 21, Growth = 22, Breathing = 23, OrganicPulsation = 24,
    //                     Bioluminescence = 25, PulsingVeins = 26, Fresnel = 27, RimLight = 28,
    //                     ColorCycling = 29, MotionSmear = 31
    // XFORM (transform_frame.hpp): visual-only procedural motion of the owner, before the flatten.
    Orbit = 14,  // the owner circles its own position
    Spiral = 15, // the owner corkscrews up or down around its own position
    Float = 16,  // a slow, never-repeating bob and wobble
    Shake = 17,  // smooth-noise jitter, continuous or settling after each activation
    Bounce = 18, // a squash-and-stretch hop, continuous or a recoil
    // TRIGGER's DF users (catalog-distortion.md, catalog-motion.md).
    Shockwave = 19,          // DF: an expanding refracting front released by an event
    Ripple = 20,             // DF: a damped train of concentric waves in a membrane
    // FXL Wave 2 (the surface slice): per-entity clip, displacement and pattern lanes.
    Dissolve = 21,         // the owner burns away through noise, shadow and all
    Growth = 22,           // the owner grows in behind a glowing front
    Breathing = 23,        // the owner swells and settles along its normals
    OrganicPulsation = 24, // bulges travel along the owner
    Bioluminescence = 25,  // a living light pattern on the owner
    PulsingVeins = 26,     // light pulses along a vein network on the owner
    Fresnel = 27,          // a view-angle rim of light
    RimLight = 28,         // a one-sided kicker rim, fixed in view space
    ColorCycling = 29,     // the owner's hue turns over time
    VelocityDistortion = 30, // DF: a refracting wake along the owner's recent path
    MotionSmear = 31,      // the owner's trailing side stretches back along its path
    // Wave 2 (the lead's): the sky's star field, owned by an effect instead of skybox constants.
    Stars = 32, // a controllable star field: density, magnitudes, colour, twinkle, a galactic band
    // Wave 3 reserves, by slice (explicit values, as Waves 1 and 2 did):
    //   bolt (BOLT):         Lightning = 33, Arc = 34, ElectricField = 35, Discharge = 36
    //   shell (SHELL):       Plasma = 37, EnergyShield = 38, ForceField = 39
    //   lens (DF):           HeatShimmer = 41, GravitationalLens = 42
    //   shell 2 (phase 2):   ChargeUp = 40, LightBeam = 43, Halo = 44, Bubble = 45, Portal = 46,
    //                        RealityTear = 47
    // BOLT (bolt_path.hpp): seeded fractal paths drawn through RIBBON.
    Lightning = 33,     // a triggered strike: leader, return stroke, restrikes, afterglow, a flash
    Arc = 34,           // a continuous writhing arc between two things
    ElectricField = 35, // a crackling web over the owner's surface, and arcs hopping across it
    Discharge = 36,     // a triggered burst of bolts, sparks and a flash from the owner
};

// Derived from the registry's schemas rather than written out here, so a type whose name does not
// round-trip is a named failure of `checkRegistry` instead of an if-chain that fell behind.
[[nodiscard]] const char* effectKindName(EffectKind k);
[[nodiscard]] std::optional<EffectKind> effectKindFromName(std::string_view name);

} // namespace avgen::world
