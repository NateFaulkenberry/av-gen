// avgen_scatter_probe: the plants a scene's terrain grows in a region, one line each (the GV3 art pass).
//
//   avgen_scatter_probe <scene.json> --region x0 z0 x1 z1 [--layers a,b,...] [--clearings file.json]
//                       [--counts]
//
// Placing something in a scattered world -- a drum kit, a hero mushroom -- asks which plants stand where
// it will go, and the only other way to find out is to render it. This runs exactly the scatter a scene
// load runs (`world::scatter` over the terrain node's world, layers and clearings, with each proximity
// layer anchored to the layer it names, in the order the composition runs them) and prints every
// instance inside the region: its layer, where it stands, its scale and its height in metres (the
// layer's authored height times the instance scale; `?` for a layer that keeps its asset's own size).
//
// `--clearings file.json` replaces the scene's clearings with that array, so a candidate clearing can
// be tried without editing the scene; `--counts` prints each layer's whole-map count against its
// `maxInstances` (a layer at its cap is thinned everywhere by any change to its count). CPU only.

#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace avgen;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: avgen_scatter_probe <scene.json> --region x0 z0 x1 z1 [--layers a,b] "
                             "[--clearings file.json] [--counts]\n");
        return 2;
    }
    const std::string scenePath = argv[1];
    float x0 = -1e9f, z0 = -1e9f, x1 = 1e9f, z1 = 1e9f;
    std::set<std::string> only;
    std::string clearingsPath;
    bool counts = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--region" && i + 4 < argc) {
            x0 = std::strtof(argv[++i], nullptr);
            z0 = std::strtof(argv[++i], nullptr);
            x1 = std::strtof(argv[++i], nullptr);
            z1 = std::strtof(argv[++i], nullptr);
        } else if (a == "--layers" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string item;
            while (std::getline(ss, item, ',')) {
                only.insert(item);
            }
        } else if (a == "--clearings" && i + 1 < argc) {
            clearingsPath = argv[++i];
        } else if (a == "--counts") {
            counts = true;
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }

    std::ifstream in(scenePath);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", scenePath.c_str());
        return 2;
    }
    nlohmann::json scene;
    in >> scene;
    const nlohmann::json* node = nullptr;
    for (const auto& n : scene.at("nodes")) {
        if (n.value("kind", "") == "terrain" && n.contains("world") && n.contains("scatter")) {
            node = &n;
            break;
        }
    }
    if (node == nullptr) {
        std::fprintf(stderr, "%s has no terrain node with a world and a scatter\n", scenePath.c_str());
        return 2;
    }
    auto map = world::worldMapFromJson(node->at("world"));
    if (!map) {
        std::fprintf(stderr, "world: %s\n", map.error().message.c_str());
        return 2;
    }
    auto ecology = world::ecologyFromJson(node->at("scatter"));
    if (!ecology) {
        std::fprintf(stderr, "scatter: %s\n", ecology.error().message.c_str());
        return 2;
    }
    nlohmann::json clearingsJson = node->contains("clearings") ? node->at("clearings") : nlohmann::json::array();
    if (!clearingsPath.empty()) {
        std::ifstream cin(clearingsPath);
        if (!cin) {
            std::fprintf(stderr, "cannot read %s\n", clearingsPath.c_str());
            return 2;
        }
        cin >> clearingsJson;
    }
    auto clearings = world::clearancesFromJson(clearingsJson);
    if (!clearings) {
        std::fprintf(stderr, "clearings: %s\n", clearings.error().message.c_str());
        return 2;
    }

    // As `Composition` runs them: in order, each proximity layer anchored to the cloud of the layer it
    // names (which runs before it).
    std::unordered_map<std::string, std::shared_ptr<spatial::PointCloud>> habitats;
    std::printf("layer x y z scale height_m\n");
    for (const world::ScatterLayer& layer : ecology->layers) {
        std::span<const glm::vec3> anchors;
        if (layer.proximity) {
            const auto found = habitats.find(layer.proximity->layer);
            if (found != habitats.end()) {
                anchors = found->second->positions();
            }
        }
        auto cloud = std::make_shared<spatial::PointCloud>(world::scatter(*map, layer, anchors, *clearings));
        habitats.emplace(layer.name, cloud);
        if (counts) {
            std::fprintf(stderr, "count %-14s %7zu of %7d%s\n", layer.name.c_str(), cloud->count(),
                         layer.maxInstances,
                         static_cast<int>(cloud->count()) >= layer.maxInstances ? "  AT CAP" : "");
        }
        if (!only.empty() && !only.contains(layer.name)) {
            continue;
        }
        const auto positions = cloud->positions();
        const auto scales = cloud->scales();
        for (std::size_t i = 0; i < cloud->count(); ++i) {
            const glm::vec3 p = positions[i];
            if (p.x < x0 || p.x > x1 || p.z < z0 || p.z > z1) {
                continue;
            }
            const float s = scales[i].y;
            if (layer.height > 0.0f) {
                std::printf("%s %.2f %.2f %.2f %.3f %.2f\n", layer.name.c_str(), p.x, p.y, p.z, s, layer.height * s);
            } else {
                std::printf("%s %.2f %.2f %.2f %.3f ?\n", layer.name.c_str(), p.x, p.y, p.z, s);
            }
        }
    }
    return 0;
}
