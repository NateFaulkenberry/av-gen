#pragma once

// ADR-927: `avgen --project <file.json> --propose-reactivity <out.json>` -- the Director's default
// reactivity proposal for a project, headless and with no GPU, as a document a generator can read,
// edit and install.
//
// An offline engine loads the project; the reactive catalogue (ADR-925) is built from it; the
// proposer (ADR-927) crosses it with the music's layers; the plan is validated and compiled
// (ADR-924/926) and installed into that scratch engine -- never into the file -- and the result is
// put through the same liveness audit `--audit-routes` writes (ADR-902), so every proposed route's
// verdict is the one the project would get with the proposal installed.
//
// The document (format "avgen-reactivity-proposal", version 1):
//
//   {
//     "format", "version", "project", "frameRate", "durationSeconds", "hasAudio",
//     "music":   {"tempoBpm", "sections": [{"type", "start", "end", "energy", "audio": {...}}]},
//     "summary": {"routes", "sources", "byLevel", "byGroup", "bySource", "byOwner", "layers", "depth", "notes",
//                 "validation": {"errors", "warnings"}, "audit": {"live", "dead", "hazard"}},
//     "items":   [{"key", "level", "group", "owner", "layer", "reason",
//                  "route": <a project route, with "planItem">, "verdict", "findings": [...], "issues": [...]}],
//     "sources": [{"key", "reason", "source": <a project source>, "parameters": {<path>: <value>}, "issues": [...]}],
//     "validation": [<issue>], "diff": [<line>],
//     "catalog": <ReactiveCatalog::toJson>,
//     "overlaps": [{"source", "target", "owners", "note"}],   // authored routes on what the plan animates
//     "install": {"routes": [...], "sources": [...], "parameters": {...}, "directingPlan": <plan>}
//   }
//
// `install` is what to add to the project to install the proposal as made: append `routes` to the
// project's `routes`, `sources` to its `sources`, merge `parameters` into its `parameters`, and put
// `directingPlan` in `directingPlans` (replacing a plan of the same id) so the Director panel shows
// the items and a later revision can replace them. Edit an item before installing by editing its
// route; a route edited after installing is a hand edit a later revision will not overwrite.

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace avgen::app {

// Loads `project` into an offline engine and returns the proposal document. `fps` > 0 overrides the
// project's render rate for the sampling rules.
[[nodiscard]] Result<nlohmann::json> proposeReactivityForProject(const std::filesystem::path& project, double fps = 0.0);

// The command: writes the document to `out` ("-" = stdout) and a one-screen summary to stdout (stderr
// when the document is on stdout). Exit codes: 0 written, 2 no --project, 3 the project did not load,
// 4 the document could not be written.
int runProposeReactivityCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps);

} // namespace avgen::app
