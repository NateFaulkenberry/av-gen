#include "assets/gltf_loader.hpp"

#include "assets/image.hpp"
#include "core/log.hpp"

// fastgltf is a private dependency of avgen_core: its headers are included only here.
#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace avgen::assets {

namespace {

using Clock = std::chrono::steady_clock;

glm::mat4 toGlm(const fastgltf::math::fmat4x4& m) {
    // Both libraries store column-major 4x4 matrices: column i, row j.
    glm::mat4 out(1.0f);
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            out[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)] = m.col(c)[r];
        }
    }
    return out;
}

glm::vec3 toGlm(const fastgltf::math::nvec3& v) {
    return {static_cast<float>(v.x()), static_cast<float>(v.y()), static_cast<float>(v.z())};
}

std::string toStd(std::string_view s) {
    return std::string(s);
}

// Returns the byte payload behind an image or buffer DataSource when it is already in memory.
std::span<const std::uint8_t> inMemoryBytes(const fastgltf::DataSource& source) {
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&source)) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) std::byte -> uint8_t view
        return {reinterpret_cast<const std::uint8_t*>(array->bytes.data()), array->bytes.size()};
    }
    if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&source)) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<const std::uint8_t*>(vector->bytes.data()), vector->bytes.size()};
    }
    if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&source)) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return {reinterpret_cast<const std::uint8_t*>(view->bytes.data()), view->bytes.size()};
    }
    return {};
}

scene::WrapMode toWrap(fastgltf::Wrap wrap) {
    switch (wrap) {
    case fastgltf::Wrap::ClampToEdge:
        return scene::WrapMode::Clamp;
    case fastgltf::Wrap::MirroredRepeat:
        return scene::WrapMode::Mirror;
    case fastgltf::Wrap::Repeat:
        break;
    }
    return scene::WrapMode::Repeat;
}

scene::AlphaMode toAlphaMode(fastgltf::AlphaMode mode) {
    switch (mode) {
    case fastgltf::AlphaMode::Mask:
        return scene::AlphaMode::Mask;
    case fastgltf::AlphaMode::Blend:
        return scene::AlphaMode::Blend;
    case fastgltf::AlphaMode::Opaque:
        break;
    }
    return scene::AlphaMode::Opaque;
}

scene::PunctualLight::Type toLightType(fastgltf::LightType type) {
    switch (type) {
    case fastgltf::LightType::Point:
        return scene::PunctualLight::Type::Point;
    case fastgltf::LightType::Spot:
        return scene::PunctualLight::Type::Spot;
    case fastgltf::LightType::Directional:
        break;
    }
    return scene::PunctualLight::Type::Directional;
}

// All import state for one loadGltf call. Builds into `local`; the caller merges on success.
class Importer {
public:
    Importer(const fastgltf::Asset& asset, std::filesystem::path directory, const GltfLoadOptions& options,
             std::string fileLabel)
        : asset_(asset)
        , directory_(std::move(directory))
        , options_(options)
        , fileLabel_(std::move(fileLabel)) {}

    void run() {
        noteUnsupportedTopLevel();
        if (!asset_.scenes.empty()) {
            std::size_t sceneIndex = 0;
            if (asset_.defaultScene.has_value() && *asset_.defaultScene < asset_.scenes.size()) {
                sceneIndex = *asset_.defaultScene;
            }
            fastgltf::iterateSceneNodes(
                asset_, sceneIndex, fastgltf::math::fmat4x4(),
                [this](const fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix) {
                    visitNode(node, toGlm(matrix));
                });
        } else {
            // No scenes: treat every node that is nobody's child as a root.
            std::vector<bool> isChild(asset_.nodes.size(), false);
            for (const auto& node : asset_.nodes) {
                for (const std::size_t child : node.children) {
                    if (child < isChild.size()) {
                        isChild[child] = true;
                    }
                }
            }
            for (std::size_t i = 0; i < asset_.nodes.size(); ++i) {
                if (!isChild[i]) {
                    walk(i, fastgltf::math::fmat4x4(), 0);
                }
            }
        }
    }

    scene::Scene& local() { return local_; }
    std::vector<std::string>& warnings() { return warnings_; }
    [[nodiscard]] std::size_t materialCount() const { return materialsUsed_.size(); }

private:
    static constexpr std::size_t kMaxDepth = 256;

    void warn(std::string message) {
        if (std::find(warnings_.begin(), warnings_.end(), message) == warnings_.end()) {
            warnings_.push_back(std::move(message));
        }
    }

    void noteUnsupportedTopLevel() {
        if (!asset_.animations.empty()) {
            warn(fmt::format("{} animation(s) ignored (not supported in 0.2)", asset_.animations.size()));
        }
        if (!asset_.skins.empty()) {
            warn(fmt::format("{} skin(s) ignored (skinning not supported in 0.2)", asset_.skins.size()));
        }
    }

    // Fallback traversal for assets without a scene list; mirrors fastgltf::iterateSceneNodes.
    void walk(std::size_t nodeIndex, const fastgltf::math::fmat4x4& parent, std::size_t depth) {
        if (nodeIndex >= asset_.nodes.size() || depth > kMaxDepth) {
            warn("node hierarchy too deep or malformed; subtree skipped");
            return;
        }
        const auto& node = asset_.nodes[nodeIndex];
        const fastgltf::math::fmat4x4 world = fastgltf::getTransformMatrix(node, parent);
        visitNode(node, toGlm(world));
        for (const std::size_t child : node.children) {
            walk(child, world, depth + 1);
        }
    }

    [[nodiscard]] std::string nodeLabel(const fastgltf::Node& node) const {
        if (!node.name.empty()) {
            return toStd(node.name);
        }
        const auto index = static_cast<std::size_t>(&node - asset_.nodes.data());
        return fmt::format("node{}", index);
    }

    void visitNode(const fastgltf::Node& node, const glm::mat4& world) {
        const std::string label = nodeLabel(node);
        if (node.meshIndex.has_value()) {
            importMeshNode(node, label, world);
        }
        if (node.lightIndex.has_value()) {
            importLight(node, label, world);
        }
        if (node.cameraIndex.has_value()) {
            importCamera(node, label, world);
        }
        if (!node.instancingAttributes.empty()) {
            warn(fmt::format("node '{}': EXT_mesh_gpu_instancing ignored", label));
        }
    }

    // ---- meshes --------------------------------------------------------------------------------

    void importMeshNode(const fastgltf::Node& node, const std::string& label, const glm::mat4& world) {
        const std::size_t meshIndex = *node.meshIndex;
        if (meshIndex >= asset_.meshes.size()) {
            warn(fmt::format("node '{}': mesh index {} out of range", label, meshIndex));
            return;
        }
        const auto& mesh = asset_.meshes[meshIndex];
        const scene::Transform transform = scene::Transform::fromMatrix(world);
        const bool multi = mesh.primitives.size() > 1;
        for (std::size_t p = 0; p < mesh.primitives.size(); ++p) {
            const auto& primitive = mesh.primitives[p];
            const scene::MeshId meshId = meshFor(meshIndex, p, label);
            if (meshId == scene::kInvalidMesh) {
                continue;
            }
            std::string entityName = options_.namePrefix + label;
            if (multi) {
                entityName += fmt::format("/prim{}", p);
            }
            scene::Entity& entity = local_.addEntity(std::move(entityName), meshId);
            entity.transform = transform;
            entity.material = materialFor(primitive, label);
        }
    }

    scene::MeshId meshFor(std::size_t meshIndex, std::size_t primitiveIndex, const std::string& label) {
        const auto key = std::make_pair(meshIndex, primitiveIndex);
        if (const auto it = meshCache_.find(key); it != meshCache_.end()) {
            return it->second;
        }
        scene::MeshId id = scene::kInvalidMesh;
        if (auto data = convertPrimitive(asset_.meshes[meshIndex].primitives[primitiveIndex], label,
                                         primitiveIndex)) {
            id = local_.addMesh(std::move(*data));
        }
        meshCache_.emplace(key, id);
        return id;
    }

    [[nodiscard]] const fastgltf::Accessor* accessorFor(const fastgltf::Primitive& primitive,
                                                        std::string_view name,
                                                        fastgltf::AccessorType expected) const {
        const auto it = primitive.findAttribute(name);
        if (it == primitive.attributes.cend()) {
            return nullptr;
        }
        if (it->accessorIndex >= asset_.accessors.size()) {
            return nullptr;
        }
        const auto& accessor = asset_.accessors[it->accessorIndex];
        return accessor.type == expected ? &accessor : nullptr;
    }

    std::optional<scene::MeshData> convertPrimitive(const fastgltf::Primitive& primitive,
                                                    const std::string& label, std::size_t primitiveIndex) {
        const std::string where = fmt::format("{}/prim{}", label, primitiveIndex);
        if (primitive.type != fastgltf::PrimitiveType::Triangles) {
            warn(fmt::format("{}: non-triangle-list primitive skipped", where));
            return std::nullopt;
        }
        if (primitive.dracoCompression) {
            warn(fmt::format("{}: KHR_draco_mesh_compression not supported; primitive skipped", where));
            return std::nullopt;
        }
        if (!primitive.targets.empty()) {
            warn(fmt::format("{}: morph targets ignored", where));
        }
        const auto* positions = accessorFor(primitive, "POSITION", fastgltf::AccessorType::Vec3);
        if (positions == nullptr) {
            warn(fmt::format("{}: missing or malformed POSITION attribute; primitive skipped", where));
            return std::nullopt;
        }
        if (positions->count == 0 || positions->count > std::numeric_limits<std::uint32_t>::max()) {
            warn(fmt::format("{}: unsupported vertex count {}; primitive skipped", where, positions->count));
            return std::nullopt;
        }

        scene::MeshData data;
        data.name = where;
        data.vertices.resize(positions->count);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset_, *positions, [&](fastgltf::math::fvec3 v, std::size_t i) {
                data.vertices[i].position = {v.x(), v.y(), v.z()};
                data.vertices[i].normal = glm::vec3(0.0f);
                data.vertices[i].uv = glm::vec2(0.0f);
            });

        bool haveNormals = false;
        if (const auto* normals = accessorFor(primitive, "NORMAL", fastgltf::AccessorType::Vec3);
            normals != nullptr && normals->count == positions->count) {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset_, *normals, [&](fastgltf::math::fvec3 n, std::size_t i) {
                    data.vertices[i].normal = {n.x(), n.y(), n.z()};
                });
            haveNormals = true;
        } else if (primitive.findAttribute("NORMAL") != primitive.attributes.cend()) {
            warn(fmt::format("{}: NORMAL attribute malformed; normals regenerated", where));
        }

        if (const auto* uvs = accessorFor(primitive, "TEXCOORD_0", fastgltf::AccessorType::Vec2);
            uvs != nullptr && uvs->count == positions->count) {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset_, *uvs,
                [&](fastgltf::math::fvec2 uv, std::size_t i) { data.vertices[i].uv = {uv.x(), uv.y()}; });
        }

        // Options::GenerateMeshIndices guarantees an index accessor for triangle lists.
        if (!primitive.indicesAccessor.has_value() || *primitive.indicesAccessor >= asset_.accessors.size()) {
            warn(fmt::format("{}: missing index accessor; primitive skipped", where));
            return std::nullopt;
        }
        const auto& indices = asset_.accessors[*primitive.indicesAccessor];
        if (indices.type != fastgltf::AccessorType::Scalar || indices.count % 3 != 0 || indices.count == 0) {
            warn(fmt::format("{}: index accessor is not a triangle list; primitive skipped", where));
            return std::nullopt;
        }
        data.indices.resize(indices.count);
        bool indexOutOfRange = false;
        const auto vertexCount = static_cast<std::uint32_t>(data.vertices.size());
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(
            asset_, indices, [&](std::uint32_t index, std::size_t i) {
                data.indices[i] = index;
                indexOutOfRange = indexOutOfRange || index >= vertexCount;
            });
        if (indexOutOfRange) {
            warn(fmt::format("{}: index out of range; primitive skipped", where));
            return std::nullopt;
        }

        if (!haveNormals) {
            if (options_.generateNormals) {
                data.computeNormals();
            } else {
                warn(fmt::format("{}: no normals and generateNormals is off", where));
            }
        }
        return data;
    }

    // ---- materials -----------------------------------------------------------------------------

    scene::Material materialFor(const fastgltf::Primitive& primitive, const std::string& label) {
        scene::Material material;
        // Defaults for primitives without a material: glTF's white, rough dielectric.
        material.baseColor = glm::vec3(1.0f);
        material.opacity = 1.0f;
        material.emissiveColor = glm::vec3(0.0f);
        material.emissiveIntensity = 0.0f;
        material.roughness = 0.5f;
        material.metallic = 0.0f;
        if (!primitive.materialIndex.has_value()) {
            return material;
        }
        const std::size_t index = *primitive.materialIndex;
        if (index >= asset_.materials.size()) {
            warn(fmt::format("{}: material index {} out of range; default material used", label, index));
            return material;
        }
        materialsUsed_.insert(index);
        const auto& src = asset_.materials[index];
        const auto& pbr = src.pbrData;
        material.baseColor = {static_cast<float>(pbr.baseColorFactor.x()),
                              static_cast<float>(pbr.baseColorFactor.y()),
                              static_cast<float>(pbr.baseColorFactor.z())};
        material.opacity = static_cast<float>(pbr.baseColorFactor.w());
        material.metallic = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.emissiveColor = toGlm(src.emissiveFactor);
        const bool emissiveFactorZero = material.emissiveColor == glm::vec3(0.0f);
        material.emissiveIntensity = (emissiveFactorZero && !src.emissiveTexture.has_value())
                                         ? 0.0f
                                         : static_cast<float>(src.emissiveStrength);
        material.alphaMode = toAlphaMode(src.alphaMode);
        material.alphaCutoff = static_cast<float>(src.alphaCutoff);
        material.doubleSided = src.doubleSided;
        material.unlit = src.unlit;
        if (src.normalTexture.has_value()) {
            material.normalScale = static_cast<float>(src.normalTexture->scale);
        }
        if (src.occlusionTexture.has_value()) {
            material.occlusionStrength = static_cast<float>(src.occlusionTexture->strength);
        }

        const std::string materialLabel =
            src.name.empty() ? fmt::format("material{}", index) : toStd(src.name);
        if (pbr.baseColorTexture.has_value()) {
            material.baseColorTexture = textureRef(*pbr.baseColorTexture, true, materialLabel, "baseColor");
        }
        if (pbr.metallicRoughnessTexture.has_value()) {
            material.metallicRoughnessTexture =
                textureRef(*pbr.metallicRoughnessTexture, false, materialLabel, "metallicRoughness");
        }
        if (src.normalTexture.has_value()) {
            material.normalTexture = textureRef(*src.normalTexture, false, materialLabel, "normal");
        }
        if (src.emissiveTexture.has_value()) {
            material.emissiveTexture = textureRef(*src.emissiveTexture, true, materialLabel, "emissive");
        }
        if (src.occlusionTexture.has_value()) {
            material.occlusionTexture = textureRef(*src.occlusionTexture, false, materialLabel, "occlusion");
        }
        if (src.transmission || src.volume || src.clearcoat || src.sheen || src.specular || src.iridescence ||
            src.anisotropy) {
            warn(fmt::format("material '{}': advanced KHR_materials_* extensions ignored", materialLabel));
        }
        return material;
    }

    // ---- textures ------------------------------------------------------------------------------

    scene::TextureRef textureRef(const fastgltf::TextureInfo& info, bool srgb,
                                 const std::string& materialLabel, const char* slot) {
        scene::TextureRef ref;
        if (!options_.loadImages) {
            return ref;
        }
        if (info.textureIndex >= asset_.textures.size()) {
            warn(fmt::format("material '{}': {} texture index {} out of range", materialLabel, slot,
                             info.textureIndex));
            return ref;
        }
        const auto& texture = asset_.textures[info.textureIndex];
        if (!texture.imageIndex.has_value()) {
            warn(fmt::format("material '{}': {} texture has no supported image source (KTX2/WebP/DDS only)",
                             materialLabel, slot));
            return ref;
        }
        if (info.texCoordIndex != 0) {
            warn(fmt::format("material '{}': {} texture uses TEXCOORD_{}; only set 0 is supported",
                             materialLabel, slot, info.texCoordIndex));
        }
        if (info.transform) {
            warn(fmt::format("material '{}': {} texture KHR_texture_transform ignored", materialLabel, slot));
        }
        ref.texture = textureFor(*texture.imageIndex, srgb);
        ref.uvSet = 0;
        if (texture.samplerIndex.has_value() && *texture.samplerIndex < asset_.samplers.size()) {
            const auto& sampler = asset_.samplers[*texture.samplerIndex];
            ref.wrapU = toWrap(sampler.wrapS);
            ref.wrapV = toWrap(sampler.wrapT);
            ref.linearFilter =
                !(sampler.magFilter.has_value() && sampler.magFilter.value() == fastgltf::Filter::Nearest);
        }
        return ref;
    }

    scene::TextureId textureFor(std::size_t imageIndex, bool srgb) {
        const auto key = std::make_pair(imageIndex, srgb);
        if (const auto it = textureCache_.find(key); it != textureCache_.end()) {
            return it->second;
        }
        scene::TextureId id = scene::kInvalidTexture;
        if (auto decoded = decodeImage(imageIndex, srgb)) {
            id = local_.addTexture(std::move(*decoded));
        }
        textureCache_.emplace(key, id);
        return id;
    }

    std::optional<scene::TextureData> decodeImage(std::size_t imageIndex, bool srgb) {
        if (imageIndex >= asset_.images.size()) {
            warn(fmt::format("image index {} out of range", imageIndex));
            return std::nullopt;
        }
        const auto& image = asset_.images[imageIndex];
        const std::string name =
            image.name.empty() ? fmt::format("{}/image{}", fileLabel_, imageIndex) : toStd(image.name);

        Result<scene::TextureData> result = fail("");
        if (const auto* bufferView = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
            result = decodeFromBufferView(*bufferView, srgb, name);
        } else if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
            if (uri->fileByteOffset != 0) {
                warn(fmt::format("image '{}': URI byte offsets are not supported", name));
                return std::nullopt;
            }
            result = loadImage(directory_ / uri->uri.fspath(), srgb);
            if (result) {
                result->name = name;
            }
        } else if (const auto bytes = inMemoryBytes(image.data); !bytes.empty()) {
            result = loadImageFromMemory(bytes, srgb, name);
        } else {
            warn(fmt::format("image '{}': unsupported image source; texture skipped", name));
            return std::nullopt;
        }
        if (!result) {
            warn(fmt::format("image '{}' could not be decoded ({}); texture skipped", name,
                             result.error().message));
            return std::nullopt;
        }
        if (result->isHdr()) {
            warn(fmt::format("image '{}': HDR material textures are not supported; texture skipped", name));
            return std::nullopt;
        }
        return std::move(*result);
    }

    Result<scene::TextureData> decodeFromBufferView(const fastgltf::sources::BufferView& source, bool srgb,
                                                    const std::string& name) {
        if (source.bufferViewIndex >= asset_.bufferViews.size()) {
            return fail("bufferView index {} out of range", source.bufferViewIndex);
        }
        const auto& view = asset_.bufferViews[source.bufferViewIndex];
        if (view.bufferIndex >= asset_.buffers.size()) {
            return fail("buffer index {} out of range", view.bufferIndex);
        }
        const auto bytes = inMemoryBytes(asset_.buffers[view.bufferIndex].data);
        if (bytes.empty()) {
            return fail("buffer {} is not resident in memory", view.bufferIndex);
        }
        if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset) {
            return fail("bufferView {} exceeds buffer {} ({} bytes)", source.bufferViewIndex,
                        view.bufferIndex, bytes.size());
        }
        return loadImageFromMemory(bytes.subspan(view.byteOffset, view.byteLength), srgb, name);
    }

    // ---- lights and cameras --------------------------------------------------------------------

    void importLight(const fastgltf::Node& node, const std::string& label, const glm::mat4& world) {
        const std::size_t index = *node.lightIndex;
        if (index >= asset_.lights.size()) {
            warn(fmt::format("node '{}': light index {} out of range", label, index));
            return;
        }
        const auto& src = asset_.lights[index];
        scene::PunctualLight light;
        light.name = src.name.empty() ? label : toStd(src.name);
        light.type = toLightType(src.type);
        light.position = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        light.direction = safeNormalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)),
                                        glm::vec3(0.0f, 0.0f, -1.0f));
        light.color = toGlm(src.color);
        light.intensity = static_cast<float>(src.intensity);
        light.range = static_cast<float>(src.range.value_or(0.0f));
        light.innerConeAngle = static_cast<float>(src.innerConeAngle.value_or(0.0f));
        light.outerConeAngle =
            static_cast<float>(src.outerConeAngle.value_or(std::numbers::pi_v<float> / 4.0f));
        local_.addLight(std::move(light));
    }

    void importCamera(const fastgltf::Node& node, const std::string& label, const glm::mat4& world) {
        const std::size_t index = *node.cameraIndex;
        if (index >= asset_.cameras.size()) {
            warn(fmt::format("node '{}': camera index {} out of range", label, index));
            return;
        }
        const auto& src = asset_.cameras[index];
        const auto* perspective = std::get_if<fastgltf::Camera::Perspective>(&src.camera);
        if (perspective == nullptr) {
            warn(fmt::format("node '{}': orthographic camera ignored", label));
            return;
        }
        scene::Camera camera;
        camera.name = src.name.empty() ? label : toStd(src.name);
        camera.position = glm::vec3(world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const glm::vec3 forward = safeNormalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)),
                                                glm::vec3(0.0f, 0.0f, -1.0f));
        camera.target = camera.position + forward;
        camera.up =
            safeNormalize(glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
        camera.fovYRadians = static_cast<float>(perspective->yfov);
        camera.nearPlane = static_cast<float>(perspective->znear);
        camera.farPlane = static_cast<float>(perspective->zfar.value_or(200.0f));
        local_.cameras.push_back(std::move(camera));
    }

    static glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
        const float len = glm::length(v);
        return len > 1e-12f ? v / len : fallback;
    }

    const fastgltf::Asset& asset_;
    std::filesystem::path directory_;
    const GltfLoadOptions& options_;
    std::string fileLabel_;
    scene::Scene local_;
    std::vector<std::string> warnings_;
    std::map<std::pair<std::size_t, std::size_t>, scene::MeshId> meshCache_;
    std::map<std::pair<std::size_t, bool>, scene::TextureId> textureCache_;
    std::set<std::size_t> materialsUsed_;
};

// Appends everything from `from` into `into`, offsetting mesh and texture ids.
void merge(scene::Scene& from, scene::Scene& into) {
    const auto meshOffset = static_cast<scene::MeshId>(into.meshes.size());
    const auto textureOffset = static_cast<scene::TextureId>(into.textures.size());
    const auto offsetRef = [textureOffset](scene::TextureRef& ref) {
        if (ref.valid()) {
            ref.texture += textureOffset;
        }
    };
    for (auto& entity : from.entities) {
        if (entity.mesh != scene::kInvalidMesh) {
            entity.mesh += meshOffset;
        }
        offsetRef(entity.material.baseColorTexture);
        offsetRef(entity.material.metallicRoughnessTexture);
        offsetRef(entity.material.normalTexture);
        offsetRef(entity.material.emissiveTexture);
        offsetRef(entity.material.occlusionTexture);
    }
    for (auto& mesh : from.meshes) {
        into.addMesh(std::move(mesh));
    }
    for (auto& texture : from.textures) {
        into.addTexture(std::move(texture));
    }
    for (auto& entity : from.entities) {
        into.entities.push_back(std::move(entity));
    }
    for (auto& light : from.lights) {
        into.addLight(std::move(light));
    }
    for (auto& camera : from.cameras) {
        into.cameras.push_back(std::move(camera));
    }
}

} // namespace

Result<GltfLoadSummary> loadGltf(const std::filesystem::path& path, scene::Scene& into,
                                 const GltfLoadOptions& options) {
    const auto start = Clock::now();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("glTF file not found: '{}'", path.string());
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        return fail("cannot read glTF '{}': {}", path.string(), fastgltf::getErrorMessage(data.error()));
    }

    fastgltf::Parser parser(
        fastgltf::Extensions::KHR_lights_punctual | fastgltf::Extensions::KHR_materials_emissive_strength |
        fastgltf::Extensions::KHR_texture_transform | fastgltf::Extensions::KHR_mesh_quantization);
    constexpr auto kOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages |
                              fastgltf::Options::GenerateMeshIndices |
                              fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::AllowDouble;
    auto asset = parser.loadGltf(data.get(), path.parent_path(), kOptions);
    if (asset.error() != fastgltf::Error::None) {
        return fail("failed to parse glTF '{}': {}", path.string(), fastgltf::getErrorMessage(asset.error()));
    }

    Importer importer(asset.get(), path.parent_path(), options, path.stem().string());
    importer.run();

    scene::Scene& local = importer.local();
    GltfLoadSummary summary;
    summary.meshes = local.meshes.size();
    summary.entities = local.entities.size();
    summary.materials = importer.materialCount();
    summary.textures = local.textures.size();
    summary.lights = local.lights.size();
    summary.cameras = local.cameras.size();
    std::tie(summary.boundsMin, summary.boundsMax) = local.bounds();
    summary.warnings = std::move(importer.warnings());

    merge(local, into);

    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    for (const auto& warning : summary.warnings) {
        log::warn("glTF '{}': {}", path.filename().string(), warning);
    }
    log::info(
        "loaded glTF '{}': {} meshes, {} entities, {} materials, {} textures, {} lights, {} cameras, bounds "
        "[{:.3f} {:.3f} {:.3f}]..[{:.3f} {:.3f} {:.3f}], {} warning(s), {:.1f} ms",
        path.filename().string(), summary.meshes, summary.entities, summary.materials, summary.textures,
        summary.lights, summary.cameras, static_cast<double>(summary.boundsMin.x),
        static_cast<double>(summary.boundsMin.y), static_cast<double>(summary.boundsMin.z),
        static_cast<double>(summary.boundsMax.x), static_cast<double>(summary.boundsMax.y),
        static_cast<double>(summary.boundsMax.z), summary.warnings.size(), elapsed);
    return summary;
}

} // namespace avgen::assets
