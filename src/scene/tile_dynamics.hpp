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

// ADR-1954: the floating platforms' response to a landing -- a closed-form under-damped spring at the landing point,
// with a delayed, attenuated copy on the tiles around it (the ripple):
//     h(r, t) = -depth * strength * e^(-r / rippleRadius) * e^(-zeta w a) * sin(w_d a),   a = t - t0 - r / rippleSpeed
// An impulse response: it starts at rest (no jump at the landing), the landed tile drops first, rebounds and settles; its neighbours follow, later and less. In metres and seconds, so
// it is the same spring whatever the tile size.
// CP-D r2: 256, from 8. Draft3's cast had 104 landings ringing at once; with 8, a landing was dropped from the drawn set
// 0.6 s after it happened (8 newer ones elsewhere) and its tile snapped flat mid-swing -- the abrupt end the owner saw.
// A spring now leaves only when its envelope is below 1e-3 (springSeconds), and its reach ends smoothly (kTileSpringReach).
inline constexpr std::size_t kMaxTileSprings = 256;
// A spring's reach, in ripple radii: full to 5, eased to 0 at 6 (e^-6 of the depth is left there), so a tile far from
// a landing can skip it on the GPU without a step.
inline constexpr float kTileSpringReachStart = 5.0f;
inline constexpr float kTileSpringReach = 6.0f;

struct TileSpringSpec {
    float depth = 0.6f;         // metres of the impulse at strength 1 (the first dip is ~0.75 of it at zeta 0.18)
    float frequency = 1.6f;     // Hz, the undamped oscillation
    float damping = 0.18f;      // zeta, 0..1
    float rippleSpeed = 12.0f;  // metres per second the copy travels outward
    float rippleRadius = 5.0f;  // metres over which it falls to 1/e
    float glowRadius = 3.0f;    // ADR-1960: metres over which the landing glow falls to 1/e (the landed tile and its ring)
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
    // ADR-1965: the waves by water body -- a wave's onWater weight times these on river and pond water (the sea's is 1),
    // and a slow ripple drifting downstream along each river (metres; metres; metres per second).
    float riverWaves = 1.0f;
    float pondWaves = 1.0f;
    float riverDriftAmplitude = 0.0f;
    float riverDriftWavelength = 16.0f;
    float riverDriftSpeed = 1.5f;
    TileSpringSpec spring;             // ADR-1954
    std::vector<TileEvent> springs;    // landings while running (from the characters; not saved, like `live`)
    // ADR-1970: contour edges. A tile side is a contour edge where the level floor((h - offset) / interval) drops to the
    // neighbour across it; the kernel flags those sides in the record (with the crest) and the `tileContour` material
    // op draws them. interval 0 (the default) = none. They follow the moving ground: the contours move with it.
    float contourInterval = 0.0f;      // metres
    float contourOffset = 0.0f;        // metres
    // ADR-1995: a side's edge fades in and out as a level nears either end of the step it crosses, over this fraction
    // of the step (0 = the binary edge of ADR-1970). The moving ground and the drifting offset carry levels across
    // steps continuously, so a far hill's edges ease on and off instead of flicking a whole tile in one frame.
    float contourFade = 0.15f;
    // ADR-1963: four gains a material program reads as its `tileGlow` input (x path, y contour, z crest, w pads), 1 =
    // as authored. Not used by the ground itself: they exist so a route (an audio band, a beat) can reach every tile
    // program's emission terms through one parameter each (env/tiles/glow/*) instead of an op index per program.
    glm::vec4 glow{1.0f};
    // ADR-1960: the landing glow's colour and gain, the `landingGlow` material input (rgb = colour x gain, w = gain). 0 =
    // off. A program adds tileContour's z (the landed tiles' outline x their glow) times this to its emission.
    float landingGain = 0.0f;
    glm::vec3 landingColor{1.0f};

    // True when anything moves: false is the static ground, exactly as before.
    [[nodiscard]] bool active() const;
    // The events that can still be seen at `t`, newest first, at most kMaxTileEvents (authored and live together).
    [[nodiscard]] std::vector<TileEvent> eventsAt(float t) const;
    // ADR-1954: the landings still ringing at `t` (in list order), at most kMaxTileSprings -- the newest when there are more.
    [[nodiscard]] std::vector<TileEvent> springsAt(float t) const;
    // How long a spring rings before it is below a millimetre in a metre of depth (its envelope at 1e-3).
    [[nodiscard]] float springSeconds() const;
};

} // namespace avgen::scene
