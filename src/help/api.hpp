#pragma once

// The Help retrieval API as JSON in, JSON out (§25, §26, §39).
//
// `HelpDatabase` is the C++ surface; this is the same five calls shaped so that a tool registry can
// register them without knowing anything about Help's internals. That shape is the deliverable: the
// AI control plane gets one knowledge layer, not a second copy of the documentation, and it gets it
// by calling the identical methods the Help panel calls.
//
// Everything here is read-only. No call mutates the database, none touches the engine, none needs
// the main thread, and none needs an undo record -- which is what a tool registry wants to know
// first, so `ToolDescriptor::readOnly` says it out loud.
//
// Wrapping this in a registry is meant to be about ten lines:
//
//     for (const help::ToolDescriptor& tool : help::tools()) {
//         // `name` is a view of a string literal with static lifetime, so copying it into the
//         // closure is safe -- and it has to be copied, or the lambda outlives the loop variable.
//         const std::string_view name = tool.name;
//         registry.add({.name = std::string(tool.name),
//                       .description = std::string(tool.description),
//                       .schema = nlohmann::json::parse(tool.parametersSchema),
//                       .readOnly = tool.readOnly,
//                       .run = [&db, name](const nlohmann::json& args) {
//                           return help::dispatch(db, name, args);
//                       }});
//     }
//
// `db` is one `help::HelpDatabase` loaded once at startup. Every call here takes it by const
// reference and none of them mutates it; the search index is built during the load rather than on
// the first query, so one loaded instance can serve the panel and the orchestrator concurrently.
// Loading it, however, is setup and must finish before either consumer starts.
//
// `dispatch` never throws: a bad argument comes back as `{"error": "..."}` with the same shape as a
// success, because a tool that throws into an orchestrator's loop is a tool that ends a task.

#include "help/database.hpp"

#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>

namespace avgen::help {

struct ToolDescriptor {
    std::string_view name;             // "help.search"
    std::string_view description;      // what it does and when to reach for it
    std::string_view parametersSchema; // a JSON Schema object, as a string literal
    bool readOnly = true;              // every Help tool is; stated so a registry need not assume
};

// The five tools §26 asks for, ready to register. Names are exactly §26's spelling.
[[nodiscard]] std::span<const ToolDescriptor> tools();

// Routes by tool name and returns the result object. Accepts both the camelCase names above and
// snake_case aliases (`help.get_shortcut`), because tool registries differ on that and refusing a
// call over a naming convention is a poor way to spend an agent's turn.
[[nodiscard]] nlohmann::json dispatch(const HelpDatabase& db, std::string_view toolName,
                                      const nlohmann::json& args);

// The individual calls, for anything that wants them without the name routing.
//
//   search        {"query": "...", "limit": 8, "category": "...", "includeGaps": true}
//                 -> {"query", "backend", "results": [{id, title, category, summary, score,
//                                                      matchedTerms, excerpt, status}]}
//   get           {"id": "audio/analysis", "format": "markdown" | "text"}
//                 -> the topic, with its metadata and its body as markdown
//   related       {"id": "audio/analysis"} -> {"id", "related": [{id, title, category, summary}]}
//   getShortcut   {"command": "transport.play"} or {"keys": "Space"}
//   getFeature    {"id": "panel.analysis"} -> the feature row plus every topic that documents it
[[nodiscard]] nlohmann::json search(const HelpDatabase& db, const nlohmann::json& args);
[[nodiscard]] nlohmann::json get(const HelpDatabase& db, const nlohmann::json& args);
[[nodiscard]] nlohmann::json related(const HelpDatabase& db, const nlohmann::json& args);
[[nodiscard]] nlohmann::json getShortcut(const HelpDatabase& db, const nlohmann::json& args);
[[nodiscard]] nlohmann::json getFeature(const HelpDatabase& db, const nlohmann::json& args);

// A compact index of everything available, for a system prompt or a capability probe: categories,
// topic ids and titles, and the counts. Small enough to include wholesale; the topics themselves
// are fetched on demand, which is what §13 of the AI brief asks for ("favour queryable state").
[[nodiscard]] nlohmann::json index(const HelpDatabase& db);

} // namespace avgen::help
