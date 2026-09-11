#pragma once

// A SceneController owns a Scene, registers its parameters and default modulation routes, and
// applies parameter finals to the scene every frame. The Engine holds exactly one; swapping
// controllers (orb preset, glTF file) resets the parameter set and routes.

#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene.hpp"
#include "signals/signal_bus.hpp"

#include <string>

namespace avgen::scene {

class SceneController {
public:
    virtual ~SceneController() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual void update(const FrameTime& time) = 0;
    // Autonomous behaviour, run after modulation and before update() (ADR-087).
    //
    // The split exists because the two halves want opposite orders. A behaviour's own knobs
    // (a hover's amplitude, a wander's speed) have to be modulatable, so the routes must have run
    // before a behaviour reads them; and a behaviour's *output* is an offset that a route should
    // be able to add to, so it must land on the parameter finals after the routes wrote theirs.
    // Running behaviours between applyRoutes() and update() satisfies both: a route and a
    // behaviour compose on the same property instead of overwriting each other.
    //
    // A no-op by default. Controllers with nothing autonomous in them need not care.
    virtual void updateBehaviour(const FrameTime& time, const signals::SignalBus& bus) {}
    [[nodiscard]] virtual const Scene& scene() const = 0;
    [[nodiscard]] virtual Scene& scene() = 0;
};

} // namespace avgen::scene
