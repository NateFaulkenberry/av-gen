#include "gpu/timeline_math.hpp"

namespace avgen::gpu {

TimelineSpan timelineIntervals(const std::uint64_t* timestamps, const std::string* labels, std::uint32_t count) {
    TimelineSpan span;
    if (timestamps == nullptr || count < 2) {
        return span;
    }
    span.passes.reserve(count - 1);
    std::uint64_t base = timestamps[0];
    for (std::uint32_t i = 1; i < count; ++i) {
        const std::uint64_t t = timestamps[i];
        if (t != 0 && t >= base) {
            span.passes.push_back(TimelineInterval{labels != nullptr ? labels[i] : std::string{},
                                                   static_cast<double>(t - base) * 1e-6});
            base = t;
        } else {
            span.passes.push_back(TimelineInterval{labels != nullptr ? labels[i] : std::string{}, 0.0});
            ++span.unwritten;
        }
    }
    span.frameMs = static_cast<double>(base - timestamps[0]) * 1e-6;
    return span;
}

} // namespace avgen::gpu
