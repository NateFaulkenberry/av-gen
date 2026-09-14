#include "scene/tree_generated.hpp"

#include "scene/tree_generator.hpp"
#include "scene/tree_mesh.hpp"
#include "scene/tree_scene.hpp"

#include <mutex>

namespace avgen::scene {
namespace {

// One tree, kept between part calls.
//
// The builder is asked for one part at a time and there are eight of them, so a cache-free
// implementation would generate the whole tree eight times per rebuild -- about two and a half
// seconds for a frame that should cost nothing. Keyed on the source's structural hash, so an edited
// parameter misses and an unchanged one hits.
//
// Guarded, because `Composition::rebuild` may run on a job thread while the main thread holds the
// previous scene. One tree at a time is enough: a scene has one of these, and a second would be
// evicting the first on every call anyway.
struct TreeCache {
    std::mutex lock;
    std::uint64_t key = 0;
    TreeMeshes meshes;
    bool valid = false;
};

TreeCache& treeCache() {
    static TreeCache cache;
    return cache;
}

Result<const TreeMeshes*> meshesFor(const GeneratedSource& source, TreeCache& cache) {
    const std::uint64_t key = source.structuralHash();
    if (cache.valid && cache.key == key) {
        return &cache.meshes;
    }
    auto params = treeParamsFrom(source.values);
    if (!params) {
        return std::unexpected(params.error());
    }
    auto graph = generateTree(*params);
    if (!graph) {
        return std::unexpected(graph.error());
    }
    auto meshes = buildTreeMeshes(*graph, TreeMeshSettings{});
    if (!meshes) {
        return std::unexpected(meshes.error());
    }
    cache.meshes = std::move(*meshes);
    cache.key = key;
    cache.valid = true;
    return &cache.meshes;
}

Result<MeshData> buildTreePart(const GeneratedSource& source, int part) {
    if (part < 0 || part >= kTreeParts) {
        return fail("tree has {} parts; asked for {}", kTreeParts, part);
    }
    TreeCache& cache = treeCache();
    const std::lock_guard<std::mutex> guard(cache.lock);
    auto meshes = meshesFor(source, cache);
    if (!meshes) {
        return std::unexpected(meshes.error());
    }
    const auto parts = (*meshes)->parts();
    if (static_cast<std::size_t>(part) >= parts.size()) {
        return fail("tree part {} is out of range", part);
    }
    // A copy: the cache outlives this call and the next part will want the same object.
    return MeshData(*parts[static_cast<std::size_t>(part)].second);
}

Result<MeshData> buildTreeEnvironmentPart(const GeneratedSource& source, int part) {
    if (part < 0 || part >= kTreeEnvironmentParts) {
        return fail("tree environment has {} parts; asked for {}", kTreeEnvironmentParts, part);
    }
    auto params = treeParamsFrom(source.values);
    if (!params) {
        return std::unexpected(params.error());
    }
    const TreeLook look;
    return part == static_cast<int>(TreeEnvironmentPart::Ground)
               ? Result<MeshData>(makeTreeGround(look))
               : Result<MeshData>(makeTreeDistantTrees(look, params->seed ^ 0xD157u));
}

} // namespace

const char* treePartName(int part) {
    switch (static_cast<TreePart>(part)) {
    case TreePart::Roots: return "roots";
    case TreePart::Trunk: return "trunk";
    case TreePart::Primary: return "primary";
    case TreePart::Secondary: return "secondary";
    case TreePart::Tertiary: return "tertiary";
    case TreePart::Foliage0: return "foliage0";
    case TreePart::Foliage1: return "foliage1";
    case TreePart::Foliage2: return "foliage2";
    }
    return "part";
}

void registerTreeGenerator() {
    registerGenerator("tree", &buildTreePart);
    registerGenerator("tree-environment", &buildTreeEnvironmentPart);
}

} // namespace avgen::scene
