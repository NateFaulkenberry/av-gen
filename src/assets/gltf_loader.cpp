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
        importRigs();
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
        // ADR-086 replaced the two warnings that used to live here ("animations ignored", "skins
        // ignored"). An animation that targets no joint of any skin is still dropped, and says so
        // per animation in importRigs(), because this engine animates rigs and nothing else yet.
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

    // ---- skins and animations (ADR-086) ---------------------------------------------------------

    // A rig per skin. The joints a skin names are not the whole hierarchy that drives it: their
    // ancestors carry transforms too, and an exporter that wraps a Mixamo skeleton in an armature
    // node animates *that* node's translation. Keeping only the skin's own joints would import a
    // character whose limbs move and whose body never leaves the origin, silently.
    void importRigs() {
        if (asset_.skins.empty()) {
            if (!asset_.animations.empty()) {
                warn(fmt::format("{} animation(s) ignored: the file has no skin to drive",
                                 asset_.animations.size()));
            }
            return;
        }
        std::vector<int> parent(asset_.nodes.size(), -1);
        for (std::size_t i = 0; i < asset_.nodes.size(); ++i) {
            for (const std::size_t child : asset_.nodes[i].children) {
                if (child < parent.size()) {
                    parent[child] = static_cast<int>(i);
                }
            }
        }
        nodeToJoint_.assign(asset_.skins.size(), {});
        for (std::size_t s = 0; s < asset_.skins.size(); ++s) {
            auto rig = buildRig(s, parent);
            if (!rig) {
                rigForSkin_.push_back(-1);
                continue;
            }
            rigForSkin_.push_back(static_cast<int>(local_.rigs.size()));
            local_.rigs.push_back(std::move(*rig));
        }
        importClips();
        for (scene::SkinnedRig& rig : local_.rigs) {
            rig.addDefaultStates();
        }
    }

    std::optional<scene::SkinnedRig> buildRig(std::size_t skinIndex, const std::vector<int>& parent) {
        const auto& skin = asset_.skins[skinIndex];
        const std::string label = skin.name.empty() ? fmt::format("skin{}", skinIndex) : toStd(skin.name);
        if (skin.joints.empty()) {
            warn(fmt::format("skin '{}': no joints; skin skipped", label));
            return std::nullopt;
        }
        if (skin.joints.size() > scene::kMaxPaletteJoints) {
            warn(fmt::format("skin '{}': {} joints exceeds the {} the palette holds; skin skipped", label,
                             skin.joints.size(), scene::kMaxPaletteJoints));
            return std::nullopt;
        }
        // Every joint, and every ancestor of one, up to the root.
        std::vector<bool> needed(asset_.nodes.size(), false);
        for (const std::size_t joint : skin.joints) {
            if (joint >= asset_.nodes.size()) {
                warn(fmt::format("skin '{}': joint index {} out of range; skin skipped", label, joint));
                return std::nullopt;
            }
            for (int n = static_cast<int>(joint); n >= 0; n = parent[static_cast<std::size_t>(n)]) {
                if (needed[static_cast<std::size_t>(n)]) {
                    break; // this branch is already marked all the way up
                }
                needed[static_cast<std::size_t>(n)] = true;
            }
        }
        // Depth-first from the roots, in index order, so a parent always precedes its children and
        // two loads of the same file produce the same joint order.
        std::vector<int>& order = nodeToJoint_[skinIndex];
        order.assign(asset_.nodes.size(), -1);
        scene::SkinnedRig rig;
        rig.name = options_.namePrefix + label;
        std::vector<std::size_t> stack;
        for (std::size_t i = asset_.nodes.size(); i-- > 0;) {
            if (parent[i] < 0) {
                stack.push_back(i);
            }
        }
        while (!stack.empty()) {
            const std::size_t node = stack.back();
            stack.pop_back();
            if (needed[node]) {
                scene::Joint joint;
                joint.name = nodeLabel(asset_.nodes[node]);
                joint.parent = parent[node] >= 0 ? order[static_cast<std::size_t>(parent[node])] : -1;
                joint.rest = restTransform(asset_.nodes[node]);
                order[node] = static_cast<int>(rig.skeleton.joints.size());
                rig.skeleton.joints.push_back(std::move(joint));
            }
            const auto& children = asset_.nodes[node].children;
            for (std::size_t c = children.size(); c-- > 0;) {
                if (children[c] < asset_.nodes.size()) {
                    stack.push_back(children[c]);
                }
            }
        }
        rig.skeleton.name = label;
        rig.skeleton.palette.reserve(skin.joints.size());
        for (const std::size_t joint : skin.joints) {
            rig.skeleton.palette.push_back(static_cast<std::uint32_t>(order[joint]));
        }
        rig.skeleton.inverseBind.assign(skin.joints.size(), glm::mat4(1.0f));
        if (skin.inverseBindMatrices.has_value() && *skin.inverseBindMatrices < asset_.accessors.size()) {
            const auto& accessor = asset_.accessors[*skin.inverseBindMatrices];
            if (accessor.type == fastgltf::AccessorType::Mat4 && accessor.count >= skin.joints.size()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
                    asset_, accessor, [&](fastgltf::math::fmat4x4 m, std::size_t i) {
                        if (i < rig.skeleton.inverseBind.size()) {
                            rig.skeleton.inverseBind[i] = toGlm(m);
                        }
                    });
            } else {
                warn(fmt::format("skin '{}': inverseBindMatrices malformed; identity binds used", label));
            }
        }
        if (!rig.skeleton.valid()) {
            warn(fmt::format("skin '{}': hierarchy could not be ordered; skin skipped", label));
            return std::nullopt;
        }
        rig.pose = scene::restPose(rig.skeleton);
        scene::skinningPalette(rig.skeleton, rig.pose, rig.scratchModel, rig.palette);
        rig.previousPalette = rig.palette;
        return rig;
    }

    static scene::Transform restTransform(const fastgltf::Node& node) {
        scene::Transform out;
        // Options::DecomposeNodeMatrices guarantees TRS; the matrix branch is the belt and braces.
        if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
            out.position = {trs->translation.x(), trs->translation.y(), trs->translation.z()};
            out.rotation = glm::quat(trs->rotation.w(), trs->rotation.x(), trs->rotation.y(), trs->rotation.z());
            out.scale = {trs->scale.x(), trs->scale.y(), trs->scale.z()};
            return out;
        }
        if (const auto* m = std::get_if<fastgltf::math::fmat4x4>(&node.transform)) {
            return scene::Transform::fromMatrix(toGlm(*m));
        }
        return out;
    }

    void importClips() {
        for (std::size_t a = 0; a < asset_.animations.size(); ++a) {
            const auto& animation = asset_.animations[a];
            const std::string name =
                animation.name.empty() ? fmt::format("animation{}", a) : toStd(animation.name);
            std::size_t placed = 0;
            for (std::size_t s = 0; s < rigForSkin_.size(); ++s) {
                if (rigForSkin_[s] < 0) {
                    continue;
                }
                scene::AnimationClip clip = buildClip(animation, name, s);
                if (clip.channels.empty()) {
                    continue;
                }
                local_.rigs[static_cast<std::size_t>(rigForSkin_[s])].clips.push_back(std::move(clip));
                ++placed;
            }
            if (placed == 0) {
                warn(fmt::format("animation '{}': no channel targets a joint of any skin; clip dropped",
                                 name));
            }
        }
    }

    scene::AnimationClip buildClip(const fastgltf::Animation& animation, const std::string& name,
                                   std::size_t skinIndex) {
        scene::AnimationClip clip;
        clip.name = name;
        const std::vector<int>& order = nodeToJoint_[skinIndex];
        for (const auto& channel : animation.channels) {
            if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= order.size()) {
                continue;
            }
            const int joint = order[*channel.nodeIndex];
            if (joint < 0 || channel.samplerIndex >= animation.samplers.size()) {
                continue;
            }
            const auto& sampler = animation.samplers[channel.samplerIndex];
            if (sampler.inputAccessor >= asset_.accessors.size() ||
                sampler.outputAccessor >= asset_.accessors.size()) {
                continue;
            }
            const auto& input = asset_.accessors[sampler.inputAccessor];
            const auto& output = asset_.accessors[sampler.outputAccessor];
            if (input.type != fastgltf::AccessorType::Scalar || input.count == 0) {
                warn(fmt::format("animation '{}': sampler input is not a scalar keyframe list; channel dropped",
                                 name));
                continue;
            }
            scene::AnimationChannel out;
            out.joint = static_cast<std::uint32_t>(joint);
            switch (channel.path) {
            case fastgltf::AnimationPath::Rotation:
                out.path = scene::AnimationPath::Rotation;
                break;
            case fastgltf::AnimationPath::Scale:
                out.path = scene::AnimationPath::Scale;
                break;
            case fastgltf::AnimationPath::Translation:
                out.path = scene::AnimationPath::Translation;
                break;
            default:
                // Morph-target weights: this engine has no morph targets, and convertPrimitive
                // already warns that the targets themselves were ignored.
                continue;
            }
            switch (sampler.interpolation) {
            case fastgltf::AnimationInterpolation::Step:
                out.interpolation = scene::Interpolation::Step;
                break;
            case fastgltf::AnimationInterpolation::CubicSpline:
                out.interpolation = scene::Interpolation::CubicSpline;
                break;
            case fastgltf::AnimationInterpolation::Linear:
                out.interpolation = scene::Interpolation::Linear;
                break;
            }
            out.times.resize(input.count);
            fastgltf::iterateAccessorWithIndex<float>(asset_, input,
                                                      [&](float t, std::size_t i) { out.times[i] = t; });
            const std::size_t perKey = out.interpolation == scene::Interpolation::CubicSpline ? 3 : 1;
            const std::size_t expected = input.count * perKey;
            if (output.count != expected) {
                warn(fmt::format("animation '{}': sampler output has {} values for {} keys; channel dropped",
                                 name, output.count, input.count));
                continue;
            }
            out.values.assign(expected, glm::vec4(0.0f));
            if (out.path == scene::AnimationPath::Rotation) {
                if (output.type != fastgltf::AccessorType::Vec4) {
                    warn(fmt::format("animation '{}': rotation sampler is not vec4; channel dropped", name));
                    continue;
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                    asset_, output, [&](fastgltf::math::fvec4 v, std::size_t i) {
                        out.values[i] = {v.x(), v.y(), v.z(), v.w()};
                    });
            } else {
                if (output.type != fastgltf::AccessorType::Vec3) {
                    warn(fmt::format("animation '{}': {} sampler is not vec3; channel dropped", name,
                                     scene::animationPathName(out.path)));
                    continue;
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                    asset_, output, [&](fastgltf::math::fvec3 v, std::size_t i) {
                        out.values[i] = {v.x(), v.y(), v.z(), 0.0f};
                    });
            }
            if (!out.valid()) {
                warn(fmt::format("animation '{}': keyframe times are not ascending; channel dropped", name));
                continue;
            }
            // Both ends of the range the keys cover. `start` is what Blender's exporter leaves at
            // 1/30 s on a take authored from frame 1, and it is the clip's loop origin.
            clip.start = clip.channels.empty() ? out.times.front()
                                               : std::min(clip.start, out.times.front());
            clip.duration = std::max(clip.duration, out.times.back());
            clip.channels.push_back(std::move(out));
        }
        return clip;
    }

    // ---- meshes --------------------------------------------------------------------------------

    // The rig a mesh node's skin resolves to, or kInvalidRig when it has none (or its skin was
    // rejected, in which case the mesh is still imported and simply renders in its bind pose).
    [[nodiscard]] scene::RigId rigFor(const fastgltf::Node& node) const {
        if (!node.skinIndex.has_value() || *node.skinIndex >= rigForSkin_.size()) {
            return scene::kInvalidRig;
        }
        const int rig = rigForSkin_[*node.skinIndex];
        return rig < 0 ? scene::kInvalidRig : static_cast<scene::RigId>(rig);
    }

    void importMeshNode(const fastgltf::Node& node, const std::string& label, const glm::mat4& world) {
        const std::size_t meshIndex = *node.meshIndex;
        if (meshIndex >= asset_.meshes.size()) {
            warn(fmt::format("node '{}': mesh index {} out of range", label, meshIndex));
            return;
        }
        const auto& mesh = asset_.meshes[meshIndex];
        // ADR-086 / glTF 2.0 "Skins": a skinned mesh node's own transform is ignored. The joint
        // matrices already carry the chain from the file's scene root, so applying the node
        // transform as well would apply it twice; the entity's transform is left identity for
        // whatever places the character in the world.
        const scene::RigId rig = rigFor(node);
        const scene::Transform transform =
            rig == scene::kInvalidRig ? scene::Transform::fromMatrix(world) : scene::Transform{};
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
            entity.rig = rig;
            entity.material = materialFor(primitive, label, &entity.materialName);
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
        readSkinInfluences(primitive, data, where);
        return data;
    }

    // ADR-086: JOINTS_0 / WEIGHTS_0 into MeshData::skin. Only the first influence set is read;
    // JOINTS_1 and beyond are warned about and dropped, which costs a vertex its fifth-heaviest
    // influence and never its heaviest, because glTF orders them by weight.
    void readSkinInfluences(const fastgltf::Primitive& primitive, scene::MeshData& data,
                            const std::string& where) {
        const auto* joints = accessorFor(primitive, "JOINTS_0", fastgltf::AccessorType::Vec4);
        const auto* weights = accessorFor(primitive, "WEIGHTS_0", fastgltf::AccessorType::Vec4);
        if (joints == nullptr || weights == nullptr) {
            if (joints != nullptr || weights != nullptr) {
                warn(fmt::format("{}: JOINTS_0 without WEIGHTS_0 (or the reverse); mesh left unskinned",
                                 where));
            }
            return;
        }
        if (joints->count != data.vertices.size() || weights->count != data.vertices.size()) {
            warn(fmt::format("{}: skin attributes do not match the vertex count; mesh left unskinned", where));
            return;
        }
        if (primitive.findAttribute("JOINTS_1") != primitive.attributes.cend()) {
            warn(fmt::format("{}: more than four influences per vertex; JOINTS_1 and beyond ignored", where));
        }
        data.skin.assign(data.vertices.size(), scene::SkinInfluence{});
        bool clamped = false;
        fastgltf::iterateAccessorWithIndex<fastgltf::math::u32vec4>(
            asset_, *joints, [&](fastgltf::math::u32vec4 j, std::size_t i) {
                for (std::size_t k = 0; k < 4; ++k) {
                    const std::uint32_t index = j[k];
                    clamped = clamped || index >= scene::kMaxPaletteJoints;
                    data.skin[i].joints[k] =
                        static_cast<std::uint16_t>(std::min<std::uint32_t>(index, scene::kMaxPaletteJoints - 1));
                }
            });
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset_, *weights, [&](fastgltf::math::fvec4 w, std::size_t i) {
                glm::vec4 v(w.x(), w.y(), w.z(), w.w());
                v = glm::max(v, glm::vec4(0.0f));
                const float sum = v.x + v.y + v.z + v.w;
                // Exporters drift, and an unnormalised weight shrinks or inflates the whole
                // vertex rather than distorting it locally, which reads as the mesh collapsing.
                data.skin[i].weights = sum > 1e-6f ? v / sum : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            });
        if (clamped) {
            warn(fmt::format("{}: joint index beyond the {}-joint palette; clamped", where,
                             scene::kMaxPaletteJoints));
        }
    }

    // ---- materials -----------------------------------------------------------------------------

    scene::Material materialFor(const fastgltf::Primitive& primitive, const std::string& label,
                                std::string* nameOut = nullptr) {
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
        if (nameOut != nullptr) {
            *nameOut = materialLabel;
        }
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
    // ADR-086: skin index -> rig index in `local_`, and per skin, node index -> joint index.
    std::vector<int> rigForSkin_;
    std::vector<std::vector<int>> nodeToJoint_;
};

// Appends everything from `from` into `into`, offsetting mesh and texture ids.
void merge(scene::Scene& from, scene::Scene& into) {
    const auto meshOffset = static_cast<scene::MeshId>(into.meshes.size());
    const auto textureOffset = static_cast<scene::TextureId>(into.textures.size());
    const auto rigOffset = static_cast<scene::RigId>(into.rigs.size());
    const auto offsetRef = [textureOffset](scene::TextureRef& ref) {
        if (ref.valid()) {
            ref.texture += textureOffset;
        }
    };
    for (auto& entity : from.entities) {
        if (entity.mesh != scene::kInvalidMesh) {
            entity.mesh += meshOffset;
        }
        if (entity.rig != scene::kInvalidRig) {
            entity.rig += rigOffset;
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
    for (auto& rig : from.rigs) {
        into.rigs.push_back(std::move(rig));
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
    summary.rigs = local.rigs.size();
    for (const auto& rig : local.rigs) {
        summary.joints += rig.skeleton.jointCount();
        summary.clips += rig.clips.size();
    }
    std::tie(summary.boundsMin, summary.boundsMax) = local.bounds();
    summary.warnings = std::move(importer.warnings());

    merge(local, into);

    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    for (const auto& warning : summary.warnings) {
        log::warn("glTF '{}': {}", path.filename().string(), warning);
    }
    log::info(
        "loaded glTF '{}': {} meshes, {} entities, {} materials, {} textures, {} lights, {} cameras, "
        "{} rig(s)/{} joints/{} clips, bounds "
        "[{:.3f} {:.3f} {:.3f}]..[{:.3f} {:.3f} {:.3f}], {} warning(s), {:.1f} ms",
        path.filename().string(), summary.meshes, summary.entities, summary.materials, summary.textures,
        summary.lights, summary.cameras, summary.rigs, summary.joints, summary.clips,
        static_cast<double>(summary.boundsMin.x),
        static_cast<double>(summary.boundsMin.y), static_cast<double>(summary.boundsMin.z),
        static_cast<double>(summary.boundsMax.x), static_cast<double>(summary.boundsMax.y),
        static_cast<double>(summary.boundsMax.z), summary.warnings.size(), elapsed);
    return summary;
}

} // namespace avgen::assets
