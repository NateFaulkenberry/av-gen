#pragma once

// A scratch directory of this process's own.
//
// ctest -j runs each test in a *separate process* of the same binary. Test helpers that write to a
// fixed path under the system temp directory are therefore sharing mutable state between concurrent
// tests: two processes create /tmp/avgen_comp_tri.glb, and the first to finish deletes it out from
// under the second. That produced a small family of tests that failed under -j4 and passed alone,
// which is the most expensive kind of failure to have -- it looks like flakiness in the code under
// test rather than in the harness.
//
// Everything that needs scratch space on disk should go through here.

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace avgen::testsupport {

inline std::filesystem::path processTempDir() {
    static const std::filesystem::path dir = [] {
#ifdef _WIN32
        const auto pid = static_cast<long long>(_getpid());
#else
        const auto pid = static_cast<long long>(::getpid());
#endif
        auto d = std::filesystem::temp_directory_path() / ("avgen_test_" + std::to_string(pid));
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

} // namespace avgen::testsupport
