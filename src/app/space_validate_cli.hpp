#pragma once

// ADR-1051: `avgen --validate-space <scene.json | project.json> [options]` -- the room / spatial validator,
// headless, no window, no GPU, no engine: it reads the scene file and prints the report.
//
//   --json <file|->     the structured report (default: none; "-" = stdout, the text then goes to stderr)
//   --text <file>       the human-readable report to a file (default: stdout)
//   --rules <file>      rules deep-merged over the built-in ones
//   --dump-rules        print the effective rules and exit
//   --margin <m>        lyric clearance margin (overrides the rules)
//   --eye <m>           camera eye height above the journey path
//   --title <text>      the report's title
//   --no-camera         skip the camera-path sweep
//   --strict            exit 1 when there are errors (default: exit 0 whenever the report was written)

namespace avgen::app {

// argv[1] is "--validate-space". Returns the process exit code.
int runSpaceValidateCommand(int argc, char** argv);

} // namespace avgen::app
