#include "ui/ai_panel.hpp"

#include "ai/control_plane.hpp"
#include "app/engine.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace avgen::ui {
namespace {

using avgen::ai::Activity;
using avgen::ai::ActivityKind;
using avgen::ai::AgentTask;
using avgen::ai::TaskState;

// Colour carries the meaning at a glance; the text carries it exactly. Both, because a status
// distinguishable only by hue is not a status for everyone.
ImVec4 colourFor(const Activity& activity) {
    switch (activity.kind) {
    case ActivityKind::Plan: return ImVec4(0.70f, 0.82f, 1.00f, 1.0f);
    case ActivityKind::ToolStarted: return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
    case ActivityKind::ToolCompleted: return ImVec4(0.58f, 0.86f, 0.62f, 1.0f);
    case ActivityKind::ToolFailed: return ImVec4(1.00f, 0.62f, 0.52f, 1.0f);
    case ActivityKind::TransactionOpened:
    case ActivityKind::TransactionCommitted: return ImVec4(0.72f, 0.68f, 0.92f, 1.0f);
    case ActivityKind::TransactionRolledBack: return ImVec4(1.00f, 0.76f, 0.44f, 1.0f);
    case ActivityKind::Note: return activity.success ? ImVec4(0.72f, 0.72f, 0.76f, 1.0f)
                                                     : ImVec4(1.00f, 0.76f, 0.44f, 1.0f);
    case ActivityKind::TaskFinished:
        return activity.success ? ImVec4(0.58f, 0.86f, 0.62f, 1.0f)
                                : ImVec4(1.00f, 0.62f, 0.52f, 1.0f);
    default: return ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
    }
}

const char* glyphFor(ActivityKind kind) {
    switch (kind) {
    case ActivityKind::Plan: return "Plan";
    case ActivityKind::ToolStarted: return "->";
    case ActivityKind::ToolCompleted: return "ok";
    case ActivityKind::ToolFailed: return "!!";
    case ActivityKind::ToolProgress: return "..";
    case ActivityKind::TransactionOpened: return "[";
    case ActivityKind::TransactionCommitted: return "]";
    case ActivityKind::TransactionRolledBack: return "<<";
    case ActivityKind::TaskStarted: return ">";
    case ActivityKind::TaskFinished: return "=";
    default: return " ";
    }
}

void wrapped(const std::string& text, ImVec4 colour) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void AiPanel::draw(app::Engine& engine) {
    if (plane == nullptr) {
        ImGui::TextWrapped("This build has no AI control plane.");
        return;
    }

    // ---- provider line: present, but light (§44) ----
    if (plane->configured()) {
        const auto& settings = plane->settings();
        const avgen::ai::ProviderConfig* active = settings.find(settings.activeProvider);
        const std::string name = active != nullptr && !active->displayName.empty()
                                     ? active->displayName
                                     : settings.activeProvider;
        const std::string model = active != nullptr ? active->model : std::string{};
        ImGui::TextDisabled("%s%s%s", name.c_str(), model.empty() ? "" : " / ",
                            model.c_str());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.76f, 0.44f, 1.0f));
        ImGui::TextWrapped("AI assistant is not configured.");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", plane->unconfiguredReason().c_str());
        if (ImGui::Button("Open Settings") && onOpenSettings) {
            onOpenSettings();
        }
        ImGui::Separator();
        // The conversation is still drawn: a task that failed for want of a provider should still
        // be readable, and an earlier successful one should not vanish because the key was removed.
    }
    ImGui::Separator();

    drawConversation(engine);
    ImGui::Separator();
    drawComposer();
}

void AiPanel::drawConversation(app::Engine& engine) {
    const auto& history = plane->history();
    const float composerHeight = ImGui::GetTextLineHeightWithSpacing() * 5.0f;
    if (!ImGui::BeginChild("ai-conversation", ImVec2(0, -composerHeight), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }

    if (history.empty()) {
        ImGui::TextDisabled(
            "Ask for something. \"Make this feel like a bioluminescent forest at night\", \"push "
            "the camera toward the helix over ten seconds\", \"make the glow pulse with the "
            "bass\". The assistant inspects the project first and reports what it changed.");
    }

    std::size_t totalActivities = 0;
    for (const std::shared_ptr<AgentTask>& task : history) {
        if (!task) {
            continue;
        }
        ImGui::PushID(task->id().c_str());

        // The prompt, as the user typed it.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
        ImGui::TextWrapped("%s", task->prompt().c_str());
        ImGui::PopStyleColor();

        // The state, always spelled out. §9: the user must be able to tell response generation,
        // planning, tool execution, validation, errors and completion apart.
        const TaskState state = task->state();
        const bool running = !task->finished();
        ImGui::TextDisabled("%s  -  %d step(s)  -  %.1fs", avgen::ai::taskStateName(state),
                            task->toolCallsSoFar(), task->elapsedSeconds());

        const std::vector<Activity> activities = task->activities();
        totalActivities += activities.size();
        for (const Activity& activity : activities) {
            // State changes are already summarised on the line above; repeating each one would
            // bury the actual work in bookkeeping.
            if (activity.kind == ActivityKind::StateChanged ||
                activity.kind == ActivityKind::TaskStarted) {
                continue;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, colourFor(activity));
            if (activity.kind == ActivityKind::Plan || activity.kind == ActivityKind::ModelText ||
                activity.kind == ActivityKind::TaskFinished) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", activity.detail.c_str());
            } else if (activity.durationMs > 0.0) {
                ImGui::TextWrapped("%s %s  -  %s  (%.0f ms)", glyphFor(activity.kind),
                                   activity.title.c_str(), activity.detail.c_str(),
                                   activity.durationMs);
            } else {
                ImGui::TextWrapped("%s %s%s%s", glyphFor(activity.kind), activity.title.c_str(),
                                   activity.detail.empty() ? "" : "  -  ",
                                   activity.detail.c_str());
            }
            ImGui::PopStyleColor();
        }

        if (!running) {
            const avgen::ai::TaskOutcome outcome = task->outcome();
            if (!outcome.error.empty()) {
                wrapped(outcome.error, ImVec4(1.0f, 0.62f, 0.52f, 1.0f));
            }
            // What actually changed, measured from the engine. The line a user reads to decide
            // whether to keep the work.
            if (!outcome.changedTargets.empty()) {
                ImGui::TextDisabled("%zu value(s) changed", outcome.changedTargets.size());
                if (ImGui::TreeNode("what changed")) {
                    for (const std::string& target : outcome.changedTargets) {
                        ImGui::BulletText("%s", target.c_str());
                    }
                    ImGui::TreePop();
                }
            }
            if (!outcome.snapshotId.empty() &&
                plane->snapshots().find(outcome.snapshotId) != nullptr) {
                ImGui::SameLine();
                // §26: the user undoes the whole task as one logical action, however many
                // underlying modifications it made. Safe to press from here: the panel draws on
                // the frame thread, which is the thread the tools themselves run on.
                if (ImGui::SmallButton("Undo this task")) {
                    if (auto r = plane->snapshots().restore(engine, outcome.snapshotId); !r) {
                        status_ = r.error().message;
                    } else {
                        status_ = "restored the project to before \"" + task->prompt() + "\"";
                    }
                }
            }
            if (outcome.usage.inputTokens > 0 || outcome.usage.outputTokens > 0) {
                ImGui::TextDisabled("%llu in / %llu out tokens",
                                    static_cast<unsigned long long>(outcome.usage.inputTokens),
                                    static_cast<unsigned long long>(outcome.usage.outputTokens));
            }
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    // Follow the tail while new work arrives, unless the user has scrolled away to read something.
    if (totalActivities != lastActivityCount_) {
        lastActivityCount_ = totalActivities;
        if (followTail_) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        followTail_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    }
    ImGui::EndChild();
}

void AiPanel::drawComposer() {
    const std::shared_ptr<AgentTask> current = plane->currentTask();
    const bool running = current && !current->finished();

    ImGui::BeginDisabled(running);
    // Multiline, because a real instruction is a sentence or three (§8). Enter inserts a newline;
    // Cmd/Ctrl+Enter sends, which is what every tool people already use for this does.
    ImGui::InputTextMultiline("##ai-prompt", prompt_.data(), prompt_.size(),
                              ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f),
                              ImGuiInputTextFlags_AllowTabInput);
    const bool textFocused = ImGui::IsItemActive();
    ImGui::EndDisabled();

    const bool hasText = prompt_[0] != '\0';
    const bool canSend = !running && hasText && plane->configured();
    const bool keyboardSend =
        canSend && textFocused && ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
        (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl);

    ImGui::BeginDisabled(!canSend);
    const bool pressed = ImGui::Button("Send");
    ImGui::EndDisabled();
    if (pressed || keyboardSend) {
        std::string text(prompt_.c_str());
        if (plane->submit(std::move(text)) != nullptr) {
            std::fill(prompt_.begin(), prompt_.end(), '\0');
            followTail_ = true;
            status_.clear();
        } else {
            status_ = "a task is already running";
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Cancel")) {
        // §28: this reaches the provider request and the queued tool calls, not just the display.
        plane->cancelCurrentTask();
        status_ = "cancelling; changes made so far will be rolled back";
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(running || plane->history().empty());
    if (ImGui::Button("New conversation")) {
        plane->clearHistory();
        lastActivityCount_ = 0;
    }
    ImGui::EndDisabled();

    if (running) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s...", avgen::ai::taskStateName(current->state()));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Cmd+Enter to send");
    if (!status_.empty()) {
        ImGui::TextDisabled("%s", status_.c_str());
    }
}

} // namespace avgen::ui
