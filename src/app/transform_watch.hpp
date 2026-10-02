#pragma once

// Which parameters are transforms, shared by the jump trace (ADR-1053) and the film validator's
// build-lock check (ADR-1057). A transform parameter is classified by its path:
//   sdf/<o>/transform/position, sdf/<o>/node/<n>/translation        metres
//   sdf/<o>/transform/scale, sdf/<o>/node/<n>/scale|size             relative
//   nodes/<n>/position, particles/<n>/position                         metres
//   nodes/<n>/scale                                                    relative
//   (optionally) sdf/<o>/transform/rotation, sdf/<o>/node/<n>/rotation, nodes/<n>/rotation   degrees
// `owner` is the second path segment (the SDF object or the composition node); `node` the named SDF
// node when there is one.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace avgen::app {

enum class TransformMeasure { Metres, Relative, Degrees };

struct TransformClass {
    TransformMeasure measure = TransformMeasure::Metres;
    std::string owner;
    std::string node; // sdf/<o>/node/<node>/...: the SDF node's name ("" for a whole object or a node)
};

inline bool transformEndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

inline std::optional<TransformClass> classifyTransform(const std::string& path, bool rotations = false) {
    auto segment = [&](std::size_t index) {
        std::size_t a = 0;
        for (std::size_t i = 0; i < index; ++i) {
            a = path.find('/', a);
            if (a == std::string::npos) return std::string();
            ++a;
        }
        const std::size_t b = path.find('/', a);
        return path.substr(a, b == std::string::npos ? std::string::npos : b - a);
    };
    const bool isNode = path.find("/node/") != std::string::npos;
    if (path.starts_with("sdf/")) {
        TransformClass c{TransformMeasure::Metres, segment(1), isNode ? segment(3) : std::string()};
        if (transformEndsWith(path, "/transform/position") || (isNode && transformEndsWith(path, "/translation"))) return c;
        if (transformEndsWith(path, "/transform/scale") ||
            (isNode && (transformEndsWith(path, "/scale") || transformEndsWith(path, "/size")))) {
            c.measure = TransformMeasure::Relative;
            return c;
        }
        if (rotations && (transformEndsWith(path, "/transform/rotation") || (isNode && transformEndsWith(path, "/rotation")))) {
            c.measure = TransformMeasure::Degrees;
            return c;
        }
        return std::nullopt;
    }
    if (path.starts_with("nodes/") && transformEndsWith(path, "/position")) return TransformClass{TransformMeasure::Metres, segment(1), {}};
    if (path.starts_with("nodes/") && transformEndsWith(path, "/scale")) return TransformClass{TransformMeasure::Relative, segment(1), {}};
    if (rotations && path.starts_with("nodes/") && transformEndsWith(path, "/rotation")) {
        return TransformClass{TransformMeasure::Degrees, segment(1), {}};
    }
    if (path.starts_with("particles/") && transformEndsWith(path, "/position")) {
        return TransformClass{TransformMeasure::Metres, segment(1), {}};
    }
    return std::nullopt;
}

// Why a route can step: its source jumps (a pulse, an event with no attack) and nothing smooths it.
inline std::string transformRouteCause(const nlohmann::json& r) {
    const std::string src = r.value("source", std::string());
    const nlohmann::json chain = r.value("chain", nlohmann::json::object());
    const bool smoothed = chain.contains("springHz") || chain.contains("attackMs") || chain.contains("smoothMs") ||
                          chain.contains("slewPerSecond") || chain.contains("lagMs");
    const bool smoothSource = transformEndsWith(src, ".wave") || transformEndsWith(src, ".phase");
    std::string why;
    if (!smoothed && !smoothSource) {
        why = src.starts_with("grid.") ? "a beat-grid pulse/event (instant rise) with no smoothing chain"
                                       : "an unsmoothed signal";
    }
    return why;
}

} // namespace avgen::app
