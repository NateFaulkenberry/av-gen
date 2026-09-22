#include "scene/motion_library.hpp"

#include "scene/motion_database_io.hpp"

#include <fmt/format.h>

namespace avgen::scene {

const char* motionLoadStateName(MotionLoadState state) {
    switch (state) {
    case MotionLoadState::Empty: return "empty";
    case MotionLoadState::Loading: return "loading";
    case MotionLoadState::Ready: return "ready";
    case MotionLoadState::Failed: return "failed";
    }
    return "?";
}

Result<void> checkMotionAsset(const MotionPack& pack, const MotionDatabase& db) {
    if (db.skeletonDigest != pack.skeletonDigest) {
        return fail("motion asset: database '{}' was built for skeleton {}, pack '{}' is {}", db.name,
                    db.skeletonDigest, pack.name, pack.skeletonDigest);
    }
    // A database built from other content indexes clips by position, so it would pose the wrong
    // clip -- or a clip that is not there -- without anything looking wrong. Only checkable when
    // the database recorded its source; one that did not is a synthesised database, not a build.
    if (!db.build.sourcePackDigest.empty()) {
        const std::string content = motionPackContentDigest(pack);
        if (content != db.build.sourcePackDigest) {
            return fail("motion asset: database '{}' was built from pack content {}, but pack '{}' "
                        "is {}; rebuild the database",
                        db.name, db.build.sourcePackDigest, pack.name, content);
        }
    }
    if (db.clipNames.size() > pack.animation.size()) {
        return fail("motion asset: database '{}' indexes {} clips and pack '{}' has {}", db.name,
                    db.clipNames.size(), pack.name, pack.animation.size());
    }
    return {};
}

Result<std::shared_ptr<const MotionAsset>> loadMotionAsset(const std::filesystem::path& packDirectory,
                                                           const std::filesystem::path& databaseFile) {
    auto pack = readMotionPack(packDirectory);
    if (!pack) {
        return std::unexpected(pack.error());
    }
    // The pack's content digest is the expectation, so a database left behind by an older pack is
    // refused before its payload is read.
    MotionDatabaseExpectations expect;
    expect.skeletonDigest = pack->skeletonDigest;
    expect.sourcePackDigest = motionPackContentDigest(*pack);
    auto db = readMotionDatabase(databaseFile, expect);
    if (!db) {
        return std::unexpected(db.error());
    }
    if (auto ok = checkMotionAsset(*pack, *db); !ok) {
        return std::unexpected(ok.error());
    }
    auto asset = std::make_shared<MotionAsset>();
    asset->pack = std::move(*pack);
    asset->db = std::move(*db);
    asset->packDirectory = packDirectory;
    asset->databaseFile = databaseFile;
    return std::shared_ptr<const MotionAsset>(std::move(asset));
}

MotionDatabaseSlot::MotionDatabaseSlot()
    : worker_([this](const std::stop_token& stop) { run(stop); }) {}

MotionDatabaseSlot::~MotionDatabaseSlot() {
    worker_.request_stop();
    {
        const std::lock_guard lock(mutex_);
        changed_.notify_all();
    }
    // jthread joins here, after the stop request, so the worker cannot outlive the slot.
}

void MotionDatabaseSlot::requestLoad(std::filesystem::path packDirectory,
                                     std::filesystem::path databaseFile) {
    const std::lock_guard lock(mutex_);
    request_ = Request{std::move(packDirectory), std::move(databaseFile), ++sequence_};
    pending_ = true;
    status_.state = MotionLoadState::Loading;
    status_.error.clear();
    changed_.notify_all();
}

Result<void> MotionDatabaseSlot::publish(std::shared_ptr<const MotionAsset> asset) {
    if (!asset) {
        return fail("motion asset: nothing to publish");
    }
    if (auto ok = checkMotionAsset(asset->pack, asset->db); !ok) {
        return ok;
    }
    const std::lock_guard lock(mutex_);
    current_ = std::move(asset);
    ++status_.publishes;
    // A direct publish supersedes any load still in flight, exactly as a newer request does.
    ++sequence_;
    if (!pending_ && !busy_) {
        status_.state = MotionLoadState::Ready;
    }
    changed_.notify_all();
    return {};
}

std::shared_ptr<const MotionAsset> MotionDatabaseSlot::current() const {
    const std::lock_guard lock(mutex_);
    return current_;
}

MotionLoadStatus MotionDatabaseSlot::status() const {
    const std::lock_guard lock(mutex_);
    return status_;
}

bool MotionDatabaseSlot::waitIdle(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [this] { return !pending_ && !busy_; });
}

void MotionDatabaseSlot::run(const std::stop_token& stop) {
    while (true) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] { return pending_ || stop.stop_requested(); });
            if (stop.stop_requested()) {
                return;
            }
            request = request_;
            pending_ = false;
            busy_ = true;
        }

        const auto begin = std::chrono::steady_clock::now();
        auto loaded = loadMotionAsset(request.pack, request.database);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        if (loaded && beforePublish) {
            beforePublish();
        }

        const std::lock_guard lock(mutex_);
        busy_ = false;
        // Superseded: a newer request or a direct publish arrived while this one loaded. Its
        // result is dropped, never published over the newer one.
        const bool superseded = request.sequence != sequence_;
        if (!superseded) {
            status_.lastLoadMs = ms;
            if (loaded) {
                current_ = std::move(*loaded);
                ++status_.publishes;
                status_.state = MotionLoadState::Ready;
            } else {
                // §76's fallback: the previous asset stays published.
                status_.state = MotionLoadState::Failed;
                status_.error = loaded.error().message;
            }
        } else if (!pending_) {
            // Superseded by a direct publish, with nothing further queued: the slot is settled on
            // what that publish put there.
            status_.state = current_ ? MotionLoadState::Ready : MotionLoadState::Empty;
        }
        changed_.notify_all();
    }
}

} // namespace avgen::scene
