#pragma once

// ADR-1803: the `AVGEN_*` environment variables set for this process, so a run can say what it ran under.
//
// About thirty `AVGEN_*` variables exist (docs/help/reference-command-line.md lists them). Several are A/B arms that
// change what a frame shows or costs -- AVGEN_LEGACY_PROCGEN, AVGEN_NO_CONTROL_REPLAY, AVGEN_SYNC_PIPELINES -- and each
// is read once into a function-local static, so nothing recorded that one was set (CA-004 of the 2026-10-14 audit).
// A profile taken with one left exported in a shell measured a different program and looked like any other profile.

#include <string>
#include <utility>
#include <vector>

namespace avgen::core {

// Every `AVGEN_*` variable in this process's environment, name and value, sorted by name.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> avgenEnvironment();

// The same, as one line ("AVGEN_A=1 AVGEN_B=x"); empty when none is set.
[[nodiscard]] std::string avgenEnvironmentLine();

} // namespace avgen::core
