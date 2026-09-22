// avgen_character_validate -- Phase D §58: validate a scene's autonomous characters before runtime.
//
//   avgen_character_validate <scene.json> [...]
//
// Prints one line per issue and exits 1 when any is an error. See `entity/character_validate.hpp`
// for what is checked and why each check exists.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "entity/character_validate.hpp"
#include "organism/mushroom.hpp"
#include "scene/composition.hpp"
#include "scene/tree_generated.hpp"

#include <cstdio>
#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: avgen_character_validate <scene.json> [...]\n");
        return 2;
    }
    log::setLevel(log::Level::Error);
    organism::registerMushroomGenerator();
    scene::registerTreeGenerator();
    int exit = 0;
    for (int i = 1; i < argc; ++i) {
        const fs::path path = fs::absolute(fs::path(argv[i]));
        assets::AssetRegistry registry{path.parent_path()};
        auto loaded = scene::Composition::loadFile(path, registry);
        if (!loaded) {
            std::printf("%s: does not load: %s\n", path.string().c_str(), loaded.error().message.c_str());
            exit = 1;
            continue;
        }
        const auto issues =
            entity::validateCharacters((*loaded)->entities(), (*loaded)->eventProfiles());
        std::size_t errors = 0;
        for (const entity::ValidationIssue& issue : issues) {
            const bool isError = issue.severity == entity::ValidationIssue::Severity::Error;
            errors += isError ? 1 : 0;
            std::printf("%s: %s: %s: %s\n", path.filename().string().c_str(),
                        isError ? "error" : "warning", issue.entity.c_str(), issue.message.c_str());
        }
        std::printf("%s: %zu error(s), %zu warning(s)\n", path.filename().string().c_str(), errors,
                    issues.size() - errors);
        if (errors > 0) {
            exit = 1;
        }
    }
    return exit;
}
