#include "app/jump_trace_cli.hpp"

#include "app/engine.hpp"
#include "app/transform_watch.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "params/parameter.hpp"
#include "scene/journey.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace avgen::app {

namespace {

using nlohmann::json;

enum class Measure { Metres, Relative };

struct Watched {
    params::IParameter* param = nullptr;
    Measure measure = Measure::Metres;
    std::string owner;      // the scene node whose visibility decides whether it matters
    params::IParameter* visible = nullptr;
    std::array<std::array<float, 3>, 4> ring{}; // the last four frames' values
    int filled = 0;
};

// Which parameters are transforms (shared with the film validator, app/transform_watch.hpp).
std::optional<std::pair<Measure, std::string>> classify(const std::string& path) {
    const auto c = classifyTransform(path, false);
    if (!c) return std::nullopt;
    return std::make_pair(c->measure == TransformMeasure::Relative ? Measure::Relative : Measure::Metres, c->owner);
}

float change(const Watched& w, const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const std::size_t n = std::min<std::size_t>(3, w.param->componentCount());
    float d2 = 0.0f, m2 = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        d2 += (a[i] - b[i]) * (a[i] - b[i]);
        m2 += b[i] * b[i];
    }
    const float d = std::sqrt(d2);
    return w.measure == Measure::Relative ? d / std::max(std::sqrt(m2), 1e-3f) : d;
}

std::string routeCause(const json& r) {
    return transformRouteCause(r);
}

} // namespace

int runJumpTraceCommand(int argc, char** argv) {
    std::string project, jsonOut;
    double fps = 0.0, from = 0.0, to = -1.0, threshold = 0.03;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--trace-jumps: %s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--fps") fps = std::atof(next().c_str());
        else if (a == "--threshold") threshold = std::atof(next().c_str());
        else if (a == "--json") jsonOut = next();
        else if (a == "--range") {
            const std::string r = next();
            const auto c = r.find(':');
            from = std::atof(r.substr(0, c).c_str());
            if (c != std::string::npos && c + 1 < r.size()) to = std::atof(r.substr(c + 1).c_str());
        } else if (!a.empty() && a[0] != '-' && project.empty()) project = a;
        else {
            std::fprintf(stderr, "--trace-jumps: unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }
    if (project.empty()) {
        std::fprintf(stderr, "usage: avgen --trace-jumps <project.json> [--fps f] [--range a:b] [--threshold m] [--json f]\n");
        return 2;
    }
    log::init(log::Level::Warn);
    json doc;
    {
        std::ifstream f(project);
        try {
            doc = json::parse(f);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "--trace-jumps: %s\n", e.what());
            return 3;
        }
    }
    // The journey, for cuts and for which chapter shows which node.
    std::optional<scene::Journey> journey;
    if (doc.contains("assets") && doc["assets"].contains("scene")) {
        const auto scenePath = std::filesystem::path(project).parent_path() / doc["assets"]["scene"].value("path", std::string());
        std::ifstream f(scenePath);
        if (f) {
            try {
                const json scene = json::parse(f);
                if (scene.contains("camera") && scene["camera"].contains("journey")) {
                    if (auto j = scene::Journey::fromJson(scene["camera"]["journey"])) journey = std::move(*j);
                }
            } catch (const std::exception&) {
            }
        }
    }
    Engine engine(EngineMode::Offline);
    engine.setLiveControl(false);
    if (auto r = engine.loadProject(project); !r) {
        std::fprintf(stderr, "--trace-jumps: %s\n", r.error().message.c_str());
        return 3;
    }
    const double rate = fps > 0.0 ? fps : std::max(1.0, engine.renderSettings().fps);
    const double end = to > 0.0 ? to : engine.durationSeconds();
    auto& set = engine.params();
    std::vector<Watched> watched;
    for (params::IParameter* p : set.ordered()) {
        if (auto c = classify(p->path())) {
            Watched w;
            w.param = p;
            w.measure = c->first;
            w.owner = c->second;
            const std::string prefix = p->path().substr(0, p->path().find('/'));
            w.visible = set.find(prefix + "/" + w.owner + "/visible");
            watched.push_back(w);
        }
    }
    params::IParameter* distance = set.find("camera/journey/distance");
    // Routes and tracks by target, for the attribution.
    std::map<std::string, std::vector<const json*>> routesByTarget, tracksByTarget;
    if (doc.contains("routes")) {
        for (const auto& r : doc["routes"]) routesByTarget[r.value("target", std::string())].push_back(&r);
    }
    if (doc.contains("timeline") && doc["timeline"].contains("tracks")) {
        for (const auto& t : doc["timeline"]["tracks"]) tracksByTarget[t.value("target", std::string())].push_back(&t);
    }

    struct Jump {
        std::string path;
        double time;
        float size;
        float neighbours;
    };
    std::vector<Jump> jumps;
    std::array<double, 4> camRing{};
    std::array<std::size_t, 4> chapterRing{};
    const auto frames = static_cast<std::uint64_t>(std::ceil(end * rate));
    for (std::uint64_t f = 0; f <= frames; ++f) {
        FrameTime t;
        t.renderTime = static_cast<double>(f) / rate;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / rate;
        t.frameIndex = f;
        engine.update(t);
        const double cam = distance != nullptr ? static_cast<double>(distance->finalComponent(0)) : 0.0;
        const std::size_t chapter = journey ? journey->chapterAt(cam) : 0;
        std::rotate(camRing.begin(), camRing.begin() + 1, camRing.end());
        std::rotate(chapterRing.begin(), chapterRing.begin() + 1, chapterRing.end());
        camRing[3] = cam;
        chapterRing[3] = chapter;
        // The frame judged is f - 1 (index 2): a cut there or next to it excuses everything.
        const bool cut = f >= 3 && (chapterRing[2] != chapterRing[1] || std::abs(camRing[2] - camRing[1]) > 1.0);
        const double judged = (static_cast<double>(f) - 1.0) / rate;
        for (Watched& w : watched) {
            std::rotate(w.ring.begin(), w.ring.begin() + 1, w.ring.end());
            for (std::size_t i = 0; i < std::min<std::size_t>(3, w.param->componentCount()); ++i) {
                w.ring[3][i] = w.param->finalComponent(i);
            }
            w.filled = std::min(w.filled + 1, 4);
            if (w.filled < 4 || cut || judged < from || judged > end) continue;
            const float d = change(w, w.ring[2], w.ring[1]);
            const float limit = w.measure == Measure::Metres ? static_cast<float>(threshold) : 0.03f;
            if (d <= limit) continue;
            const float around = std::max(change(w, w.ring[1], w.ring[0]), change(w, w.ring[3], w.ring[2]));
            if (d < 4.0f * around) continue; // fast but continuous motion, not a step
            if (w.visible != nullptr && w.visible->finalComponent(0) < 0.5f) continue;
            if (journey && !journey->nodeActive(w.owner, camRing[2])) continue;
            jumps.push_back({w.param->path(), judged, d, around});
        }
    }

    // Group per parameter, attribute.
    std::map<std::string, std::vector<const Jump*>> byPath;
    for (const Jump& j : jumps) byPath[j.path].push_back(&j);
    json report{{"project", project}, {"fps", rate}, {"threshold", threshold}, {"jumps", jumps.size()}, {"targets", json::array()}};
    std::printf("JUMP TRACE  %s  (%.0f fps, threshold %.3f m / 3%%)\n\n", project.c_str(), rate, threshold);
    std::vector<std::pair<std::string, std::vector<const Jump*>>> ordered(byPath.begin(), byPath.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    for (const auto& [path, list] : ordered) {
        json causes = json::array();
        // A route on a sub-path (sdf/x/node/y/translation component 1) or the path itself.
        for (const auto& [target, rs] : routesByTarget) {
            if (target != path) continue;
            for (const json* r : rs) {
                json c{{"kind", "route"}, {"source", r->value("source", std::string())}, {"op", r->value("op", std::string("add"))},
                       {"amount", r->value("amount", 1.0)}, {"component", r->value("component", -1)}};
                if (r->contains("chain")) c["chain"] = (*r)["chain"];
                if (r->contains("depthSource")) c["depthSource"] = (*r)["depthSource"];
                const std::string why = routeCause(*r);
                if (!why.empty()) c["steps"] = why;
                causes.push_back(c);
            }
        }
        if (auto it = tracksByTarget.find(path); it != tracksByTarget.end()) {
            for (const json* t : it->second) {
                json steps = json::array();
                for (const auto& k : (*t)["keys"]) {
                    const double kt = k.value("time", 0.0);
                    for (const Jump* j : list) {
                        if (std::abs(kt - j->time) <= 1.5 / rate && k.value("interp", std::string()) == "step") {
                            steps.push_back(kt);
                            break;
                        }
                    }
                }
                json c{{"kind", "track"}, {"mode", t->value("mode", std::string())}, {"keys", (*t)["keys"].size()}};
                if (!steps.empty()) c["stepKeysAtJumps"] = steps;
                causes.push_back(c);
            }
        }
        json times = json::array();
        float worst = 0.0f;
        for (const Jump* j : list) {
            times.push_back(std::round(j->time * 1000.0) / 1000.0);
            worst = std::max(worst, j->size);
        }
        report["targets"].push_back(json{{"target", path}, {"count", list.size()}, {"worst", worst}, {"times", times}, {"causes", causes}});
        std::printf("%-58s %4zu jumps, worst %.3f%s, first at %.2f s\n", path.c_str(), list.size(), static_cast<double>(worst),
                    path.find("scale") != std::string::npos || path.find("size") != std::string::npos ? " (relative)" : " m",
                    list.front()->time);
        for (const auto& c : causes) {
            if (c.contains("steps")) {
                std::printf("    route %s -> %s x %.3f: %s\n", c["source"].get<std::string>().c_str(), c["op"].get<std::string>().c_str(),
                            c["amount"].get<double>(), c["steps"].get<std::string>().c_str());
            }
            if (c.contains("stepKeysAtJumps")) {
                std::printf("    track (%s): %zu step key(s) at the jumps\n", c["mode"].get<std::string>().c_str(), c["stepKeysAtJumps"].size());
            }
        }
    }
    std::printf("\n%zu jumps on %zu targets\n", jumps.size(), byPath.size());
    if (!jsonOut.empty()) {
        if (jsonOut == "-") {
            std::printf("%s\n", report.dump(2).c_str());
        } else {
            std::ofstream f(jsonOut);
            f << report.dump(2) << '\n';
        }
    }
    return 0;
}

} // namespace avgen::app
