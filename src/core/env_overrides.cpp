#include "core/env_overrides.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

extern char** environ; // POSIX; declared by no header on macOS

namespace avgen::core {

std::vector<std::pair<std::string, std::string>> avgenEnvironment() {
    std::vector<std::pair<std::string, std::string>> out;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        const std::string_view entry(*e);
        if (!entry.starts_with("AVGEN_")) {
            continue;
        }
        // tools/gpu-lock.sh's own settings (the wait, the holder's name): read by the script, never by avgen
        if (entry.starts_with("AVGEN_GPU_LOCK_") || entry.starts_with("AVGEN_AGENT=")) {
            continue;
        }
        const auto eq = entry.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        out.emplace_back(std::string(entry.substr(0, eq)), std::string(entry.substr(eq + 1)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string avgenEnvironmentLine() {
    std::string line;
    for (const auto& [name, value] : avgenEnvironment()) {
        if (!line.empty()) {
            line += ' ';
        }
        line += name + '=' + value;
    }
    return line;
}

} // namespace avgen::core
