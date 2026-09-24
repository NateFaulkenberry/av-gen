#include "world/effects/ribbon_frame.hpp"

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/history_bank.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::world {

namespace {

// Points closer than this to the one before are the same point: a body standing still records the
// same position every step, and a centripetal Catmull-Rom knot interval of zero is a division by it.
constexpr float kCoincident = 1e-3f;

std::array<RibbonProducer, 256>& producers() {
    static std::array<RibbonProducer, 256> table{};
    return table;
}

// Centripetal Catmull-Rom (alpha = 1/2), Barry and Goldman's pyramid. Centripetal rather than
// uniform because a trail's samples are unevenly spaced -- a body that brakes bunches them -- and a
// uniform spline overshoots and loops at exactly those places, which reads as a kink in the strip.
glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float u) {
    const auto knot = [](const glm::vec3& a, const glm::vec3& b) {
        return std::max(std::sqrt(glm::length(b - a)), 1e-4f);
    };
    const float t0 = 0.0f;
    const float t1 = t0 + knot(p0, p1);
    const float t2 = t1 + knot(p1, p2);
    const float t3 = t2 + knot(p2, p3);
    const float t = t1 + (t2 - t1) * u;
    const glm::vec3 a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
    const glm::vec3 a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
    const glm::vec3 a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
    const glm::vec3 b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
    const glm::vec3 b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
    return (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
}

RibbonPoint lerpPoint(const RibbonPoint& a, const RibbonPoint& b, float u) {
    RibbonPoint p;
    p.position = glm::mix(a.position, b.position, u);
    p.width = a.width + (b.width - a.width) * u;
    p.color = glm::mix(a.color, b.color, u);
    p.opacity = a.opacity + (b.opacity - a.opacity) * u;
    return p;
}

} // namespace

std::uint32_t RibbonSink::verticesFor(std::size_t points, int subdivisions) {
    if (points < 2) {
        return 0;
    }
    const std::size_t dense = (points - 1) * static_cast<std::size_t>(std::max(subdivisions, 0) + 1) + 1;
    return static_cast<std::uint32_t>(dense * 2);
}

std::uint32_t RibbonSink::remaining() const {
    const auto used = static_cast<std::uint32_t>(frame_.vertices.size());
    return used >= kRibbonVertexBudget ? 0u : kRibbonVertexBudget - used;
}

RibbonFit RibbonSink::appendStrip(std::span<const RibbonPoint> points, int subdivisions, const RibbonStyle& style) {
    // Coincident points first: they are not detail, they are a body standing still.
    filtered_.clear();
    for (const RibbonPoint& p : points) {
        if (!filtered_.empty() && glm::length(p.position - filtered_.back().position) < kCoincident) {
            continue;
        }
        filtered_.push_back(p);
    }
    if (filtered_.size() < 2) {
        return RibbonFit::Nothing;
    }
    if (frame_.strips.size() >= kMaxRibbonStrips) {
        return RibbonFit::NoFit;
    }
    // The level of detail, coarsest last: the asked-for subdivision, then halved down to none,
    // then every other point, every fourth, ... while the strip still has a shape to keep.
    const std::span<const RibbonPoint> src(filtered_);
    const std::uint32_t room = remaining();
    int sub = std::max(subdivisions, 0);
    bool reduced = false;
    for (;;) {
        if (verticesFor(src.size(), sub) <= room) {
            const RibbonFit f = write(src, sub, 1, style);
            return reduced && f == RibbonFit::Written ? RibbonFit::Reduced : f;
        }
        if (sub == 0) {
            break;
        }
        sub /= 2;
        reduced = true;
    }
    for (std::size_t stride = 2; (src.size() + stride - 1) / stride >= 4; stride *= 2) {
        const std::size_t count = (src.size() - 1 + stride - 1) / stride + 1; // every stride-th, plus the tail
        if (verticesFor(count, 0) <= room) {
            const RibbonFit f = write(src, 0, stride, style);
            return f == RibbonFit::Written ? RibbonFit::Reduced : f;
        }
    }
    return RibbonFit::NoFit;
}

RibbonFit RibbonSink::write(std::span<const RibbonPoint> src, int subdivisions, std::size_t stride,
                            const RibbonStyle& style) {
    // The control points this level of detail keeps: every `stride`-th, and always the last, so the
    // tail ends where the history does.
    const auto control = [&](std::size_t i) -> const RibbonPoint& {
        return src[std::min(i * stride, src.size() - 1)];
    };
    const std::size_t n = (src.size() - 1 + stride - 1) / stride + 1;
    const int pieces = subdivisions + 1;

    dense_.clear();
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const RibbonPoint& a = control(i);
        const RibbonPoint& b = control(i + 1);
        // Phantom neighbours at the ends by reflection, so the spline passes through both ends.
        const glm::vec3 p0 = i > 0 ? control(i - 1).position : a.position * 2.0f - b.position;
        const glm::vec3 p3 = i + 2 < n ? control(i + 2).position : b.position * 2.0f - a.position;
        for (int k = 0; k < pieces; ++k) {
            const float u = static_cast<float>(k) / static_cast<float>(pieces);
            RibbonPoint p = lerpPoint(a, b, u);
            if (k > 0) {
                p.position = catmullRom(p0, a.position, b.position, p3, u);
            }
            dense_.push_back(p);
        }
    }
    dense_.push_back(control(n - 1));

    const std::size_t m = dense_.size();
    const auto first = static_cast<std::uint32_t>(frame_.vertices.size());
    glm::vec3 lastTangent(0.0f);
    for (std::size_t i = 0; i < m; ++i) {
        const glm::vec3 ahead = dense_[std::min(i + 1, m - 1)].position;
        const glm::vec3 behind = dense_[i == 0 ? 0 : i - 1].position;
        glm::vec3 tangent = ahead - behind;
        if (glm::length(tangent) < 1e-6f) {
            tangent = lastTangent;
        }
        lastTangent = tangent;
        const RibbonPoint& p = dense_[i];
        for (const float side : {-1.0f, 1.0f}) {
            RibbonVertex v;
            v.positionSide = glm::vec4(p.position, side);
            v.tangentWidth = glm::vec4(tangent, std::max(p.width, 0.0f) * 0.5f);
            v.color = glm::vec4(p.color, std::clamp(p.opacity, 0.0f, 1.0f));
            v.profile = glm::vec4(std::clamp(style.coreFraction, 0.0f, 1.0f), std::max(style.glow, 0.0f),
                                  std::max(style.coreBoost, 0.0f), style.softEdge ? 1.0f : 0.0f);
            frame_.vertices.push_back(v);
        }
    }
    RibbonStrip strip;
    strip.firstVertex = first;
    strip.vertexCount = static_cast<std::uint32_t>(frame_.vertices.size()) - first;
    strip.blend = style.blend;
    frame_.strips.push_back(strip);
    return RibbonFit::Written;
}

void registerRibbonProducer(EffectKind kind, RibbonProducer producer) {
    producers()[static_cast<std::size_t>(kind)] = producer;
}

const RibbonProducer* ribbonProducer(EffectKind kind) {
    const RibbonProducer& p = producers()[static_cast<std::size_t>(kind)];
    return p.emit != nullptr ? &p : nullptr;
}

float ribbonHistorySeconds(const EffectInstance& effect) {
    // The schema first: building it is what registers the type's producer.
    const EffectSchema* schema = effectSchema(effect.kind);
    if (schema == nullptr || schema->resolve.bucket != EffectBucket::Ribbon) {
        return 0.0f;
    }
    const RibbonProducer* p = ribbonProducer(effect.kind);
    return p != nullptr && p->historySeconds != nullptr ? p->historySeconds(effect) : 0.0f;
}

void buildRibbonFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                      const HistoryBank& history, RibbonFrame& out, std::span<const std::uint32_t> order,
                      std::span<EffectStatus> status, std::span<std::string> reasons) {
    out.vertices.clear();
    out.strips.clear();
    out.dropped = 0;
    // Kept across frames, so a frame allocates nothing once the first trail has been built.
    static thread_local std::vector<RibbonPoint> filtered;
    static thread_local std::vector<RibbonPoint> dense;
    std::string spare; // the reason slot for an instance the caller gave none
    RibbonSink sink(out, filtered, dense);
    bool reserved = out.vertices.capacity() >= kRibbonVertexBudget;

    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr || schema->resolve.bucket != EffectBucket::Ribbon) {
            continue;
        }
        std::string& reason = at < reasons.size() ? reasons[at] : spare;
        EffectStatus said = EffectStatus::Disabled;
        if (e.enabled) {
            if (!reserved) {
                // The whole budget once, the first time a ribbon type is live -- and never for a scene
                // that has none, which is the gate's CPU half.
                out.vertices.reserve(kRibbonVertexBudget);
                out.strips.reserve(kMaxRibbonStrips);
                reserved = true;
            }
            const RibbonProducer* producer = ribbonProducer(e.kind);
            if (producer == nullptr) {
                said = EffectStatus::Dropped;
                reason = "This type registered no ribbon producer, so nothing builds its strips.";
            } else {
                said = producer->emit(e, ctx, history, sink, reason);
                if (said == EffectStatus::Dropped) {
                    ++out.dropped;
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
}

} // namespace avgen::world
