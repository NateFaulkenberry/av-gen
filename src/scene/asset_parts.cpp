#include "scene/asset_parts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace avgen::scene {
namespace {

void offsetTextureRef(TextureRef& ref, TextureId offset) {
    if (ref.valid()) {
        ref.texture += offset;
    }
}

bool sameMaterial(const TextureRef& a, const TextureRef& b) {
    return a.texture == b.texture && a.uvSet == b.uvSet && a.wrapU == b.wrapU && a.wrapV == b.wrapV &&
           a.linearFilter == b.linearFilter;
}

bool sameMaterial(const Material& a, const Material& b) {
    return a.baseColor == b.baseColor && a.opacity == b.opacity && a.emissiveColor == b.emissiveColor &&
           a.emissiveIntensity == b.emissiveIntensity && a.roughness == b.roughness &&
           a.metallic == b.metallic && a.normalScale == b.normalScale &&
           a.occlusionStrength == b.occlusionStrength && a.alphaMode == b.alphaMode &&
           a.alphaCutoff == b.alphaCutoff && a.doubleSided == b.doubleSided && a.unlit == b.unlit &&
           a.program == b.program && sameMaterial(a.baseColorTexture, b.baseColorTexture) &&
           sameMaterial(a.metallicRoughnessTexture, b.metallicRoughnessTexture) &&
           sameMaterial(a.normalTexture, b.normalTexture) &&
           sameMaterial(a.emissiveTexture, b.emissiveTexture) &&
           sameMaterial(a.occlusionTexture, b.occlusionTexture);
}

double meshAreaUnder(const MeshData& mesh, const glm::mat4& model) {
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
            continue;
        }
        const glm::vec3 pa = glm::vec3(model * glm::vec4(mesh.vertices[a].position, 1.0f));
        const glm::vec3 pb = glm::vec3(model * glm::vec4(mesh.vertices[b].position, 1.0f));
        const glm::vec3 pc = glm::vec3(model * glm::vec4(mesh.vertices[c].position, 1.0f));
        area += 0.5 * static_cast<double>(glm::length(glm::cross(pb - pa, pc - pa)));
    }
    return area;
}

} // namespace

void offsetEntityIds(Entity& e, MeshId meshOffset, TextureId textureOffset) {
    if (e.mesh != kInvalidMesh) {
        e.mesh += meshOffset;
    }
    offsetTextureRef(e.material.baseColorTexture, textureOffset);
    offsetTextureRef(e.material.metallicRoughnessTexture, textureOffset);
    offsetTextureRef(e.material.normalTexture, textureOffset);
    offsetTextureRef(e.material.emissiveTexture, textureOffset);
    offsetTextureRef(e.material.occlusionTexture, textureOffset);
}

void offsetMaterialTextures(Material& m, TextureId textureOffset) {
    offsetTextureRef(m.baseColorTexture, textureOffset);
    offsetTextureRef(m.metallicRoughnessTexture, textureOffset);
    offsetTextureRef(m.normalTexture, textureOffset);
    offsetTextureRef(m.emissiveTexture, textureOffset);
    offsetTextureRef(m.occlusionTexture, textureOffset);
}

std::vector<AssetPart> assetMaterialParts(const assets::SceneAsset& asset) {
    struct Group {
        Material material;
        std::vector<std::size_t> entities;
        double area = 0.0;
        std::size_t firstEntity = 0;
    };
    std::vector<Group> groups;
    for (std::size_t e = 0; e < asset.scene.entities.size(); ++e) {
        const Entity& entity = asset.scene.entities[e];
        if (!entity.visible || entity.mesh == kInvalidMesh || entity.mesh >= asset.scene.meshes.size()) {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const Group& g) { return sameMaterial(g.material, entity.material); });
        if (it == groups.end()) {
            groups.push_back(Group{entity.material, {}, 0.0, e});
            it = std::prev(groups.end());
        }
        it->entities.push_back(e);
        it->area += meshAreaUnder(asset.scene.meshes[entity.mesh], entity.transform.matrix());
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        return a.area != b.area ? a.area > b.area : a.firstEntity < b.firstEntity;
    });

    std::vector<AssetPart> parts;
    parts.reserve(groups.size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto merged = std::make_shared<MeshData>();
        merged->name = groups.size() > 1 ? fmt::format("{}#{}", asset.path.stem().string(), g)
                                         : asset.path.stem().string();
        for (const std::size_t e : groups[g].entities) {
            const Entity& entity = asset.scene.entities[e];
            const MeshData& src = asset.scene.meshes[entity.mesh];
            const glm::mat4 model = entity.transform.matrix();
            const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));
            const auto base = static_cast<std::uint32_t>(merged->vertices.size());
            merged->vertices.reserve(merged->vertices.size() + src.vertices.size());
            for (const Vertex& v : src.vertices) {
                Vertex out = v;
                out.position = glm::vec3(model * glm::vec4(v.position, 1.0f));
                const glm::vec3 n = normalMatrix * v.normal;
                out.normal = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : v.normal;
                merged->vertices.push_back(out);
            }
            merged->indices.reserve(merged->indices.size() + src.indices.size());
            for (const std::uint32_t index : src.indices) {
                merged->indices.push_back(base + index);
            }
        }
        parts.push_back(AssetPart{std::move(merged), groups[g].material, true, groups[g].area,
                                  groups[g].firstEntity,
                                  asset.scene.entities[groups[g].firstEntity].materialName});
    }
    if (parts.empty()) {
        // Nothing drawable in the asset. One empty part keeps every caller on one code path, and
        // the object ends up with no mesh exactly as it did before.
        parts.push_back(AssetPart{std::make_shared<MeshData>(), Material{}, false, 0.0, 0, {}});
    }
    return parts;
}

std::pair<glm::vec3, glm::vec3> partsBounds(const std::vector<AssetPart>& parts) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const AssetPart& part : parts) {
        if (!part.mesh || part.mesh->vertices.empty()) {
            continue;
        }
        const auto [plo, phi] = part.mesh->bounds();
        lo = glm::min(lo, plo);
        hi = glm::max(hi, phi);
        any = true;
    }
    return any ? std::pair{lo, hi} : std::pair{glm::vec3(0.0f), glm::vec3(0.0f)};
}

int partBudget(const std::vector<AssetPart>& parts, std::size_t index, int assetBudget) {
    if (assetBudget <= 0 || parts.size() <= 1) {
        return assetBudget;
    }
    std::size_t total = 0;
    for (const AssetPart& part : parts) {
        total += part.mesh ? part.mesh->indices.size() / 3 : 0;
    }
    const std::size_t mine = parts[index].mesh ? parts[index].mesh->indices.size() / 3 : 0;
    if (total == 0 || mine == 0) {
        return assetBudget;
    }
    const auto share = static_cast<double>(assetBudget) * static_cast<double>(mine) / static_cast<double>(total);
    return std::max(1, static_cast<int>(std::lround(share)));
}

} // namespace avgen::scene
