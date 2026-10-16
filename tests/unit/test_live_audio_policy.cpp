// ADR-1408: a live project's audio -- the saved capture device, unless the command line chose otherwise.
#include "app/live_audio_policy.hpp"

#include <catch2/catch_test_macros.hpp>

using avgen::app::liveSonicOpensSavedInput;

TEST_CASE("A live project opens the saved capture device unless the command line named the audio", "[app][live]") {
    // the ordinary live case: nothing open, nothing on the command line, a device saved -> open it
    CHECK(liveSonicOpensSavedInput(false, false, false, "MacBook Pro Microphone"));
    // nothing saved: nothing to open
    CHECK_FALSE(liveSonicOpensSavedInput(false, false, false, ""));
    // already open, or --input named one (opened already): leave it
    CHECK_FALSE(liveSonicOpensSavedInput(true, false, false, "MacBook Pro Microphone"));
    CHECK_FALSE(liveSonicOpensSavedInput(false, true, false, "MacBook Pro Microphone"));
    // --audio <file>: that file is the run's audio (a live project rendered or profiled against a known track)
    CHECK_FALSE(liveSonicOpensSavedInput(false, false, true, "MacBook Pro Microphone"));
    static_assert(!liveSonicOpensSavedInput(false, false, true, "x"));
}
