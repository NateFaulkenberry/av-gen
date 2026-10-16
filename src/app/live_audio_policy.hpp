#pragma once

// ADR-1408: which audio a live (`sonic.live`) project hears when it turns live input on.

#include <string_view>

namespace avgen::app {

// Whether turning live Sonic input on should open the capture device saved in the Live panel's settings. Not when a
// live input is already open, not when the command line named a device (--input: it was opened already), and not when
// the command line named an audio FILE (--audio): that file is this run's audio -- a live project rendered or profiled
// against a known track -- and a live project carries no track of its own.
[[nodiscard]] constexpr bool liveSonicOpensSavedInput(bool liveInputOpen, bool commandLineInput, bool commandLineAudio,
                                                      std::string_view savedInput) {
    return !liveInputOpen && !commandLineInput && !commandLineAudio && !savedInput.empty();
}

} // namespace avgen::app
