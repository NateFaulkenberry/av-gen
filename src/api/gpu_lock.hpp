#pragma once

// The machine-level GPU lock, in-process (ADR-2159, Engine API stage S6; owner decision 2).
//
// `tools/gpu-lock.sh` serialises every piece of GPU work on this machine: two jobs on one device corrupt each
// other's measurements (measured; the script's header has the cases). An `avgen serve` render is GPU work, so the
// EngineHost takes the SAME lock itself, per job, rather than trusting every caller to have wrapped it. This file
// is the script's protocol in C++, so the two interoperate in both directions:
//
//   - The lock is a DIRECTORY, `${TMPDIR:-/tmp}/avgen-gpu.lock`, taken with `mkdir` (atomic on every filesystem
//     here). The path is computed from the environment at each acquire, never from the source tree, so a packaged
//     build (stage S7) takes the same lock an agent's script does.
//   - The winner writes its pid WHOLE: to `pid.<pid>`, then renamed to `pid`, so a waiter never reads a half-written
//     file. It writes `holder` (AVGEN_AGENT, or "avgen-<pid>").
//   - A waiter that finds a `pid` naming a dead process reclaims the lock -- only if the file still names that pid
//     when it removes it, so two waiters cannot both reclaim. A lock with no `pid` is a winner between its `mkdir`
//     and its write, or one that died there: it is reclaimed once its directory is older than 30 s.
//   - A holder releases only its own lock: the directory is removed while `pid` still names this process.
//
// One difference from the script, in the safe direction: `kill -0` fails with EPERM for a live process of another
// user, and the script reads that as dead. Here EPERM is alive.
//
// **Held by an ancestor.** The GPU suites, and an agent's own `gpu-lock.sh avgen serve ...`, run us UNDER the
// script, whose `pid` is the wrapper's own shell -- our parent or further up -- and which exports nothing to its
// child. An acquire that waited for that lock would wait forever on the lock its own parent holds. So a lock whose
// pid is this process or one of its ancestors counts as held for us: `tryAcquire` answers `HeldByAncestor`, and
// neither creates nor removes anything.
//
// Held per job, released between jobs; an idle host holds nothing. Thread: whichever thread owns the object.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace avgen::api {

enum class GpuLockAttempt : std::uint8_t {
    Acquired,       // this process made the directory and owns it
    HeldByAncestor, // the lock's pid is this process or an ancestor: it is ours already
    Busy,           // another live process holds it
};
[[nodiscard]] const char* gpuLockAttemptName(GpuLockAttempt attempt);

struct GpuLockHolder {
    long pid = 0;          // 0 when the directory has no pid yet
    std::string holder;    // the `holder` file, when there is one
};

class GpuLock {
public:
    // A pid-less lock older than this is reclaimed (the script's UNOWNED_GRACE).
    static constexpr int kUnownedGraceSeconds = 30;

    // `dir` empty: `defaultPath()` at each acquire (TMPDIR is read then, not now).
    explicit GpuLock(std::filesystem::path dir = {}, std::string holderName = {});
    ~GpuLock(); // releases what it owns
    GpuLock(const GpuLock&) = delete;
    GpuLock& operator=(const GpuLock&) = delete;

    // `${TMPDIR:-/tmp}/avgen-gpu.lock`, exactly the script's.
    [[nodiscard]] static std::filesystem::path defaultPath();
    // AVGEN_AGENT, or "avgen-<pid>": what `holder` says (the script writes AVGEN_AGENT or its folder's name).
    [[nodiscard]] static std::string defaultHolder();

    // One round of the script's loop, without sleeping: make the directory, or reclaim a stale one and make it,
    // or report who holds it. Idempotent while held.
    [[nodiscard]] GpuLockAttempt tryAcquire();
    // Removes the directory if it is ours (its pid is this process). An ancestor's lock is left alone.
    void release();
    // ADR-2167: what `holder` says from now on; rewritten in place when the directory is ours (a show taking over a
    // lock its job already held, and handing it back).
    void setHolderName(std::string name);

    [[nodiscard]] bool held() const { return owned_ || ancestor_; }
    [[nodiscard]] bool ownsDirectory() const { return owned_; }
    [[nodiscard]] bool heldByAncestor() const { return ancestor_; }
    // The directory the last acquire used.
    [[nodiscard]] const std::filesystem::path& path() const { return used_; }
    // Who held it at the last Busy (or HeldByAncestor) answer.
    [[nodiscard]] const std::optional<GpuLockHolder>& lastHolder() const { return lastHolder_; }

private:
    [[nodiscard]] std::filesystem::path target() const;
    [[nodiscard]] bool make(const std::filesystem::path& dir);

    std::filesystem::path dir_;
    std::filesystem::path used_;
    std::string holderName_;
    bool owned_ = false;
    bool ancestor_ = false;
    std::optional<GpuLockHolder> lastHolder_;
};

// The parent of `pid` (0 when it cannot be read: no such process).
[[nodiscard]] long parentPid(long pid);
// `pid` is this process or one of its ancestors.
[[nodiscard]] bool isSelfOrAncestor(long pid);
// `kill(pid, 0)` succeeds, or fails with EPERM (alive, another user's).
[[nodiscard]] bool processAlive(long pid);
// The pid in `<dir>/pid`, or nothing when the file is absent, empty or not a number.
[[nodiscard]] std::optional<long> readLockPid(const std::filesystem::path& dir);

} // namespace avgen::api
