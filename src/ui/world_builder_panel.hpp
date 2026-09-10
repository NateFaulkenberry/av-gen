#pragma once

// The World Builder panel (ADR-066): a recipe, a button, and an honest job monitor.
//
// This is the smallest UI that makes the whole world pipeline reachable. It deliberately does not
// own any state the project does not: the recipe it edits is a `world::WorldRecipe` that can be
// saved and loaded as a file, and Generate goes through `app::WorldBuilder` and `installWorld` --
// the same two calls the `--generate` flag makes and the same two a future agent would make. There
// is one path, and this panel is a caller of it rather than a second implementation.
//
// The job monitor half is general: it lists every job in the system, not only world generation,
// because "what is the application doing" is one question and it deserves one answer.

#include "app/job_system.hpp"
#include "app/world_builder.hpp"
#include "assets/asset_library.hpp"
#include "world/world_recipe.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

class WorldBuilderPanel {
public:
    // The recipe being edited. Public so the host can load one from disk into it.
    world::WorldRecipe recipe;

    void draw(app::Engine& engine, app::JobSystem& jobs, app::WorldBuilder& builder);
    // Installs anything that finished since the last frame. Called from the main thread every
    // frame, because installing is a scene mutation and a worker must never do it.
    void applyFinished(app::Engine& engine, app::WorldBuilder& builder);

    [[nodiscard]] const std::string& status() const { return status_; }

private:
    void drawRecipe();
    void drawJobs(app::JobSystem& jobs);
    [[nodiscard]] Result<assets::AssetLibrary> resolveLibrary() const;

    std::filesystem::path libraryPath_;
    std::string status_;
    std::optional<app::JobId> lastJob_;
    bool librarySearched_ = false;
    std::size_t libraryCount_ = 0;
    std::string libraryLabel_;
};

} // namespace avgen::ui
