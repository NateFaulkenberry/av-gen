#pragma once

// `avgen::api::EngineHost`: the host every external client's session runs in (ADR-2155, Engine API stage S2;
// 02-session-api.md section 4). `avgen serve` and `avgen call` use it; Studio does not (it keeps a LocalHost over
// `Application::engine_` until stage S10).
//
// It owns:
//   - one Offline `app::Engine`, with live control off: no OSC port, no MIDI input, no audio device;
//   - the host-thread queue, and the loop that services it (`run`).
//
// It has no `Application`, no window, no ImGui, no settings file and no listener, and -- for now -- no GPU:
// `canRender()` is false, so a tool that needs the GPU answers `Unavailable`. The GPU half (a lazily created
// `gpu::Context`, render and capture jobs, `stepJobs()` between pumps) is stage S6. Its seam is marked below:
// `canRender`, and the loop's "between pumps" point. This file lives in avgen_core, which cannot link
// `gpu::Context`, so "the CPU host touches no GPU" is a property of the build, not a promise.
//
// Threads: the thread that calls `run` (or, for a one-shot client, `bindHostThread`) is the host thread. It is
// the only one that touches the engine. `stop` and `wake` are safe from any thread.

#include "api/session.hpp"
#include "core/error.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::api {

struct EngineHostOptions {
    // Stage S1b adds the time policy here (`Stepped` by default for API hosts, 02-session-api.md section 3.4);
    // `EngineHost::EngineHost` is where it is applied to the engine.
};

class EngineHost final : public Host {
public:
    explicit EngineHost(EngineHostOptions options = {});
    ~EngineHost() override;
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    [[nodiscard]] app::Engine& engine() override { return local_.engine(); }
    [[nodiscard]] ai::MainThreadQueue& queue() override { return local_.queue(); }
    void installHooks(ai::ToolContext& ctx) override { local_.installHooks(ctx); }
    // S6: true once the GPU half exists. The CPU host has no GPU member at all.
    [[nodiscard]] bool canRender() override { return false; }

    // Makes the calling thread the host thread without running the loop: a one-shot client (`avgen call`)
    // that calls `Session::call` from this thread, which then runs each body inline.
    void bindHostThread();

    // The loop, on the calling thread (which becomes the host thread), until `stop`: `session.pump()`, then
    // -- when nothing is queued -- sleep until a call arrives. It never ticks the engine while idle.
    void run(Session& session);
    // Ends `run` after the item it is running, from any thread.
    void stop();
    [[nodiscard]] bool stopping() const { return stop_.load(std::memory_order_acquire); }

private:
    std::unique_ptr<app::Engine> engine_;
    LocalHost local_; // the queue and the (absent) hooks: the host behaviour LocalHost already has
    std::atomic<bool> stop_{false};
};

// The flags `avgen serve` and `avgen call` share: `--root d` (a read and write root), `--read-root d`,
// `--write-root d` (each repeatable) and `--allow-subprocess`. Every root is made absolute against the
// working directory here, once, on the command line -- a session never resolves anything against it.
struct HostFlags {
    SessionPermissions permissions;
};
// Consumes the flag at `argv[i]` (and its value) when it is one of these, advancing `i`. Returns false when it
// is not one of them; an error when it is one but malformed (no value, or a root that does not exist).
[[nodiscard]] Result<bool> parseHostFlag(int argc, char** argv, int& i, HostFlags& flags);

// stdout protection (02-session-api.md section 10): duplicates fd 1 for the protocol and points fd 1 at stderr,
// so a stray `printf` anywhere in the process lands in the log instead of in the stream. Returns the protocol
// descriptor. Flushes stdout first, so nothing buffered before the switch goes to the wrong place.
[[nodiscard]] Result<int> protectStdout();
// Undoes it: flushes stdout, points fd 1 back at what the protocol descriptor names, and closes that descriptor.
// For an in-process caller (the tests) that must get its own stdout back.
void restoreStdout(int protocolFd);

// `avgen call ...` (api/cli_call.cpp). Returns the exit code: 0 success, 1 a tool failed (its envelope is still
// printed), 2 usage, 7 internal or host failure.
[[nodiscard]] int runCallCommand(int argc, char** argv);
[[nodiscard]] const char* callUsage();

} // namespace avgen::api
