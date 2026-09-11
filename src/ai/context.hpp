#pragma once

// Context assembly (ADR-094, spec §29-§30, addendum §13).
//
// The rule §29 sets is: do not send the entire project on every request. The layering it asks for
// is implemented here as two functions with different lifetimes:
//
//   - **`systemPrompt`** is *static product knowledge*: what AV Gen is, how its pipeline is
//     arranged, what a parameter is, how modulation and the timeline relate, what the agent is
//     expected to do and not do. It changes only when the tool surface changes, which makes it the
//     stable prefix a provider's prompt cache can hold.
//
//   - **`ambientContext`** is *dynamic state provided automatically*: the handful of facts the
//     agent would otherwise waste a turn asking for -- scene kind, node count, parameter count,
//     playhead, tempo, whether audio is loaded, what the renderer costs. Addendum §13's
//     distinction, drawn at a specific line: anything that is O(1) and would be needed on almost
//     every task is pushed; anything O(n) in the size of the project -- the list of 300 vegetation
//     parameters -- is pulled through a tool.
//
// Everything else the agent needs, it asks for. That is not a limitation being worked around; it
// is what makes the context cost of a large scene the same as a small one.

#include "ai/tool_api.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {

// The stable half. Generated from the registry rather than written out by hand, so a tool added to
// `engine_tools.cpp` is described to the model from its own definition and there is no second copy
// of its description to drift (§24).
[[nodiscard]] std::string systemPrompt(const ToolRegistry& registry);

// The volatile half, small and bounded. Sent with the first user turn of a task.
[[nodiscard]] nlohmann::json ambientContext(const app::Engine& engine);

// The first user message: the prompt, plus the ambient context as a compact block.
[[nodiscard]] std::string composeUserTurn(const std::string& prompt,
                                          const nlohmann::json& ambient);

} // namespace avgen::ai
