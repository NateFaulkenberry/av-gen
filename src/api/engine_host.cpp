#include "api/engine_host.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace avgen::api {

namespace fs = std::filesystem;

EngineHost::EngineHost(EngineHostOptions /*options*/)
    : engine_(std::make_unique<app::Engine>(app::EngineMode::Offline)), local_(*engine_) {
    // ADR-2133/2155: an API host opens no OSC port and no MIDI input, whatever a project it opens says. Nothing
    // here or in the clients calls setLiveOsc.
    engine_->setLiveControl(false);
    // Stage S1b: the host's time policy is set here, from EngineHostOptions (Stepped by default for API hosts).
}

EngineHost::~EngineHost() {
    stop();
    // Every waiter is released before the engine goes: a client thread blocked in Session::call must not wait
    // on a pump that is never coming.
    local_.queue().shutdown();
}

void EngineHost::bindHostThread() { local_.queue().bindToCurrentThread(); }

void EngineHost::run(Session& session) {
    ai::MainThreadQueue& q = local_.queue();
    q.bindToCurrentThread();
    while (!stopping()) {
        session.pump();
        // S6: render and capture job slices run here, between pumps (`stepJobs()`), and the wait below is skipped
        // while a job has slices left.
        if (stopping()) {
            break;
        }
        if (q.pending() == 0) {
            q.waitForWork(); // a call queued, stop() or the queue shut down; never a timer
        }
    }
}

void EngineHost::stop() {
    stop_.store(true, std::memory_order_release);
    local_.queue().wake();
}

// ---- flags -------------------------------------------------------------------------------------------

Result<bool> parseHostFlag(int argc, char** argv, int& i, HostFlags& flags) {
    const std::string_view a = argv[i];
    if (a == "--allow-subprocess") {
        flags.permissions.subprocess = true;
        return true;
    }
    const bool read = a == "--read-root";
    const bool write = a == "--write-root";
    const bool both = a == "--root";
    if (!read && !write && !both) {
        return false;
    }
    if (i + 1 >= argc) {
        return fail("{} needs a directory", a);
    }
    std::error_code ec;
    const fs::path given = argv[++i];
    const fs::path root = fs::weakly_canonical(fs::absolute(given, ec), ec);
    if (ec || !fs::is_directory(root, ec)) {
        return fail("{} '{}' is not a directory", a, given.string());
    }
    // --root is both: a read root, so the listings (`asset.list_importable`, `world.list_recipes`) walk it, and a
    // write root, so a bare project name lands in it.
    if (read || both) {
        flags.permissions.readRoots.push_back(root);
    }
    if (write || both) {
        flags.permissions.writeRoots.push_back(root);
    }
    return true;
}

Result<int> protectStdout() {
    std::fflush(stdout);
    const int protocol = ::dup(STDOUT_FILENO);
    if (protocol < 0) {
        return fail("cannot duplicate stdout for the protocol: {}", std::error_code(errno, std::generic_category()).message());
    }
    if (::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        ::close(protocol);
        return fail("cannot point stdout at stderr: {}", std::error_code(errno, std::generic_category()).message());
    }
    return protocol;
}

void restoreStdout(int protocolFd) {
    std::fflush(stdout);
    (void)::dup2(protocolFd, STDOUT_FILENO);
    ::close(protocolFd);
}

} // namespace avgen::api
