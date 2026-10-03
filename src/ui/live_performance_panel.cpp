// ADR-1100/1101: the Live Performance section of the Performance panel, and the Optimize review.
//
// Everything visible here is controllable and named in plain words (the owner's UI rule): the graph's series and
// its history are choices, the inspector's sections open and close, and the Optimize review applies only what is
// ticked, writing the project's live ceilings -- never the scene -- with an Undo beside it.

#include "ui/control_panel.hpp"

#include "ui/style.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace avgen::ui {

namespace {
constexpr std::size_t kPerfTimelineCapacity = 8192; // 30 s at well over 240 fps
const ImVec4 kWarn{1.0f, 0.6f, 0.3f, 1.0f};
const ImVec4 kGood{0.45f, 0.85f, 0.5f, 1.0f};
} // namespace

void ControlPanel::drawLivePerformance(const FrameStats& stats) {
    // ---- the history, by time ----
    if (perfTimeline_.size() < kPerfTimelineCapacity) {
        perfTimeline_.reserve(kPerfTimelineCapacity);
    }
    perfClock_ += std::max(stats.frameIntervalMs, 0.0) / 1000.0;
    const PerfSample sample{perfClock_, stats.frameIntervalMs, stats.gpuFrameMs, stats.cpuFrameMs};
    if (perfTimeline_.size() < kPerfTimelineCapacity) {
        perfTimeline_.push_back(sample);
    } else {
        perfTimeline_[perfTimelineHead_] = sample;
        perfTimelineHead_ = (perfTimelineHead_ + 1) % kPerfTimelineCapacity;
    }

    ImGui::SeparatorText("Live performance");
    const PerformanceInsight& in = perfInsight;
    const double budget = in.budgetMs;
    if (in.available) {
        const double current = in.medianFrameMs;
        ImGui::Text("Target %.0f fps   budget %.2f ms   now %.2f ms", in.targetFps, budget, current);
        ImGui::SameLine();
        const double headroom = budget - current;
        ImGui::TextColored(headroom >= 0.0 ? kGood : kWarn, "headroom %+.2f ms", headroom);
        if (ImGui::IsItemHovered()) {
            tooltip("The frame budget minus the median frame of the last two seconds. Below zero, frames are\n"
                    "arriving later than the target allows.");
        }
        ImGui::Text("GPU %.2f ms   CPU %.2f ms", in.medianGpuMs, in.medianCpuMs);
        if (ImGui::IsItemHovered()) {
            tooltip("Medians of the last two seconds. GPU is the timestamp span, capped by the frame interval\n"
                    "(consecutive frames overlap on the GPU). CPU is main-thread work, waits excluded.");
        }
    }

    // ---- the graph: one series at a time, with the budget, spikes and missed deadlines ----
    ImGui::RadioButton("Frame", &perfGraphSeries_, 0);
    if (ImGui::IsItemHovered()) {
        tooltip("The time between presented frames: what the audience sees.");
    }
    ImGui::SameLine();
    ImGui::RadioButton("GPU", &perfGraphSeries_, 1);
    if (ImGui::IsItemHovered()) {
        tooltip("The GPU's time per frame (the timestamp span).");
    }
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &perfGraphSeries_, 2);
    if (ImGui::IsItemHovered()) {
        tooltip("The main thread's work per frame, waits excluded.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("History (s)", &perfGraphSeconds_, 5.0f, 30.0f, "%.0f s");
    {
        // Oldest first.
        const std::size_t n = perfTimeline_.size();
        std::vector<double> xs, ys, spikeX, spikeY, missX, missY;
        xs.reserve(n);
        ys.reserve(n);
        double peak = budget;
        std::vector<double> sortedY;
        for (std::size_t k = 0; k < n; ++k) {
            const PerfSample& s = perfTimeline_[(perfTimelineHead_ + k) % n];
            if (s.t < perfClock_ - perfGraphSeconds_) {
                continue;
            }
            const double y = perfGraphSeries_ == 0 ? s.frameMs : perfGraphSeries_ == 1 ? s.gpuMs : s.cpuMs;
            if (y < 0.0) {
                continue;
            }
            xs.push_back(s.t - perfClock_);
            ys.push_back(y);
            sortedY.push_back(y);
            peak = std::max(peak, y);
        }
        std::sort(sortedY.begin(), sortedY.end());
        const double median = sortedY.empty() ? 0.0 : sortedY[sortedY.size() / 2];
        for (std::size_t k = 0; k < ys.size(); ++k) {
            if (ys[k] > budget) {
                missX.push_back(xs[k]);
                missY.push_back(ys[k]);
            } else if (median > 0.0 && ys[k] > 1.5 * median) {
                spikeX.push_back(xs[k]);
                spikeY.push_back(ys[k]);
            }
        }
        if (ImPlot::BeginPlot("##liveperf", ImVec2(-1, 150), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect)) {
            ImPlot::SetupAxes("seconds ago", "ms", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
            ImPlot::SetupAxesLimits(-static_cast<double>(perfGraphSeconds_), 0.0, 0.0, peak * 1.15, ImPlotCond_Always);
            if (!xs.empty()) {
                ImPlot::PlotLine(perfGraphSeries_ == 0 ? "frame" : perfGraphSeries_ == 1 ? "gpu" : "cpu", xs.data(),
                                 ys.data(), static_cast<int>(xs.size()));
            }
            const double bx[2] = {-static_cast<double>(perfGraphSeconds_), 0.0};
            const double by[2] = {budget, budget};
            ImPlot::PlotLine("budget", bx, by, 2);
            if (!spikeX.empty()) {
                ImPlot::PlotScatter("spike", spikeX.data(), spikeY.data(), static_cast<int>(spikeX.size()));
            }
            if (!missX.empty()) {
                ImPlot::PlotScatter("over budget", missX.data(), missY.data(), static_cast<int>(missX.size()));
            }
            ImPlot::EndPlot();
        }
        ImGui::TextDisabled("%zu over budget, %zu spikes in the last %.0f s", missX.size(), spikeX.size(),
                            static_cast<double>(perfGraphSeconds_));
        if (ImGui::IsItemHovered()) {
            tooltip("Over budget: a frame longer than the budget line (a red cross). Spike: a frame longer than\n"
                    "one and a half times the median that is still inside the budget (a circle).");
        }
    }

    // ---- GPU by category ----
    if (in.available && !in.gpuCategories.empty()) {
        if (ImGui::TreeNodeEx("GPU by category", ImGuiTreeNodeFlags_DefaultOpen)) {
            double total = 0.0;
            for (const auto& [name, ms] : in.gpuCategories) {
                total += ms;
            }
            for (const auto& [name, ms] : in.gpuCategories) {
                ImGui::Text("%-22s %6.2f ms", name.c_str(), ms);
                ImGui::SameLine(260);
                ImGui::ProgressBar(total > 0.0 ? static_cast<float>(ms / total) : 0.0f, ImVec2(-1, 0), "");
            }
            ImGui::TextDisabled("rolling medians of each pass group, the live profiler's categories");
            ImGui::TreePop();
        }
    }

    // ---- the resource inspector ----
    if (in.available && ImGui::TreeNode("Resources")) {
        for (const auto& [heading, lines] : in.resources) {
            if (ImGui::TreeNode(heading.c_str())) {
                for (const std::string& line : lines) {
                    ImGui::BulletText("%s", line.c_str());
                }
                ImGui::TreePop();
            }
        }
        if (onMeasureMemory && ImGui::Button("Measure memory")) {
            onMeasureMemory();
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Reads the GPU's texture and buffer memory once (Dawn's own accounting). Not done every\n"
                    "frame, so it costs nothing while you are not asking.");
        }
        if (!memoryReport.empty()) {
            ImGui::TextWrapped("%s", memoryReport.c_str());
        }
        ImGui::TreePop();
    }

    // ---- Optimize ----
    if (onOptimizeReview && ImGui::Button("Optimize...")) {
        optimizeRows_ = onOptimizeReview();
        optimizeTicks_.assign(optimizeRows_.size(), 0);
        for (std::size_t k = 0; k < optimizeRows_.size(); ++k) {
            optimizeTicks_[k] = optimizeRows_[k].applicable && optimizeRows_[k].risk == "low" ? 1 : 0;
        }
        optimizeOpen_ = true;
        ImGui::OpenPopup("Optimization review");
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Suggests changes from what the last two seconds cost, with an estimated saving each. Nothing\n"
                "changes until you tick and apply; applying writes the project's live quality limits, never\n"
                "the scene, and Undo puts them back. To MEASURE a saving: avgen --live-profile\n"
                "--verify-candidates.");
    }
    if (canUndoOptimization && onUndoOptimization) {
        ImGui::SameLine();
        if (ImGui::Button("Undo optimization")) {
            onUndoOptimization();
        }
    }
    if (!optimizationStatus.empty()) {
        ImGui::TextWrapped("%s", optimizationStatus.c_str());
    }
    drawOptimizeReview();
}

void ControlPanel::drawOptimizeReview() {
    if (!ImGui::BeginPopupModal("Optimization review", &optimizeOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const PerformanceInsight& in = perfInsight;
    double potential = 0.0;
    for (std::size_t k = 0; k < optimizeRows_.size(); ++k) {
        if (optimizeTicks_[k] != 0) {
            potential += optimizeRows_[k].lowMs;
        }
    }
    ImGui::Text("Current: %.2f ms GPU   budget %.2f ms", in.medianGpuMs, in.budgetMs);
    ImGui::Text("Ticked, estimated: at least %.2f ms less (the low end of each estimate; savings overlap)", potential);
    ImGui::TextDisabled("Every saving here is ESTIMATED. avgen --live-profile --verify-candidates measures them.");
    ImGui::Separator();
    if (optimizeRows_.empty()) {
        ImGui::TextUnformatted("Nothing to suggest above 0.1 ms.");
    }
    for (std::size_t k = 0; k < optimizeRows_.size(); ++k) {
        const OptimizeCandidateView& c = optimizeRows_[k];
        ImGui::PushID(static_cast<int>(k));
        bool ticked = optimizeTicks_[k] != 0;
        ImGui::BeginDisabled(!c.applicable);
        if (ImGui::Checkbox("##tick", &ticked)) {
            optimizeTicks_[k] = ticked ? 1 : 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s: %s", c.title.c_str(), c.suggestion.c_str());
        ImGui::TextDisabled("    costs %.2f ms; estimated saving %.2f-%.2f ms; visual risk %s%s", c.costMs, c.lowMs,
                            c.highMs, c.risk.c_str(), c.applicable ? "" : " (measurement only; cannot be applied)");
        if (ImGui::IsItemHovered()) {
            tooltipUnformatted(("How it was estimated: " + c.basis).c_str());
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("Apply selected") && onApplyOptimization) {
        std::vector<std::string> levers;
        for (std::size_t k = 0; k < optimizeRows_.size(); ++k) {
            if (optimizeTicks_[k] != 0 && optimizeRows_[k].applicable) {
                levers.push_back(optimizeRows_[k].lever);
            }
        }
        onApplyOptimization(levers);
        optimizeOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        optimizeOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace avgen::ui
