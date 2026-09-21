#include "entity/motion_provider.hpp"

namespace avgen::entity {

const char* movementModeName(MovementMode mode) {
    switch (mode) {
    case MovementMode::Ground: return "ground";
    case MovementMode::Airborne: return "airborne";
    case MovementMode::Rooted: return "rooted";
    }
    return "ground";
}

const char* motionStatusName(MotionStatus status) {
    switch (status) {
    case MotionStatus::Produced: return "produced";
    case MotionStatus::NoContent: return "no-content";
    case MotionStatus::SkeletonMismatch: return "skeleton-mismatch";
    case MotionStatus::NotReady: return "not-ready";
    case MotionStatus::Unsupported: return "unsupported";
    case MotionStatus::Failed: return "failed";
    }
    return "failed";
}

} // namespace avgen::entity
