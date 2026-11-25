#pragma once

// ADR-2134: the cloud march's cost knobs after the live ladder's levers. Apart from cloud_renderer.hpp so a CPU unit
// test (and anything else without the GPU headers) can reach it.

namespace avgen::scene {
struct Clouds;
}

namespace avgen::rendering {

struct QualitySettings;

// ADR-2134: the march's cost knobs as one frame uses them: the clouds block's own values with the live ladder's
// QualitySettings levers applied (public for the unit test). `quality` null, or every lever neutral, returns exactly
// what the block authored -- the same numbers, not recomputed ones -- which is what keeps a render unchanged.
struct CloudMarchQuality {
    float resolution = 0.5f; // a fraction of the frame's, as `Clouds::resolution` (the authored value clamped 0.25..1)
    int steps = 96;
    int lightSteps = 6;
    float stepGrowth = 0.03f; // the v2 march's growth (unused by the even march)
    int erosionOctaves = 3;
};
[[nodiscard]] CloudMarchQuality cloudMarchQuality(const scene::Clouds& clouds, const QualitySettings* quality);

} // namespace avgen::rendering
