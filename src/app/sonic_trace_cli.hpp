#pragma once

// ADR-1020: `avgen --project <file.json> --sonic-trace <out.csv>` -- the Sonic Garden's signals over a whole project,
// headless and with no GPU, one row per frame at the project's render rate (or --fps).
//
// The columns are every `sonic.*`, `notes.*`, `timbre.*` and `visual.*` signal on the bus, plus the time. It is the
// diagnostic view in a form a script, a chart or another agent can read, and the stdout summary (the mean of each
// character dimension over the frames with sound, and the cost of the subsystem per frame) is the quick "do the
// four sounds differ" check.

#include <filesystem>

namespace avgen::app {

// Exit codes: 0 written, 2 no --project, 3 the project did not load (or has no `sonic` block), 4 cannot write.
int runSonicTraceCommand(const std::filesystem::path& project, const std::filesystem::path& out, double fps);

} // namespace avgen::app
