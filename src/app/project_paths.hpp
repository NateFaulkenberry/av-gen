#pragma once

// How a project's stored asset path becomes a file: "~/..." is the user's home, an absolute path is itself, and
// anything else is relative to the project's folder. One definition, shared by the project loader (engine.cpp) and
// the examples menu (examples.cpp), so the menu's "is this file here?" asks about the file the open will read.

#include <cstdlib>
#include <filesystem>
#include <string>

namespace avgen::app {

inline std::filesystem::path resolveFrom(const std::string& stored, const std::filesystem::path& baseDir) {
    std::filesystem::path p(stored);
    // "~/..." is the user's home: a project may name a file outside every checkout that way.
    if (stored.size() >= 2 && stored[0] == '~' && (stored[1] == '/' || stored[1] == '\\')) {
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            p = std::filesystem::path(home) / stored.substr(2);
        }
    }
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (baseDir / p).lexically_normal();
}

} // namespace avgen::app
