#include "app/viewport_camera.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::app {
namespace {

// Rotates `v` about `axis` by `angle`. Rodrigues rather than a quaternion because there is one of
// them and it is two lines.
glm::vec3 rotateAbout(glm::vec3 v, glm::vec3 axis, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0f - c);
}

// The angle between `dir` and `up`, clamped away from both poles. Returns the elevation change that
// may actually be applied.
float clampPitch(glm::vec3 dir, glm::vec3 up, float requested, float margin) {
    const float current = std::acos(std::clamp(glm::dot(glm::normalize(dir), up), -1.0f, 1.0f));
    const float lo = margin;
    const float hi = 3.14159265f - margin;
    return std::clamp(current + requested, lo, hi) - current;
}

glm::vec3 safeNormalize(glm::vec3 v, glm::vec3 fallback) {
    const float length = glm::length(v);
    return length > 1e-6f ? v / length : fallback;
}

// The camera's right and the up it should pan along. Derived from the view direction rather than
// stored, so a pose that arrived from anywhere -- a saved project, an automated parameter, a
// cinematic shot -- is controllable without the controller having watched it get there.
void basis(glm::vec3 forward, glm::vec3 up, glm::vec3& right, glm::vec3& viewUp) {
    right = safeNormalize(glm::cross(forward, up), glm::vec3(1.0f, 0.0f, 0.0f));
    viewUp = safeNormalize(glm::cross(right, forward), up);
}
} // namespace

CameraPose applyDrag(const CameraPose& pose, ViewportGesture gesture, glm::vec2 deltaPixels,
                     const ViewportControlSettings& settings, glm::vec3 up) {
    CameraPose out = pose;
    if (gesture == ViewportGesture::None) {
        return out;
    }
    up = safeNormalize(up, glm::vec3(0.0f, 1.0f, 0.0f));

    switch (gesture) {
    case ViewportGesture::Orbit: {
        glm::vec3 offset = pose.eye - pose.target;
        const float distance = glm::length(offset);
        if (distance < 1e-5f) {
            return out;
        }
        // Yaw about world up, so a horizontal drag always feels horizontal however the camera is
        // tilted. Pitch about the camera's own right, clamped off the poles.
        offset = rotateAbout(offset, up, -deltaPixels.x * settings.orbitRadiansPerPixel);
        glm::vec3 right;
        glm::vec3 viewUp;
        basis(-offset, up, right, viewUp);
        const float pitch =
            clampPitch(offset, up, deltaPixels.y * settings.orbitRadiansPerPixel, settings.poleMargin);
        offset = rotateAbout(offset, right, pitch);
        out.eye = pose.target + offset;
        break;
    }
    case ViewportGesture::Look: {
        glm::vec3 forward = pose.target - pose.eye;
        const float distance = glm::length(forward);
        if (distance < 1e-5f) {
            return out;
        }
        forward = rotateAbout(forward, up, -deltaPixels.x * settings.lookRadiansPerPixel);
        glm::vec3 right;
        glm::vec3 viewUp;
        basis(forward, up, right, viewUp);
        // Negated against orbit: dragging down while looking swings the view down, whereas dragging
        // down while orbiting brings the camera *under* the subject. They are opposite gestures on
        // purpose and the sign is what makes each feel like the thing it is named after.
        const float pitch = clampPitch(-forward, up, -deltaPixels.y * settings.lookRadiansPerPixel,
                                       settings.poleMargin);
        forward = rotateAbout(forward, right, -pitch);
        // The target keeps its distance: looking around does not also dolly.
        out.target = pose.eye + safeNormalize(forward, glm::vec3(0.0f, 0.0f, -1.0f)) * distance;
        break;
    }
    case ViewportGesture::Pan: {
        const glm::vec3 forward = pose.target - pose.eye;
        const float distance = std::max(glm::length(forward), settings.minDistance);
        glm::vec3 right;
        glm::vec3 viewUp;
        basis(safeNormalize(forward, glm::vec3(0.0f, 0.0f, -1.0f)), up, right, viewUp);
        const float rate = settings.panPerPixelPerMetre * distance;
        const glm::vec3 move = right * (-deltaPixels.x * rate) + viewUp * (deltaPixels.y * rate);
        out.eye = pose.eye + move;
        out.target = pose.target + move;
        break;
    }
    case ViewportGesture::None:
        break;
    }
    return out;
}

CameraPose applyDolly(const CameraPose& pose, float notches, const ViewportControlSettings& settings) {
    CameraPose out = pose;
    const glm::vec3 offset = pose.eye - pose.target;
    const float distance = glm::length(offset);
    if (distance < 1e-6f) {
        return out;
    }
    // Geometric, not linear. A fixed step per notch crawls when you are far away and slams into the
    // subject when you are close; a fixed *fraction* takes the same number of notches to halve the
    // distance wherever you start, which is what makes a wheel feel like a zoom.
    const float scale = std::pow(1.0f - std::clamp(settings.dollyPerNotch, 0.0f, 0.95f), notches);
    const float wanted = std::clamp(distance * scale, settings.minDistance, settings.maxDistance);
    out.eye = pose.target + offset * (wanted / distance);
    return out;
}

CameraPose frameSphere(const CameraPose& pose, glm::vec3 center, float radius,
                       float verticalFovRadians) {
    CameraPose out;
    const glm::vec3 direction =
        safeNormalize(pose.eye - pose.target, glm::vec3(0.0f, 0.35f, 1.0f));
    const float fov = std::clamp(verticalFovRadians, 0.05f, 3.0f);
    // The distance at which a sphere of this radius exactly fills the vertical field, with a little
    // air so the subject is not jammed against the frame edge.
    const float distance = std::max(radius / std::tan(fov * 0.5f), 1e-3f) * 1.25f;
    out.target = center;
    out.eye = center + direction * distance;
    return out;
}

} // namespace avgen::app
