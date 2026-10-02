#include "app/live_scenes.hpp"

#include "params/parameter_set.hpp"

#include <algorithm>
#include <system_error>

namespace avgen::app {

namespace {

std::filesystem::path normalised(const std::filesystem::path& p) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(p, ec);
    return (ec ? p : abs).lexically_normal();
}

// Attack and release are multipliers: carried as a ratio. The rest are 0..1 positions: carried as a difference.
bool ratio(std::size_t i) {
    return i >= 3;
}

} // namespace

std::vector<ExampleInfo> liveSceneList(const std::vector<ExampleInfo>& examples) {
    std::vector<ExampleInfo> out;
    for (const ExampleInfo& e : examples) {
        if (e.name == "Sonic Live") {
            out.push_back(e);
        }
    }
    for (const ExampleInfo& e : examples) {
        if (e.category == kLiveSceneCategory) {
            out.push_back(e);
        }
    }
    return out;
}

int liveSceneIndex(const std::vector<ExampleInfo>& list, const std::filesystem::path& project) {
    if (project.empty()) {
        return -1;
    }
    const auto want = normalised(project);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (normalised(list[i].file) == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int steppedLiveScene(int current, int delta, int count) {
    if (count <= 0) {
        return -1;
    }
    if (current < 0 || current >= count) {
        return delta >= 0 ? 0 : count - 1;
    }
    return ((current + delta) % count + count) % count;
}

int liveSceneForProgram(int program, int count) {
    return count > 0 && program >= 0 ? program % count : -1;
}

std::string liveSceneLabel(const ExampleInfo& scene) {
    const std::string prefix = std::string(kLiveSceneCategory) + " - ";
    return scene.name.rfind(prefix, 0) == 0 ? scene.name.substr(prefix.size()) : scene.name;
}

std::optional<ResponseCarry> captureResponse(const params::ParameterSet& params) {
    ResponseCarry carry;
    for (std::size_t i = 0; i < kResponseParameterPaths.size(); ++i) {
        const params::IParameter* p = params.find(kResponseParameterPaths[i]);
        if (p == nullptr) {
            return std::nullopt;
        }
        const float base = p->baseComponent(0);
        const float def = p->defaultComponent(0);
        carry.offset[i] = ratio(i) ? (def > 1e-6f ? base / def : 1.0f) : base - def;
    }
    return carry;
}

void applyResponse(const ResponseCarry& carry, params::ParameterSet& params) {
    for (std::size_t i = 0; i < kResponseParameterPaths.size(); ++i) {
        params::IParameter* p = params.find(kResponseParameterPaths[i]);
        if (p == nullptr) {
            continue;
        }
        const float def = p->defaultComponent(0);
        const float v = ratio(i) ? def * carry.offset[i] : def + carry.offset[i];
        p->setBaseComponent(0, std::clamp(v, p->hardMin(0), p->hardMax(0)));
    }
}

} // namespace avgen::app
