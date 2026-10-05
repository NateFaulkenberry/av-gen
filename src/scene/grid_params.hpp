#pragma once

// ADR-1122: a simulated grid's BEHAVIOUR is parameters; only its LAYOUT re-seeds it.
//
// A grid (ADR-032) and an agents grid (ADR-1120) were scene-file only: no parameter, no route, no MIDI could
// change how a grid behaves while it runs, and editing any value in code re-seeded it, because every setting
// was in the one hash the simulation compares to decide that the state is no longer valid. A performer who
// wants the organism to wander, swarm or starve has nothing to hold.
//
// The split: `GridField::layoutHash()` is what the state's shape and its seed depend on (mode, wrap,
// resolution, bounds, sim rate, seed, the agents' count and species, the input fields' names). A change there
// re-seeds the grid, as before. Everything else is behaviour, a coefficient a step reads, and is registered
// here as `grid/<name>/<leaf>`:
//
//   every grid   injectRate, advect, diffusion, dissipation
//   rd           feed, kill, diffusionA, diffusionB
//   agents       sensorAngle, sensorDistance, turnAngle, stepSize, depositAmount, repel
//
// Changing one of them changes the grid's next steps and nothing it has already done. Checkpoints still key on
// every setting (`structuralHash`, ADR-1119), so a changed behaviour drops them: a grid whose behaviour is
// modulated replays from its newest matching checkpoint, and a modulated value is sampled at the frame, not at
// each sub-step, so its scrub equals its play only when the modulation is a pure function of time (the same
// rule as a node transform the engine animates; ADR-1119 "Still frame-sampled").

#include "params/parameter_set.hpp"
#include "spatial/grid_field.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

struct GridParameters {
    std::string prefix; // "grid/<name>/"
    std::vector<params::IParameter*> all;
};

// Registers the behaviour leaves of `rest` under `prefix` (the agents and Gray-Scott leaves only for those
// modes). The defaults are the authored values, so registering changes nothing.
[[nodiscard]] GridParameters registerGridParameters(params::ParameterSet& params, const spatial::GridField& rest,
                                                    const std::string& prefix);
// Copies the finals into `live`'s behaviour; the layout comes from `rest` and is never touched.
void applyGridParameters(const GridParameters& p, const spatial::GridField& rest, spatial::GridField& live);

} // namespace avgen::scene
