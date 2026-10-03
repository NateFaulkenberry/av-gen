#pragma once

// The event list of a bus EVENT signal as a function of the piece (ADR-1061): what a Signal trigger
// (`{"source": "signal", "name": "response.kick"}`) fires on in a render, so a seek finds the fronts a play
// reaches.
//
//   audio.onset, audio.beat, audio.onsetLow/Mid/High   the analysis track's frames, with the strengths
//                                                      `AudioSignals::publish` gives them
//   sonic.*, response.*, notes.*, timbre.* events      a scratch `SonicRuntime` stepped over the timbre track
//                                                      and published at every analysis frame (with no audio,
//                                                      at 100 Hz over the note track): the same runtime and
//                                                      the same publish the engine runs, so the list is the
//                                                      bus's events at the analysis frames' instants
//
// Anything else (an LFO, a timeline, a control, live input) is not a function of the piece here, and the
// trigger clock records it from the bus instead. One walk answers every Sonic name, and is cached on the
// inputs' identity.

#include "analysis/analysis_track.hpp"
#include "sonic/sonic_runtime.hpp"
#include "world/effects/effect_trigger.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::sonic {

class SignalEventDeriver {
public:
    // The derived list for `name`, or nothing when it is not a function of these inputs (record it).
    // `track` and `setup` may be null. Cheap after the first Sonic name: one walk serves them all.
    [[nodiscard]] std::optional<std::vector<world::TriggerOnset>> derive(std::string_view name,
                                                                         const analysis::AnalysisTrack* track,
                                                                         const SonicSetup* setup);
    void clear();

    // Whether a name belongs to a family this deriver can answer for (the rest are recorded).
    [[nodiscard]] static bool derivable(std::string_view name);

private:
    void walkSonic(const analysis::AnalysisTrack* track, const SonicSetup& setup);

    std::uint64_t key_ = 0;
    bool walked_ = false;
    std::map<std::string, std::vector<world::TriggerOnset>, std::less<>> sonic_; // every Sonic event name fired
    std::vector<std::string> sonicEventNames_;                                    // every Sonic event name declared
};

} // namespace avgen::sonic
