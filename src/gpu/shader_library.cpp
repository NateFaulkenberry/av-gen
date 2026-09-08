#include "gpu/shader_library.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace avgen::gpu {

ShaderLibrary::ShaderLibrary(Context& context, std::vector<std::filesystem::path> searchDirs)
    : context_(context), searchDirs_(std::move(searchDirs)) {}

std::vector<std::filesystem::path> ShaderLibrary::defaultSearchDirs(const std::filesystem::path& executablePath) {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_SHADER_DIR")) {
        dirs.emplace_back(env);
    }
    const auto exeDir = executablePath.parent_path();
    dirs.push_back(exeDir / "shaders");
    dirs.push_back(exeDir / ".." / "shaders");
    dirs.push_back(exeDir / ".." / "Resources" / "shaders"); // app bundle layout
#ifdef AVGEN_SHADER_SOURCE_DIR
    dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
#endif
    return dirs;
}

Result<std::filesystem::path> ShaderLibrary::locate(const std::string& fileName) const {
    for (const auto& dir : searchDirs_) {
        std::error_code ec;
        const auto candidate = dir / fileName;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    std::string tried;
    for (const auto& dir : searchDirs_) {
        tried += "\n  " + dir.string();
    }
    return fail("shader '{}' not found in search directories:{}", fileName, tried);
}

Result<std::string> ShaderLibrary::resolveIncludes(const std::string& source, const std::filesystem::path& origin,
                                                   int depth) const {
    if (depth > 8) {
        return fail("shader include depth exceeded in '{}'", origin.string());
    }
    std::ostringstream out;
    std::istringstream in(source);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            const auto q1 = line.find('"', pos);
            const auto q2 = q1 == std::string::npos ? std::string::npos : line.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) {
                return fail("{}:{}: malformed #include", origin.string(), lineNo);
            }
            const std::string includeName = line.substr(q1 + 1, q2 - q1 - 1);
            auto path = locate(includeName);
            if (!path) {
                return fail("{}:{}: {}", origin.string(), lineNo, path.error().message);
            }
            std::ifstream file(*path);
            if (!file) {
                return fail("cannot read shader include '{}'", path->string());
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            auto resolved = resolveIncludes(buffer.str(), *path, depth + 1);
            if (!resolved) {
                return resolved;
            }
            out << "// ---- begin include " << includeName << "\n" << *resolved << "\n// ---- end include "
                << includeName << "\n";
        } else {
            out << line << '\n';
        }
    }
    return out.str();
}

Result<std::string> ShaderLibrary::loadSource(const std::string& fileName) const {
    auto path = locate(fileName);
    if (!path) {
        return std::unexpected(path.error());
    }
    std::ifstream file(*path);
    if (!file) {
        return fail("cannot read shader '{}'", path->string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return resolveIncludes(buffer.str(), *path, 0);
}

Result<wgpu::ShaderModule> ShaderLibrary::load(const std::string& fileName) const {
    auto source = loadSource(fileName);
    if (!source) {
        return std::unexpected(source.error());
    }
    return compile(*source, fileName);
}

Result<wgpu::ShaderModule> ShaderLibrary::compile(const std::string& source, const std::string& label) const {
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = source.c_str();
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl;
    desc.label = label.c_str();

    // Shader creation errors are reported through the uncaptured error callback; use an error
    // scope so this call's failures are attributed to this shader.
    context_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::ShaderModule module = context_.device().CreateShaderModule(&desc);

    std::string scopeError;
    auto scopeFuture = context_.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
            if (type != wgpu::ErrorType::NoError) {
                scopeError = Context::toString(message);
            }
        });
    context_.waitFor(scopeFuture);

    std::string diagnostics;
    bool hasErrors = !scopeError.empty();
    if (module) {
        auto infoFuture = module.GetCompilationInfo(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
                if (status != wgpu::CompilationInfoRequestStatus::Success || info == nullptr) {
                    return;
                }
                for (std::size_t i = 0; i < info->messageCount; ++i) {
                    const auto& m = info->messages[i];
                    const char* kind = m.type == wgpu::CompilationMessageType::Error     ? "error"
                                       : m.type == wgpu::CompilationMessageType::Warning ? "warning"
                                                                                          : "info";
                    if (m.type == wgpu::CompilationMessageType::Error) {
                        hasErrors = true;
                    }
                    diagnostics += fmt::format("\n  {}:{}:{}: {}: {}", label, m.lineNum, m.linePos, kind,
                                               Context::toString(m.message));
                }
            });
        context_.waitFor(infoFuture);
    }

    if (hasErrors || !module) {
        return fail("shader '{}' failed to compile:{}{}", label, diagnostics,
                    scopeError.empty() ? "" : "\n  " + scopeError);
    }
    if (!diagnostics.empty()) {
        log::warn("shader '{}' compiled with messages:{}", label, diagnostics);
    }
    return module;
}

} // namespace avgen::gpu
