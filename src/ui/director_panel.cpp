#include "ui/director_panel.hpp"

#include "ai/control_plane.hpp"
#include "ai/director_tools.hpp"
#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "directing/plan.hpp"
#include "ui/director_panel_logic.hpp"

#include <imgui.h>
#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace avgen::ui {
namespace {

using avgen::ai::ActivityKind;
using avgen::ai::AgentTask;
using avgen::ai::TaskState;

const ImVec4 kOk(0.45f, 0.85f, 0.55f, 1.0f);
const ImVec4 kWarn(1.0f, 0.78f, 0.35f, 1.0f);
const ImVec4 kBlocked(1.0f, 0.45f, 0.40f, 1.0f);

// A check, an exclamation or a cross, drawn: the UI font is ASCII, and "[x]" reads as "selected".
void markIcon(ItemMark mark) {
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 c = mark == ItemMark::Ok ? kOk : mark == ItemMark::Warning ? kWarn : kBlocked;
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(c);
    const float t = std::max(1.5f, h * 0.12f);
    const ImVec2 o(p.x + 2.0f, p.y + 2.0f);
    const float s = h - 4.0f;
    switch (mark) {
    case ItemMark::Ok:
        dl->AddLine(ImVec2(o.x, o.y + s * 0.55f), ImVec2(o.x + s * 0.38f, o.y + s * 0.9f), col, t);
        dl->AddLine(ImVec2(o.x + s * 0.38f, o.y + s * 0.9f), ImVec2(o.x + s, o.y + s * 0.1f), col, t);
        break;
    case ItemMark::Warning:
        dl->AddLine(ImVec2(o.x + s * 0.5f, o.y), ImVec2(o.x + s * 0.5f, o.y + s * 0.62f), col, t);
        dl->AddCircleFilled(ImVec2(o.x + s * 0.5f, o.y + s * 0.9f), t * 0.75f, col);
        break;
    case ItemMark::Blocked:
        dl->AddLine(ImVec2(o.x + s * 0.1f, o.y + s * 0.1f), ImVec2(o.x + s * 0.9f, o.y + s * 0.9f), col, t);
        dl->AddLine(ImVec2(o.x + s * 0.9f, o.y + s * 0.1f), ImVec2(o.x + s * 0.1f, o.y + s * 0.9f), col, t);
        break;
    }
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine();
}

void heading(const char* text) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", text);
    ImGui::Separator();
}

// The newest task that proposed a plan, or the newest task at all: the conversation the panel is about.
std::shared_ptr<AgentTask> directorTask(const avgen::ai::ControlPlane& plane) {
    const auto& history = plane.history();
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if ((*it)->proposal()) {
            return *it;
        }
    }
    return history.empty() ? nullptr : history.back();
}

} // namespace

void DirectorPanel::refresh(app::Engine& engine, const std::shared_ptr<AgentTask>& task) {
    const std::uint64_t state = edits != nullptr ? edits->history().stateId() : 0;
    const std::string id = task ? task->id() : std::string();
    const auto proposalNow = task ? task->proposal() : std::nullopt;
    const std::string diffNow = proposalNow ? proposalNow->diff : std::string();
    if (id == cachedTask_ && state == cachedState_ && diffNow == cachedDiff_) {
        return;
    }
    // While its own preview is installed, the proposal is shown as it was proposed. Re-compiling it
    // against the project would compile it against itself: "revision 2", every line a replacement.
    if (!previewTask_.empty() && id == previewTask_ && id == cachedTask_ && compiled_ && diffNow == cachedDiff_) {
        return;
    }
    cachedDiff_ = diffNow;
    cachedTask_ = id;
    cachedState_ = state;
    compiled_.reset();
    compileError_.clear();
    const auto proposal = task ? task->proposal() : std::nullopt;
    if (!proposal) {
        return;
    }
    directing::PlanParse parsed = directing::parsePlan(proposal->plan);
    if (!parsed.plan) {
        compileError_ = "the proposed plan does not parse";
        return;
    }
    // The same dry run the approval re-checks: against the project as it is now.
    compiled_ = directing::compilePlan(*parsed.plan, app::sceneFactsFor(engine));
}

std::uint64_t DirectorPanel::baseState() const {
    if (edits == nullptr) {
        return 0;
    }
    const bool newestIsPreview =
        !previewTask_.empty() && edits->history().canUndo() && edits->history().stateId() == previewState_;
    return newestIsPreview ? previewBase_ : edits->history().stateId();
}

bool DirectorPanel::endPreview(app::Engine& engine) {
    if (previewTask_.empty() || edits == nullptr) {
        return false;
    }
    const bool newest = edits->history().canUndo() && edits->history().stateId() == previewState_;
    previewTask_.clear();
    if (!newest) {
        status_ = "the preview edit is still in the history: later edits were made after it";
        return false;
    }
    return edits->execute(app::EditAction::Undo, engine);
}

void DirectorPanel::draw(app::Engine& engine) {
    if (plane == nullptr) {
        ImGui::TextWrapped("The Director needs the AI assistant, which is not available in this session.");
        return;
    }
    const std::shared_ptr<AgentTask> task = directorTask(*plane);
    refresh(engine, task);
    const auto proposal = task ? task->proposal() : std::nullopt;
    const TaskState state = task ? task->state() : TaskState::Completed;
    const bool awaiting = task && state == TaskState::AwaitingApproval && task == plane->currentTask();

    // ---- the request, beside its context ------------------------------------------------------
    if (ImGui::BeginTable("##director-top", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        heading("Conversation");
        if (!task) {
            ImGui::TextWrapped("Nothing asked yet. Describe a shot in the AI panel, e.g. \"At 1:30 cut to Rook "
                               "running past Umbra, low-angle chase\".");
        } else {
            ImGui::TextWrapped("%s", task->prompt().c_str());
            ImGui::TextDisabled("%s", avgen::ai::taskStateName(state));
            for (const auto& a : task->activities()) {
                if (a.kind == ActivityKind::ModelText && !a.detail.empty()) {
                    ImGui::TextWrapped("%s", a.detail.c_str());
                }
            }
        }
        ImGui::TableNextColumn();
        heading("Context");
        const directing::Plan* resolved = compiled_ ? &compiled_->plan : nullptr;
        const ContextView ctx = contextView(resolved != nullptr ? *resolved : directing::Plan{},
                                            app::musicalContextFor(engine), engine.positionSeconds());
        ImGui::Text("Playhead %s%s%s", ctx.time.c_str(), ctx.section.empty() ? "" : "  -  ", ctx.section.c_str());
        ImGui::TextWrapped("Project: %s", engine.projectPath().empty() ? "(unsaved)"
                                                                       : engine.projectPath().filename().string().c_str());
        for (const std::string& s : ctx.subjects) {
            ImGui::BulletText("%s", s.c_str());
        }
        ImGui::EndTable();
    }

    // The decision first, under the request it answers: a long plan must not push the buttons out of
    // the window, where neither a person nor the UI script can reach them.
    // ---- the decision ----------------------------------------------------------------------------
    if (!previewTask_.empty() && task && previewTask_ != task->id()) {
        (void)endPreview(engine); // the proposal it previewed is gone
    }
    // A finished recording becomes the proposal, approved like any other (ADR-765).
    if (auto done = recording_.take()) {
        if (!*done && cancelledRecording_) {
            status_ = "recording cancelled; the proposal is as it was";
        } else if (!*done) {
            status_ = "recording failed: " + done->error().message;
        } else if (!task || task->id() != recordingTask_ || !awaiting) {
            status_ = "the recording finished, but the proposal it was for is no longer waiting";
        } else if (auto revised = ai::proposalFor(engine, (*done)->plan); !revised) {
            status_ = "the recording cannot be proposed: " + revised.error().message;
        } else {
            const std::string note = fmt::format(
                "Recorded: {}. Played back {:.4f} m from the recording; a scrub landed {:.4f} m from the play.",
                (*done)->notes.empty() ? std::string("the live performances") : (*done)->notes.front(),
                (*done)->replayWorstMetres, (*done)->scrubWorstMetres);
            status_ = plane->reviseCurrentProposal(std::move(*revised), note) ? "recorded: the proposal is now the recording"
                                                                              : "the proposal could not be revised";
        }
    }
    const bool liveToRecord =
        compiled_ && std::any_of(compiled_->plan.performances.begin(), compiled_->plan.performances.end(),
                                 [&](const directing::PlanPerformance& p) {
                                     return p.mode != directing::PerformanceMode::Scripted && !p.recording &&
                                            !compiled_->validation.isBlocked(p.key);
                                 });
    PanelState ps;
    ps.proposal = proposal.has_value() && compiled_.has_value();
    ps.liveToRecord = liveToRecord;
    ps.recording = recording_.running();
    ps.awaiting = awaiting;
    ps.changesAnything = compiled_ && compiled_->changesAnything();
    ps.previewing = !previewTask_.empty();
    ps.previewIsNewest = ps.previewing && edits != nullptr && edits->history().canUndo() &&
                         edits->history().stateId() == previewState_;
    const PanelActions actions = panelActions(ps);
    ImGui::Separator();
    const auto button = [](const char* label, const Button& b, Rect& where) {
        ImGui::BeginDisabled(!b.enabled);
        const bool pressed = ImGui::Button(label);
        ImGui::EndDisabled();
        const ImVec2 lo = ImGui::GetItemRectMin();
        const ImVec2 hi = ImGui::GetItemRectMax();
        // In view means WHOLLY inside the window: a button half past a narrow dock's edge is drawn
        // (ImGui calls it visible) but its middle, where a click lands, is not there.
        const ImVec2 wlo = ImGui::GetWindowPos();
        const ImVec2 whi(wlo.x + ImGui::GetWindowSize().x, wlo.y + ImGui::GetWindowSize().y);
        const bool inside = lo.x >= wlo.x && lo.y >= wlo.y && hi.x <= whi.x && hi.y <= whi.y;
        where = Rect{lo.x, lo.y, hi.x - lo.x, hi.y - lo.y, ImGui::IsItemVisible() && inside};
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !b.why.empty()) {
            ImGui::SetTooltip("%s", b.why.c_str());
        }
        return pressed;
    };
    if (ps.previewing) {
        if (button("End preview", actions.endPreview, buttons_.preview)) {
            status_ = endPreview(engine) ? "preview ended; nothing is changed" : status_;
        }
    } else if (button("Preview", actions.preview, buttons_.preview) && edits != nullptr && compiled_) {
        const std::uint64_t before = edits->history().stateId();
        if (app::applyCompilation(engine, edits->history(), *compiled_)) {
            previewBase_ = before;
            previewTask_ = task->id();
            previewState_ = edits->history().stateId();
            if (onRequestStills && stills.task != task->id()) {
                onRequestStills(task->id(), *compiled_); // a preview made is a moment to see the shots
            }
            status_ = "previewing: play the timeline to watch it; End preview, Accept or Reject ends it";
        } else {
            status_ = "the preview could not be installed";
        }
    }
    ImGui::SameLine();
    if (button("Accept", actions.accept, buttons_.accept)) {
        if (actions.revertPreviewFirst) {
            (void)endPreview(engine);
        }
        status_ = plane->approveCurrentTask() ? "applied as one undo" : "could not apply";
    }
    ImGui::SameLine();
    if (button("Reject", actions.reject, buttons_.reject)) {
        if (actions.revertPreviewFirst) {
            (void)endPreview(engine);
        }
        (void)plane->rejectCurrentTask();
        status_ = "rejected; nothing was changed";
    }
    // The second row: what makes or shows something from the proposal, rather than decides it. Its
    // own row, so a narrow docked panel does not push a button past the window's edge.
    if (button("Record", actions.record, buttons_.record) && compiled_) {
        if (actions.revertPreviewFirst) {
            (void)endPreview(engine); // record the proposal against the project, not against its preview
        }
        if (auto started = recording_.start(engine, *compiled_, app::RecordOptions{}); started) {
            recordingTask_ = task->id();
            cancelledRecording_ = false;
            status_ = "recording...";
        } else {
            status_ = "cannot record: " + started.error().message;
        }
    }
    if (recording_.running()) {
        ImGui::SameLine();
        if (button("Cancel recording", actions.cancelRecording, buttons_.cancelRecording)) {
            recording_.cancel();
            cancelledRecording_ = true;
            status_ = "cancelling the recording...";
        }
    }
    ImGui::SameLine();
    Button stillsButton;
    stillsButton.enabled = compiled_.has_value() && compiled_->changesAnything() && static_cast<bool>(onRequestStills);
    stillsButton.why = "renders one small frame per proposed shot, at its middle, from a scratch copy";
    if (button("Stills", stillsButton, buttons_.stills) && compiled_) {
        onRequestStills(task->id(), *compiled_);
    }
    if (recording_.running()) {
        ImGui::TextDisabled("%s", recording_.phase().c_str());
    }
    if (!stills.note.empty() && task && stills.task == task->id()) {
        ImGui::TextDisabled("%s", stills.note.c_str());
    }
    if (!status_.empty()) {
        ImGui::TextDisabled("%s", status_.c_str());
    } else if (awaiting) {
        ImGui::TextDisabled("nothing is changed until you accept");
    }

    // ---- the plan --------------------------------------------------------------------------------
    if (!compileError_.empty()) {
        ImGui::TextColored(kBlocked, "%s", compileError_.c_str());
    }
    if (compiled_) {
        const directing::Compilation& c = *compiled_;
        heading(("Plan: " + (c.plan.title.empty() ? c.plan.id : c.plan.title) +
                 (c.plan.revision > 1 ? "  (revision " + std::to_string(c.plan.revision) + ")" : ""))
                    .c_str());
        for (const PlanItemRow& row : planItemRows(c.plan, c.validation)) {
            ImGui::PushID(row.key.c_str());
            markIcon(row.mark);
            ImGui::TextWrapped("%s %s%s%s", row.kind.c_str(), row.key.c_str(), row.label.empty() ? "" : "  -  ",
                               row.label.c_str());
            if (stills.texture != 0 && task && stills.task == task->id()) {
                if (const auto s = stills.byItem.find(row.key); s != stills.byItem.end()) {
                    ImGui::Indent(ImGui::GetTextLineHeight() + 6.0f);
                    ImGui::Image(static_cast<ImTextureID>(stills.texture), ImVec2(stills.width, stills.height),
                                 ImVec2(s->second.u0, s->second.v0), ImVec2(s->second.u1, s->second.v1));
                    ImGui::SameLine();
                    ImGui::TextDisabled("at %s", clockText(s->second.seconds).c_str());
                    ImGui::Unindent(ImGui::GetTextLineHeight() + 6.0f);
                }
            }
            ImGui::Indent(ImGui::GetTextLineHeight() + 6.0f);
            for (const std::string& line : row.lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, row.mark == ItemMark::Blocked ? kBlocked : kWarn);
                ImGui::TextWrapped("%s", line.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(ImGui::GetTextLineHeight() + 6.0f);
            ImGui::PopID();
        }

        heading("Proposed changes");
        const std::vector<ChangeGroup> groups = changeGroups(c.diff);
        if (groups.empty()) {
            ImGui::TextDisabled("nothing would change");
        }
        for (const ChangeGroup& g : groups) {
            for (const directing::DiffLine& l : g.lines) {
                const ImVec4 colour = l.sign == '+' ? kOk : l.sign == '-' ? kBlocked : kWarn;
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                ImGui::TextWrapped("%c %s", l.sign, l.text.c_str());
                ImGui::PopStyleColor();
            }
        }
    } else if (task && !proposal) {
        ImGui::Spacing();
        ImGui::TextDisabled("This request proposed no plan.");
    }

    // ---- what the project already carries --------------------------------------------------------
    const auto& plans = engine.directingPlans();
    if (!plans.empty()) {
        heading("Plans in this project");
        for (const directing::Plan& p : plans) {
            ImGui::BulletText("%s  -  revision %d, %zu piece(s) of content", p.title.empty() ? p.id.c_str() : p.title.c_str(),
                              p.revision, p.produced.size());
        }
    }
}

} // namespace avgen::ui
