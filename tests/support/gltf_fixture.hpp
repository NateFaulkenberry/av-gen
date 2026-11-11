#pragma once

// In-memory GLB fixtures for scene tests (ADR-009): small, deterministic files written to the
// temp directory so tests need no binary assets in the repository.

#include "support/temp_dir.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace avgen::testsupport {

// Minimal GLB: one node at (2, 0, 0) with a unit right triangle, one material (emissive 1,
// roughness 0.6), one point light "lamp" at (0, 3, 0). Returns the file path.
inline std::filesystem::path writeTriangleGlb(const std::string& name) {
    std::vector<float> positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::vector<float> normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    std::vector<std::uint16_t> indices = {0, 1, 2, 0};
    std::vector<std::uint8_t> bin;
    auto append = [&](const void* data, std::size_t bytes) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        while (bin.size() % 4 != 0) {
            bin.push_back(0);
        }
    };
    append(positions.data(), positions.size() * 4);
    append(normals.data(), normals.size() * 4);
    append(indices.data(), indices.size() * 2);

    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
"nodes":[{"mesh":0,"translation":[2,0,0],"name":"tri"},{"name":"lamp","translation":[0,3,0],"extensions":{"KHR_lights_punctual":{"light":0}}}],
"extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[{"type":"point","intensity":7.0,"color":[1,0.5,0.25]}]}},
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],
"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.1,0.2,0.3,1.0],"metallicFactor":0.0,"roughnessFactor":0.6},"emissiveFactor":[1,1,1]}],
"buffers":[{"byteLength":)" +
                       std::to_string(bin.size()) + R"(}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }

    std::vector<std::uint8_t> glb;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            glb.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    };
    u32(0x46546C67u); // "glTF"
    u32(2);
    u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    u32(static_cast<std::uint32_t>(json.size()));
    u32(0x4E4F534Au); // JSON
    glb.insert(glb.end(), json.begin(), json.end());
    u32(static_cast<std::uint32_t>(bin.size()));
    u32(0x004E4942u); // BIN
    glb.insert(glb.end(), bin.begin(), bin.end());

    // Per-process: a fixed name here is shared between every concurrently running test process.
    const auto path = processTempDir() / ("avgen_" + name + ".glb");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    return path;
}

// One mesh of a multi-mesh GLB fixture: its own node and its own named material. `skinned` gives it a one-joint skin
// (every vertex fully on that joint, the identity bind), which is what makes the loader produce a rigged entity.
struct GlbMesh {
    std::string node;
    std::string material;
    glm::vec3 color{0.8f};
    float roughness = 0.5f;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<std::uint32_t> indices;
    bool skinned = false;
};

// An axis-aligned box, 24 vertices (each face its own, so the normals are the face's), outward and counter-clockwise.
inline void appendBox(GlbMesh& mesh, glm::vec3 lo, glm::vec3 hi) {
    const glm::vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    struct Face { int a, b, cc, d; glm::vec3 n; };
    const Face faces[6] = {{4, 5, 6, 7, {0, 0, 1}},  {1, 0, 3, 2, {0, 0, -1}}, {5, 1, 2, 6, {1, 0, 0}},
                           {0, 4, 7, 3, {-1, 0, 0}}, {7, 6, 2, 3, {0, 1, 0}},  {0, 1, 5, 4, {0, -1, 0}}};
    for (const Face& f : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        for (const int k : {f.a, f.b, f.cc, f.d}) {
            mesh.positions.push_back(c[k]);
            mesh.normals.push_back(f.n);
        }
        for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + k);
        }
    }
}

// A flat rectangle in the XZ plane at height `y`, facing +Y.
inline void appendPlane(GlbMesh& mesh, glm::vec2 lo, glm::vec2 hi, float y) {
    const auto base = static_cast<std::uint32_t>(mesh.positions.size());
    for (const glm::vec3 p : {glm::vec3(lo.x, y, hi.y), glm::vec3(hi.x, y, hi.y), glm::vec3(hi.x, y, lo.y),
                              glm::vec3(lo.x, y, lo.y)}) {
        mesh.positions.push_back(p);
        mesh.normals.emplace_back(0.0f, 1.0f, 0.0f);
    }
    for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) {
        mesh.indices.push_back(base + k);
    }
}

// A GLB of `meshes`, one node, mesh and material each, written to the per-process temp directory. Returns the path.
inline std::filesystem::path writeMeshesGlb(const std::string& name, std::span<const GlbMesh> meshes) {
    std::vector<std::uint8_t> bin;
    nlohmann::json views = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    const auto append = [&](const void* data, std::size_t bytes) {
        const auto offset = bin.size();
        const auto* p = static_cast<const std::uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        while (bin.size() % 4 != 0) {
            bin.push_back(0);
        }
        views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes}});
        return views.size() - 1;
    };
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json gltfMeshes = nlohmann::json::array();
    nlohmann::json materials = nlohmann::json::array();
    nlohmann::json skins = nlohmann::json::array();
    nlohmann::json roots = nlohmann::json::array();
    for (const GlbMesh& m : meshes) {
        glm::vec3 lo(1e30f);
        glm::vec3 hi(-1e30f);
        for (const glm::vec3& p : m.positions) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const auto count = m.positions.size();
        const auto pos = append(m.positions.data(), count * sizeof(glm::vec3));
        accessors.push_back({{"bufferView", pos}, {"componentType", 5126}, {"count", count}, {"type", "VEC3"},
                             {"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}});
        const auto posAccessor = accessors.size() - 1;
        accessors.push_back({{"bufferView", append(m.normals.data(), count * sizeof(glm::vec3))},
                             {"componentType", 5126}, {"count", count}, {"type", "VEC3"}});
        const auto normalAccessor = accessors.size() - 1;
        accessors.push_back({{"bufferView", append(m.indices.data(), m.indices.size() * 4)},
                             {"componentType", 5125}, {"count", m.indices.size()}, {"type", "SCALAR"}});
        const auto indexAccessor = accessors.size() - 1;
        nlohmann::json attributes{{"POSITION", posAccessor}, {"NORMAL", normalAccessor}};
        if (m.skinned) {
            const std::vector<std::uint8_t> joints(count * 4, 0);
            std::vector<float> weights(count * 4, 0.0f);
            for (std::size_t v = 0; v < count; ++v) {
                weights[v * 4] = 1.0f;
            }
            accessors.push_back({{"bufferView", append(joints.data(), joints.size())},
                                 {"componentType", 5121}, {"count", count}, {"type", "VEC4"}});
            attributes["JOINTS_0"] = accessors.size() - 1;
            accessors.push_back({{"bufferView", append(weights.data(), weights.size() * 4)},
                                 {"componentType", 5126}, {"count", count}, {"type", "VEC4"}});
            attributes["WEIGHTS_0"] = accessors.size() - 1;
        }
        materials.push_back({{"name", m.material},
                             {"pbrMetallicRoughness",
                              {{"baseColorFactor", {m.color.r, m.color.g, m.color.b, 1.0}},
                               {"metallicFactor", 0.0},
                               {"roughnessFactor", m.roughness}}}});
        gltfMeshes.push_back({{"primitives", {{{"attributes", attributes}, {"indices", indexAccessor},
                                                {"material", materials.size() - 1}}}}});
        nlohmann::json node{{"name", m.node}, {"mesh", gltfMeshes.size() - 1}};
        if (m.skinned) {
            nodes.push_back({{"name", m.node + "_joint"}});
            skins.push_back({{"joints", {nodes.size() - 1}}});
            roots.push_back(nodes.size() - 1);
            node["skin"] = skins.size() - 1;
        }
        nodes.push_back(std::move(node));
        roots.push_back(nodes.size() - 1);
    }
    nlohmann::json doc{{"asset", {{"version", "2.0"}}}, {"scene", 0}, {"scenes", {{{"nodes", roots}}}},
                       {"nodes", nodes}, {"meshes", gltfMeshes}, {"materials", materials},
                       {"buffers", {{{"byteLength", bin.size()}}}}, {"bufferViews", views},
                       {"accessors", accessors}};
    if (!skins.empty()) {
        doc["skins"] = skins;
    }
    std::string json = doc.dump();
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<std::uint8_t> glb;
    const auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            glb.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    };
    u32(0x46546C67u); // "glTF"
    u32(2);
    u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    u32(static_cast<std::uint32_t>(json.size()));
    u32(0x4E4F534Au); // JSON
    glb.insert(glb.end(), json.begin(), json.end());
    u32(static_cast<std::uint32_t>(bin.size()));
    u32(0x004E4942u); // BIN
    glb.insert(glb.end(), bin.begin(), bin.end());
    const auto path = processTempDir() / ("avgen_" + name + ".glb");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    return path;
}

} // namespace avgen::testsupport
