#include "scene/tile_motion.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

float TileMotionTrack::value(const Lane& lane, double t) const {
    const std::vector<float>& v = lane.values;
    if (v.empty()) {
        return 0.0f;
    }
    const double u = (t - start) * rate;
    if (!(u > 0.0)) {
        return v.front();
    }
    const auto i = static_cast<std::size_t>(std::floor(u));
    if (i + 1 >= v.size()) {
        return v.back();
    }
    const auto f = static_cast<float>(u - static_cast<double>(i));
    return v[i] + (v[i + 1] - v[i]) * f;
}

void TileMotionTrack::apply(TileDynamics& d, double t) const {
    for (const Lane& lane : lanes) {
        const float x = value(lane, t);
        TileWave* w = lane.wave >= 0 && static_cast<std::size_t>(lane.wave) < d.waves.size()
                          ? &d.waves[static_cast<std::size_t>(lane.wave)]
                          : nullptr;
        switch (lane.field) {
        case TileMotionField::Amplitude: d.amplitude = x; break;
        case TileMotionField::WaveAmplitude: if (w) w->amplitude = x; break;
        case TileMotionField::WaveSpeed: if (w) w->speed = x; break;
        case TileMotionField::WaveWavelength: if (w) w->wavelength = x; break;
        case TileMotionField::WaveDirection: if (w) w->direction = x; break;
        case TileMotionField::WaveOnLand: if (w) w->onLand = x; break;
        case TileMotionField::BreathingAmplitude: d.breathingAmplitude = x; break;
        case TileMotionField::BreathingPeriod: d.breathingPeriod = x; break;
        case TileMotionField::RollingX: d.rolling.x = x; break;
        case TileMotionField::RollingZ: d.rolling.y = x; break;
        case TileMotionField::RingAmplitude: d.rings.amplitude = x; break;
        case TileMotionField::RingSpeed: d.rings.speed = x; break;
        case TileMotionField::RingDecay: d.rings.decay = x; break;
        }
    }
    if (hasRings) {
        // the rings dropped by `t` and not yet gone (six decays, as `eventsAt` keeps them), at most a buffer's worth
        const auto now = static_cast<float>(t);
        const float horizon = 6.0f * d.rings.decay;
        const auto end = std::upper_bound(rings.begin(), rings.end(), now,
                                          [](float x, const TileEvent& e) { return x < e.time; });
        auto begin = std::lower_bound(rings.begin(), end, now - horizon,
                                      [](const TileEvent& e, float x) { return e.time < x; });
        if (end - begin > static_cast<std::ptrdiff_t>(4 * kMaxTileEvents)) {
            begin = end - static_cast<std::ptrdiff_t>(4 * kMaxTileEvents);
        }
        d.live.assign(begin, end);
    }
}

} // namespace avgen::scene
