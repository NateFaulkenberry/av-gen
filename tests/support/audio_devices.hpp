#pragma once

// ADR-1550: tests that open the machine's real audio devices run only when asked. Opening a device a performer is
// playing through (BlackHole inside a Multi-Output, the speakers) or building aggregates on it reconfigures their
// audio mid-play: measured 2026-10-19, a day of suite runs started BlackHole 78 times and the speakers 50. CI
// runners have no audio devices, so nothing is lost there; on a workstation, set AVGEN_AUDIO_DEVICE_TESTS=1.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string_view>

namespace avgen::testsupport {

[[nodiscard]] inline bool audioDeviceTestsEnabled() {
    const char* v = std::getenv("AVGEN_AUDIO_DEVICE_TESTS");
    return v != nullptr && *v != '\0' && std::string_view(v) != "0";
}

} // namespace avgen::testsupport

#define AVGEN_REQUIRE_AUDIO_DEVICE_TESTS()                                                                      \
    do {                                                                                                         \
        if (!::avgen::testsupport::audioDeviceTestsEnabled()) {                                                  \
            SKIP("opens the machine's real audio devices; set AVGEN_AUDIO_DEVICE_TESTS=1 to run (ADR-1550)");    \
        }                                                                                                        \
    } while (false)
