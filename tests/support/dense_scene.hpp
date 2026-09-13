#pragma once

// A scene with more simultaneously-visible entities than the renderer's old object cap (ADR-128).
//
// This is a fixture, not a throwaway: it exists so that "more than 256 entities on screen at once"
// is a thing the suite can ask for by name, and so the next person who changes object addressing
// has a scene that fails when they get it wrong. Three properties are load-bearing and the reason
// the numbers below are what they are:
//
//   1. **Every entity is visible at once.** Frustum culling is what has been hiding the old cap --
//      Glowmere flattens to 278 entities and only survives because a typical camera sees about half
//      of them. A scene whose camera sees all of them is the only kind that tests the cap at all,
//      so the camera is placed to frame the whole grid and nothing is culled.
//   2. **There is no ground plane.** A floor would be legitimate content and would also cover most
//      of the frame, which is exactly what would hide an identifier-target bug: the check that the
//      sky is claimed by nobody is only worth running when there is a lot of sky. The boxes are
//      spaced so background shows between them as well as above them.
//   3. **Entity 0 is drawn and is not at the edge.** `packPickId(PickSpace::Entity, 0)` is literally
//      zero, so entity zero's pick id and "nothing was drawn here" are the same bits in the low half
//      of the identifier texel. What separates them is the material id in the high 16 bits being
//      one-based. A scene that does not draw entity zero cannot notice if that stops being true.

#include "scene/scene.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace avgen::testing {

// An axis-aligned cube of half-extent `h` centred on its own origin, with per-face normals.
inline scene::MeshData denseSceneBox(float h) {
    scene::MeshData mesh;
    mesh.name = "dense-box";
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u =
            std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(glm::vec3(0, 1, 0), normal));
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        mesh.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        mesh.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        mesh.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

// `side * side` boxes on a grid in the XZ plane, every one of them in frame, lit by one directional
// key. `side = 19` gives 361 entities, which is above the old 256-slot cap and above Glowmere's own
// 278 -- and far below the 16,383 distinct indices the pick-id encoding can name, so the identifier
// target is being asked a question it can actually answer.
//
// The grid pitch (2 units) against the box extent (0.8 units across, 0.8 tall) is chosen so that
// nothing occludes anything at the camera's elevation: a 0.4-high box at a 25-degree view angle
// hides 0.86 units behind it, which is less than the pitch. Every entity therefore owns pixels of
// its own, and "did every entity draw?" is a question the identifier target can be asked directly.
inline scene::Scene denseEntityGrid(int side = 19) {
    scene::Scene s;
    s.environment.showSkybox = false;
    s.environment.backgroundColor = glm::vec3(0.02f, 0.03f, 0.05f);

    const float pitch = 2.0f;
    const float half = 0.5f * pitch * static_cast<float>(side - 1);
    const auto mesh = s.addMesh(denseSceneBox(0.4f));

    // Framing is part of the fixture's contract, not decoration: every box must be inside the
    // frame *and* big enough to own a pixel, or "did every entity draw?" stops being answerable.
    // A 50-degree explicit fov, the camera pulled back to 1.7x the grid's half-width and raised to
    // 1.1x it, puts the near row roughly 14 degrees below the aim and the far row 8 above -- both
    // comfortably inside the 25-degree half-angle -- and leaves the far row's boxes about five
    // pixels across at 640x480. The horizon lands inside the top of the frame, which is what gives
    // the sky check something to be about.
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.87f;
    s.camera.position = {0.0f, half * 1.1f + 6.0f, half * 1.7f + 15.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 400.0f;

    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            const int index = row * side + column;
            auto& e = s.addEntity("box-" + std::to_string(index), mesh);
            e.transform.position = {static_cast<float>(column) * pitch - half, 0.4f,
                                    static_cast<float>(row) * pitch - half};
            // A per-entity colour so a frame capture shows *which* entity is where rather than a
            // uniform grey field in which a missing box is invisible.
            const float t = static_cast<float>(index) / static_cast<float>(side * side);
            e.material.baseColor = glm::vec3(0.25f + 0.7f * t, 0.55f, 0.95f - 0.7f * t);
            e.material.roughness = 0.6f;
        }
    }

    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

} // namespace avgen::testing
