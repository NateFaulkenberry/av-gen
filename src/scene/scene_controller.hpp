#pragma once

// A SceneController owns a Scene, registers its parameters and default modulation routes, and
// applies parameter finals to the scene every frame. The Engine holds exactly one; swapping
// controllers (orb preset, glTF file) resets the parameter set and routes.

#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene.hpp"

#include <string>

namespace avgen::scene {

class SceneController {
public:
    virtual ~SceneController() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual void update(const FrameTime& time) = 0;
    [[nodiscard]] virtual const Scene& scene() const = 0;
    [[nodiscard]] virtual Scene& scene() = 0;
};

} // namespace avgen::scene
