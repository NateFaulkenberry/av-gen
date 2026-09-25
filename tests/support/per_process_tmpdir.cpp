// Every test process gets its own temporary directory.
//
// About 150 fixtures write to fixed names under `std::filesystem::temp_directory_path()` --
// `avgen_render_job`, `avgen-authored-lights`, `avgen_tracejob.exr`. Each name is unique within one
// run, and that used to be enough. It is not when several agents run suites on one machine at once:
// two processes share one `$TMPDIR`, and one's `remove_all` deletes the other's fixture mid-test.
// That is exactly how "Watching a render does not change it" failed on 2026-09-24: its project file
// vanished under it, and it passed alone, twice. This is docs/testing.md's "the scratchpad is shared
// by every agent in a session", one level down.
//
// The fix is structural rather than 150 edits. On POSIX, `temp_directory_path()` reads `TMPDIR`, so
// pointing `TMPDIR` at a per-process directory before the first test runs moves every fixture at
// once, including the ones nobody has written yet. The directory is removed when the run ends
// (unless `AVGEN_TEST_KEEP` is set, which the fixtures already honour).
//
// Compiled into both test binaries: tests/CMakeLists.txt globs `support/*.cpp` into each.

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

class PerProcessTempDir final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        std::error_code ec;
        const std::filesystem::path shared = std::filesystem::temp_directory_path(ec);
        if (ec) {
            return; // no temp directory at all: leave the environment as it is
        }
        dir_ = shared / ("avgen-tests-" + std::to_string(static_cast<long long>(::getpid())));
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
        if (ec) {
            dir_.clear();
            return;
        }
        ::setenv("TMPDIR", dir_.c_str(), 1);
    }

    void testRunEnded(const Catch::TestRunStats&) override {
        if (dir_.empty() || std::getenv("AVGEN_TEST_KEEP") != nullptr) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

private:
    std::filesystem::path dir_;
};

} // namespace

CATCH_REGISTER_LISTENER(PerProcessTempDir)

#include <catch2/catch_test_macros.hpp>

TEST_CASE("each test process has a temporary directory of its own", "[support][tmpdir]") {
    const std::filesystem::path tmp = std::filesystem::temp_directory_path();
    CHECK(tmp.filename().string() == "avgen-tests-" + std::to_string(static_cast<long long>(::getpid())));
    CHECK(std::filesystem::is_directory(tmp));
}
