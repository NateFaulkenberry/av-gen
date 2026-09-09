#pragma once

// Built-in examples (procedural phase): scene/project files shipped in the repository's
// `examples/` folder and listed by `examples/index.json`:
//   { "examples": [ { "name": "The Temple", "description": "...", "category": "Showcase",
//                     "project": "temple/temple.json" } ] }
// Each entry names a project (preferred: it carries the scene, presets, routes and timeline) or a
// scene file, relative to the index. Nothing is compiled in: the showcases are data.

#include "core/error.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::app {

struct ExampleInfo {
    std::string name;
    std::string description;
    std::string category;
    std::filesystem::path file; // absolute path of the project or scene file
};

// Candidate locations for `examples/`: $AVGEN_EXAMPLES_DIR, <exe>/examples, <exe>/../examples,
// <exe>/../../examples (build tree), <exe>/../Resources/examples, and the source tree.
[[nodiscard]] std::vector<std::filesystem::path> exampleSearchDirs(const std::filesystem::path& executablePath);
// Loads the first index found. Missing index = empty list (not an error); malformed = error.
[[nodiscard]] Result<std::vector<ExampleInfo>> loadExamples(const std::vector<std::filesystem::path>& searchDirs);
[[nodiscard]] Result<std::vector<ExampleInfo>> loadExampleIndex(const std::filesystem::path& indexFile);

} // namespace avgen::app
