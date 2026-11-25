#pragma once

// The engine's published contracts, generated from the C++ that decides them (ADR-2150).
//
// Nothing here is a second copy of a fact. The tool catalogue is the registry the engine runs
// (`registerEngineTools`, `registerDirectorTools`), the error catalogue is `ToolErrorCode` and its
// meanings, and the version is `api::version()`. `avgen --describe` prints them with no window, no
// GPU and no Application, and `tools/gen_api.sh` writes the checked-in artefacts from it:
//
//   schemas/tool-catalogue.json           the catalogue (every tool: schemas, annotations, MCP name)
//   schemas/tool-catalogue-baseline.json  the contract at the current API level (see apiBaseline)
//   schemas/tools-without-output-schema.json  the output-schema ratchet (authored; may only shrink)
//   docs/api/tools.md, docs/api/errors.md, docs/api/protocol.md (ADR-2155: the JSON-RPC server's own tables)
//   schemas/avgen-project.schema.json, schemas/avgen-scene.schema.json  (ADR-2152)
//
// The CPU suite regenerates each in-process and compares it with the committed file byte for byte
// (tests/unit/test_api_catalogue.cpp), so a tool changed without regenerating fails a test that
// prints the command to fix it.
//
// This lives in avgen_core, which cannot link gpu::Context: that is what makes "GPU-free" a property
// of the build rather than a promise.

#include "ai/tool_api.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::api {

// The registry every client sees: the engine tools, the Director tools and the session tools
// (`api::registerAllTools`, which every Session uses). Needs no engine; the engine arrives per call
// through ToolContext.
[[nodiscard]] ai::ToolRegistry makeCatalogueRegistry();

// A tool name as MCP clients accept it (`^[a-zA-Z0-9_-]{1,64}$`): `parameter.set` -> `parameter_set`.
// The catalogue records the mapping for every tool, so the reverse lookup is a table, not a
// string reversal.
[[nodiscard]] std::string mcpToolName(std::string_view name);

[[nodiscard]] nlohmann::json toolCatalogue(const ai::ToolRegistry& registry);
[[nodiscard]] nlohmann::json errorCatalogue();
[[nodiscard]] nlohmann::json versionDocument();

// The breaking-change baseline: per tool, the parts of its contract a client depends on (that it
// exists, its required inputs, each input's type, its output properties), as they stood when the
// API level was last set. `breakingChanges` compares a catalogue with it; any entry means the API
// level must go up (and the baseline is then rewritten with `--describe baseline`).
[[nodiscard]] nlohmann::json apiBaseline(const nlohmann::json& catalogue);
[[nodiscard]] std::vector<std::string> breakingChanges(const nlohmann::json& baseline, const nlohmann::json& catalogue);

// ADR-2152: the document schemas, JSON Schema 2020-12 in the subset `ai::validateAgainstSchema` enforces
// (plus annotations), so the engine validates its own documents. Completeness comes from the parsers'
// key lists, typing from the tables in schema_types_{project,scene}.cpp; see api/schema_types.hpp.
[[nodiscard]] nlohmann::json projectSchema(); // schemas/avgen-project.schema.json
[[nodiscard]] nlohmann::json sceneSchema();   // schemas/avgen-scene.schema.json

// ADR-2152: the feature ids this build can parse, generated from the key lists and name tables that decide
// them (scene keys, environment keys, node kinds, effect kinds, render outputs, AOV names). Defined in
// api/features.cpp, so `capability.list` reaches it without the catalogue's registry. Not an artefact:
// it is printed by `avgen --describe features` and returned by `capability.list`.
[[nodiscard]] std::vector<std::string> featureList();

[[nodiscard]] std::string toolsMarkdown(const nlohmann::json& catalogue);
[[nodiscard]] std::string errorsMarkdown(const nlohmann::json& errors);

// The exact bytes a committed JSON artefact holds: two-space indent, sorted keys, final newline.
[[nodiscard]] std::string artefactText(const nlohmann::json& document);

// Relative paths of the generated artefacts under a repository root, and their contents.
struct Artefact {
    std::filesystem::path relativePath;
    std::string contents;
};
[[nodiscard]] std::vector<Artefact> generatedArtefacts(const ai::ToolRegistry& registry);

// `avgen --version` and
// `avgen --describe <tools|errors|schema:project|schema:scene|features|version|baseline|all> [--out <dir>]`.
// Returns the process exit code: 0, or 2 for a usage error.
[[nodiscard]] int runVersionCommand();
[[nodiscard]] int runDescribeCommand(int argc, char** argv);

} // namespace avgen::api
