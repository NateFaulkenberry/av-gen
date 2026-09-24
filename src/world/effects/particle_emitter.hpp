#pragma once

// EMIT (ADR-703): effect-owned particle systems.
//
// The engine's particle system (`scene::ParticleSystem`, about 85 fields, GPU-simulated) already has
// everything nine of the Effect Library's ten particle effects need -- curl turbulence, buoyancy,
// firefly blinking and clustering, orbit and attractors, trails, velocity stretch, wind, soft
// particles, fog coupling (docs/design/effect-library/catalog-particles.md). What it did not have is
// a way to be an EFFECT: attached to an owner, in a stack, with `fx/<id>/` parameters, activation,
// status and presets. That is all this adds. One registry type, `particleEmitter`, whose `look`
// selects a base configuration (Fireflies, Embers, Magic Particles) and whose rows are the artist
// controls on top of it.
//
// Each live instance owns exactly one system in `scene::Scene::particles`, named `fx:<id>` and
// appended after the composition's own systems. It is updated IN PLACE every frame -- found by name,
// not rebuilt -- so a running system allocates nothing and keeps its GPU pool. The composition's own
// systems are untouched and so are their indices (the particle renderer keys pools by index). The
// composition's particle registrar never sees an `fx:` system: its parameters are registered from
// nodes, not from `scene.particles`, so there is exactly one authority per number (`fx/<id>/rate`,
// never also `particles/fx:<id>/spawnRate`).

#include "scene/particles.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::world {

// How many effect-owned systems a frame may carry. Past it, the lowest in evaluation order are
// `Dropped` with a reason. A particle system is a compute dispatch and a pool of up to its capacity;
// sixteen is generous for a stack of looks and small enough to keep a mistaken duplicate loop from
// taking the GPU with it.
inline constexpr std::size_t kMaxEffectParticleSystems = 16;

// The name an instance's system carries in `scene.particles`.
[[nodiscard]] std::string particleSystemName(std::string_view effectId);

// The particle system an emitter instance describes, before placement: its look's base
// configuration with the instance's rows applied. Pure; defined in kinds/particle_emitter_effect.cpp
// beside the rows. `envelope` (0..1, activation x timing) scales emission, so a closing window lets
// the particles already in the air finish their lives instead of vanishing.
void describeParticleSystem(const EffectInstance& effect, float envelope, scene::ParticleSystem& out);

// ONE live instance's record count (0 or 1) -- the registry's `records` hook.
[[nodiscard]] std::size_t particleEmitterRecords(const EffectInstance& effect, const EffectContext& context);

// The Particles-stage builder. Walks `order` (empty: list order); for every Emitter-bucket
// instance, writes its `fx:<id>` system into `particles` (added once, then updated in place),
// placed on its owner, and its status and reason. Systems for instances that no longer exist are
// removed. Systems that are not `fx:` are never touched.
void buildParticleFrame(std::span<const EffectInstance> effects, const EffectContext& context,
                        std::vector<scene::ParticleSystem>& particles, std::span<const std::uint32_t> order,
                        std::span<EffectStatus> status, std::span<std::string> reasons);

} // namespace avgen::world
