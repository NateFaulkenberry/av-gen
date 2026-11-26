#include "pathtrace/surface.hpp"

#include "core/color.hpp"
#include "pathtrace/snapshot.hpp"
#include "pathtrace/texture.hpp"
#include "scene/material_program.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <span>

namespace avgen::pathtrace {
namespace {

// The triangle's three vertex indices in the mesh `p` names.
struct Corners {
    const TriangleMesh* mesh = nullptr;
    std::uint32_t i0 = 0;
    std::uint32_t i1 = 0;
    std::uint32_t i2 = 0;
};

[[nodiscard]] Corners cornersOf(const Snapshot& snap, const ShadingPoint& p) {
    Corners c;
    c.mesh = p.instanced ? &snap.instanced[p.object].source : &snap.meshes[p.object];
    const std::size_t t = static_cast<std::size_t>(p.prim) * 3;
    if (t + 2 >= c.mesh->indices.size()) {
        c.mesh = nullptr;
        return c;
    }
    c.i0 = c.mesh->indices[t + 0];
    c.i1 = c.mesh->indices[t + 1];
    c.i2 = c.mesh->indices[t + 2];
    return c;
}

template <class T>
[[nodiscard]] T lerp3(const std::vector<T>& v, const Corners& c, const ShadingPoint& p) {
    return v[c.i0] * p.b0 + v[c.i1] * p.b1 + v[c.i2] * p.b2;
}

// shaders/pbr_shade.wgsl `emissionLane`.
[[nodiscard]] glm::vec3 emissionLane(const glm::vec3& e, float hue, float gain) {
    glm::vec3 o = e;
    if (hue != 0.0f) o = color::hueShift(o, hue);
    return o * std::max(gain, 0.0f);
}

// shaders/chroma.wgsl `livingChromaMultiplier`.
[[nodiscard]] glm::vec3 livingChromaMultiplier(const glm::vec3& base, const glm::vec3& mult, float turns) {
    if (std::abs(turns) < 1e-6f) return mult;
    const glm::vec3 safe = glm::max(base, glm::vec3(1e-4f));
    const glm::vec3 rotated = glm::max(color::hueShift(safe * mult, turns), glm::vec3(0.0f));
    return glm::clamp(rotated / safe, glm::vec3(0.0f), glm::vec3(96.0f));
}

} // namespace

glm::vec2 surfaceUv(const Snapshot& snap, const ShadingPoint& p) {
    const Corners c = cornersOf(snap, p);
    if (c.mesh == nullptr || c.mesh->uvs.empty()) return glm::vec2(0.0f);
    return lerp3(c.mesh->uvs, c, p);
}

SurfaceMaterial shadeSurfaceMaterial(const Snapshot& snap, const ShadingPoint& p) {
    const Corners c = cornersOf(snap, p);
    SurfaceMaterial out;
    if (c.mesh == nullptr) return out;
    const TriangleMesh& mesh = *c.mesh;
    const scene::Material& mat = mesh.material;
    const glm::vec2 uv = mesh.uvs.empty() ? glm::vec2(0.0f) : lerp3(mesh.uvs, c, p);

    // ---- the instance's lanes (procedural.wgsl procVertex; an entity's are pbr_shade.wgsl materialInstanceZero) ----
    glm::vec4 instRandom{0.0f};
    glm::vec4 instColor{1.0f};
    glm::vec4 instEmissive{1.0f};
    float instId = 0.0f;
    float instIndex = 0.0f;
    float instSides = 0.0f;
    glm::vec2 variation{0.0f, 1.0f};
    if (p.instanced) {
        const InstancedObject& obj = snap.instanced[p.object];
        if (p.instance < obj.records.size()) {
            const spatial::InstanceRecord& rec = obj.records[p.instance];
            instRandom = rec.random;
            instColor = rec.color;
            instEmissive = rec.emissive;
            instId = rec.color.w;
            instIndex = rec.scale.w;
            instSides = rec.position.w;
            if (obj.chroma.w > 0.5f) {
                variation = color::emissionVariationOf(mat.emissiveColor, glm::vec3(rec.emissive));
            }
            if (obj.chroma.x > 0.0f) {
                const float turns = color::livingChromaTurns(glm::vec3(rec.position), static_cast<float>(snap.renderTime),
                                                             obj.chroma.x, obj.chroma.y, obj.chroma.z);
                instEmissive = glm::vec4(livingChromaMultiplier(mat.emissiveColor, glm::vec3(instEmissive), turns),
                                         instEmissive.w);
                variation.x += turns;
            }
        }
    }

    // ---- the factors, and the program over them (pbr_shade.wgsl shadeSurface) ----------------------------------
    glm::vec4 matColor{mat.baseColor, mat.opacity};
    glm::vec4 matEmissive{mat.emissiveColor, mat.emissiveIntensity};
    glm::vec2 roughMetal{mat.roughness, mat.metallic};
    bool programRuns = false;
    glm::vec3 programNormal{0.0f, 0.0f, 1.0f}; // tangent space; (0, 0, 1) = unperturbed
    const scene::MaterialProgram* program =
        mesh.program >= 0 && static_cast<std::size_t>(mesh.program) < snap.programs.size()
            ? &snap.programs[static_cast<std::size_t>(mesh.program)]
            : nullptr;
    const glm::vec3 eye = snap.camera.position;
    const float cameraDistance = glm::length(p.position - eye);
    if (program != nullptr && program->gate.admits(instRandom, cameraDistance)) {
        programRuns = true;
        scene::MaterialContext ctx;
        ctx.worldPosition = p.position;
        ctx.localPosition = mesh.localPositions.empty() ? glm::vec3(0.0f) : lerp3(mesh.localPositions, c, p);
        ctx.normal = p.normal;
        ctx.uv = uv;
        ctx.objectId = static_cast<float>(std::min<std::uint32_t>(mesh.entityIndex, scene::kPickMaxIndex));
        ctx.instanceIndex = instIndex;
        ctx.instanceId = instId;
        ctx.instanceRandom = instRandom;
        ctx.instanceColor = instColor;
        ctx.instanceEmissive = instEmissive;
        ctx.tileSides = instSides;
        ctx.time = static_cast<float>(snap.renderTime);
        ctx.viewDirection = cameraDistance > 0.0f ? (eye - p.position) / cameraDistance : glm::vec3(0.0f, 0.0f, 1.0f);
        ctx.depth = cameraDistance;
        ctx.occlusion = 1.0f;
        ctx.materialId = static_cast<float>(mesh.entityIndex + 1);
        ctx.materialEmission = glm::vec4(mat.emissiveColor * glm::vec3(instEmissive) * mat.emissiveIntensity, 1.0f);
        scene::MaterialResult base;
        base.baseColor = mat.baseColor;
        base.metallic = mat.metallic;
        base.roughness = mat.roughness;
        base.emission = mat.emissiveColor * mat.emissiveIntensity;
        base.opacity = mat.opacity;
        const scene::MaterialResult r = scene::evaluateMaterialProgram(*program, ctx, base);
        matColor = glm::vec4(r.baseColor, r.opacity);
        matEmissive = glm::vec4(r.emission, 1.0f);
        roughMetal = glm::vec2(r.roughness, r.metallic);
        programNormal = r.normal;
    }

    glm::vec3 ownEmissiveMul = glm::vec3(instEmissive);
    float ownHue = 0.0f;
    float ownGain = 1.0f;
    if (programRuns && program->writesEmission()) {
        ownEmissiveMul = glm::vec3(1.0f);
        if (!program->emissionReadsInstance()) {
            ownHue = variation.x;
            ownGain = variation.y;
        }
    }

    // ---- textures after the program --------------------------------------------------------------------------
    const std::span<const scene::TextureData> textures{snap.textures};
    glm::vec4 baseColor = matColor * glm::vec4(glm::vec3(instColor), 1.0f);
    baseColor *= sampleSlot(textures, mat.baseColorTexture, uv, glm::vec4(1.0f));
    const glm::vec4 mr = sampleSlot(textures, mat.metallicRoughnessTexture, uv, glm::vec4(1.0f));
    const glm::vec3 emissiveTexel = glm::vec3(sampleSlot(textures, mat.emissiveTexture, uv, glm::vec4(1.0f)));

    out.baseColor = glm::vec3(baseColor);
    out.opacity = baseColor.w;
    out.roughness = std::clamp(roughMetal.x * mr.y, 0.0f, 1.0f);
    out.metallic = std::clamp(roughMetal.y * mr.z, 0.0f, 1.0f);
    // ---- the normal (pbr_shade.wgsl): the program's tangent-space normal, then the normal map over it ----------
    glm::vec3 mapNormal = programNormal;
    bool perturb = programNormal.z < 0.99999f;
    if (mat.normalTexture.valid()) {
        glm::vec3 mapN = glm::vec3(sampleSlot(textures, mat.normalTexture, uv, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f))) *
                             2.0f - 1.0f;
        mapN = glm::vec3(glm::vec2(mapN) * mat.normalScale, mapN.z);
        const float len = glm::length(mapN);
        mapNormal = scene::reorientNormal(mapNormal, len > 1e-9f ? mapN / len : glm::vec3(0.0f, 0.0f, 1.0f));
        perturb = true;
    }
    if (perturb) {
        // The raster's cotangent frame (Schueler) from screen derivatives of position and uv is the gradient of u and
        // v over the surface; the triangle's own edges give the same gradient, so the frame is the same.
        glm::vec3 w0 = mesh.positions[c.i0];
        glm::vec3 w1 = mesh.positions[c.i1];
        glm::vec3 w2 = mesh.positions[c.i2];
        if (p.instanced) {
            const glm::mat4& xf = snap.instanced[p.object].transforms[p.instance];
            w0 = glm::vec3(xf * glm::vec4(w0, 1.0f));
            w1 = glm::vec3(xf * glm::vec4(w1, 1.0f));
            w2 = glm::vec3(xf * glm::vec4(w2, 1.0f));
        }
        const glm::vec3 n = p.normal;
        const glm::vec3 dp1 = w1 - w0;
        const glm::vec3 dp2 = w2 - w0;
        const glm::vec2 duv1 = mesh.uvs.empty() ? glm::vec2(0.0f) : mesh.uvs[c.i1] - mesh.uvs[c.i0];
        const glm::vec2 duv2 = mesh.uvs.empty() ? glm::vec2(0.0f) : mesh.uvs[c.i2] - mesh.uvs[c.i0];
        const glm::vec3 dp2perp = glm::cross(dp2, n);
        const glm::vec3 dp1perp = glm::cross(n, dp1);
        glm::vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
        glm::vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
        const float scale = std::max(glm::dot(t, t), glm::dot(b, b));
        if (scale < 1e-12f) {
            // No usable uv gradient: the raster's fallback, a frame from the position gradient.
            glm::vec3 tf = dp1 - n * glm::dot(n, dp1);
            const float tl = glm::length(tf);
            tf = tl > 1e-9f ? tf / tl : glm::vec3(1.0f, 0.0f, 0.0f);
            t = tf;
            b = glm::cross(n, tf);
        } else {
            const float invMax = 1.0f / std::sqrt(scale + 1e-12f);
            t *= invMax;
            b *= invMax;
        }
        const glm::vec3 perturbed = t * mapNormal.x + b * mapNormal.y + n * mapNormal.z;
        const float pl = glm::length(perturbed);
        if (pl > 1e-9f) out.normal = perturbed / pl;
    }

    glm::vec3 emissive = glm::vec3(matEmissive) * ownEmissiveMul * emissiveTexel * matEmissive.w;
    emissive = emissionLane(emissive, ownHue + mesh.emissionHue, ownGain * mesh.emissionGain);
    out.emission = glm::max(emissive, glm::vec3(0.0f));
    return out;
}

} // namespace avgen::pathtrace
