#pragma once

// Keeping a body on the ground (ADR-093, §4).
//
// The naive version is one line -- `y = groundHeight(x, z)` -- and it is what was here. It is also
// wrong in three visible ways, and each of them is a note in §4:
//
//   * **Jitter.** The ground is a noise function. A body snapped to it at four metres a second
//     samples a new height sixty times a second and every high-frequency wrinkle in the terrain
//     becomes a vertical twitch the animation did not ask for. Terrain detail that reads as texture
//     when it is under a mesh reads as vibration when it is under a character.
//   * **No orientation.** A figure walking up a bank stays vertical, which makes the bank look flat
//     and the figure look pasted on.
//   * **Penetration and floating.** Smoothing alone fixes the jitter and introduces both: a body
//     lags the surface, so it sinks going uphill and hangs going down.
//
// So: smooth, but inside a hard band that the body may never leave. The band is what makes this
// honest -- a smoother on its own is a promise that the error is usually small, and a clamped
// smoother is a guarantee that it is never larger than a stated number.
//
// This is a component rather than a behaviour because §7 asks for one. `explore` uses it, the
// `ground` behaviour exposes it on its own for a character whose motion comes from a keyframe or a
// spline, and a future vehicle can use it without inheriting a walk cycle.

#include "entity/navigation.hpp"

#include <glm/glm.hpp>

namespace avgen::entity {

struct GroundSettings {
    // The body's footprint, in metres. The ground is read over a disc this wide and averaged, which
    // is where nearly all of the smoothing comes from -- and it is the *right* place for it. A
    // temporal filter removes jitter by lagging the surface, which is a lag you can see and which
    // has to be clamped back before it becomes floating; a spatial filter removes exactly the
    // wrinkles that are smaller than the body standing on them, costs no lag at all, and is a pure
    // function of position, so it survives a timeline seek without any history to rebuild.
    //
    // Getting this wrong the other way is instructive: with the filter purely temporal, the body
    // spent the whole walk pinned against its own clamp and its vertical acceleration came out
    // slightly *worse* than snapping straight onto the surface.
    float footprint = 0.55f;
    // How fast the body converges on what is left. Short, because the spatial filter has already
    // done the work: this is for the residual, not for the terrain.
    float heightSmoothingMs = 45.0f;
    // How far the body may ever be from the surface, in metres, in each direction. The smoother
    // works inside this; the clamp is what turns "usually close" into "never further than".
    float maxFloat = 0.28f;
    float maxSink = 0.0f; // a body is never below the ground it stands on
    // 0 stays vertical, 1 lies along the surface normal. Something short of 1 is almost always
    // right: a walker leans into a slope, it does not become part of it.
    float slopeAlign = 0.55f;
    float slopeSmoothingMs = 240.0f;
    float maxTilt = 34.0f; // degrees, either axis
    // How far ahead of itself the body reads the ground, as a multiple of its speed in seconds. A
    // walker anticipates a rise rather than discovering it, which is the difference between
    // striding up a bank and climbing into one.
    float lookaheadSeconds = 0.18f;
};

struct GroundResult {
    float height = 0.0f; // the world y the body should adopt
    float pitch = 0.0f;  // degrees about the body's right axis, nose-up positive
    float roll = 0.0f;   // degrees about the body's forward axis
    bool grounded = true;
};

// One body's vertical state. Copyable and cheap; reset it when the character is teleported or the
// timeline is seeked, or it will glide to its new home from its old one.
class GroundFollower {
public:
    void reset() { primed_ = false; height_ = 0.0f; pitch_ = 0.0f; roll_ = 0.0f; }

    // `p` is the body's XZ, `yaw` its facing in radians about +Y, `speed` its horizontal m/s.
    // Samples the surface once, or twice when looking ahead.
    [[nodiscard]] GroundResult update(const Navigator& nav, glm::vec2 p, float yaw, float speed,
                                      double dt, const GroundSettings& settings);

private:
    bool primed_ = false;
    float height_ = 0.0f;
    float pitch_ = 0.0f;
    float roll_ = 0.0f;
};

} // namespace avgen::entity
