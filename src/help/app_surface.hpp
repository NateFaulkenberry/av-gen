#pragma once

// What the application actually has: its panels, its menu commands and its key bindings, read off
// the implementation rather than remembered.
//
// This is the half of §35 that makes the other half honest. A validator that compares documentation
// against a hand-maintained list of features is comparing two documents, and two documents drift
// together; one that compares documentation against the source is comparing documentation against
// the thing §34 says wins.
//
// It is a development-time text scan, and it is deliberately a text scan. The alternative -- having
// every panel, menu and key register itself with a runtime registry -- is the right long-term
// answer and is what the editor pass is expected to build; until then the call sites are inline
// ImGui and SDL, there is nothing to link against, and a scanner that reads them is the only way to
// know what is there. §35 asks for exactly this: "even a development-time validation report is
// valuable".
//
// The danger of scraping is a scan that quietly matches nothing and reports a clean bill of health.
// So every scan carries its own coverage -- files read, patterns matched -- and `ScanCoverage::ok()`
// is false when a file that should have yielded rows yielded none. The test asserts on that before
// it asserts on anything else, which means a restructure of control_panel.cpp fails the build
// rather than silently emptying the inventory.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::help {

struct AppPanel {
    std::string id;     // the ImGui window title, which is also the registry key
    std::string label;  // what the View menu calls it
    std::string region; // left / right / bottom / centre / floating
    std::string hint;   // the registry's one-line description
    bool openByDefault = false;
};

struct AppCommand {
    std::string menuPath;  // "File > Open Audio..."
    std::string label;     // "Open Audio..."
    std::string shortcut;  // the string the menu *advertises*, which may bind to nothing
    std::string sourceRef; // "src/ui/control_panel.cpp:144"
};

struct AppShortcut {
    std::string keys;      // "Space", "O", "Left"
    std::string action;    // the call the handler makes, as written: "engine_->togglePlay()"
    std::string sourceRef; // "src/app/application.cpp:1235"
    bool modified = false; // the call site tests a modifier key
};

struct ScanCoverage {
    std::size_t filesRead = 0;
    std::size_t filesMissing = 0;
    std::vector<std::string> missing;  // paths that were expected and are not there
    std::vector<std::string> warnings; // a file read that yielded nothing it should have

    [[nodiscard]] bool ok() const { return filesMissing == 0 && warnings.empty(); }
};

// Everything the validator compares documentation against. Populated by `scanApplicationSource`
// from a source tree, or by hand in a test.
struct AppSurface {
    std::vector<AppPanel> panels;
    std::vector<AppCommand> commands;
    std::vector<AppShortcut> shortcuts;
    // Parameter paths the running application registers. Empty means "not supplied", and the
    // parameter checks are skipped rather than reporting every cited path as unknown -- a scan
    // cannot know these, because most are registered per node and per material at load time.
    std::vector<std::string> parameterPaths;
    ScanCoverage coverage;

    [[nodiscard]] const AppPanel* findPanel(std::string_view id) const;
    [[nodiscard]] bool hasShortcutKeys(std::string_view keys) const;
};

// Reads the source tree at `sourceDir` (the repository root -- AVGEN_SOURCE_DIR in a dev build).
// Never throws and never fails: a missing file becomes a coverage entry, because "the validator
// could not look" and "there is nothing to find" are different answers and must not be confused.
[[nodiscard]] AppSurface scanApplicationSource(const std::filesystem::path& sourceDir);

// The source-tree root a dev build was configured against, or empty if it was not recorded.
[[nodiscard]] std::filesystem::path configuredSourceDir();

} // namespace avgen::help
