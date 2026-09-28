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

} // namespace avgen::app
