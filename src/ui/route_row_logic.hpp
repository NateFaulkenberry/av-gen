#pragma once

// The Modulation panel's route rows, the parts that are decisions rather than drawing (ADR-900,
// ADR-902). ImGui-free, header-only, so the CPU suite can hold them; control_panel.cpp draws them.

#include "params/liveness.hpp"
#include "params/modulation.hpp"
#include "signals/signal_bus.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {

// What a route row shows beside its "source -> target" header: nothing for a live route, else
// "[dead: <rule>]" or "[hazard: <rule>]" -- the worst finding's rule, and "+n" when there are more --
// with every finding in the tooltip. The same kind of fact as a parameter's "[ignored]": the route is
// real and saved, and it cannot move the picture (or moves it in a way nobody meant).
struct RouteBadge {
    bool show = false;
    bool dead = false; // dead is drawn in the warning colour, a hazard in the caution colour
    std::string text;
    std::string tooltip;
};

[[nodiscard]] inline RouteBadge routeBadge(std::span<const params::liveness::Finding> findings) {
    RouteBadge badge;
    const params::liveness::Verdict verdict = params::liveness::verdictOf(findings);
    if (verdict == params::liveness::Verdict::Live) {
        return badge;
    }
    badge.show = true;
    badge.dead = verdict == params::liveness::Verdict::Dead;
    const params::liveness::Finding* worst = nullptr;
    for (const params::liveness::Finding& f : findings) {
        if (f.verdict == verdict && worst == nullptr) {
            worst = &f;
        }
        badge.tooltip += fmt::format("{}{} ({}): {}", badge.tooltip.empty() ? "" : "\n",
                                     params::liveness::verdictName(f.verdict), f.rule, f.reason);
    }
    badge.text = fmt::format("[{}: {}{}]", params::liveness::verdictName(verdict), worst != nullptr ? worst->rule : "",
                             findings.size() > 1 ? fmt::format(" +{}", findings.size() - 1) : std::string());
    return badge;
}

// Everything about a route the liveness rules read. The panel re-checks a route when this changes,
// and re-checks every route now and then anyway, because a rule also reads the scene.
[[nodiscard]] inline std::uint64_t routeSignature(const params::ModRoute& r) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto mixBytes = [&h](const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ p[i]) * 0x100000001b3ull;
        }
    };
    const auto mixString = [&](const std::string& s) {
        mixBytes(s.data(), s.size());
        const char sep = '\0';
        mixBytes(&sep, 1);
    };
    const auto mixFloat = [&](float f) { mixBytes(&f, sizeof f); };
    mixString(r.source);
    mixString(r.target);
    mixString(r.depthSource);
    mixBytes(&r.component, sizeof r.component);
    const auto op = static_cast<std::uint8_t>(r.op);
    const auto polarity = static_cast<std::uint8_t>(r.polarity);
    mixBytes(&op, 1);
    mixBytes(&polarity, 1);
    const std::uint8_t enabled = r.enabled ? 1 : 0;
    mixBytes(&enabled, 1);
    for (const float f : {r.amount, r.depthMin, r.depthMax, r.chain.delayMs, r.chain.gain, r.chain.offset,
                          r.chain.curveAmount, r.chain.clampMin, r.chain.clampMax, r.chain.thresholdLevel,
                          r.chain.attackMs, r.chain.decayMs, r.chain.envelopeHoldMs, r.chain.envelopeFallPerSecond,
                          r.chain.remapInMin, r.chain.remapInMax, r.chain.remapOutMin, r.chain.remapOutMax}) {
        mixFloat(f);
    }
    const std::uint8_t kinds[] = {static_cast<std::uint8_t>(r.chain.curve), static_cast<std::uint8_t>(r.chain.threshold),
                                  static_cast<std::uint8_t>(r.chain.envelope),
                                  static_cast<std::uint8_t>((r.chain.clampEnabled ? 1 : 0) | (r.chain.remapEnabled ? 2 : 0))};
    mixBytes(kinds, sizeof kinds);
    return h;
}

// The depth-source combo's entries: "(none)" first -- full depth, the route as it always was -- then
// every signal on the bus, sorted so a name can be found. A depth source the bus does not carry (a
// signal from a project that has not attached it yet) stays listed, so the combo never silently
// shows a different choice from the route's own.
[[nodiscard]] inline std::vector<std::string> depthSourceChoices(const signals::SignalBus& bus,
                                                                 std::string_view current = {}) {
    std::vector<std::string> names;
    names.reserve(bus.size() + 2);
    for (const signals::SignalInfo& info : bus.infos()) {
        names.push_back(info.name);
    }
    if (!current.empty() && std::find(names.begin(), names.end(), current) == names.end()) {
        names.emplace_back(current);
    }
    std::sort(names.begin(), names.end());
    names.insert(names.begin(), "(none)");
    return names;
}

// The route's entry in `choices` (0 = none).
[[nodiscard]] inline int depthSourceIndex(const std::vector<std::string>& choices, std::string_view current) {
    if (current.empty()) {
        return 0;
    }
    const auto it = std::find(choices.begin() + 1, choices.end(), current);
    return it == choices.end() ? 0 : static_cast<int>(it - choices.begin());
}

} // namespace avgen::ui
