#include "ui/particle_weather_rows.hpp"

#include <iterator>

namespace avgen::ui {

// ADR-520. See the header for why this is a table and not twenty ImGui calls.
//
// The ranges are the *useful* range of each control, not its hard range -- `registerParticleParameters`
// owns the hard limits and this cannot exceed them (a drag clamps to lo/hi, and every pair here is
// inside the pair there). Ordered the way an artist reaches for them: what it hits, how big they
// are, whether it blinks, how it moves, and how it catches the light.
std::span<const ParticleWeatherRow> particleWeatherRows() {
    static constexpr ParticleWeatherRow kRows[] = {
        {"collisionHeight", "ground height", -20.0f, 20.0f, "%.2f m",
         "The world height of the plane particles land on. Only does anything when the system's "
         "collision response is not 'none' -- see the scene file's `collision` key."},
        {"splashSize", "splash size", 0.0f, 30.0f, "%.1fx",
         "How wide the ring a landing leaves grows, as a multiple of the particle's own size."},
        {"sizeVariance", "size spread", 0.0f, 1.0f, "%.2f",
         "How much particle sizes differ from one another. 0 makes every particle the same size, "
         "which is the single most recognisable tell of a computer-generated field."},
        {"sizeSkew", "size skew", 0.2f, 6.0f, "%.2f",
         "Above 1: many small particles and a few large ones, which is what real drops, flakes, "
         "motes and embers look like. 1 is an even spread."},
        {"dragSizeBias", "size drives speed", 0.0f, 1.0f, "%.2f",
         "How much a particle's size decides how fast it falls. At 0 every particle falls at the "
         "same speed, which reads as a machine. Turn it up and the big ones fall faster, the way "
         "big drops and dense flakes do."},
        {"pulseRate", "blink rate", 0.0f, 8.0f, "%.2f Hz",
         "How often each particle brightens and dims. 0 is a steady glow."},
        {"pulseDepth", "blink depth", 0.0f, 1.0f, "%.2f", "How far down the blink takes the brightness."},
        {"pulseSync", "blink sync", 0.0f, 1.0f, "%.2f",
         "0: every particle blinks on its own rhythm. 1: the whole field flashes together."},
        {"pulseSharpness", "blink sharpness", 0.5f, 16.0f, "%.1f",
         "1 is a slow swell. Higher turns it into a brief flash with darkness between."},
        {"clusterRadius", "cluster radius", 0.0f, 20.0f, "%.2f m",
         "How tightly particles gather around each cluster. Only does anything when the scene gives "
         "the system a `clusterCount`."},
        {"pauseRate", "dart rate", 0.0f, 4.0f, "%.2f Hz",
         "How often each particle stops and hangs before moving again. 0 means it never stops."},
        {"pauseFraction", "dart pause", 0.0f, 1.0f, "%.2f", "The share of that cycle spent nearly still."},
        {"scatterStrength", "light catch", 0.0f, 6.0f, "%.2f",
         "How strongly particles catch the key light when you look towards it. This is what makes "
         "dust suddenly visible in a sunbeam. 0 is off."},
        {"scatterAnisotropy", "light focus", -0.9f, 0.9f, "%.2f",
         "Towards 1 the catch is confined to looking almost straight into the light; 0 spreads it "
         "over the whole sky; below 0 it brightens looking away from the light instead."},
    };
    return std::span<const ParticleWeatherRow>(kRows, std::size(kRows));
}

} // namespace avgen::ui
