#include "pathtrace/snapshot.hpp"

#include "core/log.hpp"
#include "scene/procedural.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

namespace avgen::pathtrace {
namespace {

// Normals transform by the inverse transpose, not by the matrix. With a uniform scale the two agree
// and the bug hides; the moment a scene scales an object non-uniformly the shading normals shear and
// the lighting goes subtly wrong in a way that looks like a BSDF problem. Done once per entity.
[[nodiscard]] glm::mat3 normalMatrix(const glm::mat4& m) { return glm::inverseTranspose(glm::mat3(m)); }

// Linear blend skinning, the same four-influence model `shaders/skinning.wgsl` runs on the GPU.
// `MeshData` is the BIND POSE; without this a character renders wherever the artist modelled it,
// which is usually the origin, and nothing in the image says why.
[[nodiscard]] glm::mat4 skinMatrix(const scene::SkinInfluence& inf,
                                   const std::vector<glm::mat4>& palette) {
    glm::mat4 m(0.0f);
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float w = inf.weights[i];
        if (w <= 0.0f) continue;
        const std::uint16_t j = inf.joints[i];
        if (j >= palette.size()) continue;   // a malformed index must not read out of bounds
        m += palette[j] * w;
        total += w;
    }
    // Weights are normalised on import, but a clipped or partially-out-of-range influence can leave
    // the sum short; renormalising keeps the vertex on the surface instead of shrinking it toward
    // the origin, which reads as a deflating character.
    if (total > 1e-6f && std::abs(total - 1.0f) > 1e-4f) m /= total;
    return total > 1e-6f ? m : glm::mat4(1.0f);
}

} // namespace

std::string_view supportName(Support s) {
    switch (s) {
    case Support::Full: return "full";
    case Support::Degraded: return "degraded";
    case Support::Unsupported: return "unsupported";
    }
    return "unknown";
}

void CapabilityReport::note(std::string feature, Support support, std::string detail, int count) {
    // Collapse repeats: one row per feature, counting how many were met.
    for (auto& e : entries) {
        if (e.feature == feature) {
            e.count += count;
            return;
        }
    }
    entries.push_back(Capability{std::move(feature), support, std::move(detail), count});
}

void CapabilityReport::caveat(std::string text) {
    for (const auto& c : caveats) {
        if (c == text) return;
    }
    caveats.push_back(std::move(text));
}

bool CapabilityReport::anyDegraded() const {
    return std::any_of(entries.begin(), entries.end(),
                       [](const Capability& c) { return c.support == Support::Degraded && c.count > 0; });
}

bool CapabilityReport::anyUnsupported() const {
    return std::any_of(entries.begin(), entries.end(),
                       [](const Capability& c) { return c.support == Support::Unsupported && c.count > 0; });
}

std::string CapabilityReport::format() const {
    std::string out;
    for (const auto& e : entries) {
        out += fmt::format("  [{:<11}] {:<22} x{:<6} {}\n", supportName(e.support), e.feature, e.count, e.detail);
    }
    for (const auto& c : caveats) {
        out += fmt::format("  [{:<11}] {}\n", "caveat", c);
    }
    return out;
}

bool TriangleMesh::valid() const {
    if (positions.empty() || indices.empty()) return false;
    if (indices.size() % 3 != 0) return false;
    if (normals.size() != positions.size()) return false;
    if (!uvs.empty() && uvs.size() != positions.size()) return false;
    return std::all_of(indices.begin(), indices.end(),
                       [&](std::uint32_t i) { return i < positions.size(); });
}

std::size_t Snapshot::triangleCount() const {
    std::size_t n = 0;
    for (const auto& m : meshes) n += m.triangleCount();
    return n;
}

Snapshot buildSnapshot(const scene::Scene& scene) {
    Snapshot snap;
    snap.camera = scene.camera;
    snap.backgroundColor = scene.environment.backgroundColor;

    // Environment. The sky is resolved against the scene's own lights by the project's own
    // resolver, so the sun the tracer sees is the sun the rasteriser sees.
    snap.skyEnabled = scene.environment.sky.enabled;
    snap.sky = scene::resolveSky(scene.environment.sky, scene.lights);
    if (scene.environment.environmentMap != scene::kInvalidTexture) {
        snap.capabilities.note("environment map", Support::Degraded,
                               "an HDR environment map is present but image-based lighting is not built yet; "
                               "the analytic sky (ADR-036) is used instead",
                               1);
    } else if (snap.skyEnabled) {
        snap.capabilities.note("analytic sky", Support::Full,
                               "scene::skyRadiance, the same model shaders/environment.wgsl draws", 1);
    }

    // Renderer-level caveats, printed on every render whatever the scene holds. This one is here
    // rather than only in ADR-345 because the ADR is not where somebody debugging an unexpectedly
    // bright interior would think to look, and the startup log is.
    snap.capabilities.caveat(
        "the glTF metallic-roughness BRDF is kept faithful to the specification (ADR-345), and the "
        "specification's model GAINS energy at grazing angles: up to 1.68x directional albedo on a "
        "bright, smooth, non-metallic surface. It compounds per bounce. If an interior render looks "
        "inexplicably bright, check this first.");

    snap.textures = scene.textures;
    snap.lights = scene.lights;
    int directional = 0;
    int punctual = 0;
    int area = 0;
    for (const auto& l : scene.lights) {
        if (!l.enabled) continue;
        switch (l.type) {
        case scene::PunctualLight::Type::Directional: ++directional; break;
        case scene::PunctualLight::Type::Point:
        case scene::PunctualLight::Type::Spot: ++punctual; break;
        default: ++area; break;
        }
    }
    if (directional > 0) snap.capabilities.note("directional light", Support::Full, "sampled as a delta direction", directional);
    if (area > 0) snap.capabilities.note("area light", Support::Full, "sampled over the emitter", area);
    if (punctual > 0) {
        snap.capabilities.note("point / spot light", Support::Degraded,
                               "treated as a delta position, so its shadow is hard; the realtime path softens it",
                               punctual);
    }

    // ---- geometry -------------------------------------------------------------------------------
    int skippedInvisible = 0;
    int skippedNoMesh = 0;
    int skippedSkinned = 0;
    int water = 0;
    int grid = 0;
    int degenerate = 0;

    for (std::uint32_t ei = 0; ei < scene.entities.size(); ++ei) {
        const scene::Entity& e = scene.entities[ei];

        if (!e.visible) { ++skippedInvisible; continue; }
        if (e.mesh == scene::kInvalidMesh || e.mesh >= scene.meshes.size()) { ++skippedNoMesh; continue; }

        // NOTE: `cameraCulled` is deliberately NOT consulted. It means "outside the realtime
        // camera's frustum", and off-screen geometry still casts shadows and still bounces light
        // into the frame. Inheriting it would be the ADR-146 mistake in a new place.

        const scene::MeshData& src = scene.meshes[e.mesh];

        // Skinning. A rig whose palette is empty has not been evaluated this frame -- usually
        // `SkinnedRig::cullDistance` froze it -- and its bind pose is not where the character is.
        // Refusing is better than drawing it in the wrong place (spec section 87).
        const std::vector<glm::mat4>* palette = nullptr;
        if (e.rig != scene::kInvalidRig) {
            if (e.rig >= scene.rigs.size() || scene.rigs[e.rig].palette.empty() || src.skin.empty()) {
                ++skippedSkinned;
                continue;
            }
            palette = &scene.rigs[e.rig].palette;
        }
        if (src.vertices.empty() || src.indices.size() < 3) { ++degenerate; continue; }

        if (e.style == scene::MeshStyle::Water) ++water;
        if (e.style == scene::MeshStyle::Grid) ++grid;

        TriangleMesh out;
        out.entityIndex = ei;
        out.entityName = e.name;
        out.material = e.material;

        const glm::mat4 m = e.transform.matrix();
        const glm::mat3 nm = normalMatrix(m);

        out.positions.reserve(src.vertices.size());
        out.normals.reserve(src.vertices.size());
        const bool hasUv = true; // scene::Vertex always carries a uv; it is zero when unauthored
        if (hasUv) out.uvs.reserve(src.vertices.size());

        for (std::size_t vi = 0; vi < src.vertices.size(); ++vi) {
            const scene::Vertex& v = src.vertices[vi];
            glm::vec3 pos = v.position;
            glm::vec3 nrm = v.normal;
            if (palette != nullptr && vi < src.skin.size()) {
                const glm::mat4 sk = skinMatrix(src.skin[vi], *palette);
                pos = glm::vec3(sk * glm::vec4(pos, 1.0f));
                // The skin matrix can scale and shear, so the normal takes its inverse transpose
                // too. Using the matrix directly shears the shading and looks like a BSDF fault.
                nrm = glm::inverseTranspose(glm::mat3(sk)) * nrm;
            }
            out.positions.push_back(glm::vec3(m * glm::vec4(pos, 1.0f)));
            const glm::vec3 n = nm * nrm;
            const float len = glm::length(n);
            out.normals.push_back(len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f));
            out.uvs.push_back(v.uv);
        }
        out.indices = src.indices;

        if (!out.valid()) { ++degenerate; continue; }
        snap.meshes.push_back(std::move(out));
    }

    if (skippedInvisible > 0) {
        snap.capabilities.note("invisible entity", Support::Full, "not drawn, as the scene asks", skippedInvisible);
    }
    if (skippedNoMesh > 0) {
        snap.capabilities.note("entity with no mesh", Support::Full, "nothing to intersect", skippedNoMesh);
    }
    if (skippedSkinned > 0) {
        snap.capabilities.note("unposed rig", Support::Unsupported,
                               "the rig has no evaluated joint palette this frame (usually "
                               "SkinnedRig::cullDistance froze it), and its bind pose is not where the "
                               "character is; set cullDistance and updateHz to 0 for an offline trace",
                               skippedSkinned);
    }
    if (degenerate > 0) {
        snap.capabilities.note("degenerate mesh", Support::Unsupported, "empty or malformed index/vertex data", degenerate);
    }
    if (water > 0) {
        snap.capabilities.note("water surface", Support::Degraded,
                               "the mesh is a flat CPU sheet; ripples, swell, foam and refraction live in "
                               "shaders/water.wgsl and are not reproduced",
                               water);
    }
    if (grid > 0) {
        snap.capabilities.note("grid-styled entity", Support::Degraded,
                               "MeshStyle::Grid is a shader look; shaded as an ordinary surface", grid);
    }

    // ---- what exists in the scene and cannot be reached at all ------------------------------------
    if (!scene.particles.empty()) {
        snap.capabilities.note("particle system", Support::Unsupported,
                               "GPU-only by construction (src/scene/particles.hpp): the CPU holds settings and the "
                               "simulation lives in compute shaders, so no positions exist to intersect",
                               static_cast<int>(scene.particles.size()));
    }
    // ---- procedural scatter ----------------------------------------------------------------
    //
    // The realtime renderer resolves these with `scene::makeSourceMesh` on the CPU and then
    // instances them on the GPU; the tracer calls the same function, so the two agree about what
    // the object IS. LOD level 0 always: rungs 2 and 3 are camera-facing billboards and a billboard
    // in a path trace is a flat card lit from the wrong direction.
    int proceduralInstances = 0;
    int proceduralFailed = 0;
    for (std::size_t pi = 0; pi < scene.procedurals.size(); ++pi) {
        const scene::ProceduralGeometry& proc = scene.procedurals[pi];
        if (proc.instances.empty()) continue;

        auto sourceMesh = scene::makeSourceMesh(proc.source);
        if (!sourceMesh) {
            ++proceduralFailed;
            continue;
        }
        const scene::MeshData& src = *sourceMesh;
        if (src.vertices.empty() || src.indices.size() < 3) {
            ++proceduralFailed;
            continue;
        }

        // The instance transform is composed exactly as shaders/procedural.wgsl composes it --
        // `inst.position + quatRotate(inst.rotation, p * inst.scale)`, under the object model --
        // and NOT with `ProceduralGeometry::instanceMatrix`, which re-derives placement from the
        // distribution and ignores the baked `instances` array entirely. The realtime renderer
        // uploads `object.instances` (procedural_renderer.cpp:1418), so that is what the tracer
        // must read or the two renderers draw different worlds. Measured the hard way: using
        // `instanceMatrix` put twelve cubes spanning 22 m into a 7 m span.
        const glm::mat4 objectModel = proc.distributionTransform.matrix();
        const glm::mat4 sourceXf = proc.sourceTransform.matrix();
        for (std::uint32_t ii = 0; ii < proc.instances.size(); ++ii) {
            const spatial::InstanceRecord& rec = proc.instances[ii];
            const glm::quat q{rec.rotation.w, rec.rotation.x, rec.rotation.y, rec.rotation.z};
            const glm::mat4 xf = objectModel *
                                 glm::translate(glm::mat4(1.0f), glm::vec3(rec.position)) *
                                 glm::mat4_cast(q) *
                                 glm::scale(glm::mat4(1.0f), glm::vec3(rec.scale)) * sourceXf;
            const glm::mat3 pnm = normalMatrix(xf);

            TriangleMesh out;
            out.entityIndex = static_cast<std::uint32_t>(pi);
            out.entityName = fmt::format("procedural[{}]#{}", pi, ii);
            out.material = proc.material;
            out.positions.reserve(src.vertices.size());
            out.normals.reserve(src.vertices.size());
            out.uvs.reserve(src.vertices.size());
            for (const scene::Vertex& v : src.vertices) {
                out.positions.push_back(glm::vec3(xf * glm::vec4(v.position, 1.0f)));
                const glm::vec3 n = pnm * v.normal;
                const float len = glm::length(n);
                out.normals.push_back(len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f));
                out.uvs.push_back(v.uv);
            }
            out.indices = src.indices;
            if (!out.valid()) continue;
            snap.meshes.push_back(std::move(out));
            ++proceduralInstances;
        }
    }
    if (proceduralInstances > 0) {
        snap.capabilities.note("procedural instance", Support::Degraded,
                               "baked to world-space triangles rather than Embree instances, so memory "
                               "grows with instance count; deformers and effectors are not applied",
                               proceduralInstances);
    }
    if (proceduralFailed > 0) {
        snap.capabilities.note("procedural source", Support::Unsupported,
                               "scene::makeSourceMesh could not produce geometry for this object",
                               proceduralFailed);
    }
    if (!scene.sdfs.empty()) {
        int raymarch = 0;
        for (const auto& s : scene.sdfs) {
            if (s.renderMode != scene::SdfRenderMode::Mesh) ++raymarch;
        }
        if (raymarch > 0) {
            snap.capabilities.note("SDF object (raymarch)", Support::Unsupported,
                                   "no triangles exist in Raymarch mode; spatial::SdfTree is CPU-evaluable but "
                                   "sphere tracing is a separate integrator path",
                                   raymarch);
        }
    }
    if (scene.environment.volumeDensity > 0.0f) {
        snap.capabilities.note("volumetrics", Support::Unsupported,
                               "participating media are a stated non-goal for now (spec section 72)", 1);
    }

    // ---- emissive geometry ------------------------------------------------------------------
    //
    // Collected AFTER the meshes, because it indexes into them. A triangle is a light if its
    // material emits; the emission is uniform over the triangle since there is no emissive texture
    // lookup here (that would need per-sample UV interpolation, and is a refinement not a blocker).
    for (std::uint32_t mi = 0; mi < snap.meshes.size(); ++mi) {
        const TriangleMesh& mesh = snap.meshes[mi];
        const scene::Material& mat = mesh.material;
        const glm::vec3 emission = mat.emissiveColor * mat.emissiveIntensity;
        if (emission.x <= 0.0f && emission.y <= 0.0f && emission.z <= 0.0f) continue;

        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            EmissiveTriangle e;
            e.meshIndex = mi;
            e.primIndex = static_cast<std::uint32_t>(t / 3);
            e.v0 = mesh.positions[mesh.indices[t + 0]];
            e.v1 = mesh.positions[mesh.indices[t + 1]];
            e.v2 = mesh.positions[mesh.indices[t + 2]];
            const glm::vec3 cross = glm::cross(e.v1 - e.v0, e.v2 - e.v0);
            const float len = glm::length(cross);
            if (len <= 1e-12f) continue;   // degenerate: zero area, would divide by zero
            e.normal = cross / len;
            e.area = 0.5f * len;
            e.emission = emission;
            snap.totalEmissiveArea += e.area;
            snap.emissiveTriangles.push_back(e);
        }
    }
    if (snap.totalEmissiveArea > 0.0f) {
        float running = 0.0f;
        for (auto& e : snap.emissiveTriangles) {
            running += e.area;
            e.cdf = running / snap.totalEmissiveArea;
        }
        snap.emissiveTriangles.back().cdf = 1.0f;   // exact, so a u of 0.999999 cannot fall off the end
        snap.capabilities.note("emissive geometry", Support::Full,
                               "sampled directly and combined with BSDF sampling by MIS",
                               static_cast<int>(snap.emissiveTriangles.size()));
    }

    return snap;
}

} // namespace avgen::pathtrace
