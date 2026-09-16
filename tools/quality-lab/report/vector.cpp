#include "report/vector.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace avgen::quality {
namespace {

double percentileSorted(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index =
        static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1) + 0.5);
    return sorted[std::min(index, sorted.size() - 1)];
}

} // namespace

Pooled pool(const std::vector<double>& values, bool higherIsWorse) {
    Pooled out;
    if (values.empty()) {
        return out;
    }
    out.frames = values.size();
    double total = 0.0;
    out.min = values.front();
    out.max = values.front();
    std::size_t worst = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        total += values[i];
        out.min = std::min(out.min, values[i]);
        out.max = std::max(out.max, values[i]);
        if (higherIsWorse ? values[i] > values[worst] : values[i] < values[worst]) {
            worst = i;
        }
    }
    out.mean = total / static_cast<double>(values.size());
    out.worstFrameIndex = worst;
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    out.p5 = percentileSorted(sorted, 0.05);
    out.p95 = percentileSorted(sorted, 0.95);
    return out;
}

Metric Metric::unavailable(std::string name, std::string reason) {
    Metric metric;
    metric.name = std::move(name);
    metric.available = false;
    metric.unavailableReason = std::move(reason);
    return metric;
}

const Metric* Report::find(std::string_view name) const {
    const auto it = std::find_if(metrics.begin(), metrics.end(),
                                 [&](const Metric& m) { return m.name == name; });
    return it == metrics.end() ? nullptr : &*it;
}

void Report::add(Metric metric) {
    metrics.push_back(std::move(metric));
}

std::string Report::pairingViolation() const {
    const Metric* alternation = find("temporal.temporalAlternation");
    const Metric* laplacian = find("detail.spatialLaplacian");
    const bool haveAlternation = alternation != nullptr && alternation->available;
    const bool haveLaplacian = laplacian != nullptr && laplacian->available;
    if (haveAlternation && !haveLaplacian) {
        return "temporal.temporalAlternation is present without detail.spatialLaplacian beside it. "
               "ADR-243: a temporal measure reported alone ranked two anti-aliasing remedies "
               "backwards against a human reviewer, on both arms.";
    }
    return {};
}

std::string Report::toJson() const {
    if (!pairingViolation().empty()) {
        return {};
    }
    nlohmann::ordered_json root;
    root["schemaVersion"] = schemaVersion;

    nlohmann::ordered_json runJson;
    runJson["id"] = run.id;
    runJson["timestamp"] = run.timestamp;
    runJson["gitCommit"] = run.gitCommit;
    runJson["scene"] = run.scene;
    runJson["targetProfile"] = run.targetProfile;
    runJson["candidateDirectory"] = run.candidateDirectory;
    runJson["referenceDirectory"] = run.referenceDirectory;
    runJson["framesAnalysed"] = run.framesAnalysed;
    runJson["frameStride"] = run.frameStride;
    runJson["candidateSequenceHash"] = run.candidateSequenceHash;
    runJson["referenceSequenceHash"] = run.referenceSequenceHash;
    root["run"] = std::move(runJson);

    nlohmann::ordered_json rendererJson;
    rendererJson["configuration"] = renderer.configuration;
    rendererJson["gpu"] = renderer.gpu;
    rendererJson["contentionWitness"] = renderer.contentionWitness;
    if (renderer.gpuFrameMsMin.has_value()) {
        rendererJson["gpuFrameMsMin"] = *renderer.gpuFrameMsMin;
    } else {
        rendererJson["gpuFrameMsMin"] = nullptr;
    }
    if (renderer.cpuPhaseMsMin.has_value()) {
        rendererJson["cpuPhaseMsMin"] = *renderer.cpuPhaseMsMin;
    } else {
        rendererJson["cpuPhaseMsMin"] = nullptr;
    }
    root["renderer"] = std::move(rendererJson);

    nlohmann::ordered_json metricsJson = nlohmann::ordered_json::array();
    for (const Metric& metric : metrics) {
        nlohmann::ordered_json m;
        m["name"] = metric.name;
        m["available"] = metric.available;
        if (!metric.available) {
            m["reason"] = metric.unavailableReason;
        }
        m["unit"] = metric.unit;
        m["source"] = metric.source;
        m["pooling"] = metric.pooling;
        if (metric.value.has_value() && std::isfinite(*metric.value)) {
            m["value"] = *metric.value;
        } else if (metric.value.has_value()) {
            // PSNR of an identical pair is infinite, and that is the alignment test passing. JSON
            // has no infinity, so it is named rather than silently becoming null or a large number.
            m["value"] = nullptr;
            m["valueNote"] = *metric.value > 0.0 ? "infinite" : "negative infinite";
        } else {
            m["value"] = nullptr;
        }
        if (metric.pooled.has_value()) {
            nlohmann::ordered_json p;
            p["mean"] = metric.pooled->mean;
            p["min"] = metric.pooled->min;
            p["max"] = metric.pooled->max;
            p["p5"] = metric.pooled->p5;
            p["p95"] = metric.pooled->p95;
            p["worstFrameIndex"] = metric.pooled->worstFrameIndex;
            p["frames"] = metric.pooled->frames;
            m["pooled"] = std::move(p);
        }
        m["limitations"] = metric.limitations;
        metricsJson.push_back(std::move(m));
    }
    root["metrics"] = std::move(metricsJson);

    nlohmann::ordered_json findingsJson = nlohmann::ordered_json::array();
    for (const Finding& finding : findings) {
        nlohmann::ordered_json f;
        f["kind"] = "measurement";
        f["metric"] = finding.metric;
        f["statement"] = finding.statement;
        f["value"] = std::isfinite(finding.value) ? nlohmann::ordered_json(finding.value)
                                                  : nlohmann::ordered_json(nullptr);
        if (finding.frameIndex.has_value()) {
            f["frameIndex"] = *finding.frameIndex;
        }
        findingsJson.push_back(std::move(f));
    }
    root["findings"] = std::move(findingsJson);

    nlohmann::ordered_json hypothesesJson = nlohmann::ordered_json::array();
    for (const Hypothesis& hypothesis : hypotheses) {
        nlohmann::ordered_json h;
        h["kind"] = "hypothesis";
        h["statement"] = hypothesis.statement;
        h["evidence"] = hypothesis.evidence;
        h["confidence"] = hypothesis.confidence;
        h["note"] = "an inference, not a measurement; nothing promotes this to findings";
        hypothesesJson.push_back(std::move(h));
    }
    root["hypotheses"] = std::move(hypothesesJson);

    root["diagnostics"] = diagnostics;
    root["limitations"] = limitations;
    return root.dump(2);
}

} // namespace avgen::quality
