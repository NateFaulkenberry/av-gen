#pragma once

// ADR-1106: `performance.profile_scene`'s host side. The project is written to a scratch copy on the main thread, then
// `avgen --live-profile` runs on it in a child process on a thread of its own (the way `director.evaluate` runs the
// Critic), and the record it writes is the tool's answer. The editor does not depend on the profiler: without this
// hook the tool says it is unavailable.

#include "ai/tool_context.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::app {

// The child's command line for one request (tested without a GPU).
[[nodiscard]] std::vector<std::string> liveProfileCommand(const std::filesystem::path& executable,
                                                          const std::filesystem::path& project,
                                                          const std::filesystem::path& json,
                                                          const ai::ProfileRequest& request);
[[nodiscard]] ai::ProfileHook makeProfileHook(std::filesystem::path executable, std::filesystem::path scratchDir);

} // namespace avgen::app
