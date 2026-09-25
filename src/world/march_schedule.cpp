// ADR-710: the CPU twin of `shaders/march_schedule.wgsl`. Every line here has a line there, in the
// same order, doing the same f32 arithmetic -- the parity test compares them sample for sample.

#include "world/march_schedule.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {

MarchSchedule marchSchedule(const std::array<glm::vec2, kMarchMaxIntervals>& intervals, float maxDistance, int steps,
                            bool ambient) {
    MarchSchedule out;
    const int n = std::max(steps, 1);
    const float legacy = maxDistance / static_cast<float>(n);
    if (!(legacy > 0.0f)) {
        return out;
    }

    // The live intervals, widened to the legacy grid when the environment fog needs the gaps to be
    // whole legacy cells.
    std::array<glm::vec2, kMarchMaxIntervals> live{};
    std::uint32_t count = 0;
    for (const glm::vec2& iv : intervals) {
        if (!(iv.y > iv.x)) {
            continue;
        }
        glm::vec2 v = iv;
        if (ambient) {
            v.x = std::floor(v.x / legacy) * legacy;
            v.y = std::min(std::ceil(v.y / legacy) * legacy, maxDistance);
        }
        live[count++] = v;
    }

    if (count == 0) {
        if (ambient) {
            out.segments[0] = MarchSegment{0.0f, legacy, n, false};
            out.segmentCount = 1;
        }
        return out;
    }

    // Insertion sort by entry, then merge what overlaps.
    for (std::uint32_t i = 1; i < count; ++i) {
        const glm::vec2 key = live[i];
        std::uint32_t j = i;
        while (j > 0 && live[j - 1].x > key.x) {
            live[j] = live[j - 1];
            --j;
        }
        live[j] = key;
    }
    std::array<glm::vec2, kMarchMaxIntervals> spans{};
    std::uint32_t spanCount = 0;
    float total = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (spanCount > 0 && live[i].x <= spans[spanCount - 1].y) {
            spans[spanCount - 1].y = std::max(spans[spanCount - 1].y, live[i].y);
        } else {
            spans[spanCount++] = live[i];
        }
    }
    for (std::uint32_t i = 0; i < spanCount; ++i) {
        total += spans[i].y - spans[i].x;
    }

    // The media's spacing: the authored count over their summed length, never coarser than the grid.
    const float inside = std::min(legacy, total / static_cast<float>(n));

    float cursor = 0.0f;
    for (std::uint32_t i = 0; i < spanCount; ++i) {
        const glm::vec2 span = spans[i];
        if (ambient && span.x > cursor) {
            const int cells = static_cast<int>(std::floor((span.x - cursor) / legacy + 0.5f));
            if (cells > 0) {
                out.segments[out.segmentCount++] =
                    MarchSegment{cursor, (span.x - cursor) / static_cast<float>(cells), cells, false};
            }
        }
        const float length = span.y - span.x;
        const int samples = std::max(1, static_cast<int>(std::ceil(length / inside - 1e-3f)));
        out.segments[out.segmentCount++] =
            MarchSegment{span.x, length / static_cast<float>(samples), samples, true};
        cursor = span.y;
    }
    if (ambient && maxDistance > cursor) {
        const int cells = static_cast<int>(std::floor((maxDistance - cursor) / legacy + 0.5f));
        if (cells > 0) {
            out.segments[out.segmentCount++] =
                MarchSegment{cursor, (maxDistance - cursor) / static_cast<float>(cells), cells, false};
        }
    }
    return out;
}

int marchSampleCount(const MarchSchedule& schedule) {
    int total = 0;
    for (std::uint32_t i = 0; i < schedule.segmentCount; ++i) {
        total += schedule.segments[i].count;
    }
    return total;
}

} // namespace avgen::world
