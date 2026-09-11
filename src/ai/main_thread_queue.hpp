#pragma once

// Marshalling AI work onto the main thread (ADR-094, spec §38).
//
// ## The problem this solves, stated precisely
//
// The orchestrator runs on a `JobSystem` worker, because it makes blocking network calls and §38
// forbids those anywhere near the frame. But every tool it invokes touches `ParameterSet`,
// `Modulator`, `Timeline` or `Scene` -- and those are read and written by `Engine::update()` on the
// main thread, every frame, with no lock anywhere. A tool call from a worker would be a data race
// against the frame loop, which on a good day is a torn `vec3` and on a bad one is a dangling
// `IParameter*` while `rebind()` reallocates.
//
// So: the *decision* of what to do happens on a worker; the *doing* happens on the main thread.
// This is the same split ADR-066 made for Generate World (compose on a worker, install on the main
// thread) and it is made here for the same reason.
//
// ## Why every tool goes through it, including reads
//
// A read of a parameter while the main thread is writing finals is still a race. The honest and
// simple rule is that everything touching engine state is marshalled, and `ToolAnnotations`
// records `requiresMainThread` per tool so a later scheduler can relax it with evidence rather
// than with optimism. The cost is a queue hop of a few microseconds against a network round trip
// of a few hundred milliseconds.
//
// ## Why the frame does not stall
//
// `pump()` takes a time budget and stops when it is spent, leaving the rest for the next frame. A
// tool that turns out to be slow costs one budget slice, not a dropped frame. The tools in this
// pass are parameter writes and small queries, all far inside the default budget -- the budget is
// there because "all the tools are cheap today" is not a property anyone can promise about
// tomorrow.
//
// ## Deadlock
//
// `run()` called from the pumping thread itself executes inline rather than enqueuing and waiting
// for a pump that can never come. Without that, one accidental call from the UI thread would hang
// the application, and the mistake is easy enough to make that guarding against it is cheaper than
// relying on nobody making it.

#include "ai/tool_context.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace avgen::ai {

class MainThreadQueue {
public:
    static constexpr double kDefaultBudgetMs = 4.0;
    static constexpr auto kDefaultTimeout = std::chrono::seconds{30};

    MainThreadQueue() = default;
    ~MainThreadQueue();
    MainThreadQueue(const MainThreadQueue&) = delete;
    MainThreadQueue& operator=(const MainThreadQueue&) = delete;

    // Declares which thread will pump. Called once, from that thread. `pump()` also sets it, so a
    // caller that simply pumps in its loop need not call this.
    void bindToCurrentThread();

    // Runs `fn` on the pumping thread and waits for it. Returns false if the queue was shut down,
    // the cancel token fired before the work started, or the wait timed out -- in which case `fn`
    // is guaranteed not to have run, or to have finished, never to be running still.
    [[nodiscard]] bool run(std::function<void()> fn, const CancelToken& cancel,
                           std::chrono::milliseconds timeout =
                               std::chrono::duration_cast<std::chrono::milliseconds>(kDefaultTimeout));

    // Main thread, once per frame. Runs queued work until the budget is spent. Returns how many
    // items ran.
    std::size_t pump(double budgetMs = kDefaultBudgetMs);

    // Releases every waiter with a failure and refuses new work. Call before tearing the engine
    // down, or a worker will wait on a pump that is never coming again.
    void shutdown();

    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] bool running() const { return !stopped_; }

private:
    struct Item {
        std::function<void()> fn;
        bool done = false;
        bool ran = false;
    };

    mutable std::mutex mutex_;
    std::condition_variable done_;
    std::deque<std::shared_ptr<Item>> queue_;
    std::thread::id owner_{};
    bool haveOwner_ = false;
    bool stopped_ = false;
};

} // namespace avgen::ai
