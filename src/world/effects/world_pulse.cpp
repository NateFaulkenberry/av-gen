// ADR-2124: the frame's travelling fronts (see world_pulse.hpp).

#include "world/effects/world_pulse.hpp"

#include "world/effects/distortion_frame.hpp"

#include <algorithm>

namespace avgen::world {

void buildPulseFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, PulseFrame& out,
                     std::span<const std::uint32_t> order) {
    out.count = 0;
    out.sourceClear = 0.0f;
    out.sourceEdge = 0.0f;
    out.sourceColumn = false;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n && out.count < kMaxPulseFronts; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        if (!e.enabled || e.kind != EffectKind::Shockwave) {
            continue;
        }
        const float envelope = distortionEnvelope(e, ctx);
        if (envelope <= 0.0f) {
            continue;
        }
        const std::size_t made = shockwavePulseFronts(e, ctx, envelope, std::span(out.fronts).subspan(out.count));
        if (made > 0 && shockwaveSourceClear(e) > out.sourceClear) {
            out.sourceClear = shockwaveSourceClear(e); // ADR-2128
            out.sourceEdge = shockwaveSourceEdge(e);   // ADR-2130: the edge and shape go with the clearance
            out.sourceColumn = shockwaveSourceColumn(e);
        }
        out.count += static_cast<std::uint32_t>(made);
    }
    for (std::size_t i = out.count; i < kMaxPulseFronts; ++i) {
        out.fronts[i] = PulseFront{};
    }
}

} // namespace avgen::world
