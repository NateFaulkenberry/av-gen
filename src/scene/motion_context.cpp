#include "scene/motion_context.hpp"

namespace avgen::scene {

const char* locomotionModeName(LocomotionMode mode) {
    switch (mode) {
    case LocomotionMode::Idle: return "idle";
    case LocomotionMode::Walk: return "walk";
    case LocomotionMode::Run: return "run";
    case LocomotionMode::Turn: return "turn";
    case LocomotionMode::Other: return "other";
    }
    return "other";
}

} // namespace avgen::scene
