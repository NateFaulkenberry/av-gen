#pragma once

// A copy of `shaders/water.wgsl` with one marked region rewritten, in a scratch directory of its own,
// for the water tests that need a second shader to compare against (ADR-914, 915, 916).
//
// The comparison arm is *derived from the live shader* rather than frozen beside it. A frozen copy
// is right on the day it is taken and wrong on the first day anyone edits the surface for a reason
// of their own -- then it fails a test that has nothing to do with the edit, or it is re-frozen by
// someone who does not know what it was guarding. A transform of the live file keeps guarding the
// one thing it names, whatever else the file has become.
//
// Each transform names its markers exactly and REQUIREs that it found them, so a marker that was
// renamed or deleted fails loudly instead of quietly comparing the shader with itself.
//
// A ShaderLibrary searches its directories in order and resolves `#include` through the same search
// (`ShaderLibrary::locate`), so a library over `{variant dir, AVGEN_SHADER_SOURCE_DIR}` takes this
// file's `water.wgsl` and every other module -- including everything `water.wgsl` includes -- from
// the working tree.

#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace avgen::testsupport {

inline std::string readWaterShaderSource() {
    const std::filesystem::path path = std::filesystem::path(AVGEN_SHADER_SOURCE_DIR) / "water.wgsl";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Removes every line from one carrying `begin` through the next line carrying `end`, inclusive.
// REQUIREs at least one such block, and that every block closes.
inline std::string stripMarkedBlocks(const std::string& source, const std::string& begin, const std::string& end,
                                     int* blocks = nullptr) {
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    bool inside = false;
    int found = 0;
    while (std::getline(in, line)) {
        if (!inside && line.find(begin) != std::string::npos) {
            inside = true;
            ++found;
            continue;
        }
        if (inside) {
            if (line.find(end) != std::string::npos) {
                inside = false;
            }
            continue;
        }
        out << line << '\n';
    }
    INFO("markers '" << begin << "' / '" << end << "'");
    REQUIRE(found > 0);
    REQUIRE_FALSE(inside);
    if (blocks != nullptr) {
        *blocks = found;
    }
    return out.str();
}

// Replaces the lines strictly between the (single) `begin` and `end` marker lines with `body`.
inline std::string replaceMarkedBody(const std::string& source, const std::string& begin, const std::string& end,
                                     const std::string& body) {
    const std::size_t b = source.find(begin);
    INFO("marker '" << begin << "'");
    REQUIRE(b != std::string::npos);
    REQUIRE(source.find(begin, b + begin.size()) == std::string::npos); // exactly one
    const std::size_t bodyStart = source.find('\n', b);
    REQUIRE(bodyStart != std::string::npos);
    const std::size_t e = source.find(end, bodyStart);
    REQUIRE(e != std::string::npos);
    const std::size_t endLine = source.rfind('\n', e);
    REQUIRE(endLine != std::string::npos);
    REQUIRE(endLine >= bodyStart);
    return source.substr(0, bodyStart + 1) + body + source.substr(endLine + 1);
}

// Replaces the one occurrence of `from` with `to`. REQUIREs exactly one.
inline std::string replaceOnce(const std::string& source, const std::string& from, const std::string& to) {
    const std::size_t at = source.find(from);
    INFO("text '" << from << "'");
    REQUIRE(at != std::string::npos);
    REQUIRE(source.find(from, at + from.size()) == std::string::npos);
    return source.substr(0, at) + to + source.substr(at + from.size());
}

// Writes `source` as `water.wgsl` in a scratch directory named `name` and returns the directory.
inline std::filesystem::path writeWaterShaderVariant(const std::string& name, const std::string& source) {
    const std::filesystem::path dir = processTempDir() / ("water-variant-" + name);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / "water.wgsl", std::ios::trunc);
    out << source;
    out.close();
    REQUIRE(out.good());
    return dir;
}

} // namespace avgen::testsupport
