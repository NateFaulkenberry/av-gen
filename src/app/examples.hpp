#pragma once

// Built-in examples (procedural phase): scene/project files shipped in the repository's
// `examples/` folder and listed by `examples/index.json`:
//   { "examples": [ { "name": "The Temple", "description": "...", "category": "Showcase",
//                     "project": "temple/temple.json" } ] }
// Each entry names a project (preferred: it carries the scene, presets, routes and timeline) or a
// scene file, relative to the index. Nothing is compiled in: the showcases are data.
//
// An entry with `"live": true` is a project made to be played (live audio input and/or MIDI; in practice a
// `sonic.live` project, ADR-1025). The File menu lists those in their own Live Projects submenu, beside Open
// Project, rather than among the Examples. `live` is a flag rather than a category because the category is
// also the live scene switcher's set (ADR-1063/1074: "Sonic VFX", "Sonic Abstract"), so a live project keeps
// its family as its category and the menu shows the family as a heading inside Live Projects.

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
    bool live = false;          // `"live": true`: listed under File > Live Projects, not File > Examples
};

// The menu's title for the live projects.
inline constexpr const char* kLiveProjectsMenu = "Live Projects";

// One heading of a menu: a category and its entries (indices into the examples list, in index order).
struct ExampleSection {
    std::string category;
    std::vector<std::size_t> entries;
};

// The headings of one menu: the examples whose `live` equals `live`, grouped by category. Categories come in the
// order each first appears in the index and a category that recurs later joins its first heading, so a menu never
// shows the same heading twice.
[[nodiscard]] std::vector<ExampleSection> exampleSections(const std::vector<ExampleInfo>& examples, bool live);

// Candidate locations for `examples/`: $AVGEN_EXAMPLES_DIR, <exe>/examples, <exe>/../examples,
// <exe>/../../examples (build tree), <exe>/../Resources/examples, and the source tree.
[[nodiscard]] std::vector<std::filesystem::path> exampleSearchDirs(const std::filesystem::path& executablePath);
// Loads the first index found. Missing index = empty list (not an error); malformed = error.
[[nodiscard]] Result<std::vector<ExampleInfo>> loadExamples(const std::vector<std::filesystem::path>& searchDirs);
[[nodiscard]] Result<std::vector<ExampleInfo>> loadExampleIndex(const std::filesystem::path& indexFile);

} // namespace avgen::app
