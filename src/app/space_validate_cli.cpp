#include "app/space_validate_cli.hpp"

#include "scene/space_validator.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace avgen::app {

namespace {

bool readJsonFile(const std::filesystem::path& p, nlohmann::json& out, std::string& error) {
    std::ifstream f(p);
    if (!f) {
        error = "cannot open '" + p.string() + "'";
        return false;
    }
    try {
        out = nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        error = "'" + p.string() + "': " + e.what();
        return false;
    }
    return true;
}

} // namespace

int runSpaceValidateCommand(int argc, char** argv) {
    std::string input, jsonOut, textOut, rulesPath;
    bool dumpRules = false, strict = false;
    scene::SpaceValidateOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--validate-space: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--json") jsonOut = next("--json");
        else if (a == "--text") textOut = next("--text");
        else if (a == "--rules") rulesPath = next("--rules");
        else if (a == "--dump-rules") dumpRules = true;
        else if (a == "--margin") options.lyricMargin = std::atof(next("--margin").c_str());
        else if (a == "--eye") options.eyeHeight = std::atof(next("--eye").c_str());
        else if (a == "--title") options.title = next("--title");
        else if (a == "--no-camera") options.cameraPath = false;
        else if (a == "--strict") strict = true;
        else if (!a.empty() && a[0] != '-' && input.empty()) input = a;
        else {
            std::fprintf(stderr, "--validate-space: unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }
    nlohmann::json overrides = nlohmann::json::object();
    std::string error;
    if (!rulesPath.empty() && !readJsonFile(rulesPath, overrides, error)) {
        std::fprintf(stderr, "--validate-space: rules: %s\n", error.c_str());
        return 3;
    }
    const nlohmann::json rules = scene::mergeSpaceRules(overrides);
    if (dumpRules) {
        std::printf("%s\n", rules.dump(2).c_str());
        return 0;
    }
    if (input.empty()) {
        std::fprintf(stderr, "usage: avgen --validate-space <scene.json|project.json> [--json f] [--text f] [--rules f] "
                             "[--margin m] [--eye m] [--title t] [--no-camera] [--strict] [--dump-rules]\n");
        return 2;
    }
    nlohmann::json doc;
    if (!readJsonFile(input, doc, error)) {
        std::fprintf(stderr, "--validate-space: %s\n", error.c_str());
        return 3;
    }
    std::filesystem::path scenePath = input;
    // A project: follow assets.scene.path (relative to the project file).
    if (doc.is_object() && doc.value("format", std::string()) == "avgen-project") {
        const auto& assets = doc.value("assets", nlohmann::json::object());
        const std::string rel = assets.value("scene", nlohmann::json::object()).value("path", std::string());
        if (rel.empty()) {
            std::fprintf(stderr, "--validate-space: the project names no scene (assets.scene.path)\n");
            return 3;
        }
        // The eye height, when the project holds it constant (a journey film often walks eye-level paths
        // with camera/journey/height keyed to 0).
        if (options.eyeHeight < 0.0 && doc.contains("timeline") && doc["timeline"].is_object() &&
            doc["timeline"].contains("tracks") && doc["timeline"]["tracks"].is_array()) {
            for (const auto& t : doc["timeline"]["tracks"]) {
                if (t.value("target", std::string()) != "camera/journey/height" || !t.contains("keys")) continue;
                bool constant = true;
                double v0 = 0.0;
                bool first = true;
                for (const auto& k : t["keys"]) {
                    const auto& v = k.value("value", nlohmann::json());
                    const double x = v.is_array() && !v.empty() ? v[0].get<double>() : v.is_number() ? v.get<double>() : 0.0;
                    if (first) v0 = x;
                    constant = constant && std::abs(x - v0) < 1e-6;
                    first = false;
                }
                if (constant && !first) {
                    options.eyeHeight = v0;
                    std::fprintf(stderr, "eye height %.2f m (the project's camera/journey/height)\n", v0);
                }
            }
        }
        // The distances the film visits on the journey: its camera/journey/distance keys.
        if (doc.contains("timeline") && doc["timeline"].is_object() && doc["timeline"].contains("tracks") &&
            doc["timeline"]["tracks"].is_array()) {
            for (const auto& t : doc["timeline"]["tracks"]) {
                if (t.value("target", std::string()) != "camera/journey/distance" || !t.contains("keys")) continue;
                for (const auto& k : t["keys"]) {
                    const auto& v = k.value("value", nlohmann::json());
                    if (v.is_array() && !v.empty() && v[0].is_number()) options.journeyDistances.push_back(v[0].get<double>());
                    else if (v.is_number()) options.journeyDistances.push_back(v.get<double>());
                }
            }
        }
        scenePath = std::filesystem::path(input).parent_path() / rel;
        if (!readJsonFile(scenePath, doc, error)) {
            std::fprintf(stderr, "--validate-space: %s\n", error.c_str());
            return 3;
        }
    }
    options.source = scenePath.string();
    auto report = scene::validateSpace(doc, rules, options);
    if (!report) {
        std::fprintf(stderr, "%s\n", report.error().message.c_str());
        return 3;
    }
    const std::string text = scene::formatSpaceReport(*report);
    if (!jsonOut.empty()) {
        if (jsonOut == "-") {
            std::printf("%s\n", report->dump(2).c_str());
        } else {
            std::ofstream f(jsonOut);
            f << report->dump(2) << '\n';
            if (!f) {
                std::fprintf(stderr, "--validate-space: cannot write '%s'\n", jsonOut.c_str());
                return 4;
            }
        }
    }
    if (!textOut.empty()) {
        std::ofstream f(textOut);
        f << text;
        if (!f) {
            std::fprintf(stderr, "--validate-space: cannot write '%s'\n", textOut.c_str());
            return 4;
        }
    } else {
        std::fputs(text.c_str(), jsonOut == "-" ? stderr : stdout);
    }
    const int errors = (*report)["summary"].value("errors", 0);
    return strict && errors > 0 ? 1 : 0;
}

} // namespace avgen::app
