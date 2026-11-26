#pragma once

// `avgen --project <file.json> --pathtrace <out> [--pt-* ...] [--range a:b]` (ADR-351, ADR-383, ADR-2244).
//
// Runs BEFORE the Application exists, like the route audit and the sonic trace: no window, no GPU device, no pipeline
// cache, and no second copy of the project. Until ADR-2244 the flag went through `Application::init` first, which
// created a Dawn device, warmed the pipeline cache and loaded the whole project into the application's engine --
// about 8 s and several GB on Neon Dreams -- before `pathtrace::TraceJob` loaded the project again into the offline
// engine it actually traces. The tracer needs none of it ("no GPU is involved", docs/pathtrace/overview.md).

namespace avgen::app {

struct AppOptions;

// Exit codes, unchanged from the in-application path: 0 written; 2 no project, a project that cannot be read, or an
// invalid request; 3 a cancelled range, 4 a failed range; 7 a failed frame, 8 a cancelled frame.
int runPathTraceCommand(const AppOptions& options);

} // namespace avgen::app
