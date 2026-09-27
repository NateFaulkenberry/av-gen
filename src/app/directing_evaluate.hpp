#pragma once

// The host's side of `director.evaluate` (ADR-931): a span of the film, with a plan in it, rendered
// on a scratch copy and judged by the Creative Critic.
//
// The Critic is an independent local program (`~/Documents/GitHub/creative-critic`). It reads a render
// plus a generic scene and intent -- which its own AV Gen adapter writes from AV Gen's output files --
// and returns findings with evidence. So the engine's part is to produce those files for exactly the
// film the plan would make, without touching the person's project:
//
//   1. copy    the project as it is now, written to a scratch file (main thread: it reads the engine)
//   2. install the candidate plan into a scratch engine loaded from the copy, and save it (a revision
//              the project already holds is rendered as it is)
//   3. render  `avgen --project copy --render clip.mov --range a:b --size WxH`, behind a configurable
//              prefix (`tools/gpu-lock.sh` on a machine where agents share the GPU)
//   4. trace   `avgen_cast_trace --project copy --start a --seconds b-a --hz 20`, where the bodies were
//   5. adapt   the Critic's `adapters/avgen/avgen_adapter.py`: scene, shots, cast and clip -> inputs.json
//   6. judge   `critic submit --inputs ... --video-start a --wait --json --strict --no-autostart`
//   7. report  the Critic's `critic.report/1` into an `EvaluationReport`, each finding attributed to
//              the plan items whose film-time spans it overlaps
//
// Steps 2-7 touch only the copy and the files beside it, so they run on a thread of their own and the
// editor keeps drawing. A missing Critic, an unreachable one (exit 4) and a failed job (exit 2) are
// clear errors; a PARTIAL job (exit 5 under `--strict`) is a report marked `partial`, never a full one.

#include "ai/tool_context.hpp"
#include "app/directing_record.hpp"
#include "core/error.hpp"
#include "directing/evaluation.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace avgen::app {

class Engine;

struct EvaluatorOptions {
    std::filesystem::path critic;  // the Critic's CLI (`.venv/bin/critic`); empty = not configured
    std::string criticUrl;         // `critic --url`; empty = the CLI's default ($CRITIC_URL, :8765)
    std::filesystem::path adapter; // `adapters/avgen/avgen_adapter.py`; empty = beside the Critic
    std::filesystem::path python;  // runs the adapter; empty = the Critic's own `.venv/bin/python`
    std::filesystem::path avgen;   // renders the clip; empty = this executable
    std::filesystem::path castTrace; // empty = `tools/avgen_cast_trace` beside `avgen`'s build
    std::vector<std::string> renderPrefix; // e.g. {"<repo>/tools/gpu-lock.sh"}: the GPU is shared
    unsigned width = 960;          // the clip's size: the previews' 960x540 unless asked otherwise
    unsigned height = 540;
    std::string session = "avgen-director"; // the Critic's session; the track is per plan and span
    std::filesystem::path scratchDir;       // empty = the system's temporary directory
    double timeoutSeconds = 1800.0;         // per step
    bool keepFiles = false;                 // leave the scratch files for a person to look at
};

// The options a session gets from its command line and environment: `--critic` / AVGEN_CRITIC,
// `--critic-url` / AVGEN_CRITIC_URL, AVGEN_CRITIC_ADAPTER, AVGEN_CRITIC_PYTHON, AVGEN_RENDERER,
// AVGEN_CAST_TRACE, AVGEN_RENDER_PREFIX (split on spaces), AVGEN_EVALUATE_SIZE ("960x540"). Paths the
// Critic's layout implies are filled from it (`<repo>/.venv/bin/critic` -> the adapter and python).
[[nodiscard]] EvaluatorOptions evaluatorOptionsFrom(const std::optional<std::filesystem::path>& critic,
                                                    const std::optional<std::string>& criticUrl);
// The options with every empty path derived -- the adapter and python from the Critic's repository,
// avgen from this executable, the tracer from avgen's build -- and checked: the first thing missing is
// the error, in words that say how to supply it.
[[nodiscard]] Result<EvaluatorOptions> resolveEvaluatorOptions(EvaluatorOptions options);

// ---- the parts, for testing and for a host that wants one of them -------------------------------------

struct ProcessOutcome {
    int exitCode = -1;
    bool timedOut = false;
    bool cancelled = false;
    std::string out; // what it wrote on stdout
    std::string err; // ...and on stderr
};
// Runs `argv` (argv[0] a path, or a name looked up on PATH) with stdin closed, waiting for it; kills it
// on cancellation or after `timeoutSeconds`. Fails only when it cannot be started.
[[nodiscard]] Result<ProcessOutcome> runProcess(const std::vector<std::string>& argv,
                                                const std::filesystem::path& workDir,
                                                const std::atomic<bool>* cancel = nullptr,
                                                double timeoutSeconds = 1800.0);

// `critic submit`'s command line for one evaluation.
[[nodiscard]] std::vector<std::string> criticSubmitCommand(const EvaluatorOptions& options,
                                                           const std::filesystem::path& inputs, double videoStart,
                                                           const std::string& mode, const std::string& track,
                                                           const std::string& label);
// What `critic submit --wait --json --strict` said: its exit code, and the JSON line it printed, whose
// `report_json` names the report on disk. 0 and 5 are reports (5 marked partial); 4 is "not running",
// 2 "the job failed", 3 "cancelled"; anything else, or output that is not the Critic's, is an error.
[[nodiscard]] Result<directing::EvaluationReport> readCriticOutcome(const ProcessOutcome& outcome,
                                                                    const std::string& criticUrl);

// Steps 2-7 on a copy already written. Synchronous; `cancel` is polled between steps and by every child.
[[nodiscard]] Result<directing::EvaluationReport> evaluateFromCopy(const std::filesystem::path& copy,
                                                                   const ai::EvaluationRequest& request,
                                                                   const EvaluatorOptions& options,
                                                                   const std::atomic<bool>* cancel = nullptr,
                                                                   const RecordProgress& progress = {});

// The host's side of `director.evaluate`: each call writes the copy on the main thread and evaluates on
// a thread of its own; `settle` keeps the report in `Engine::directingEvaluations()`.
[[nodiscard]] ai::EvaluationHook makeEvaluationHook(EvaluatorOptions options);

} // namespace avgen::app
