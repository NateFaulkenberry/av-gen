#pragma once

// Mouse control of the viewport camera (ADR-068).
//
// Until now every mouse event went to ImGui and nothing else, so the only way to move the camera
// was to type numbers into the inspector. That is a fine way to set a shot and an impossible way to
// find one.
//
// This is deliberately *only maths*. It takes a camera's eye and target and a mouse gesture, and
// returns a new eye and target. It does not touch parameters, the engine, SDL or ImGui, which is
// what lets orbiting be tested without a window and what keeps "where the camera is" in exactly one
// place: the `camera/position` and `camera/target` parameters, which already save, load, automate
// and route. A viewport camera that kept its own eye and target beside those would be a second
// source of truth for the same two vectors, and the two would disagree the first time anything else
// moved the camera.
//
// One consequence worth stating: `camera/position` and `camera/target` are read only in free mode
// (`camera/mode` 1). Driving them in orbit mode moves nothing and looks exactly like a dead input,
// so a caller that wants these gestures to have an effect must put the camera in free mode first.

#include <glm/glm.hpp>

namespace avgen::app {

// Where the camera is and what it is looking at. The pair the parameters already hold.
struct CameraPose {
    glm::vec3 eye{0.0f, 2.0f, 6.0f};
    glm::vec3 target{0.0f};
};

// What the mouse is asking for. Named by intent rather than by button so the binding lives in one
// place (the application) and the maths does not care which button the user prefers.
enum class ViewportGesture : std::uint8_t {
    None,
    Orbit,   // swing around the target, which stays put
    Pan,     // slide both eye and target across the view plane
    Look,    // swing the target around the eye, which stays put
};

struct ViewportControlSettings {
    float orbitRadiansPerPixel = 0.006f;
    float lookRadiansPerPixel = 0.004f;
    // Pan is in world units per pixel *per metre of distance*, so dragging moves the same amount of
    // picture whether the camera is two metres from its target or two hundred. A fixed world-space
    // rate feels weightless up close and immovable far away.
    float panPerPixelPerMetre = 0.0016f;
    float dollyPerNotch = 0.12f;        // fraction of the current distance per wheel notch
    float minDistance = 0.05f;          // never let the eye reach the target
    float maxDistance = 100000.0f;
    // How close the view direction may come to straight up or straight down, in radians. Reaching
    // the pole makes the azimuth undefined and the camera spins on the spot; every orbit camera
    // that has ever done that was missing this clamp.
    float poleMargin = 0.02f;
};

// Applies a drag of `deltaPixels` to `pose`. `up` is the world up axis.
[[nodiscard]] CameraPose applyDrag(const CameraPose& pose, ViewportGesture gesture,
                                   glm::vec2 deltaPixels, const ViewportControlSettings& settings,
                                   glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f));

// Applies `notches` of wheel: positive moves the eye toward the target. The target does not move,
// so dollying is a change of framing rather than of subject.
[[nodiscard]] CameraPose applyDolly(const CameraPose& pose, float notches,
                                    const ViewportControlSettings& settings);

// Frames `center` with a `radius`-metre bounding sphere, keeping the current view direction. Used
// by "focus on selection": the direction is the user's, the distance is arithmetic.
[[nodiscard]] CameraPose frameSphere(const CameraPose& pose, glm::vec3 center, float radius,
                                     float verticalFovRadians);

} // namespace avgen::app
