#pragma once

// The command-line flags that run a batch job and exit, and therefore imply `--headless`.
//
// It is a list because the rule was scattered: `--render`, `--pathtrace`, `--queue` and `--ab`
// each set `headless` in their own branch of `parseArgs`, and `--export-bundle` did not. So
// `avgen --project p.json --export-bundle out/` opened the live windowed app, ran it until it was
// closed, and on exit wrote the person's recent-files list and editor layout as a side effect of an
// export (QA pass, 2026-09-28). Kept header-only so the CPU suite can check it without linking the
// application.

#include <array>
#include <string_view>

namespace avgen::app {

inline constexpr std::array<std::string_view, 5> kBatchFlags = {
    "--render", "--pathtrace", "--queue", "--ab", "--export-bundle",
};

[[nodiscard]] constexpr bool flagImpliesHeadless(std::string_view flag) {
    for (const auto f : kBatchFlags) {
        if (f == flag) {
            return true;
        }
    }
    return false;
}

// ADR-2133: which runs open live control. Before it every engine opened an OSC listener on 0.0.0.0:9000 and MIDI input
// -- headless captures and `--ai-script` runs included -- because the hub was on unless a job switched it off. Now it
// is off unless this says otherwise:
//   - the interactive editor (a window a person works in, not `--capture-ui`/`--ui-script`/`--ui-ab` automation) opens
//     OSC, on 127.0.0.1, and MIDI, as the owner's live use expects;
//   - any other run opens OSC only when the command line asks (`--osc-port` or `--osc-bind`) and MIDI only when it
//     asks (`--midi <filter>`). A project file is not a request: every save before ADR-2133 wrote "enabled": true;
//   - `--no-osc` keeps OSC shut in any run.
struct LiveControlAsk {
    bool interactive = false; // the editor, for a person
    bool oscFlag = false;     // --osc-port or --osc-bind
    bool midiFlag = false;    // --midi <filter>
    bool noOsc = false;       // --no-osc
};
struct LiveControl {
    bool osc = false;
    bool midi = false;
};
[[nodiscard]] constexpr LiveControl liveControlFor(const LiveControlAsk& ask) {
    return LiveControl{.osc = !ask.noOsc && (ask.interactive || ask.oscFlag), .midi = ask.interactive || ask.midiFlag};
}

} // namespace avgen::app
