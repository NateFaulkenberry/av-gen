#pragma once

// Loads WGSL shader files from disk (ADR-006), resolves a simple `#include "file.wgsl"` line
// convention, and creates shader modules with compilation diagnostics surfaced as Errors.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::gpu {

class Context;

class ShaderLibrary {
public:
    // searchDirs are tried in order; the first directory containing the file wins.
    explicit ShaderLibrary(Context& context, std::vector<std::filesystem::path> searchDirs);

    // Resolves includes and returns the full WGSL source.
    [[nodiscard]] Result<std::string> loadSource(const std::string& fileName) const;

    // Loads, includes, compiles. On WGSL errors returns an Error with file/line diagnostics.
    [[nodiscard]] Result<wgpu::ShaderModule> load(const std::string& fileName) const;

    // Compiles WGSL source directly (tests, generated shaders).
    [[nodiscard]] Result<wgpu::ShaderModule> compile(const std::string& source, const std::string& label) const;

    [[nodiscard]] Result<std::filesystem::path> locate(const std::string& fileName) const;
    [[nodiscard]] const std::vector<std::filesystem::path>& searchDirs() const { return searchDirs_; }

    // Default search order: $AVGEN_SHADER_DIR, <executable dir>/shaders, <executable dir>/../shaders,
    // the compile-time source tree directory.
    static std::vector<std::filesystem::path> defaultSearchDirs(const std::filesystem::path& executablePath);

private:
    Result<std::string> resolveIncludes(const std::string& source, const std::filesystem::path& origin, int depth) const;

    Context& context_;
    std::vector<std::filesystem::path> searchDirs_;
};

} // namespace avgen::gpu
