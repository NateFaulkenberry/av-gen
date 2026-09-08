#include "core/file_watcher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <thread>

using namespace avgen;

TEST_CASE("FileWatcher reports modified, removed and recreated files", "[core][watcher]") {
    const auto path = std::filesystem::temp_directory_path() / "avgen_watch.txt";
    {
        std::ofstream(path) << "a";
    }
    FileWatcher watcher(0.0);
    watcher.watch(path);
    CHECK(watcher.pollNow().empty());

    // Ensure the mtime differs even on coarse filesystems.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(path) << "bb";
    }
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now());
    auto changed = watcher.pollNow();
    REQUIRE(changed.size() == 1);
    CHECK(changed[0] == path);
    CHECK(watcher.pollNow().empty());

    std::filesystem::remove(path);
    changed = watcher.pollNow();
    REQUIRE(changed.size() == 1);
    {
        std::ofstream(path) << "c";
    }
    changed = watcher.pollNow();
    REQUIRE(changed.size() == 1);
    watcher.unwatch(path);
    CHECK(watcher.size() == 0);
    std::filesystem::remove(path);
}

TEST_CASE("FileWatcher poll respects its interval", "[core][watcher]") {
    FileWatcher watcher(1000.0);
    const auto path = std::filesystem::temp_directory_path() / "avgen_watch2.txt";
    {
        std::ofstream(path) << "a";
    }
    watcher.watch(path);
    (void)watcher.poll(); // first poll happens (lastPoll_ starts at epoch)
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(watcher.poll().empty());    // interval not elapsed
    CHECK(watcher.pollNow().size() == 1);
    std::filesystem::remove(path);
}
