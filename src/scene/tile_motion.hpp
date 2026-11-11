#pragma once

// ADR-1998: the moving ground's music, as a function of song time.
//
// The routes on `env/tiles/*` (the swell on `response.low`, the rings on onsets, the breath on the energy) move the
// ground the viewer sees. A route's output is history -- its smoothing, its spring -- so the value a frame draws depends
// on the frames before it, and a seek used to leave the routes out of everything the characters decide (ADR-1964). A
// `TileMotionTrack` is those parameters' final values sampled on a fixed grid over the whole song, made once from the
// analysed audio (offline, where the audio is a function of time) and read by every surface at any instant: the drawn
// ground, the bodies' stance, their planning, and the crest that throws them (ADR-1997). The same track at the same
// instant gives the same ground, whatever the frame rate and however the playhead got there.
//
// Each lane is one scalar of `TileDynamics`; a scalar without a lane keeps the value the dynamics already carry. Between
// samples a lane is linear, before the first and after the last it holds. `rings` are the routed trigger's rising
// edges, with the position and strength they were dropped at; with `hasRings` they replace the dynamics' live rings.

#include "scene/tile_dynamics.hpp"

#include <cstdint>
#include <vector>

namespace avgen::scene {

enum class TileMotionField : std::uint8_t {
    Amplitude,
    WaveAmplitude,
    WaveSpeed,
    WaveWavelength,
    WaveDirection,
    WaveOnLand,
    BreathingAmplitude,
    BreathingPeriod,
    RollingX,
    RollingZ,
    RingAmplitude,
    RingSpeed,
    RingDecay,
};

struct TileMotionTrack {
    struct Lane {
        TileMotionField field = TileMotionField::Amplitude;
        int wave = -1; // the train, for the wave fields
        std::vector<float> values;
    };
    double start = 0.0; // the instant of sample 0 (s)
    double rate = 60.0; // samples per second
    std::vector<Lane> lanes;
    bool hasRings = false;
    std::vector<TileEvent> rings; // sorted by time
    std::uint64_t key = 0;        // what it was made from (0 = unknown)

    [[nodiscard]] bool empty() const { return lanes.empty() && !hasRings; }
    // A lane's value at `t` (linear between samples, held outside them).
    [[nodiscard]] float value(const Lane& lane, double t) const;
    // Writes every lane's value at `t` into `d`, and with `hasRings` its rings that can still be seen at `t` into
    // `d.live`.
    void apply(TileDynamics& d, double t) const;
};

} // namespace avgen::scene
