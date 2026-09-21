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
    case IntentType::Socialize: return "socialize";
    case IntentType::ReturnTo: return "returnTo";
    case IntentType::React: return "react";
    case IntentType::Custom: return "custom";
    }
    return "idle";
}

bool intentTypeFromName(std::string_view name, IntentType& out) {
    for (int i = 0; i <= static_cast<int>(IntentType::Custom); ++i) {
        const auto type = static_cast<IntentType>(i);
        if (name == intentTypeName(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

} // namespace avgen::entity
