#include "entity/character_intent.hpp"

namespace avgen::entity {

const char* intentTypeName(IntentType type) {
    switch (type) {
    case IntentType::Idle: return "idle";
    case IntentType::Wander: return "wander";
    case IntentType::MoveTo: return "moveTo";
    case IntentType::Follow: return "follow";
    case IntentType::Investigate: return "investigate";
    case IntentType::Observe: return "observe";
    case IntentType::Flee: return "flee";
    case IntentType::Avoid: return "avoid";
    case IntentType::Interact: return "interact";
    case IntentType::Custom: return "custom";
    }
    return "idle";
}

} // namespace avgen::entity
