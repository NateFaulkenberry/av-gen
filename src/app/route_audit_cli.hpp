#pragma once

// ADR-902: `avgen --project <file.json> --audit-routes <out.json>` -- the liveness report of a
// project, headless and with no GPU: an offline Engine loads the project and every route, timeline
// track, effect default route and effect is put through `params::liveness::Registry::standard()`.

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace avgen::app {

// Loads `project` into an offline engine and returns the report (`scene::routeAuditToJson`).
// `fps` > 0 overrides the project's render rate for the rules that sample on the frame grid.
[[nodiscard]] Result<nlohmann::json> auditProjectFile(const std::filesystem::path& project, double fps = 0.0);

// The command: writes the report to `out` ("-" = stdout) and a one-screen summary to stdout.
// Exit codes: 0 written, 2 no --project, 3 the project did not load, 4 the report could not be
// written.
int runRouteAuditCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps);

} // namespace avgen::app
