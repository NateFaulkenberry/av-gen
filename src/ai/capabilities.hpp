#pragma once

// The capability registry (ADR-094, spec §24, §54).
//
// ## Why this is generated rather than written
//
// §24 asks for a machine-readable registry of what AV Gen supports, and adds: *"This registry is
// the source of truth used to construct tool definitions. Do not duplicate tool descriptions in
// several unrelated places."*
//
// The literal reading -- author capabilities, generate tools from them -- would put every tool's
// name, description and schema in a data table and its behaviour in a function, and the two would
// drift the first time somebody edited one. The intent is single-sourcing, so the dependency is
// inverted: a tool's definition is the source of truth, and the capability document is *derived*
// from the registry. Editing `engine_tools.cpp` updates both, and there is no second copy to
// forget.
//
// ## The half that is not derived, and why it matters more
//
// A registry built only from the tools would answer "what can you do" and never "what does this
// engine have that you cannot reach". That second answer is what §54 is about: where an operation
// cannot yet be safely exposed, *say so* rather than papering over it. So the domain records below
// are authored, and several of them exist specifically to report absence -- terrain, water,
// entity creation and deletion, navigation -- each with the reason and the smallest engine
// abstraction that would be needed.
//
// An agent that knows a thing is unavailable stops trying to do it and says so to the user. An
// agent that does not know finds a plausible-looking parameter and produces a change nobody asked
// for.

#include "ai/tool_api.hpp"

#include <nlohmann/json.hpp>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {

struct DomainCapability {
    std::string id;
    std::string title;
    std::string description;
    bool available = true;
    std::string unavailableReason; // why, and what would be needed -- never just "no"
};

// The authored half: one record per engine domain, whether or not it has tools.
[[nodiscard]] const std::vector<DomainCapability>& domainCapabilities();

// The whole document: domains, their availability, and the tools each one actually has, folded
// together from the registry. `engine` is probed for the things that are only knowable at runtime
// (is there a composition, is audio loaded, is there a light rig).
[[nodiscard]] nlohmann::json capabilityDocument(const ToolRegistry& registry,
                                                const app::Engine& engine);

} // namespace avgen::ai
