#pragma once

// ADR-1540: the fisheye lens -- a remap of the rendered rectilinear frame to a fisheye projection.
//
// The scene is rendered rectilinear, as always, with the camera's own field of view; the post chain's lens pass
// then gives each OUTPUT pixel the angle theta it stands for under a fisheye model and samples the rectilinear
// frame at tan(theta). The model's focal length is fitted so the output's half-DIAGONAL lands on the source's
// half-diagonal angle: every output pixel samples inside the source (no black, no clamped smear), the centre is
// magnified and the edges are compressed, and straight lines bow round the centre. `amount` blends the sample
// position from the rectilinear frame (0) to the fisheye (1), so the lens can be eased in.
//
// This is the reference the shader (`shaders/post.wgsl`, `fisheyeUv`) transliterates; the unit tests hold it.
// Rainbow World's lens study (docs/prototypes/rainbow-world/05-gate1-experiments.md, E1) measured the remap
// against a true fisheye: identical to 150 degrees on the diagonal from a 140-degree rectilinear frame.

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

enum class FisheyeModel : int {
    Stereographic = 0, // r = 2 f tan(theta / 2): keeps small shapes round, the gentlest edge squeeze
    Equisolid = 1,     // r = 2 f sin(theta / 2): the usual photographic fisheye
    Equidistant = 2,   // r = f theta
};

[[nodiscard]] inline FisheyeModel fisheyeModelFrom(float value) {
    const int m = std::clamp(static_cast<int>(std::lround(value)), 0, 2);
    return static_cast<FisheyeModel>(m);
}

// The source uv (0..1, either orientation; the mapping is symmetric) that output uv `uv` samples, for a
// rectilinear source whose half-angle tangents are `tanHalfX` and `tanHalfY`. amount 0 returns `uv`.
[[nodiscard]] inline glm::vec2 fisheyeSourceUv(glm::vec2 uv, float tanHalfX, float tanHalfY, FisheyeModel model,
                                              float amount) {
    if (!(amount > 0.0f) || !(tanHalfX > 0.0f) || !(tanHalfY > 0.0f)) {
        return uv;
    }
    const float aspect = tanHalfX / tanHalfY;
    // Output offset in units of half the frame height (isotropic).
    const glm::vec2 d{(uv.x - 0.5f) * 2.0f * aspect, (uv.y - 0.5f) * 2.0f};
    const float rho = glm::length(d);
    const float rhoDiag = std::sqrt(aspect * aspect + 1.0f);
    const float thetaDiag = std::atan(std::sqrt(tanHalfX * tanHalfX + tanHalfY * tanHalfY));
    glm::vec2 fish = uv;
    if (rho > 1e-7f) {
        float theta = 0.0f;
        switch (model) {
        case FisheyeModel::Stereographic: {
            const float k = rhoDiag / std::tan(thetaDiag * 0.5f);
            theta = 2.0f * std::atan(rho / k);
            break;
        }
        case FisheyeModel::Equisolid: {
            const float k = rhoDiag / std::sin(thetaDiag * 0.5f);
            theta = 2.0f * std::asin(std::clamp(rho / k, 0.0f, 1.0f));
            break;
        }
        case FisheyeModel::Equidistant:
            theta = rho / rhoDiag * thetaDiag;
            break;
        }
        const glm::vec2 t = d / rho * std::tan(std::min(theta, thetaDiag)); // a point on the rectilinear plane
        fish = glm::vec2(0.5f) + 0.5f * glm::vec2(t.x / tanHalfX, t.y / tanHalfY);
    }
    return glm::mix(uv, fish, std::clamp(amount, 0.0f, 1.0f));
}

} // namespace avgen::scene
