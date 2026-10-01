#pragma once

// ADR-1053: `avgen --trace-jumps <project.json> [options]` -- finds entities that jump.
//
// The project is played offline (no window, no GPU) frame by frame, and every transform parameter is
// sampled after each update: SDF objects' transform/position and scale, named SDF nodes' translation,
// scale and size, composition nodes' position and scale, and particle emitters' position. A JUMP is a
// one-frame change that is both large (over the threshold) and a step (several times the parameter's
// own motion in the frames around it), on a visible object, away from a camera cut. Each jump is
// attributed to the routes and timeline tracks that drive the parameter, with what makes them step
// (a pulse or an instant-attack event with no smoothing chain, a step key).
//
//   --fps <f>          sampling rate (default: the project's render rate)
//   --range a:b        seconds (default: the whole project)
//   --threshold <m>    the smallest jump reported, metres (default 0.03; scale/size use 3%)
//   --json <file|->    the report (default: none; the summary goes to stdout)

namespace avgen::app {

// argv[1] is "--trace-jumps". Returns the process exit code.
int runJumpTraceCommand(int argc, char** argv);

} // namespace avgen::app
