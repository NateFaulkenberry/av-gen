#pragma once

// Built-in examples (procedural phase): scene/project files shipped in the repository's
// `examples/` folder and listed by `examples/index.json`:
//   { "examples": [ { "name": "The Temple", "description": "...", "category": "Showcase",
//                     "project": "temple/temple.json" } ] }
// Each entry names a project (preferred: it carries the scene, presets, routes and timeline) or a
// scene file, relative to the index. Nothing is compiled in: the showcases are data.
//
// File > Examples has exactly two submenus, Live and Offline (repository cleanup, 2026-10-14). An entry with
// `"live": true` is a project made to be played (live audio input and/or MIDI; in practice a `sonic.live` project,
// ADR-1025) and is listed under Live; every other entry is made to be rendered (a film, a showcase, a look, a lab)
// and is listed under Offline. `live` is a flag rather than a category because the category is also the live scene
// switcher's set (ADR-1063/1074: "Sonic VFX", "Sonic Abstract"), so an entry keeps its family as its category and
// each submenu shows the families as headings.
//
// Nothing is hidden. An entry whose files are not on this machine -- a generated scene a checkout does not have
// until its generator runs (THE RIFT's rift.scene.json), the owner's song -- is listed with what is missing, so a
// reviewer sees that the scene exists and how to make it openable, instead of a menu that silently omits it or an
// open that fails half-way. An optional `"generate"` names the command that makes the missing files.

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
    bool live = false;          // `"live": true`: listed under Examples > Live, else Examples > Offline
    // What the entry cannot open without, read from the project when the index loads: the project or scene file
    // itself and the scene file a project names. Absolute.
    std::vector<std::filesystem::path> required;
    std::filesystem::path audio; // the project's song, if it names one; without it the scene opens silent
    std::string generate;        // index `"generate"`: the command that makes the entry's generated files
};

// File > Examples and its two submenus.
inline constexpr const char* kExamplesMenu = "Examples";
inline constexpr const char* kLiveExamplesMenu = "Live";
inline constexpr const char* kOfflineExamplesMenu = "Offline";

// Whether an entry can be opened here, asked when the menu is drawn (a file check, no parsing), so a scene whose
// generator has just run becomes openable without a restart.
enum class ExampleReadiness {
    Ready,       // everything it loads is here
    NoAudio,     // opens, but its song is not on this machine: the scene runs without its audio
    Unavailable, // the project or its scene file is missing: opening it would fail
};
struct ExampleStatus {
    ExampleReadiness readiness = ExampleReadiness::Ready;
    std::vector<std::filesystem::path> missing; // the missing files, required ones first
};
[[nodiscard]] ExampleStatus exampleStatus(const ExampleInfo& example);
// The menu label: the name, plus "(needs generating)", "(missing files)" or "(no audio)" when it is not Ready.
[[nodiscard]] std::string exampleLabel(const ExampleInfo& example, const ExampleStatus& status);
// The hover text: the description, then what is missing and the command that makes it.
[[nodiscard]] std::string exampleTooltip(const ExampleInfo& example, const ExampleStatus& status);

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
