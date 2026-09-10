#pragma once

// Generate World (ADR-066): the join between the tested-but-unreachable world systems and the
// running application.
//
// The whole pipeline already exists in pieces. A recipe describes intent; `world::composeWorld`
// turns it into `world::ScatterLayer`s; a Terrain node carries a `world::Ecology`, which is nothing
// more than a vector of exactly those layers; the existing ecology places them and the existing
// renderer draws them. What was missing was the twenty lines that connect them and somewhere to
// press.
//
// The one real design constraint is threading. Composition is pure and can take a moment, so it
// runs on a job worker. Installing the result mutates the scene, and mutating the scene from a
// worker while the renderer is reading it is a data race -- so the worker produces a value and the
// *main thread* installs it, at a point of its choosing. That split is why `WorldBuilder` holds
// finished results rather than applying them itself, and it is the same split a future agent would
// need, which is why it lives here rather than inside the UI.
//
// Both the human UI and any later agent go through `app::Engine` for the actual mutation. There is
// no second path and there must not be one.

#include "assets/asset_library.hpp"
#include "app/job_system.hpp"
#include "core/error.hpp"
#include "world/world_composer.hpp"
#include "world/world_recipe.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace avgen::app {

class Engine;

// A generated world waiting to be installed, plus what it cost to make.
struct GeneratedWorld {
    world::WorldRecipe recipe;
    world::ComposedWorld composed;
    // The library it was composed from, carried along so installation can resolve the heroes' asset
    // ids to files. The alternative is for the composer to write absolute paths into every hero,
    // which bakes this machine's filesystem into a saved world.
    assets::AssetLibrary library;
    double composeSeconds = 0.0;
    std::size_t assetsConsidered = 0;
};

// Installs a composed world into the engine's composition, on the calling thread. Finds the scene's
// Terrain node and replaces its ecology; creates one sized to the recipe when the scene has none, so
// "Generate World" works on an empty project rather than requiring a terrain first.
//
// Main thread only.
[[nodiscard]] Result<void> installWorld(Engine& engine, const GeneratedWorld& world);

// Runs generation as a job and holds the results until somebody collects them.
class WorldBuilder {
public:
    explicit WorldBuilder(JobSystem& jobs) : jobs_(jobs) {}

    // Submits a generation job. The library is copied, because the job outlives the caller's frame
    // and a reference into something the UI owns is a use-after-free waiting for a scroll.
    JobId generate(world::WorldRecipe recipe, assets::AssetLibrary library);

    // Takes any worlds that finished since the last call. Main thread; installing is the caller's
    // decision, not this class's.
    [[nodiscard]] std::vector<GeneratedWorld> collect();

    [[nodiscard]] bool busy() const;

private:
    JobSystem& jobs_;
    mutable std::mutex mutex_;
    std::vector<GeneratedWorld> ready_;
};

} // namespace avgen::app
