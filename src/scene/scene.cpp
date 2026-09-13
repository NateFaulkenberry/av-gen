#include "scene/scene.hpp"

#include "core/log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace avgen::scene {
namespace {

// glm has no `isfinite` for vectors in this build, and the component-wise question is the only one
// worth asking here.
[[nodiscard]] bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

Transform Transform::fromMatrix(const glm::mat4& m) {
    Transform t;
    glm::vec3 skew;
    glm::vec4 perspective;
    if (!glm::decompose(m, t.scale, t.rotation, t.position, skew, perspective)) {
        t = Transform{};
        t.position = glm::vec3(m[3]);
    }
    return t;
}

glm::mat4 Camera::view() const {
    const glm::vec3 forward = target - position;
    glm::vec3 viewUp = up;
    if (glm::dot(forward, forward) > 1e-12f && glm::dot(viewUp, viewUp) > 1e-12f) {
        const glm::vec3 direction = glm::normalize(forward);
        if (std::abs(glm::dot(direction, glm::normalize(viewUp))) > 0.999f) {
            viewUp = std::abs(direction.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                    : glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
    return glm::lookAtRH(position, target, viewUp);
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspectiveRH_ZO(effectiveFovY(), aspect, nearPlane, farPlane);
}

bool TextureData::valid() const {
    return width > 0 && height > 0 && data.size() == static_cast<std::size_t>(width) * height * bytesPerPixel();
}

bool MeshData::valid() const {
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(vertices.size());
    for (const std::uint32_t index : indices) {
        if (index >= count) {
            return false;
        }
    }
    return true;
}

std::pair<glm::vec3, glm::vec3> MeshData::bounds() const {
    if (vertices.empty()) {
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    std::size_t dropped = 0;
    for (const auto& v : vertices) {
        // Counted, because the fold cannot report it. `glm::min`/`glm::max` are `(y<x)?y:x`, and a
        // NaN loses every comparison -- so a non-finite vertex is silently *discarded* and the box
        // comes out finite and too small. A mesh with a corrupt vertex then looks like a mesh with
        // a modelling mistake, and the number that would have said otherwise never existed.
        if (!finite(v.position)) {
            ++dropped;
            continue;
        }
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    if (dropped > 0) {
        log::warn("mesh '{}': {} of {} vertices are not finite and are not in its bounds", name,
                  dropped, vertices.size());
    }
    if (!glm::all(glm::lessThanEqual(lo, hi))) {
        // Every vertex was refused. Zero rather than the inverted seed: a caller testing a box it
        // was handed should see an empty one, not `FLT_MAX .. lowest()` masquerading as finite.
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    return {lo, hi};
}

void MeshData::computeNormals() {
    for (auto& v : vertices) {
        v.normal = glm::vec3(0.0f);
    }
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::uint32_t a = indices[i];
        const std::uint32_t b = indices[i + 1];
        const std::uint32_t c = indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
            continue;
        }
        const glm::vec3 n = glm::cross(vertices[b].position - vertices[a].position,
                                       vertices[c].position - vertices[a].position); // area-weighted
        vertices[a].normal += n;
        vertices[b].normal += n;
        vertices[c].normal += n;
    }
    for (auto& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

MeshId Scene::addMesh(MeshData mesh) {
    const auto id = static_cast<MeshId>(meshes.size());
    meshes.push_back(std::move(mesh));
    ++meshVersion;
    return id;
}

TextureId Scene::addTexture(TextureData texture) {
    const auto id = static_cast<TextureId>(textures.size());
    textures.push_back(std::move(texture));
    ++textureVersion;
    return id;
}

Entity& Scene::addEntity(std::string name, MeshId mesh) {
    Entity entity;
    entity.name = std::move(name);
    entity.mesh = mesh;
    entities.push_back(std::move(entity));
    return entities.back();
}

PunctualLight& Scene::addLight(PunctualLight light) {
    lights.push_back(std::move(light));
    return lights.back();
}

const std::pair<glm::vec3, glm::vec3>& Scene::meshBounds(MeshId mesh) const {
    static const std::pair<glm::vec3, glm::vec3> empty{glm::vec3(0.0f), glm::vec3(0.0f)};
    if (mesh >= meshes.size()) {
        return empty;
    }
    if (meshBoundsVersion_ != meshVersion || meshBoundsCache_.size() != meshes.size()) {
        meshBoundsCache_.resize(meshes.size());
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            meshBoundsCache_[i] = meshes[i].bounds();
        }
        meshBoundsVersion_ = meshVersion;
    }
    return meshBoundsCache_[mesh];
}

std::pair<glm::vec3, glm::vec3> Scene::bounds() const {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const auto& e : entities) {
        if (!e.visible || e.style != MeshStyle::Lit || e.mesh >= meshes.size() || !meshes[e.mesh].valid()) {
            continue;
        }
        const auto& [mlo, mhi] = meshBounds(e.mesh);
        const glm::mat4 m = e.transform.matrix();
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? mhi.x : mlo.x, (corner & 2) ? mhi.y : mlo.y, (corner & 4) ? mhi.z : mlo.z);
            const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
            any = true;
        }
    }
    for (const auto& pg : procedurals) {
        if (!pg.visible || pg.instances.empty()) {
            continue;
        }
        lo = glm::min(lo, pg.boundsMin);
        hi = glm::max(hi, pg.boundsMax);
        any = true;
    }
    if (!any) {
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    return {lo, hi};
}

void Scene::clear() {
    cameras.clear();
    lights.clear();
    meshes.clear();
    textures.clear();
    entities.clear();
    particles.clear();
    procedurals.clear();
    rigs.clear();
    waters.clear();
    ++meshVersion;
    ++textureVersion;
}

CullBounds entityCullBounds(const Scene& scene, const Entity& entity, float padFraction,
                            float padAbsolute) {
    CullBounds out;
    if (entity.mesh == kInvalidMesh || entity.mesh >= scene.meshes.size()) {
        return out;
    }
    const MeshData& mesh = scene.meshes[entity.mesh];
    auto [lo, hi] = mesh.bounds();

    // The posed box, when there is a palette to pose with. Every vertex is transformed by its own
    // weighted joints -- the same arithmetic the skinning shader does -- so the box is the geometry
    // the frame will actually draw rather than the geometry the asset was authored in.
    if (entity.rig != kInvalidRig && entity.rig < scene.rigs.size() && mesh.skinned()) {
        const SkinnedRig& rig = scene.rigs[entity.rig];
        if (!rig.palette.empty() && !rig.skeleton.palette.empty()) {
            glm::vec3 posedLo(std::numeric_limits<float>::max());
            glm::vec3 posedHi(std::numeric_limits<float>::lowest());
            bool any = false;
            for (std::size_t i = 0; i < mesh.vertices.size() && i < mesh.skin.size(); ++i) {
                glm::vec3 position(0.0f);
                float weightSum = 0.0f;
                const SkinInfluence& influence = mesh.skin[i];
                for (std::size_t j = 0; j < kJointInfluences; ++j) {
                    const float weight = influence.weights[j];
                    if (weight <= 0.0f || influence.joints[j] >= rig.palette.size()) {
                        continue;
                    }
                    position += glm::vec3(rig.palette[influence.joints[j]] *
                                          glm::vec4(mesh.vertices[i].position, 1.0f)) *
                                weight;
                    weightSum += weight;
                }
                if (weightSum <= 1e-6f) {
                    continue; // an unweighted vertex is not posed by anything; the bind box covers it
                }
                position /= weightSum;
                posedLo = glm::min(posedLo, position);
                posedHi = glm::max(posedHi, position);
                any = true;
            }
            if (any) {
                lo = posedLo;
                hi = posedHi;
                out.posed = true;
            }
        }
    }

    const glm::vec3 pad = (hi - lo) * padFraction + glm::vec3(padAbsolute);
    const glm::mat4 model = entity.transform.matrix();
    glm::vec3 worldLo(std::numeric_limits<float>::max());
    glm::vec3 worldHi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local((corner & 1) ? hi.x + pad.x : lo.x - pad.x,
                              (corner & 2) ? hi.y + pad.y : lo.y - pad.y,
                              (corner & 4) ? hi.z + pad.z : lo.z - pad.z);
        const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
        worldLo = glm::min(worldLo, world);
        worldHi = glm::max(worldHi, world);
    }
    // A box that contains nothing, reported as one.
    //
    // The fold above is `glm::min`/`glm::max`, which are `(y<x)?y:x` -- so a NaN corner loses every
    // comparison and is *discarded* rather than propagated. A transform carrying an infinity makes
    // `glm::scale` compute `0 * inf`, every corner comes out NaN, every fold is refused, and the
    // result is the untouched seed: `min = FLT_MAX`, `max = lowest()`. That box is **finite**, so a
    // `isfinite` guard downstream can never fire, and it is inverted, so every frustum test rejects
    // it: the object vanishes and nothing anywhere says why. It is the exact shape of the reports
    // this investigation started from.
    //
    // The repair is to fail visibly and safely: say which entity and what its transform was, and
    // hand back the *unposed* world position as a minimal box so the object is drawn rather than
    // silently culled. A thing in the wrong place can be seen and chased; a thing that is not there
    // cannot.
    if (!glm::all(glm::lessThanEqual(worldLo, worldHi))) {
        log::warn("cull bounds: entity '{}' (mesh {}) produced no valid box -- position ({}, {}, {}) "
                  "scale ({}, {}, {}); drawing it unculled",
                  entity.name, entity.mesh, entity.transform.position.x, entity.transform.position.y,
                  entity.transform.position.z, entity.transform.scale.x, entity.transform.scale.y,
                  entity.transform.scale.z);
        const glm::vec3 at = entity.transform.position;
        const glm::vec3 fallback = finite(at) ? at : glm::vec3(0.0f);
        out.min = fallback - glm::vec3(padAbsolute);
        out.max = fallback + glm::vec3(padAbsolute);
        return out;
    }
    out.min = worldLo;
    out.max = worldHi;
    return out;
}

} // namespace avgen::scene
