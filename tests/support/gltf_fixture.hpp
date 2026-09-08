#pragma once

// In-memory GLB fixtures for scene tests (ADR-009): small, deterministic files written to the
// temp directory so tests need no binary assets in the repository.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

    const auto path = std::filesystem::temp_directory_path() / ("avgen_" + name + ".glb");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    return path;
}

} // namespace avgen::testsupport
