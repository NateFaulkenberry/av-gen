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

const char* motionPhaseName(MotionPhase phase) {
    switch (phase) {
    case MotionPhase::Idle: return "idle";
    case MotionPhase::Starting: return "starting";
    case MotionPhase::Moving: return "moving";
    case MotionPhase::Stopping: return "stopping";
    case MotionPhase::Turning: return "turning";
    case MotionPhase::Strafing: return "strafing";
    }
    return "idle";
}

} // namespace avgen::scene
