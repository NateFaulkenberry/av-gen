// ADR-710: where the volumetric march puts its samples along one camera ray.
//
// `src/world/march_schedule.cpp` is the CPU transliteration of this function, line for line, and
// `tests/rendering/test_march_schedule_parity_gpu.cpp` holds the two to each other. This file
// declares no bindings, so the parity harness can compile it standalone (the rule `fog.wgsl`
// follows for the same reason).
//
// The rule: **the authored step count is spent INSIDE the union of the media's ray intervals**,
// and the environment fog outside them keeps the grid it always had.
//
//   * No medium on the ray, environment fog on: one segment -- the legacy march, sample for sample.
//   * No medium on the ray, environment fog off: no segments; the ray has nothing to integrate.
//   * Media on the ray: each merged interval is one segment, and together they take `steps`
//     samples spaced evenly by length, never coarser than the legacy spacing. With the environment
//     fog on, the intervals are first widened to the legacy grid's cell edges so the gaps between
//     them are whole legacy cells, marched exactly as before; with it off the gaps are skipped.
//
// **The spans are UNIONS of convex chords, never hulls**, and that is load-bearing. The sample
// spacing is the summed span length over `steps`, so it must vary continuously from pixel to pixel
// or the frame shows a seam where it jumps. A ray's chord through a convex solid varies
// continuously, and so does the measure of a union of such chords. The hull of a funnel's chord and
// a far-off cloud's chord does not: it jumps by the whole gap between them the moment the ray grazes
// the cloud. The first version of ADR-710 passed hulls and drew exactly that diagonal seam through
// the hero's funnel.
//
// Why: ADR-562 §4 gave every medium an interval and then marched the fixed grid anyway, so a
// 200 m funnel 4 km down a 32-step ray got one or two samples (ADR-708). The interval was already
// computed; this is the step it was computed for.

// Up to eight intervals: two per slot (a column and a cap, ADR-710), four slots. Merged, they are at
// most eight spans with a gap before, between and after each.
const kMarchMaxIntervals: u32 = 8u;
const kMarchMaxSegments: u32 = 17u;

struct MarchSchedule {
    start: array<f32, 17>,
    step: array<f32, 17>,
    count: array<i32, 17>,
    segments: u32,
};

fn marchSchedule(intervals: array<vec2<f32>, 8>, maxDistance: f32, steps: i32, ambient: bool) -> MarchSchedule {
    var out: MarchSchedule;
    out.segments = 0u;
    let n = max(steps, 1);
    let legacy = maxDistance / f32(n);
    if (!(legacy > 0.0)) {
        return out;
    }

    var live: array<vec2<f32>, 8>;
    var count = 0u;
    for (var s = 0u; s < kMarchMaxIntervals; s = s + 1u) {
        let iv = intervals[s];
        if (!(iv.y > iv.x)) {
            continue;
        }
        var v = iv;
        if (ambient) {
            v.x = floor(v.x / legacy) * legacy;
            v.y = min(ceil(v.y / legacy) * legacy, maxDistance);
        }
        live[count] = v;
        count = count + 1u;
    }

    if (count == 0u) {
        if (ambient) {
            out.start[0] = 0.0;
            out.step[0] = legacy;
            out.count[0] = n;
            out.segments = 1u;
        }
        return out;
    }

    // Insertion sort by entry, then merge what overlaps.
    for (var i = 1u; i < count; i = i + 1u) {
        let key = live[i];
        var j = i;
        while (j > 0u && live[j - 1u].x > key.x) {
            live[j] = live[j - 1u];
            j = j - 1u;
        }
        live[j] = key;
    }
    var spans: array<vec2<f32>, 8>;
    var spanCount = 0u;
    var total = 0.0;
    for (var i = 0u; i < count; i = i + 1u) {
        if (spanCount > 0u && live[i].x <= spans[spanCount - 1u].y) {
            spans[spanCount - 1u].y = max(spans[spanCount - 1u].y, live[i].y);
        } else {
            spans[spanCount] = live[i];
            spanCount = spanCount + 1u;
        }
    }
    for (var i = 0u; i < spanCount; i = i + 1u) {
        total = total + (spans[i].y - spans[i].x);
    }

    let inside = min(legacy, total / f32(n));

    var cursor = 0.0;
    for (var i = 0u; i < spanCount; i = i + 1u) {
        let span = spans[i];
        if (ambient && span.x > cursor) {
            let cells = i32(floor((span.x - cursor) / legacy + 0.5));
            if (cells > 0) {
                out.start[out.segments] = cursor;
                out.step[out.segments] = (span.x - cursor) / f32(cells);
                out.count[out.segments] = cells;
                out.segments = out.segments + 1u;
            }
        }
        let spanLength = span.y - span.x;
        let samples = max(1, i32(ceil(spanLength / inside - 1e-3)));
        out.start[out.segments] = span.x;
        out.step[out.segments] = spanLength / f32(samples);
        out.count[out.segments] = samples;
        out.segments = out.segments + 1u;
        cursor = span.y;
    }
    if (ambient && maxDistance > cursor) {
        let cells = i32(floor((maxDistance - cursor) / legacy + 0.5));
        if (cells > 0) {
            out.start[out.segments] = cursor;
            out.step[out.segments] = (maxDistance - cursor) / f32(cells);
            out.count[out.segments] = cells;
            out.segments = out.segments + 1u;
        }
    }
    return out;
}
