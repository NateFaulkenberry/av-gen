#include "rendering/render_stats.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

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
    // What the baseline's own median did across the session. One block cannot show spread, so a
    // single pair falls back on the calibrated constant and says so by reporting zero spread.
    if (baseUsed.size() >= 2 && out.baselineMs > 0.0) {
        const auto [lo, hi] = std::minmax_element(baseUsed.begin(), baseUsed.end());
        spreadOut = (*hi - *lo) / out.baselineMs * 100.0;
    } else {
        spreadOut = 0.0;
    }
    // The floor is whichever is larger: the spread measured on the reference machine, or the
    // spread this session actually showed. A session that wobbled 5% cannot certify a 3% win.
    out.noiseFloorPercent = std::max(floorPercent, spreadOut);
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

} // namespace avgen::rendering
