#include "ui/gizmo.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::ui {
namespace {

constexpr float kPlaneInner = 0.28f; // where a plane handle's quad starts, as a fraction of the arm
constexpr float kPlaneOuter = 0.58f;

// The point on the infinite line (origin, direction) closest to the infinite line (rayO, rayD).
// Returns the parameter along the first line. `ok` is false when the two are parallel, which for a
// gizmo means the axis points exactly at the camera and there is no honest answer.
float closestOnLine(glm::vec3 origin, glm::vec3 direction, glm::vec3 rayO, glm::vec3 rayD, bool& ok) {
    const glm::vec3 w = origin - rayO;
    const float a = glm::dot(direction, direction);
    const float b = glm::dot(direction, rayD);
    const float c = glm::dot(rayD, rayD);
    const float d = glm::dot(direction, w);
    const float e = glm::dot(rayD, w);
    const float denominator = a * c - b * b;
    if (std::abs(denominator) < 1e-7f) {
        ok = false;
        return 0.0f;
    }
    ok = true;
    return (b * e - c * d) / denominator;
}

// Where a ray meets a plane. False when it runs parallel to it.
bool rayPlane(glm::vec3 rayO, glm::vec3 rayD, glm::vec3 planeP, glm::vec3 planeN, glm::vec3& out) {
    const float denominator = glm::dot(rayD, planeN);
    if (std::abs(denominator) < 1e-6f) {
        return false;
    }
    const float t = glm::dot(planeP - rayO, planeN) / denominator;
    if (t < 0.0f) {
        return false; // behind the eye
    }
    out = rayO + rayD * t;
    return true;
}

// Distance from `p` to the segment `a`..`b`, all in NDC. NDC is anisotropic -- x spans the width and
// y the height -- but so is the tolerance the caller passes, because both come from the same
// rectangle. Comparing in NDC is therefore consistent even though it is not isotropic.
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float length2 = glm::dot(ab, ab);
    if (length2 < 1e-12f) {
        return glm::length(p - a);
    }
    const float t = std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

float snapTo(float value, float step) {
    return step > 0.0f ? std::round(value / step) * step : value;
}

} // namespace

const char* gizmoModeName(GizmoMode mode) {
    switch (mode) {
    case GizmoMode::Move:
        return "move";
    case GizmoMode::Rotate:
        return "rotate";
    case GizmoMode::Scale:
        return "scale";
    }
    return "move";
}

const char* gizmoHandleName(GizmoHandle handle) {
    switch (handle) {
    case GizmoHandle::None:
        return "none";
    case GizmoHandle::AxisX:
        return "X";
    case GizmoHandle::AxisY:
        return "Y";
    case GizmoHandle::AxisZ:
        return "Z";
    case GizmoHandle::PlaneYZ:
        return "YZ";
    case GizmoHandle::PlaneXZ:
        return "XZ";
    case GizmoHandle::PlaneXY:
        return "XY";
    case GizmoHandle::Screen:
        return "screen";
    }
    return "none";
}

bool handleIsAxis(GizmoHandle handle) {
    return handle == GizmoHandle::AxisX || handle == GizmoHandle::AxisY || handle == GizmoHandle::AxisZ;
}

bool handleIsPlane(GizmoHandle handle) {
    return handle == GizmoHandle::PlaneYZ || handle == GizmoHandle::PlaneXZ ||
           handle == GizmoHandle::PlaneXY;
}

glm::vec3 handleAxis(GizmoHandle handle) {
    switch (handle) {
    case GizmoHandle::AxisX:
    case GizmoHandle::PlaneYZ:
        return glm::vec3(1.0f, 0.0f, 0.0f);
    case GizmoHandle::AxisY:
    case GizmoHandle::PlaneXZ:
        return glm::vec3(0.0f, 1.0f, 0.0f);
    case GizmoHandle::AxisZ:
    case GizmoHandle::PlaneXY:
        return glm::vec3(0.0f, 0.0f, 1.0f);
    default:
        return glm::vec3(0.0f);
    }
}

glm::vec3 GizmoFrame::axis(GizmoHandle handle) const {
    const glm::vec3 local = handleAxis(handle);
    if (glm::dot(local, local) < 1e-9f) {
        return glm::vec3(0.0f);
    }
    return glm::normalize(basis * local);
}

float gizmoWorldScale(const scene::Camera& camera, glm::vec3 origin, float screenFraction) {
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const float depth = std::max(glm::dot(origin - camera.position, forward), camera.nearPlane);
    // The half-height of the frame at that depth, times the fraction the gizmo should take of it.
    return depth * std::tan(camera.effectiveFovY() * 0.5f) * std::max(screenFraction, 0.01f);
}

std::pair<glm::vec3, glm::vec3> axisSegment(const GizmoFrame& frame, GizmoHandle handle) {
    const glm::vec3 direction = frame.axis(handle);
    return {frame.origin, frame.origin + direction * frame.scale};
}

std::array<glm::vec3, 4> planeQuad(const GizmoFrame& frame, GizmoHandle handle) {
    // The two axes the plane is spanned by: whichever two are not its normal.
    const glm::vec3 normal = handleAxis(handle);
    glm::vec3 u(0.0f);
    glm::vec3 v(0.0f);
    if (normal.x > 0.5f) {
        u = glm::vec3(0.0f, 1.0f, 0.0f);
        v = glm::vec3(0.0f, 0.0f, 1.0f);
    } else if (normal.y > 0.5f) {
        u = glm::vec3(1.0f, 0.0f, 0.0f);
        v = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        u = glm::vec3(1.0f, 0.0f, 0.0f);
        v = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    const glm::vec3 a = frame.basis * u * frame.scale;
    const glm::vec3 b = frame.basis * v * frame.scale;
    return {frame.origin + a * kPlaneInner + b * kPlaneInner, frame.origin + a * kPlaneOuter + b * kPlaneInner,
            frame.origin + a * kPlaneOuter + b * kPlaneOuter, frame.origin + a * kPlaneInner + b * kPlaneOuter};
}

std::vector<glm::vec3> rotationRing(const GizmoFrame& frame, GizmoHandle handle, std::size_t count) {
    std::vector<glm::vec3> out;
    const glm::vec3 normal = handleAxis(handle);
    if (glm::dot(normal, normal) < 1e-9f || count < 3) {
        return out;
    }
    glm::vec3 u(0.0f);
    glm::vec3 v(0.0f);
    if (normal.x > 0.5f) {
        u = glm::vec3(0.0f, 1.0f, 0.0f);
        v = glm::vec3(0.0f, 0.0f, 1.0f);
    } else if (normal.y > 0.5f) {
        u = glm::vec3(0.0f, 0.0f, 1.0f);
        v = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        u = glm::vec3(1.0f, 0.0f, 0.0f);
        v = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    const glm::vec3 a = frame.basis * u * frame.scale;
    const glm::vec3 b = frame.basis * v * frame.scale;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        out.push_back(frame.origin + a * std::cos(angle) + b * std::sin(angle));
    }
    return out;
}

GizmoHandle pickHandle(const scene::Camera& camera, float aspect, const GizmoFrame& frame, GizmoMode mode,
                       glm::vec2 ndc, float pickRadius) {
    const auto project = [&](glm::vec3 p) { return projectPoint(camera, aspect, p); };
    const Projected origin = project(frame.origin);
    if (!origin.inFront) {
        return GizmoHandle::None;
    }

    // Planes before axes: their quads sit between the arms and a plane handle you cannot hit
    // because the axis next to it wins every time is a handle that does not exist.
    if (mode != GizmoMode::Rotate) {
        for (const GizmoHandle handle :
             {GizmoHandle::PlaneXZ, GizmoHandle::PlaneXY, GizmoHandle::PlaneYZ}) {
            const std::array<glm::vec3, 4> quad = planeQuad(frame, handle);
            glm::vec2 lo(0.0f);
            glm::vec2 hi(0.0f);
            bool any = false;
            for (const glm::vec3& corner : quad) {
                const Projected p = project(corner);
                if (!p.inFront) {
                    any = false;
                    break;
                }
                lo = any ? glm::min(lo, p.ndc) : p.ndc;
                hi = any ? glm::max(hi, p.ndc) : p.ndc;
                any = true;
            }
            if (any && ndc.x >= lo.x && ndc.x <= hi.x && ndc.y >= lo.y && ndc.y <= hi.y) {
                return handle;
            }
        }
    }

    GizmoHandle best = GizmoHandle::None;
    float bestDistance = pickRadius;
    if (mode == GizmoMode::Rotate) {
        for (const GizmoHandle handle : {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ}) {
            const std::vector<glm::vec3> ring = rotationRing(frame, handle, 48);
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Projected a = project(ring[i]);
                const Projected b = project(ring[(i + 1) % ring.size()]);
                if (!a.inFront || !b.inFront) {
                    continue;
                }
                const float d = distanceToSegment(ndc, a.ndc, b.ndc);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = handle;
                }
            }
        }
        return best;
    }

    for (const GizmoHandle handle : {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ}) {
        const auto [a, b] = axisSegment(frame, handle);
        const Projected pa = project(a);
        const Projected pb = project(b);
        if (!pa.inFront || !pb.inFront) {
            continue;
        }
        const float d = distanceToSegment(ndc, pa.ndc, pb.ndc);
        if (d < bestDistance) {
            bestDistance = d;
            best = handle;
        }
    }
    // The centre: a free move in the screen plane, or a uniform scale. Tested last so it cannot
    // steal the base of an axis arm, which is where half of all axis drags start.
    if (best == GizmoHandle::None && glm::length(ndc - origin.ndc) < pickRadius) {
        best = GizmoHandle::Screen;
    }
    return best;
}

namespace {

// The world point a drag is measured from, for one handle. This is the whole of the "drag along an
// axis" problem: for an axis it is the closest point on the axis line to the cursor ray; for a
// plane or the screen handle it is the ray's intersection with that plane.
bool anchorFor(const GizmoFrame& frame, GizmoHandle handle, const scene::Camera& camera,
               const ViewRay& ray, glm::vec3& out) {
    if (handleIsAxis(handle)) {
        const glm::vec3 axis = frame.axis(handle);
        bool ok = false;
        const float t = closestOnLine(frame.origin, axis, ray.origin, ray.direction, ok);
        if (!ok) {
            return false;
        }
        out = frame.origin + axis * t;
        return true;
    }
    glm::vec3 normal;
    if (handleIsPlane(handle)) {
        normal = frame.axis(handle);
    } else {
        // The screen handle slides in the plane facing the camera.
        normal = glm::normalize(camera.position - frame.origin);
    }
    return rayPlane(ray.origin, ray.direction, frame.origin, normal, out);
}

// The angle of a point about a rotation handle's axis, measured in the plane of the ring.
bool angleOn(const GizmoFrame& frame, GizmoHandle handle, const scene::Camera& camera, const ViewRay& ray,
             float& out) {
    glm::vec3 normal = handleIsAxis(handle) ? frame.axis(handle) : glm::normalize(camera.position - frame.origin);
    glm::vec3 hit;
    if (!rayPlane(ray.origin, ray.direction, frame.origin, normal, hit)) {
        return false;
    }
    const glm::vec3 radial = hit - frame.origin;
    if (glm::dot(radial, radial) < 1e-9f) {
        return false;
    }
    // A stable pair of axes in the plane, so the angle is measured against the same reference at
    // the start of the drag and at every update.
    glm::vec3 reference = std::abs(normal.y) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 u = glm::normalize(glm::cross(reference, normal));
    const glm::vec3 v = glm::cross(normal, u);
    out = std::atan2(glm::dot(radial, v), glm::dot(radial, u));
    return true;
}

} // namespace

bool beginGizmoDrag(GizmoDrag& drag, const scene::Camera& camera, float aspect, const GizmoFrame& frame,
                    GizmoMode mode, GizmoHandle handle, glm::vec2 ndc) {
    drag = GizmoDrag{};
    if (handle == GizmoHandle::None) {
        return false;
    }
    const ViewRay ray = rayThroughNdc(camera, aspect, ndc);
    drag.mode = mode;
    drag.handle = handle;
    drag.frame = frame;
    if (mode == GizmoMode::Rotate) {
        if (!angleOn(frame, handle, camera, ray, drag.anchorAngle)) {
            return false;
        }
        drag.active = true;
        return true;
    }
    if (!anchorFor(frame, handle, camera, ray, drag.anchor)) {
        return false;
    }
    if (mode == GizmoMode::Scale) {
        drag.anchorDistance = glm::length(drag.anchor - frame.origin);
        if (drag.anchorDistance < 1e-4f) {
            // Pressed exactly on the pivot: any ratio from here is a division by nothing. Standing
            // the anchor off by a fraction of the gizmo keeps the drag usable rather than refusing.
            drag.anchorDistance = std::max(frame.scale * 0.1f, 1e-3f);
        }
    }
    drag.active = true;
    return true;
}

GizmoDelta updateGizmoDrag(const GizmoDrag& drag, const scene::Camera& camera, float aspect, glm::vec2 ndc,
                           const GizmoSnap& snap) {
    GizmoDelta out;
    if (!drag.active) {
        return out;
    }
    const ViewRay ray = rayThroughNdc(camera, aspect, ndc);

    if (drag.mode == GizmoMode::Rotate) {
        float angle = 0.0f;
        if (!angleOn(drag.frame, drag.handle, camera, ray, angle)) {
            return out;
        }
        float degrees = glm::degrees(angle - drag.anchorAngle);
        // Wrapped into -180..180 so a drag past the back of the ring reads as "a bit the other way"
        // rather than as 350 degrees.
        while (degrees > 180.0f) {
            degrees -= 360.0f;
        }
        while (degrees < -180.0f) {
            degrees += 360.0f;
        }
        degrees = snapTo(degrees, snap.rotate);
        const glm::vec3 axis = handleIsAxis(drag.handle) ? drag.frame.axis(drag.handle)
                                                         : glm::normalize(camera.position - drag.frame.origin);
        out.rotation = glm::angleAxis(glm::radians(degrees), axis);
        out.readout = fmt::format("{:+.1f} deg about {}", degrees, gizmoHandleName(drag.handle));
        out.valid = true;
        return out;
    }

    glm::vec3 now;
    if (!anchorFor(drag.frame, drag.handle, camera, ray, now)) {
        return out;
    }

    if (drag.mode == GizmoMode::Scale) {
        const float distance = glm::length(now - drag.frame.origin);
        float ratio = distance / std::max(drag.anchorDistance, 1e-4f);
        ratio = std::max(snapTo(ratio, snap.scale), 0.01f);
        if (handleIsAxis(drag.handle)) {
            const glm::vec3 mask = handleAxis(drag.handle);
            out.scale = glm::vec3(1.0f) + mask * (ratio - 1.0f);
            out.readout = fmt::format("x{:.3f} on {}", ratio, gizmoHandleName(drag.handle));
        } else if (handleIsPlane(drag.handle)) {
            const glm::vec3 mask = glm::vec3(1.0f) - handleAxis(drag.handle);
            out.scale = glm::vec3(1.0f) + mask * (ratio - 1.0f);
            out.readout = fmt::format("x{:.3f} in {}", ratio, gizmoHandleName(drag.handle));
        } else {
            out.scale = glm::vec3(ratio);
            out.readout = fmt::format("x{:.3f}", ratio);
        }
        out.valid = true;
        return out;
    }

    glm::vec3 delta = now - drag.anchor;
    if (snap.move > 0.0f) {
        // Snapped along the gizmo's own axes, then put back into world space: snapping a world
        // vector component-wise would put a local-space drag on a diagonal.
        const glm::quat inverse = glm::conjugate(drag.frame.basis);
        glm::vec3 local = inverse * delta;
        local = glm::vec3(snapTo(local.x, snap.move), snapTo(local.y, snap.move), snapTo(local.z, snap.move));
        delta = drag.frame.basis * local;
    }
    out.translation = delta;
    if (handleIsAxis(drag.handle)) {
        const float along = glm::dot(delta, drag.frame.axis(drag.handle));
        out.readout = fmt::format("{:+.2f} m along {}", along, gizmoHandleName(drag.handle));
    } else {
        out.readout = fmt::format("{:+.2f}, {:+.2f}, {:+.2f} m", delta.x, delta.y, delta.z);
    }
    out.valid = true;
    return out;
}

} // namespace avgen::ui
