#pragma once

// The transform gizmo (ADR-092, world-authoring-spec §24): move, rotate and scale, on axis and
// plane handles, in world or local space, with snapping.
//
// All of this file is geometry. Where a handle is on screen, which one the pointer is over, and
// what a drag from here to there means in metres or degrees are questions with exact answers that
// do not need a window to ask -- so they are asked here and checked in tests/unit/test_gizmo.cpp,
// and the ImGui half in viewport_overlay.cpp only draws the answers and reports the mouse.
//
// The one piece of this that is easy to get subtly wrong, and that is therefore written out rather
// than left to a library: **a drag along an axis is not the mouse delta projected onto the axis.**
// It is the point on the axis line closest to the mouse *ray*, which is a different number as soon
// as the axis is not parallel to the screen, and gets more different the more the axis points at
// the camera. Done the easy way, dragging the green handle on a top-down view moves the object a
// few centimetres for a full sweep of the screen and everybody blames the mouse.

#include "scene/camera.hpp"
#include "ui/world_probe.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace avgen::ui {

enum class GizmoMode : std::uint8_t { Move, Rotate, Scale };
[[nodiscard]] const char* gizmoModeName(GizmoMode mode);

// Which piece of the gizmo. The plane handles are named by the axis they are *normal* to, which is
// how every tool names them: "the XZ plane" is the one you use to slide something along the ground.
enum class GizmoHandle : std::uint8_t { None, AxisX, AxisY, AxisZ, PlaneYZ, PlaneXZ, PlaneXY, Screen };
[[nodiscard]] const char* gizmoHandleName(GizmoHandle handle);
[[nodiscard]] bool handleIsAxis(GizmoHandle handle);
[[nodiscard]] bool handleIsPlane(GizmoHandle handle);
// The axis a handle moves along, or the normal of the plane it slides in. Zero for None/Screen.
[[nodiscard]] glm::vec3 handleAxis(GizmoHandle handle);

// Where the gizmo is and what frame it works in.
struct GizmoFrame {
    glm::vec3 origin{0.0f};
    // World space is the identity; local space is the active object's rotation. Both are offered
    // because both are right: "move it east" is a world question and "move it along its own length"
    // is a local one, and an editor that only has one of them makes the other into arithmetic.
    glm::quat basis{1.0f, 0.0f, 0.0f, 0.0f};
    // How long the axis arms are in metres, chosen so the gizmo is the same size on screen at any
    // distance. A gizmo that shrinks with perspective becomes unusable on a distant object and
    // swallows the screen on a near one.
    float scale = 1.0f;

    [[nodiscard]] glm::vec3 axis(GizmoHandle handle) const;
};

// The metres-per-screen-height factor that keeps the gizmo a constant size. `screenFraction` is how
// much of the frame's half-height the arms should span.
[[nodiscard]] float gizmoWorldScale(const scene::Camera& camera, glm::vec3 origin,
                                    float screenFraction = 0.16f);

// What the pointer is over. `ndc` is the cursor; `pickRadius` is the grab distance in NDC units,
// which is how a tolerance in pixels arrives here without this file knowing about pixels.
[[nodiscard]] GizmoHandle pickHandle(const scene::Camera& camera, float aspect, const GizmoFrame& frame,
                                     GizmoMode mode, glm::vec2 ndc, float pickRadius);

// Snapping. Zero means off, which is the default: a snap nobody asked for is an edit that silently
// disagrees with the number in the field.
struct GizmoSnap {
    float move = 0.0f;    // metres
    float rotate = 0.0f;  // degrees
    float scale = 0.0f;   // fraction
    [[nodiscard]] bool any() const { return move > 0.0f || rotate > 0.0f || scale > 0.0f; }
};

// A drag in progress. Started on button-down over a handle, updated per frame, ended on release.
//
// The anchor is captured once, at the start: every update is measured from the *press*, not from
// the previous frame. Accumulating per-frame deltas drifts, and drift in a transform tool shows up
// as an object that does not come back when you drag the mouse back to where you pressed.
struct GizmoDrag {
    bool active = false;
    GizmoMode mode = GizmoMode::Move;
    GizmoHandle handle = GizmoHandle::None;
    GizmoFrame frame;              // as it was when the drag began
    glm::vec3 anchor{0.0f};        // the world point under the cursor at the press
    float anchorAngle = 0.0f;      // radians, for Rotate
    float anchorDistance = 1.0f;   // metres from the origin, for Scale
};

// What a drag has come to so far, relative to where it started. Exactly one of these is meaningful
// per mode; the others are identity.
struct GizmoDelta {
    bool valid = false;
    glm::vec3 translation{0.0f};   // world space
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};         // multiplier per axis, in the gizmo's basis
    // What to show next to the cursor while dragging: "+4.00 m along X", "-30 degrees about Y".
    std::string readout;
};

// Opens a drag. Returns false when the ray cannot be resolved against the handle -- an axis seen
// exactly end-on, which is the one configuration where there is no answer rather than a poor one.
[[nodiscard]] bool beginGizmoDrag(GizmoDrag& drag, const scene::Camera& camera, float aspect,
                                  const GizmoFrame& frame, GizmoMode mode, GizmoHandle handle,
                                  glm::vec2 ndc);

// Where the drag has got to. Pure: the same cursor gives the same delta however many times it is
// asked, which is what lets the caller re-apply from the original transforms every frame instead of
// accumulating.
[[nodiscard]] GizmoDelta updateGizmoDrag(const GizmoDrag& drag, const scene::Camera& camera, float aspect,
                                         glm::vec2 ndc, const GizmoSnap& snap);

// ---- the pieces the overlay draws ----------------------------------------------------------------

// The world-space endpoints of an axis arm, and of the small square of a plane handle.
[[nodiscard]] std::pair<glm::vec3, glm::vec3> axisSegment(const GizmoFrame& frame, GizmoHandle handle);
// Four corners of a plane handle's quad, in order.
[[nodiscard]] std::array<glm::vec3, 4> planeQuad(const GizmoFrame& frame, GizmoHandle handle);
// `count` points around the circle of a rotation handle.
[[nodiscard]] std::vector<glm::vec3> rotationRing(const GizmoFrame& frame, GizmoHandle handle,
                                                   std::size_t count = 48);

} // namespace avgen::ui
