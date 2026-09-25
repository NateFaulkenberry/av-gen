#include "world/effects/shell_frame.hpp"

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>

namespace avgen::world {

namespace {

std::array<ShellProducer, 256>& producers() {
    static std::array<ShellProducer, 256> table{};
    return table;
}

std::atomic<std::uint8_t>& depthState() {
    static std::atomic<std::uint8_t> state{static_cast<std::uint8_t>(ShellDepthState::Unknown)};
    return state;
}

constexpr std::uint16_t keyOf(ShellShading shading, ShellMesh mesh) {
    return static_cast<std::uint16_t>(static_cast<std::size_t>(shading) * kShellMeshCount + static_cast<std::size_t>(mesh));
}
constexpr std::size_t kKeyCount = kShellShadingCount * kShellMeshCount;

// Formatted into a stack buffer and assigned: `assign` reuses the string's storage, so a reason
// said every frame allocates once, not per frame.
template <typename... Args>
void sayReason(std::string& reason, fmt::format_string<Args...> f, Args&&... args) {
    std::array<char, 256> buffer{};
    const auto result = fmt::format_to_n(buffer.data(), buffer.size(), f, std::forward<Args>(args)...);
    reason.assign(buffer.data(), std::min<std::size_t>(result.size, buffer.size()));
}

} // namespace

const char* shellShadingName(ShellShading s) {
    switch (s) {
    case ShellShading::Plasma: return "plasma";
    case ShellShading::Shield: return "shield";
    case ShellShading::Barrier: return "barrier";
    case ShellShading::Beam: return "beam";
    case ShellShading::Glare: return "glare";
    case ShellShading::Ring: return "ring";
    case ShellShading::Bubble: return "bubble";
    case ShellShading::Portal: return "portal";
    case ShellShading::Tear: return "tear";
    }
    return "?";
}

void ShellFrame::clear() {
    instances.clear();
    extra.clear();
    batches.clear();
    dropped = 0;
}

std::uint32_t ShellSink::remaining() const {
    const auto used = static_cast<std::uint32_t>(frame_.instances.size());
    return used >= kMaxShells ? 0u : kMaxShells - used;
}

bool ShellSink::append(ShellShading shading, ShellMesh mesh, const ShellInstance& shell,
                       std::span<const glm::vec4> extra) {
    if (remaining() == 0) {
        return false;
    }
    const std::size_t n = std::min<std::size_t>(extra.size(), kMaxShellExtraPerShell);
    ShellInstance record = shell;
    record.params[1].z = static_cast<float>(frame_.extra.size());
    record.params[1].w = static_cast<float>(n);
    frame_.extra.insert(frame_.extra.end(), extra.begin(), extra.begin() + static_cast<std::ptrdiff_t>(n));
    frame_.instances.push_back(record);
    keys_.push_back(keyOf(shading, mesh));
    return true;
}

void ShellSink::requestLight(const EffectLight& light) {
    wantsLight_ = true;
    light_ = light;
}

void registerShellProducer(EffectKind kind, ShellProducer producer) {
    producers()[static_cast<std::size_t>(kind)] = producer;
}

const ShellProducer* shellProducer(EffectKind kind) {
    const ShellProducer& p = producers()[static_cast<std::size_t>(kind)];
    return p.emit != nullptr ? &p : nullptr;
}

void reportShellLinearDepth(bool available) {
    depthState().store(static_cast<std::uint8_t>(available ? ShellDepthState::Available : ShellDepthState::Missing),
                       std::memory_order_relaxed);
}

ShellDepthState shellLinearDepthState() {
    return static_cast<ShellDepthState>(depthState().load(std::memory_order_relaxed));
}

glm::vec3 shellHitDirection(float seed, double eventSeconds) {
    // The event time to the millisecond, so two frames that see the same event agree on it however
    // the double was produced; then two decorrelated uniforms through a 32-bit mix (lowbias32).
    const auto ms = static_cast<std::uint64_t>(std::llround(std::max(eventSeconds, 0.0) * 1000.0));
    auto mix = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    };
    const std::uint32_t s = static_cast<std::uint32_t>(std::max(seed, 0.0f)) * 0x9e3779b9U;
    const std::uint32_t a = mix(static_cast<std::uint32_t>(ms) ^ s);
    const std::uint32_t b = mix(a ^ static_cast<std::uint32_t>(ms >> 32) ^ 0x68e31da4U);
    const float u = static_cast<float>(a) * (1.0f / 4294967296.0f);
    const float v = static_cast<float>(b) * (1.0f / 4294967296.0f);
    // Uniform on the sphere (Archimedes: uniform in height, uniform in angle).
    const float z = 1.0f - 2.0f * u;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float phi = 6.28318530718f * v;
    return {r * std::cos(phi), z, r * std::sin(phi)};
}

void buildShellFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, ShellFrame& out,
                     EffectLightFrame& lights, std::span<const std::uint32_t> order,
                     std::span<EffectStatus> status, std::span<std::string> reasons) {
    out.clear();
    // Kept across frames, so a steady frame allocates nothing once the first shell has been built.
    static thread_local std::vector<std::uint16_t> keys;
    static thread_local std::vector<ShellInstance> sorted;
    static thread_local std::vector<EffectLightRequest> requests;
    keys.clear();
    requests.clear();
    std::string spare; // the reason slot for an instance the caller gave none
    bool reserved = out.instances.capacity() >= kMaxShells;
    const ShellDepthState depth = shellLinearDepthState();

    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        const EffectSchema* schema = effectSchema(e.kind);
        if (schema == nullptr || schema->resolve.bucket != EffectBucket::Shell) {
            continue;
        }
        std::string& reason = at < reasons.size() ? reasons[at] : spare;
        EffectStatus said = EffectStatus::Disabled;
        if (e.enabled) {
            if (!reserved) {
                // The whole budget once, the first time a shell type is live -- and never for a scene
                // that has none, which is the gate's CPU half.
                out.instances.reserve(kMaxShells);
                out.extra.reserve(kMaxShellExtra);
                out.batches.reserve(kKeyCount);
                keys.reserve(kMaxShells);
                sorted.reserve(kMaxShells);
                requests.reserve(kMaxShells);
                reserved = true;
            }
            const ShellProducer* producer = shellProducer(e.kind);
            if (producer == nullptr) {
                said = EffectStatus::Dropped;
                reason = "This type registered no shell producer, so nothing builds its shells.";
            } else {
                ShellSink sink(out, keys);
                const std::uint32_t before = sink.remaining();
                said = producer->emit(e, ctx, sink, reason);
                if (said == EffectStatus::Dropped) {
                    ++out.dropped;
                    if (reason.empty() && before == 0) {
                        sayReason(reason, "The shell budget ({}) is full this frame.", kMaxShells);
                    }
                }
                const bool drew = said == EffectStatus::Drawn || said == EffectStatus::Partial;
                if (drew && sink.wantsDepth() && depth == ShellDepthState::Missing && said == EffectStatus::Drawn) {
                    said = EffectStatus::Partial;
                    reason = "Its ground line and depth fade are off: the depth prepass did not run on "
                             "the last frame (it runs with ambient occlusion, contact shadows, the "
                             "shadow mask or water in the scene).";
                }
                if (drew && sink.wantsLight()) {
                    EffectLightRequest request;
                    request.instance = static_cast<std::uint32_t>(at);
                    request.priority = schema->priority;
                    request.order = static_cast<std::uint32_t>(walk);
                    request.light = sink.light();
                    requests.push_back(request);
                }
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }

    // ---- batches: a counting sort by (shading, mesh), stack order kept within each ----
    if (!out.instances.empty()) {
        std::array<std::uint32_t, kKeyCount> counts{};
        for (const std::uint16_t k : keys) {
            ++counts[k];
        }
        std::array<std::uint32_t, kKeyCount> starts{};
        std::uint32_t running = 0;
        for (std::size_t k = 0; k < kKeyCount; ++k) {
            starts[k] = running;
            if (counts[k] > 0) {
                ShellBatch batch;
                batch.shading = static_cast<ShellShading>(k / kShellMeshCount);
                batch.mesh = static_cast<ShellMesh>(k % kShellMeshCount);
                batch.first = running;
                batch.count = counts[k];
                out.batches.push_back(batch);
            }
            running += counts[k];
        }
        sorted.resize(out.instances.size());
        for (std::size_t i = 0; i < out.instances.size(); ++i) {
            sorted[starts[keys[i]]++] = out.instances[i];
        }
        out.instances.swap(sorted);
    }

    // ---- LIGHTMOD: what is left of the pool after the lanes' spills ----
    if (!requests.empty()) {
        EffectLightFrame mine;
        resolveEffectLights(requests, ctx.cameraPosition, mine, status, reasons);
        const std::uint32_t used = std::min<std::uint32_t>(lights.count, static_cast<std::uint32_t>(kEffectLightBudget));
        const std::uint32_t room = static_cast<std::uint32_t>(kEffectLightBudget) - used;
        const std::uint32_t granted = std::min(mine.count, room);
        for (std::uint32_t i = 0; i < granted; ++i) {
            lights.lights[used + i] = mine.lights[i];
        }
        lights.count = used + granted;
        lights.dropped += mine.dropped + (mine.count - granted);
        // `resolveEffectLights` sorted `requests` into rank order: past `granted` is a loser, whether
        // the pool as a whole or only what the lanes left of it turned it away.
        for (std::size_t i = granted; i < requests.size() && i < mine.count; ++i) {
            const std::uint32_t at = requests[i].instance;
            if (at < status.size() && status[at] == EffectStatus::Drawn) {
                status[at] = EffectStatus::Partial;
            }
            if (at < reasons.size()) {
                sayReason(reasons[at],
                          "Its light did not fit: the effect light budget ({}) is full, {} taken by "
                          "entity glows and {} by higher-ranked shells.",
                          kEffectLightBudget, used, granted);
            }
        }
    }
}

} // namespace avgen::world
