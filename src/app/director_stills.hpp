#pragma once

// Preview stills for a proposed plan (spec §55, ADR-764): one small offscreen frame per proposed
// shot, at the shot's mid-time, rendered from a SCRATCH COPY of the project with the proposal
// installed -- never from the person's project, which is not touched (ADR-753's isolation).
//
// Deliberately synchronous and on request: it loads a scratch session, uploads its textures once
// into a renderer of its own (so the editor's renderer keeps its uploads), and seeks to each
// mid-time. Seconds of work, paid only when the person asks or makes a preview.

#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "gpu/readback.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::app {

class Engine;

struct ShotStill {
    std::string item;     // the plan's shot key
    std::string shot;     // the sequence shot's name
    double seconds = 0.0; // the instant rendered: the shot's middle
    gpu::Image8 image;
};

struct ShotStillsReport {
    std::vector<ShotStill> stills;
    double loadMs = 0.0;   // the scratch session
    double renderMs = 0.0; // every seek and frame
};

// The plan's shots as `compilation` would install them, in plan order: (item key, shot).
[[nodiscard]] std::vector<std::pair<std::string, const seq::Shot*>> proposedShots(const directing::Compilation& c);

// Writes `live`'s scratch copy into `scratchDir`, loads it, installs `compilation`, and renders each
// proposed shot's middle at `width` x `height`. The copy is deleted before returning.
[[nodiscard]] Result<ShotStillsReport> renderShotStills(gpu::Context& context, gpu::ShaderLibrary& shaders, Engine& live,
                                                       const directing::Compilation& compilation, std::uint32_t width,
                                                       std::uint32_t height, const std::filesystem::path& scratchDir);

} // namespace avgen::app
