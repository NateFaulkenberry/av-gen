#include "rendering/render_stats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace avgen::rendering {

std::uint64_t drawTriangles(std::uint32_t indexCount, std::uint32_t instanceCount) {
    // Widened before the multiply, not after. A scatter of 114,296 records against a 132-triangle
    // source is 15 million triangles, and a 32-bit product of two plausible 32-bit numbers wraps
    // into a small plausible number rather than an obviously wrong one.
    return static_cast<std::uint64_t>(indexCount / 3u) * static_cast<std::uint64_t>(instanceCount);
}

SubmittedGeometry& SubmittedGeometry::operator+=(const SubmittedGeometry& other) {
    triangles += other.triangles;
    instances += other.instances;
    draws += other.draws;
    estimatedDraws += other.estimatedDraws;
    unmeasuredDraws += other.unmeasuredDraws;
    return *this;
}

void SubmittedGeometry::record(std::uint32_t indexCount, std::uint32_t instanceCount, bool estimated) {
    triangles += drawTriangles(indexCount, instanceCount);
    instances += instanceCount;
    ++draws;
    // The draw is recorded whether or not its instance count is known for this frame; what is
    // conditional is the claim that the count is this frame's.
    if (estimated) {
        ++estimatedDraws;
    }
}

void SubmittedGeometry::recordUnmeasured() {
    ++draws;
    ++unmeasuredDraws;
}

SubmittedGeometry GeometryCounters::total() const {
    SubmittedGeometry sum = camera;
    sum += depth;
    sum += shadow;
    return sum;
}

StateChangeCounters& StateChangeCounters::operator+=(const StateChangeCounters& other) {
    pipelineBinds += other.pipelineBinds;
    bindGroupBinds += other.bindGroupBinds;
    vertexBufferBinds += other.vertexBufferBinds;
    indexBufferBinds += other.indexBufferBinds;
    redundantBindsAvoided += other.redundantBindsAvoided;
    renderPasses += other.renderPasses;
    computePasses += other.computePasses;
    return *this;
}

double CpuFrameBreakdown::stagesMs() const {
    return uploadsMs + lightsMs + objectsMs + fieldsMs + simulationMs + particlesMs + proceduralMs +
           sdfMs + shadowEncodeMs + backgroundEncodeMs + depthEncodeMs + sceneEncodeMs +
           volumeEncodeMs + postEncodeMs + tonemapEncodeMs + finishMs + submitMs + queueWaitMs;
}

double CpuFrameBreakdown::unattributedMs() const {
    // Clamped, because the stages are timed by separate clock reads inside the interval the total
    // is timed over: rounding can put the sum a nanosecond past it, and a residual that reads
    // "-0.0000001 ms" invites someone to believe a stage is being double-counted.
    return std::max(0.0, totalMs - stagesMs());
}

std::vector<gpu::TimelineInterval> sumByLabel(const std::vector<gpu::TimelineInterval>& passes) {
    std::vector<gpu::TimelineInterval> out;
    for (const auto& pass : passes) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.label == pass.label; });
        if (it == out.end()) {
            out.push_back(pass);
        } else {
            it->ms += pass.ms;
        }
    }
    return out;
}

std::vector<gpu::TimelineInterval>
medianByLabel(const std::vector<std::vector<gpu::TimelineInterval>>& frames) {
    std::vector<std::string> labels;
    std::vector<std::vector<double>> samples;
    for (const auto& frame : frames) {
        for (const auto& pass : sumByLabel(frame)) {
            auto it = std::find(labels.begin(), labels.end(), pass.label);
            if (it == labels.end()) {
                labels.push_back(pass.label);
                samples.push_back({pass.ms});
            } else {
                samples[static_cast<std::size_t>(it - labels.begin())].push_back(pass.ms);
            }
        }
    }
    std::vector<gpu::TimelineInterval> out;
    out.reserve(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::vector<double>& v = samples[i];
        std::sort(v.begin(), v.end());
        out.push_back(gpu::TimelineInterval{labels[i], v[v.size() / 2]});
    }
    return out;
}

RemovalAttribution attributeRemoval(const std::vector<gpu::TimelineInterval>& baseline,
                                    const std::vector<gpu::TimelineInterval>& arm,
                                    std::string_view removedLabel) {
    const auto find = [](const std::vector<gpu::TimelineInterval>& list, std::string_view label) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const auto& e) { return e.label == label; });
        return it == list.end() ? 0.0 : it->ms;
    };
    RemovalAttribution out;
    double baselineTotal = 0.0;
    for (const auto& pass : baseline) {
        baselineTotal += pass.ms;
        out.byLabel.push_back(gpu::TimelineInterval{pass.label, pass.ms - find(arm, pass.label)});
    }
    double armTotal = 0.0;
    for (const auto& pass : arm) {
        armTotal += pass.ms;
        // A label the arm has and the baseline does not is a pass that appeared when the phase was
        // removed. Rare, but silently dropping it would make the per-label deltas stop summing to
        // the frame delta, which is the property that makes this attribution worth anything.
        const auto known = std::find_if(baseline.begin(), baseline.end(),
                                        [&](const auto& e) { return e.label == pass.label; });
        if (known == baseline.end()) {
            out.byLabel.push_back(gpu::TimelineInterval{pass.label, -pass.ms});
        }
    }
    out.frameDeltaMs = baselineTotal - armTotal;
    // A pass may be reported as a family -- ADR-139 split the volumetric pass into `volume.march`
    // and `volume.composite` because they are different costs and one number hid that the composite
    // is a single timestamp tick. Asking what removing "volume" cost has to mean the family, or the
    // attribution silently reports zero for a pass that plainly ran and the whole frame delta lands
    // in `elsewhereMs`. Sum the exact label and anything under it.
    out.removedPassMs = 0.0;
    for (const auto& pass : baseline) {
        const std::string_view label(pass.label);
        if (label == removedLabel ||
            (label.size() > removedLabel.size() + 1 &&
             label.substr(0, removedLabel.size()) == removedLabel &&
             label[removedLabel.size()] == '.')) {
            out.removedPassMs += pass.ms;
        }
    }
    out.elsewhereMs = out.frameDeltaMs - out.removedPassMs;
    std::stable_sort(out.byLabel.begin(), out.byLabel.end(),
                     [](const auto& a, const auto& b) { return a.ms > b.ms; });
    return out;
}


// ---- distributions ----------------------------------------------------------------------------

double percentileOf(std::vector<double> samples, double q) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const double n = static_cast<double>(samples.size());
    // Nearest rank. `q <= 0` gives the minimum; the ceil puts q = 0.99 of 108 samples on the 107th
    // (index 106), which is the second-slowest frame -- one real frame, not a blend of two.
    const double rank = std::ceil(std::clamp(q, 0.0, 1.0) * n);
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1u;
    return samples[std::min(index, samples.size() - 1u)];
}

namespace {

// The mean of the slowest `fraction` of a sorted-ascending sample -- the tail, not a point in it.
// At least one frame always, so a short run reports its worst frame rather than nothing.
double slowestMean(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto n = static_cast<double>(sorted.size());
    const auto take = std::max<std::size_t>(1u, static_cast<std::size_t>(std::ceil(n * fraction)));
    const auto count = std::min(take, sorted.size());
    double sum = 0.0;
    for (std::size_t i = sorted.size() - count; i < sorted.size(); ++i) {
        sum += sorted[i];
    }
    return sum / static_cast<double>(count);
}

double medianOfSorted(const std::vector<double>& sorted) {
    return sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
}

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return medianOfSorted(v);
}

} // namespace

Distribution describe(std::vector<double> samples) {
    Distribution out;
    if (samples.empty()) {
        // Deliberately not "0.00 ms": `count == 0` and `valid()` is how a caller tells a frame
        // that cost nothing from a measurement that never happened.
        return out;
    }
    std::sort(samples.begin(), samples.end());
    out.count = samples.size();
    out.min = samples.front();
    out.max = samples.back();
    out.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    const auto at = [&](double q) {
        const double rank = std::ceil(std::clamp(q, 0.0, 1.0) * static_cast<double>(samples.size()));
        const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1u;
        return samples[std::min(index, samples.size() - 1u)];
    };
    out.p10 = at(0.10);
    out.p50 = at(0.50);
    out.p90 = at(0.90);
    out.p95 = at(0.95);
    out.p99 = at(0.99);
    out.low1Percent = slowestMean(samples, 0.01);
    out.low01Percent = slowestMean(samples, 0.001);
    double sq = 0.0;
    for (double v : samples) {
        const double d = v - out.mean;
        sq += d * d;
    }
    // Population variance: these samples are the whole measured window, not a draw from a larger
    // population, so the n-1 correction would be answering a question nobody asked.
    out.variance = sq / static_cast<double>(samples.size());
    out.stddev = std::sqrt(out.variance);
    return out;
}

// ---- A/B comparison ---------------------------------------------------------------------------

bool PairedDelta::isResult() const {
    if (baselineMs <= 0.0) {
        return false;
    }
    return std::abs(deltaPercent) >= noiseFloorPercent;
}

namespace {

PairedDelta pairDeltas(const std::vector<double>& baselineMedians, const std::vector<double>& armMedians,
                       double floorPercent, std::vector<double>& blockDeltasOut, double& spreadOut) {
    PairedDelta out;
    const std::size_t pairs = std::min(baselineMedians.size(), armMedians.size());
    if (pairs == 0) {
        return out;
    }
    blockDeltasOut.clear();
    for (std::size_t i = 0; i < pairs; ++i) {
        blockDeltasOut.push_back(baselineMedians[i] - armMedians[i]);
    }
    const std::vector<double> baseUsed(baselineMedians.begin(),
                                       baselineMedians.begin() + static_cast<std::ptrdiff_t>(pairs));
    const std::vector<double> armUsed(armMedians.begin(),
                                      armMedians.begin() + static_cast<std::ptrdiff_t>(pairs));
    out.baselineMs = medianOf(baseUsed);
    out.armMs = medianOf(armUsed);
    // The median of the per-block deltas, not the difference of the medians: pairing is the point,
    // and a drift that moved both arms cancels inside each pair rather than surviving into the
    // aggregate.
    out.deltaMs = medianOf(blockDeltasOut);
    out.deltaPercent = out.baselineMs > 0.0 ? out.deltaMs / out.baselineMs * 100.0 : 0.0;
    // What this session actually did, measured three ways. One block cannot show spread, so a
    // single pair reports zero for all three and falls back on the calibrated constant.
    //
    // Each is a peak-to-peak over a reference median, in percent. The three are not
    // interchangeable and each catches a case the others cannot:
    //
    //   * the **baseline's** own spread -- the session was noisy (ADR-113 §4's rule, and what
    //     correctly rejects a Constellation null);
    //   * the **arm's** own spread -- the session was noisy *on the side the baseline could not
    //     see*, which is the defect ADR-148 records: in a null A/B both arms are the same code, so
    //     a tight baseline against a loose arm certified the looseness;
    //   * the **per-pair deltas'** spread -- the pairs disagree with each other, which neither arm's
    //     own steadiness can reveal and which is precisely a claim its own evidence contradicts.
    const auto peakToPeakPercent = [](const std::vector<double>& values, double reference) {
        if (values.size() < 2 || reference <= 0.0) {
            return 0.0;
        }
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        return (*hi - *lo) / reference * 100.0;
    };
    out.calibratedFloorPercent = floorPercent;
    out.baselineSpreadPercent = peakToPeakPercent(baseUsed, out.baselineMs);
    out.armSpreadPercent = peakToPeakPercent(armUsed, out.armMs);
    // Against the baseline median, so it is on the same scale as `deltaPercent`, which is the number
    // it is the floor for.
    out.deltaSpreadPercent = peakToPeakPercent(blockDeltasOut, out.baselineMs);
    // `spreadOut` stays the *baseline's* spread: it is published as `AbSummary::gpuSpreadPercent`
    // and documented as that, and quietly changing what a named field means is how a reader ends up
    // comparing two different quantities.
    spreadOut = out.baselineSpreadPercent;
    // The floor is the largest of the four. Never below the constant, because a session that
    // happened to be quiet is not licence to certify below what the reference machine has been
    // measured to produce; and never below anything this session actually showed.
    out.noiseFloorPercent = std::max({floorPercent, out.baselineSpreadPercent, out.armSpreadPercent,
                                      out.deltaSpreadPercent});
    return out;
}

} // namespace

AbSummary compareArms(std::string_view arm, const std::vector<AbBlock>& baseline,
                      const std::vector<AbBlock>& armBlocks) {
    AbSummary out;
    out.arm = std::string(arm);
    const std::size_t pairs = std::min(baseline.size(), armBlocks.size());
    out.blocks = static_cast<int>(pairs);
    if (pairs == 0) {
        return out;
    }
    std::vector<double> baseGpu;
    std::vector<double> baseWall;
    std::vector<double> armGpu;
    std::vector<double> armWall;
    for (std::size_t i = 0; i < pairs; ++i) {
        // A block whose GPU timing never landed (no timestamp queries) contributes to neither
        // side; a zero would read as an infinitely fast arm.
        baseGpu.push_back(baseline[i].gpuMs.valid() ? baseline[i].gpuMs.p50 : 0.0);
        armGpu.push_back(armBlocks[i].gpuMs.valid() ? armBlocks[i].gpuMs.p50 : 0.0);
        baseWall.push_back(baseline[i].wallMs.p50);
        armWall.push_back(armBlocks[i].wallMs.p50);
    }
    out.gpu = pairDeltas(baseGpu, armGpu, kGpuNoiseFloorPercent, out.gpuBlockDeltaMs, out.gpuSpreadPercent);
    out.wall = pairDeltas(baseWall, armWall, kWallNoiseFloorPercent, out.wallBlockDeltaMs, out.wallSpreadPercent);
    return out;
}

// ---- the machine-readable record ---------------------------------------------------------------

namespace {

nlohmann::ordered_json distributionJson(const Distribution& d) {
    if (!d.valid()) {
        return nullptr; // never a zero: see Distribution::valid()
    }
    return nlohmann::ordered_json{
        {"frames", d.count}, {"min", d.min},   {"p10", d.p10},
        {"p50", d.p50},      {"p90", d.p90},   {"p95", d.p95},
        {"p99", d.p99},      {"max", d.max},   {"mean", d.mean},
        // The names carry their definitions with them, because "low1Percent" alone is exactly the
        // ambiguity this whole file exists to remove.
        {"low1Percent_meanOfSlowest1Pct", d.low1Percent},
        {"low01Percent_meanOfSlowest01Pct", d.low01Percent},
        {"varianceMs2", d.variance},
        {"stddevMs", d.stddev}};
}

nlohmann::ordered_json conditionsJson(const BenchmarkConditions& c) {
    return nlohmann::ordered_json{{"scene", c.scene},
                                  {"sceneKind", c.sceneKind},
                                  {"arm", c.arm},
                                  {"camera", c.camera},
                                  {"cameraPosition", {c.cameraPosition[0], c.cameraPosition[1], c.cameraPosition[2]}},
                                  {"cameraTarget", {c.cameraTarget[0], c.cameraTarget[1], c.cameraTarget[2]}},
                                  {"fovYDegrees", c.fovYDegrees},
                                  {"width", c.width},
                                  {"height", c.height},
                                  {"qualityTier", c.qualityTier},
                                  {"gitRevision", c.gitRevision},
                                  {"gitDirty", c.gitDirty},
                                  {"buildType", c.buildType},
                                  {"backend", c.backend},
                                  {"platform", c.platform},
                                  {"sessionId", c.sessionId},
                                  {"startedAt", c.startedAt},
                                  {"framesRendered", c.framesRendered},
                                  {"warmupFrames", c.warmupFrames},
                                  {"measuredFrames", c.measuredFrames},
                                  {"offlineFps", c.offlineFps},
                                  {"clusterStatsPerturbsWallClock", c.clusterStats}};
}

nlohmann::ordered_json countersJson(const BenchmarkCounters& c) {
    return nlohmann::ordered_json{
        {"medianOverMeasuredWindow", true},
        {"varied", c.varied},
        {"draws", c.draws},
        {"shadowDraws", c.shadowDraws},
        {"submittedTriangles", c.triangles},
        {"logicalTriangles", c.logicalTriangles},
        {"visibleInstances", c.visibleInstances},
        {"culledInstances", c.culledInstances},
        {"lod", {c.lod[0], c.lod[1], c.lod[2], c.lod[3]}},
        {"shadowCasters", c.shadowCasters},
        {"lights", c.lights},
        {"directionalLights", c.directionalLights},
        {"clusteredLights", c.clusteredLights},
        {"uniformFallbackPathLights", c.uniformPathLights},
        {"particleSystems", c.particleSystems},
        {"particleCapacity", c.particleCapacity},
        {"particlesEmittedPerFrame", c.particlesEmitted},
        {"transientTextures", c.transientTextures},
        {"entities", c.entities},
        {"computeDispatches", c.computeDispatches},
        {"gpuPasses", c.gpuPasses}};
}

nlohmann::ordered_json clustersJson(const ClusterOccupancy& o) {
    return nlohmann::ordered_json{
        {"note", "counts are uncapped demand: how many lights reach the cluster, not how many fit"},
        {"clusters", o.clusters},
        {"localLights", o.lights},
        {"capPerCluster", o.cap},
        {"empty", o.empty},
        {"overflowed", o.overflowed},
        {"min", o.min},
        {"p50", o.p50},
        {"p90", o.p90},
        {"p99", o.p99},
        {"max", o.max},
        {"mean", o.mean},
        {"demand", o.demand},
        {"droppedByCap", o.dropped}};
}

nlohmann::ordered_json cpuJson(const CpuFrameBreakdown& c) {
    return nlohmann::ordered_json{{"uploads", c.uploadsMs},
                                  {"lights", c.lightsMs},
                                  {"objects", c.objectsMs},
                                  {"fields", c.fieldsMs},
                                  {"simulation", c.simulationMs},
                                  {"particles", c.particlesMs},
                                  {"procedural", c.proceduralMs},
                                  {"sdf", c.sdfMs},
                                  {"shadowEncode", c.shadowEncodeMs},
                                  {"backgroundEncode", c.backgroundEncodeMs},
                                  {"depthEncode", c.depthEncodeMs},
                                  {"sceneEncode", c.sceneEncodeMs},
                                  {"volumeEncode", c.volumeEncodeMs},
                                  {"postEncode", c.postEncodeMs},
                                  {"tonemapEncode", c.tonemapEncodeMs},
                                  {"finish", c.finishMs},
                                  {"submit", c.submitMs},
                                  {"queueWait", c.queueWaitMs},
                                  {"total", c.totalMs},
                                  {"unattributed", c.unattributedMs()}};
}

nlohmann::ordered_json pairedJson(const PairedDelta& d) {
    return nlohmann::ordered_json{{"baselineMs", d.baselineMs},
                                  {"armMs", d.armMs},
                                  {"deltaMs", d.deltaMs},
                                  {"deltaPercent", d.deltaPercent},
                                  {"noiseFloorPercent", d.noiseFloorPercent},
                                  // Which component bound is part of the result, not a detail: a
                                  // difference rejected by the calibrated constant and one rejected
                                  // because this session's arm wobbled are different findings and
                                  // call for different next steps.
                                  {"noiseFloorComponents",
                                   nlohmann::ordered_json{
                                       {"calibratedPercent", d.calibratedFloorPercent},
                                       {"baselineBlockSpreadPercent", d.baselineSpreadPercent},
                                       {"armBlockSpreadPercent", d.armSpreadPercent},
                                       {"perPairDeltaSpreadPercent", d.deltaSpreadPercent}}},
                                  {"clearsNoiseFloor", d.isResult()}};
}

} // namespace

std::string benchmarkJson(const std::vector<BenchmarkRecord>& records, const AbSummary* ab) {
    nlohmann::ordered_json root;
    root["schema"] = "avgen.benchmark/1";
    root["definitions"] = nlohmann::ordered_json{
        {"percentile", "nearest rank: the element at ceil(q*n)-1 of the ascending sample, no interpolation"},
        {"low1Percent", "the MEAN of the slowest ceil(n/100) frames -- not p99, which is a single frame"},
        {"low01Percent", "the mean of the slowest ceil(n/1000) frames; with ~108 measured frames this is one frame and equals max"},
        {"deltaMs", "baseline minus arm: positive means the arm was faster"},
        {"cpuFrameMs", "SceneRenderer::render() plus, on the offline path, Finish/Submit and the "
                       "block on the queue -- so it includes waiting for the GPU; see "
                       "cpuStageMedianMs.queueWait before calling a frame CPU-bound"},
        {"comparability", "records may only be compared when their conditions.sessionId is the same"}};
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (const auto& r : records) {
        nlohmann::ordered_json entry;
        entry["conditions"] = conditionsJson(r.conditions);
        entry["wallMs"] = distributionJson(r.wallMs);
        entry["gpuMs"] = distributionJson(r.gpuMs);
        entry["cpuFrameMs"] = distributionJson(r.cpuMs);
        nlohmann::ordered_json passes = nlohmann::ordered_json::array();
        for (const auto& pass : r.passMedianMs) {
            passes.push_back(nlohmann::ordered_json{{"label", pass.label}, {"medianMs", pass.ms}});
        }
        entry["gpuPassMedianMs"] = std::move(passes);
        entry["cpuStageMedianMs"] = cpuJson(r.cpuMedian);
        entry["counters"] = countersJson(r.counters);
        if (r.haveClusters) {
            entry["clusterOccupancy"] = clustersJson(r.clusters);
        }
        list.push_back(std::move(entry));
    }
    root["records"] = std::move(list);
    if (ab != nullptr && ab->blocks > 0) {
        root["ab"] = nlohmann::ordered_json{{"arm", ab->arm},
                                            {"pairedBlocks", ab->blocks},
                                            {"gpu", pairedJson(ab->gpu)},
                                            {"wall", pairedJson(ab->wall)},
                                            {"gpuBlockDeltaMs", ab->gpuBlockDeltaMs},
                                            {"wallBlockDeltaMs", ab->wallBlockDeltaMs},
                                            {"baselineGpuSpreadPercent", ab->gpuSpreadPercent},
                                            {"baselineWallSpreadPercent", ab->wallSpreadPercent}};
    }
    return root.dump(2) + "\n";
}


// ---- scalability sweeps (ADR-144) --------------------------------------------------------------

SweepSummary summariseSweep(std::string subject, std::string xLabel, std::string yLabel,
                            std::vector<SweepPoint> points) {
    SweepSummary out;
    out.subject = std::move(subject);
    out.xLabel = std::move(xLabel);
    out.yLabel = std::move(yLabel);
    out.points = std::move(points);

    // The floor starts at the engine-wide constant and is raised by whatever this session's own
    // arms actually did. It is never lowered: a curve that happened to repeat well is not licence
    // to certify a difference smaller than the machine has been measured to produce.
    out.noiseFloorPercent = kGpuNoiseFloorPercent;
    for (SweepPoint& point : out.points) {
        point.stats = describe(point.repeats);
        if (!point.stats.valid() || point.stats.p50 <= 0.0) {
            point.spreadPercent = 0.0;
            continue;
        }
        point.spreadPercent = (point.stats.max - point.stats.min) / point.stats.p50 * 100.0;
        out.noiseFloorPercent = std::max(out.noiseFloorPercent, point.spreadPercent);
    }

    // The ends are the first and last arms that actually measured something, not the first and last
    // entries: an arm that failed is kept in the table and must not become an endpoint.
    const SweepPoint* first = nullptr;
    const SweepPoint* last = nullptr;
    for (const SweepPoint& point : out.points) {
        if (point.stats.valid() && point.stats.p50 > 0.0) {
            if (first == nullptr) {
                first = &point;
            }
            last = &point;
        }
    }
    if (first == nullptr || last == nullptr || first == last) {
        return out;
    }
    out.endpointChangePercent = (last->stats.p50 - first->stats.p50) / first->stats.p50 * 100.0;
    out.endpointsSeparated = std::abs(out.endpointChangePercent) >= out.noiseFloorPercent;
    const double dx = last->x - first->x;
    out.slopePerUnitX = dx != 0.0 ? (last->stats.p50 - first->stats.p50) / dx : 0.0;

    // The contention-robust reading. Its floor is not the arms' full spread -- that is the quantity
    // the minimum exists to ignore -- but how far the two *fastest* repeats of an arm were apart. An
    // arm whose two best repeats disagree has no reliable minimum either.
    for (const SweepPoint& point : out.points) {
        if (point.repeats.size() < 2) {
            continue;
        }
        std::vector<double> sorted = point.repeats;
        std::sort(sorted.begin(), sorted.end());
        if (sorted[0] <= 0.0) {
            continue;
        }
        out.minFloorPercent = std::max(out.minFloorPercent, (sorted[1] - sorted[0]) / sorted[0] * 100.0);
    }
    if (first->stats.min > 0.0) {
        out.endpointChangeMinPercent = (last->stats.min - first->stats.min) / first->stats.min * 100.0;
        out.endpointsSeparatedByMin =
            std::abs(out.endpointChangeMinPercent) >= std::max(out.minFloorPercent, kGpuNoiseFloorPercent);
    }

    // Adjacent steps. A knee is a local property and an endpoint comparison cannot see one -- the
    // whole reason ADR-131's sweep is reported as two halves rather than as one ratio.
    const SweepPoint* previous = nullptr;
    for (std::size_t i = 0; i < out.points.size(); ++i) {
        const SweepPoint& point = out.points[i];
        if (!point.stats.valid() || point.stats.p50 <= 0.0) {
            continue;
        }
        if (previous != nullptr) {
            const double step = (point.stats.p50 - previous->stats.p50) / previous->stats.p50 * 100.0;
            if (std::abs(step) >= out.noiseFloorPercent && std::abs(step) > std::abs(out.largestStepPercent)) {
                out.largestStepPercent = step;
                out.largestStepIndex = i;
                out.haveStep = true;
            }
        }
        previous = &point;
    }
    return out;
}

std::string sweepTable(const SweepSummary& summary) {
    std::ostringstream text;
    text << summary.subject << "\n";
    text << std::left << std::setw(22) << "  arm" << std::right << std::setw(12) << summary.xLabel
         << std::setw(11) << summary.yLabel << std::setw(9) << "min" << std::setw(9) << "max"
         << std::setw(9) << "spread" << std::setw(7) << "n" << std::setw(9) << "draws" << std::setw(12)
         << "tris" << std::setw(11) << "visible" << "\n";
    text << std::fixed;
    for (const SweepPoint& point : summary.points) {
        text << "  " << std::left << std::setw(20) << point.arm << std::right;
        text << std::setw(12) << std::setprecision(0) << point.x;
        if (!point.stats.valid()) {
            text << std::setw(11) << "not measured" << "\n";
            continue;
        }
        text << std::setw(11) << std::setprecision(3) << point.stats.p50;
        text << std::setw(9) << std::setprecision(3) << point.stats.min;
        text << std::setw(9) << std::setprecision(3) << point.stats.max;
        text << std::setw(8) << std::setprecision(1) << point.spreadPercent << "%";
        text << std::setw(7) << std::setprecision(0) << static_cast<double>(point.stats.count);
        text << std::setw(9) << std::setprecision(0) << point.draws;
        text << std::setw(12) << std::setprecision(0) << point.triangles;
        text << std::setw(11) << std::setprecision(0) << point.visibleInstances;
        text << "\n";
    }
    text << std::setprecision(2);
    text << "  noise floor " << summary.noiseFloorPercent
         << "% (the worst arm's own repeat spread, floored at " << kGpuNoiseFloorPercent << "%)\n";
    if (!summary.valid()) {
        text << "  NO CURVE: fewer than two arms\n";
        return text.str();
    }
    text << "  ends: " << (summary.endpointChangePercent >= 0.0 ? "+" : "") << summary.endpointChangePercent
         << "% over the swept range -- "
         << (summary.endpointsSeparated ? "A RESULT: larger than this curve's floor"
                                        : "NOT A RESULT: inside this curve's own noise")
         << "\n";
    text << "  fastest repeats: " << (summary.endpointChangeMinPercent >= 0.0 ? "+" : "")
         << summary.endpointChangeMinPercent << "% over the range against a "
         << summary.minFloorPercent << "% min-to-second-min floor -- "
         << (summary.endpointsSeparatedByMin ? "A RESULT under contention" : "not a result even on the minima")
         << "\n";
    if (summary.haveStep) {
        text << "  largest resolvable step: " << (summary.largestStepPercent >= 0.0 ? "+" : "")
             << summary.largestStepPercent << "% at arm '" << summary.points[summary.largestStepIndex].arm
             << "'\n";
    } else {
        text << "  no adjacent step clears the floor: every arm is indistinguishable from its neighbour\n";
    }
    return text.str();
}

} // namespace avgen::rendering
