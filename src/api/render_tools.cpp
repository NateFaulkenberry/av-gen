// The render and job tools (ADR-2159, Engine API stage S6; 02-session-api.md section 8): `render.capture`,
// `render.start`, `job.status`, `job.wait`, `job.cancel`, `job.list`.
//
// A render tool's body runs on the host thread and does only the cheap, checkable half, in this order, so a refusal
// costs no GPU work at all:
//   1. resolves the output through `PathPolicy::forWrite` (outside every write root: PermissionDenied);
//   2. snapshots the session's in-memory project with `Engine::writeProjectCopy` into the session's scratch
//      directory -- never the user's file, and nothing is saved, so a render right after a `parameter.set` shows it;
//   3. records the job in the session's table and hands it to the host (`Host::submitJob`: Busy when its FIFO is
//      full), then returns `{job}` at once.
// The host loads a fresh Offline engine from the snapshot, takes the machine's GPU lock, creates the device on the
// first job, and steps the RenderJob between pumps. `job.wait` blocks the caller on the table (Session::call serves
// it on the caller's thread); on the host thread it drives the jobs itself.
//
// A capture is a one-frame RenderJob: the deliverable's own render path, the same snapshot, warm-up, pre-roll and
// frame hash as a sequence. `history: play` (the default) renders and discards every frame from 0 to the frame
// first, so a capture of frame N is frame N of a render from 0; `seek` jumps there, which is O(1) but may differ by
// small amounts on stateful content (route envelopes, simulations, temporal history: the probe in ADR-2159).

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/jobs.hpp"
#include "api/session.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "app/render_settings.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cctype>
#include <cmath>
#include <thread>

namespace avgen::api {
namespace {

using nlohmann::json;
namespace schema = ai::schema;
namespace fs = std::filesystem;
using ai::ToolErrorCode;
using ai::ToolResult;

std::atomic<std::uint64_t> gSnapshotSerial{1};

json anything(std::string description) { return json{{"description", std::move(description)}}; }

ToolResult noSession(std::string_view tool) {
    return ToolResult::failure(ToolErrorCode::FailedPrecondition,
                               fmt::format("'{}' runs inside a session, and this call has none", tool),
                               "call it through api::Session::call");
}

void add(ai::ToolRegistry& r, std::string name, std::string title, std::string description, json input,
         ai::ToolAnnotations annotations, json output, ai::ToolFn fn) {
    ai::Tool tool;
    tool.definition.name = std::move(name);
    tool.definition.title = std::move(title);
    tool.definition.description = std::move(description);
    tool.definition.inputSchema = std::move(input);
    tool.definition.outputSchema = std::move(output);
    tool.definition.annotations = std::move(annotations);
    tool.definition.since = kApiLevel; // new at this level; adding tools is not a breaking change
    tool.execute = std::move(fn);
    r.add(std::move(tool));
}

ai::ToolAnnotations renderAnnotations() {
    ai::ToolAnnotations a;
    a.readOnly = false;
    a.mutatesProject = false;
    a.mutatesSession = true; // a job record
    a.expensive = true;
    a.undoable = false;
    a.supportsCancellation = true;
    a.supportsProgress = true;
    a.deterministic = true;
    a.executionClass = ai::ExecutionClass::Job;
    a.permissions = {"gpu", "write"};
    a.requirements = {"composition"};
    return a;
}

ai::ToolAnnotations jobAnnotations(bool readOnly) {
    ai::ToolAnnotations a;
    a.readOnly = readOnly;
    a.mutatesSession = !readOnly;
    a.idempotent = true;
    a.undoable = false;
    return a;
}

json jobRecordSchema(bool withTimedOut) {
    json props{
        {"id", schema::string("The job's id, e.g. job-3")},
        {"tool", schema::string("The tool that started it")},
        {"kind", schema::string("capture or sequence", {"capture", "sequence"})},
        {"state", schema::string("Where it is in its life",
                                 {"queued", "preparing", "running", "succeeded", "failed", "cancelled"})},
        {"phase", schema::string("What it is doing now, in words (waiting for the GPU lock, compiling pipelines, "
                                 "playing history, rendering)")},
        {"progress", schema::number("0..1 over the whole job, history frames included", 0.0, 1.0)},
        {"framesDone", schema::integer("Frames of the range rendered")},
        {"framesTotal", schema::integer("Frames in the range")},
        {"request", anything("The normalised request: time range, size, fps, format, history, output path")},
        {"snapshot", schema::string("The project copy it renders (in the session's scratch directory)")},
        {"outputs", schema::array(anything("{path, frame?, bytes, sha256}"),
                                  "The files written, partial ones too when it failed or was cancelled")},
        {"result", anything("When it ended: frameHashes, sequenceHash, frameHash, outputs, timing, lock, warnings, "
                            "gpuErrors; null before")},
        {"cancelRequested", schema::boolean("job.cancel was called")},
        {"created", schema::string("UTC, ISO 8601")},
        {"finished", anything("UTC, ISO 8601, or null")},
        {"error", anything("{code, message, recovery, details?} when it failed, else null")}};
    std::vector<std::string> required{"id", "tool", "kind", "state", "phase", "progress", "outputs", "result", "error"};
    if (withTimedOut) {
        props["timedOut"] = schema::boolean("The wait ended at timeoutMs with the job still unfinished");
        required.push_back("timedOut");
    }
    return schema::object(std::move(props), std::move(required), true);
}

json submittedSchema() {
    return schema::object(
        {{"job", schema::string("The job's id: pass it to job.wait, job.status or job.cancel")},
         {"state", schema::string("queued", {"queued"})},
         {"kind", schema::string("capture or sequence", {"capture", "sequence"})},
         {"frames", schema::integer("Frames it will render (the range is [start, end))")},
         {"start", schema::number("Range start, seconds")},
         {"end", schema::number("Range end, seconds (exclusive)")},
         {"fps", schema::number("Frames per second")},
         {"width", schema::integer("Pixels")},
         {"height", schema::integer("Pixels")},
         {"format", schema::string("png, exr or video", {"png", "exr", "video"})},
         {"history", schema::string("play or seek", {"play", "seek"})},
         {"historyFrames", schema::integer("history: play -- frames rendered and discarded before the range")},
         {"path", schema::string("Where the output goes (absolute)")},
         {"snapshot", schema::string("The project copy the job renders")}},
        {"job", "state", "kind", "frames", "path"}, true);
}

std::optional<RenderHistory> historyArg(const json& args) {
    const std::string h = args.value("history", std::string("play"));
    if (h == "play") {
        return RenderHistory::Play;
    }
    if (h == "seek") {
        return RenderHistory::Seek;
    }
    return std::nullopt;
}

std::string lowerExtension(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

// "a:b" in seconds, b exclusive.
std::optional<std::pair<double, double>> parseRange(const std::string& text) {
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const double a = std::stod(text.substr(0, colon), &used);
        if (used != colon) {
            return std::nullopt;
        }
        const std::string rest = text.substr(colon + 1);
        const double b = std::stod(rest, &used);
        if (used != rest.size() || !std::isfinite(a) || !std::isfinite(b)) {
            return std::nullopt;
        }
        return std::pair{a, b};
    } catch (...) {
        return std::nullopt;
    }
}

std::uint64_t framesIn(double start, double end, double fps) {
    return static_cast<std::uint64_t>(std::max(1.0, std::ceil((end - start) * fps - 1e-9)));
}

// The shared half of both render tools: the size, rate and tier (the project's render block unless the call says
// otherwise), the snapshot, the record and the submission.
ToolResult submit(const json& args, ai::ToolContext& ctx, const std::string& tool, RenderRequest request,
                  const fs::path& reported) {
    Session* session = ctx.session();
    app::Engine& engine = ctx.engine();
    if (!session->host().canRender()) {
        return ToolResult::failure(ToolErrorCode::Unavailable, "this host cannot render",
                                   "run it in avgen serve or avgen call on a machine with a GPU", json{{"field", "gpu"}});
    }
    const app::RenderSettings& project = engine.renderSettings();
    request.fps = args.value("fps", project.fps);
    request.width = args.value("width", project.width);
    request.height = args.value("height", project.height);
    request.tier = args.value("tier", std::string{});
    if (!(request.fps > 0.0) || request.width == 0 || request.height == 0) {
        return ToolResult::failure(ToolErrorCode::InvalidArguments, "fps, width and height must be positive");
    }
    auto scratch = session->scratchDir();
    if (!scratch) {
        return ToolResult::failure(ToolErrorCode::Internal, scratch.error().message);
    }
    // The in-memory project, as it is now: no save, and the user's file is not touched.
    const std::uint64_t serial = gSnapshotSerial.fetch_add(1, std::memory_order_relaxed);
    const fs::path snapshot = *scratch / fmt::format("snapshot-{}.json", serial);
    if (auto r = engine.writeProjectCopy(snapshot); !r) {
        return ToolResult::failure(ToolErrorCode::Internal,
                                   fmt::format("cannot snapshot the project for the render: {}", r.error().message));
    }
    request.project = snapshot;
    const std::uint64_t frames = framesIn(request.startSeconds, request.endSeconds, request.fps);
    const auto historyFrames =
        request.history == RenderHistory::Play
            ? static_cast<std::uint64_t>(std::floor(request.startSeconds * request.fps + 1e-6))
            : std::uint64_t{0};
    json normalised{{"start", request.startSeconds},
                    {"end", request.endSeconds},
                    {"fps", request.fps},
                    {"width", request.width},
                    {"height", request.height},
                    {"format", renderFormatName(request.format)},
                    {"history", renderHistoryName(request.history)},
                    {"tier", request.tier.empty() ? json(nullptr) : json(request.tier)},
                    {"path", reported.string()}};
    JobTable& table = session->jobs();
    const std::string id = table.create(tool, request.capture ? "capture" : "sequence", normalised, snapshot.string());
    request.jobId = id;
    if (request.capture) {
        request.outputDir = *scratch / (id + "-staging");
    }
    table.update(id, [&](JobRecord& r) { r.framesTotal = frames; });
    if (auto refused = session->host().submitJob(JobSubmission{session->jobTable(), request})) {
        table.discard(id); // the call failed: no job exists
        std::error_code ignored;
        fs::remove(snapshot, ignored);
        return ToolResult::failure(std::move(*refused));
    }
    json out{{"job", id},
             {"state", "queued"},
             {"kind", request.capture ? "capture" : "sequence"},
             {"frames", frames},
             {"start", request.startSeconds},
             {"end", request.endSeconds},
             {"fps", request.fps},
             {"width", request.width},
             {"height", request.height},
             {"format", renderFormatName(request.format)},
             {"history", renderHistoryName(request.history)},
             {"historyFrames", historyFrames},
             {"path", reported.string()},
             {"snapshot", snapshot.string()}};
    return ToolResult::ok(std::move(out), fmt::format("{} queued: {} frame(s) at {}x{} -> {}", id, frames,
                                                      request.width, request.height, reported.string()));
}

ToolResult recordResult(const JobRecord& r) {
    return ToolResult::ok(r.toJson(), fmt::format("{} {}{}", r.id, jobStateName(r.state),
                                                  r.phase != jobStateName(r.state) ? ": " + r.phase : std::string{}));
}

ToolResult unknownJob(const std::string& id) {
    return ToolResult::failure(ToolErrorCode::NotFound, fmt::format("no job '{}' in this session", id),
                               "job.list reports this session's jobs", json{{"field", "id"}});
}

} // namespace

CallResult jobWaitResult(const JobRecord& record, bool timedOut) {
    CallResult out;
    out.success = true;
    out.result = record.toJson();
    out.result["timedOut"] = timedOut;
    out.summary = timedOut ? fmt::format("{} still {} after the wait: {}", record.id, jobStateName(record.state),
                                         record.phase)
                           : fmt::format("{} {}", record.id, jobStateName(record.state));
    out.job = record.id;
    return out;
}

void registerRenderTools(ai::ToolRegistry& registry) {
    add(registry, "render.capture", "Capture a frame",
        "Render one frame of the project as it is in this session -- unsaved edits included; nothing is saved -- to a "
        "PNG (or EXR, by the path's extension), through the same offline render path a sequence uses, so the frame "
        "hash equals that frame's hash in a render.start sequence. Give `frame` (at `fps`) or `time` (seconds); "
        "neither is time 0. `history: play` (the default) first renders and discards every frame from 0, so the frame "
        "has the history a play-through had (cost grows with the time); `seek` jumps there, which is fast but may "
        "differ slightly on stateful content (envelopes, simulations). Returns a job id at once: job.wait for the "
        "result (frame hash, file sha256, timing). The size, rate and tier default to the project's render settings.",
        schema::object({{"frame", schema::integer("The frame index at `fps` (frame / fps seconds)", 0.0)},
                        {"time", schema::number("Seconds; used when `frame` is not given", 0.0)},
                        {"fps", schema::number("Frames per second (default: the project's render fps)", 0.001)},
                        {"width", schema::integer("Pixels (default: the project's render width)", 1.0, 16384.0)},
                        {"height", schema::integer("Pixels (default: the project's render height)", 1.0, 16384.0)},
                        {"path", schema::string("The image to write, .png or .exr; relative to the project folder; "
                                                "inside a write root")},
                        {"tier", schema::string("Quality tier (default: the project's, normally offline)",
                                                {"preview", "realtime", "high", "offline"})},
                        {"history", schema::string("play (default): render from 0 and keep the last frame; seek: "
                                                   "jump to the frame",
                                                   {"play", "seek"})}},
                       {"path"}),
        renderAnnotations(), submittedSchema(),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            if (ctx.session() == nullptr) {
                return noSession("render.capture");
            }
            const std::string given = args.at("path").get<std::string>();
            auto path = ctx.paths().forWrite(given);
            if (!path) {
                return ToolResult::failure(path.error());
            }
            const std::string ext = lowerExtension(*path);
            if (ext != ".png" && ext != ".exr") {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a .png or .exr path", given),
                                           "name the image file, e.g. renders/a.png", json{{"field", "path"}});
            }
            const auto history = historyArg(args);
            if (!history) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "history is play or seek");
            }
            RenderRequest request;
            request.capture = true;
            request.file = *path;
            request.format = ext == ".exr" ? RenderFormat::Exr : RenderFormat::Png;
            request.history = *history;
            const double fps = args.value("fps", ctx.engine().renderSettings().fps);
            if (!(fps > 0.0)) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "fps must be positive");
            }
            const double t = args.contains("frame") ? static_cast<double>(args.at("frame").get<std::int64_t>()) / fps
                                                    : args.value("time", 0.0);
            request.startSeconds = t;
            request.endSeconds = t + 1.0 / fps; // one frame: [t, t + 1/fps)
            return submit(args, ctx, "render.capture", std::move(request), *path);
        });

    add(registry, "render.start", "Render a sequence",
        "Render a range of the project as it is in this session (unsaved edits included; nothing is saved) to a PNG "
        "or EXR sequence in a folder (frame_000000.png, numbered from the range's first frame) or to a video file. "
        "`range` is \"start:end\" in seconds, end exclusive, as --range is. Every frame is hashed in order as it is "
        "read back, before any encoder, so a video and a PNG sequence of one range report the same frame hashes. "
        "`history` as render.capture: play (the default) renders and discards the frames before a range that does "
        "not start at 0. Returns a job id at once; job.wait for the result, job.cancel to stop (the frames written so "
        "far are kept and listed).",
        schema::object({{"range", schema::string("\"start:end\" in seconds, end exclusive, e.g. \"0:4\"")},
                        {"fps", schema::number("Frames per second (default: the project's render fps)", 0.001)},
                        {"width", schema::integer("Pixels (default: the project's render width)", 1.0, 16384.0)},
                        {"height", schema::integer("Pixels (default: the project's render height)", 1.0, 16384.0)},
                        {"output", schema::string("png or exr (a folder of frames) or video (a file)",
                                                  {"png", "exr", "video"})},
                        {"path", schema::string("The folder (png, exr) or movie file (video, .mp4 or .mov); relative "
                                                "to the project folder; inside a write root")},
                        {"tier", schema::string("Quality tier (default: the project's, normally offline)",
                                                {"preview", "realtime", "high", "offline"})},
                        {"history", schema::string("play (default) or seek, for a range that starts after 0",
                                                   {"play", "seek"})}},
                       {"range", "output", "path"}),
        renderAnnotations(), submittedSchema(),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            if (ctx.session() == nullptr) {
                return noSession("render.start");
            }
            const std::string given = args.at("path").get<std::string>();
            auto path = ctx.paths().forWrite(given);
            if (!path) {
                return ToolResult::failure(path.error());
            }
            const auto range = parseRange(args.at("range").get<std::string>());
            if (!range || range->first < 0.0 || range->second <= range->first) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("range '{}' is not \"start:end\" with 0 <= start < end",
                                                       args.at("range").get<std::string>()),
                                           "e.g. \"0:4\" for the first four seconds", json{{"field", "range"}});
            }
            const auto history = historyArg(args);
            if (!history) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "history is play or seek");
            }
            const std::string output = args.at("output").get<std::string>();
            RenderRequest request;
            request.history = *history;
            request.startSeconds = range->first;
            request.endSeconds = range->second;
            if (output == "video") {
                const std::string ext = lowerExtension(*path);
                if (ext != ".mp4" && ext != ".mov") {
                    return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                               fmt::format("'{}' is not a .mp4 or .mov path", given),
                                               "name the movie file, e.g. renders/seq.mp4", json{{"field", "path"}});
                }
                request.format = RenderFormat::Video;
                request.file = *path;
                request.outputDir = path->parent_path();
            } else {
                std::error_code ec;
                if (fs::exists(*path, ec) && !fs::is_directory(*path, ec)) {
                    return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                               fmt::format("'{}' is a file; a {} sequence goes in a folder", given, output),
                                               "name a folder, e.g. renders/seq/", json{{"field", "path"}});
                }
                request.format = output == "exr" ? RenderFormat::Exr : RenderFormat::Png;
                request.outputDir = *path;
            }
            return submit(args, ctx, "render.start", std::move(request), *path);
        });

    add(registry, "job.status", "Job status",
        "One job's record: its state (queued, preparing, running, succeeded, failed, cancelled), what it is doing now, "
        "its progress, the files written so far and, once it has ended, its result -- per-frame hashes, the sequence "
        "hash, each file's sha256, timing (cold or warm device, pipeline compiles), whether it waited for the "
        "machine's GPU lock and for how long, and the warnings it logged.",
        schema::object({{"id", schema::string("The job id a render tool returned")}}, {"id"}), jobAnnotations(true),
        jobRecordSchema(false),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("job.status");
            }
            const std::string id = args.at("id").get<std::string>();
            const auto r = session->jobs().get(id);
            return r ? recordResult(*r) : unknownJob(id);
        });

    add(registry, "job.wait", "Wait for a job",
        "Block until the job ends or `timeoutMs` passes (default 30000), then return its record, with `timedOut` "
        "true when it is still running. It blocks only this caller: the session keeps answering other clients' calls "
        "and the job keeps rendering. A failed or cancelled job is a successful wait whose record says so.",
        schema::object({{"id", schema::string("The job id a render tool returned")},
                        {"timeoutMs", schema::integer("How long to wait, milliseconds (default 30000)", 0.0)}},
                       {"id"}),
        jobAnnotations(true), jobRecordSchema(true),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            // Reached only on the host thread (a one-shot client): Session::call serves every other caller on its
            // own thread. Here the waiting thread is the one that runs the jobs, so it drives them while it waits.
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("job.wait");
            }
            const std::string id = args.at("id").get<std::string>();
            const auto timeout = std::chrono::milliseconds(std::max<std::int64_t>(0, args.value("timeoutMs", std::int64_t{30'000})));
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (true) {
                const auto r = session->jobs().get(id);
                if (!r) {
                    return unknownJob(id);
                }
                const bool finished = jobFinished(r->state);
                if (finished || std::chrono::steady_clock::now() >= deadline || ctx.cancelled()) {
                    CallResult w = jobWaitResult(*r, !finished);
                    return ToolResult::ok(std::move(w.result), w.summary);
                }
                const bool more = session->host().stepJobs();
                const auto now = session->jobs().get(id);
                if (!more || (now && now->phase.rfind("waiting for the GPU lock", 0) == 0)) {
                    (void)session->jobs().wait(id, std::chrono::milliseconds{200});
                }
            }
        });

    add(registry, "job.cancel", "Cancel a job",
        "Stop a job: a queued one never starts; a running one stops after the frame it is on, keeping (and listing) "
        "the frames already written. Cancelling a job that has ended changes nothing and says so.",
        schema::object({{"id", schema::string("The job id a render tool returned")}}, {"id"}), jobAnnotations(false),
        jobRecordSchema(false),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("job.cancel");
            }
            const std::string id = args.at("id").get<std::string>();
            if (!session->jobs().get(id)) {
                return unknownJob(id);
            }
            (void)session->jobs().requestCancel(id);
            return recordResult(*session->jobs().get(id));
        });

    add(registry, "job.list", "List jobs",
        "This session's jobs, oldest first, each as job.status reports it.",
        schema::object({}), jobAnnotations(true),
        schema::object({{"jobs", schema::array(jobRecordSchema(false), "The session's jobs, oldest first")}},
                       {"jobs"}, true),
        [](const json& /*args*/, ai::ToolContext& ctx) -> ToolResult {
            Session* session = ctx.session();
            if (session == nullptr) {
                return noSession("job.list");
            }
            json jobs = json::array();
            std::size_t running = 0;
            for (const JobRecord& r : session->jobs().list()) {
                running += jobFinished(r.state) ? 0 : 1;
                jobs.push_back(r.toJson());
            }
            const std::size_t n = jobs.size();
            return ToolResult::ok(json{{"jobs", std::move(jobs)}},
                                  fmt::format("{} job(s), {} not finished", n, running));
        });
}

} // namespace avgen::api
