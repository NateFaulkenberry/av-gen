#pragma once

// ADR-1953: the data of Musical Geography's moving ground. Kept free of the engine's error header so the scene types can
// carry it; the rule that evaluates it, its validation and its JSON are in scene/tile_terrain.hpp.

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace avgen::scene {

// ---- ADR-1953: the moving ground (Phase 3) ----
//
// `H(tile, t) = base(tile, t) + amplitude * (waves + rings)`, every term a pure function of `t` and the event list, so a
// seek lands exactly where playing would have (ADR-091). One block for the whole scene (`environment.tileDynamics`):
// every "tiles" generator -- each ring and the underlay -- moves with the same ground, so the rings never disagree.
//   * WAVES: up to four trains, planar (a heading) or circular (from a centre), each with a wavelength, a speed, an
//     amplitude and a crest sharpness; they interfere by summing. `onLand` / `onWater` weight a train by the tile's
//     water channel (a sea swell that dies at the shore is onLand 0).
//   * BREATHING: the hills' relief scales by 1 + amplitude * sin(2 pi (t / period + n)), n a slow regional noise, so
//     the land swells in broad regions, out of step with each other.
//   * ROLLING: the hills' domain is advected at `rolling` m/s: the formations drift across the fixed lattice.
//   * RINGS: a disturbance at a point and a time -- a ring front leaving it at `speed`, a ripple of `wavelength`
//     across a Gaussian of `width`, fading with `decay` seconds. Authored (`events`) or triggered live.
// The CREST channel is the normalised crest of the trains and rings that ask for it (0..1), for the light that rides a
// wavefront (request 1971); the kernel writes it into the record's emissive alpha.
// A term shorter than two cells of a coarse ring would alias there, so each ring fades it out between two and four of
// its cells; the near ring keeps everything.
inline constexpr std::size_t kMaxTileWaves = 4;
inline constexpr std::size_t kMaxTileEvents = 16;

struct TileWave {
    bool circular = false;
    float amplitude = 0.0f;   // metres, crest to rest
    float wavelength = 40.0f; // metres
    float speed = 6.0f;       // metres per second along the travel (negative: the other way)
    float direction = 0.0f;   // degrees: the heading a planar train travels (0 = +X, 90 = +Z)
    glm::vec2 centre{0.0f};   // a circular train's centre (generator space, metres)
    float sharpness = 0.0f;   // 0 = a sine; toward 1 the crests narrow and the troughs flatten
    float crest = 1.0f;       // this train's weight in the crest channel
    float onLand = 1.0f;      // weight where the tile is land
    float onWater = 1.0f;     // weight where the tile is water
    float phase = 0.0f;       // turns
};

struct TileRingSpec {
    float amplitude = 0.0f;   // metres, at strength 1
    float speed = 40.0f;      // metres per second, the front
    float wavelength = 10.0f; // metres, the ripple inside the front
    float width = 8.0f;       // metres, the front's Gaussian
    float decay = 2.5f;       // seconds to 1/e; a ring older than six of them is gone
    float crest = 1.0f;       // its weight in the crest channel
};

struct TileEvent {
    float time = 0.0f;        // seconds on the timeline
    glm::vec2 position{0.0f}; // generator space, metres
    float strength = 1.0f;
};

struct TileDynamics {
    float amplitude = 1.0f; // scales the waves and the rings together (the obvious route for a loudness)
    std::vector<TileWave> waves;
    float breathingAmplitude = 0.0f;   // fraction of the relief
    float breathingPeriod = 16.0f;     // seconds
    float breathingWavelength = 800.0f; // metres, the size of a region that breathes together
    glm::vec2 rolling{0.0f};           // metres per second
    TileRingSpec rings;
    std::vector<TileEvent> events;     // authored
    std::vector<TileEvent> live;       // triggered while running (not saved; seeking back drops the later ones)
    // ADR-1970: contour edges. A tile side is a contour edge where the level floor((h - offset) / interval) drops to the
    // neighbour across it; the kernel flags those sides in the record (with the crest) and the `tileContour` material
    // op draws them. interval 0 (the default) = none. They follow the moving ground: the contours move with it.
    float contourInterval = 0.0f;      // metres
    float contourOffset = 0.0f;        // metres

    // True when anything moves: false is the static ground, exactly as before.
    [[nodiscard]] bool active() const;
    // The events that can still be seen at `t`, newest first, at most kMaxTileEvents (authored and live together).
    [[nodiscard]] std::vector<TileEvent> eventsAt(float t) const;
};

} // namespace avgen::scene
