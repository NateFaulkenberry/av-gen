#pragma once

// `avgen --input-meter <device> [seconds]` (ADR-1522): the hardware check for live multichannel input. Opens the
// device as the live engine would for sources -- through the Core Audio backend on macOS, every channel of every
// input stream -- and prints each channel's peak (dBFS) once a second, with the drops, gaps and overloads seen. An
// unplugged device has its IO stopped and is reopened by id once it is back, as the live engine does. Move a
// signal from input to input: the channel that answers is the one AV Gen will analyse for that input.

#include <string>

namespace avgen::app {

int runInputMeter(const std::string& device, double seconds);

} // namespace avgen::app
