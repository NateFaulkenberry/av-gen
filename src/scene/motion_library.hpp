#pragma once

// Phase C §39/§40/§41/§75/§76/§81: loading a motion database without stalling anything, and
// replacing one without anything seeing it half-built.
//
// **The shape is the one §76 draws**: old database -> new database loads asynchronously ->
// validated -> atomic swap -> existing instances migrate or fall back. Each arrow is a property
// something here enforces:
//
//   * **asynchronously** (§39/§81). `requestLoad` returns at once; the read, the decode and every
//     check run on the slot's own worker thread. A timeline scrub, a project load and a UI frame
//     never wait on a motion database. Nothing on the runtime path *builds* one -- that is the
//     offline half (§37), and a slot only ever reads what the builder wrote.
//   * **validated** before it is visible. `readMotionDatabase` refuses a stale, corrupt or
//     mismatched file (§36), and the slot additionally checks the database against the pack it
//     is published with: same skeleton, same pack content, a clip for every clip index.
//   * **atomic swap** (§40). What is published is a `shared_ptr<const MotionAsset>`, swapped under
//     a lock held for a pointer copy. A reader gets the old asset or the new one; there is no state
//     in between, and a reader still holding the old one keeps it alive until it lets go.
//   * **migrate or fall back**. A character's `MotionMemory` is stamped with the identity of the
//     database it indexes; `MatchMotionProvider` treats a memory from another database as a first
//     selection, and its `pose` declines rather than posing a stale index (see `MotionMemory`).
//
// **Immutable and shared** (§41/§74/§75). The asset is `const` from the moment it is published.
// Every character holds the same one -- a hundred aliens, one feature array -- and per-character
// state is the `MotionMemory` value the entity already owns. Nothing here is per character.
//
// **Last request wins.** A request issued while another is loading supersedes it: the earlier
// result is discarded when it completes rather than published over the newer one, which is the
// order a person editing a database expects.

#include "core/error.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace avgen::scene {

// One pack and one database built from it: everything a matcher needs, as one immutable value.
// The pack is carried because the database indexes its clips; keeping them together is what
// makes it impossible to swap one without the other.
struct MotionAsset {
    MotionPack pack;
    MotionDatabase db;
    std::filesystem::path packDirectory;
    std::filesystem::path databaseFile;
};

// Load and cross-check a pack and a database from disk. Synchronous: this is what the slot's worker
// runs, and what an offline tool calls directly.
[[nodiscard]] Result<std::shared_ptr<const MotionAsset>> loadMotionAsset(
    const std::filesystem::path& packDirectory, const std::filesystem::path& databaseFile);

// Check that `db` can be searched against `pack`: the same skeleton, built from this pack's
// content, and every clip index it holds naming a clip the pack has. Shared by the loader and by
// `MotionDatabaseSlot::publish`, so an asset assembled in memory meets the same bar as one read.
[[nodiscard]] Result<void> checkMotionAsset(const MotionPack& pack, const MotionDatabase& db);

enum class MotionLoadState : std::uint8_t { Empty, Loading, Ready, Failed };
[[nodiscard]] const char* motionLoadStateName(MotionLoadState state);

struct MotionLoadStatus {
    MotionLoadState state = MotionLoadState::Empty;
    // Why the last request failed. **A failed load does not unpublish anything**: the previous
    // asset stays current, which is the fallback half of §76.
    std::string error;
    // How many assets have been published over the slot's life.
    std::uint64_t publishes = 0;
    // Wall-clock milliseconds the last completed load took on the worker. Reported so the cost a
    // load did NOT put on the caller is visible.
    double lastLoadMs = 0.0;
};

class MotionDatabaseSlot {
public:
    MotionDatabaseSlot();
    ~MotionDatabaseSlot();
    MotionDatabaseSlot(const MotionDatabaseSlot&) = delete;
    MotionDatabaseSlot& operator=(const MotionDatabaseSlot&) = delete;

    // Returns immediately. The load runs on the slot's worker and is published only if it passes
    // every check and no later request has been made.
    void requestLoad(std::filesystem::path packDirectory, std::filesystem::path databaseFile);

    // Publish an asset already in memory -- a rebuild done elsewhere, or an offline tool's own.
    // Checked exactly as a loaded one is; on failure nothing changes and the error is returned.
    [[nodiscard]] Result<void> publish(std::shared_ptr<const MotionAsset> asset);

    // The published asset, or null. Take it once per frame and hold it for the frame: holding it
    // is what keeps it alive if a swap happens meanwhile.
    [[nodiscard]] std::shared_ptr<const MotionAsset> current() const;
    [[nodiscard]] MotionLoadStatus status() const;

    // Block until no load is in flight, or the timeout passes. For offline tools and tests only; a
    // frame loop polls `status()` instead.
    bool waitIdle(std::chrono::milliseconds timeout) const;

    // Test hook: runs on the worker after validation and immediately before publishing. Set before
    // the first `requestLoad`.
    std::function<void()> beforePublish;

private:
    void run(const std::stop_token& stop);

    struct Request {
        std::filesystem::path pack;
        std::filesystem::path database;
        std::uint64_t sequence = 0;
    };

    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::shared_ptr<const MotionAsset> current_;
    MotionLoadStatus status_;
    bool pending_ = false;
    bool busy_ = false;
    Request request_;
    std::uint64_t sequence_ = 0;
    std::jthread worker_;
};

} // namespace avgen::scene
