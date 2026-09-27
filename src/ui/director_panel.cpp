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
#include <optional>
#include <string>
#include <utility>

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
    // A task still working (a Modify or Regenerate that has not proposed yet) is the conversation now,
    // not the proposal it superseded.
    if (auto current = plane.currentTask(); current != nullptr && !current->finished()) {
        return current;
    }
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

void DirectorPanel::drawProjectPlans(app::Engine& engine) {
    const auto& plans = engine.directingPlans();
    if (plans.empty()) {
        return;
    }
    heading("Plans in this project");
    for (const directing::Plan& p : plans) {
        ImGui::BulletText("%s  -  revision %d, %zu piece(s) of content", p.title.empty() ? p.id.c_str() : p.title.c_str(),
                          p.revision, p.produced.size());
        if (p.routes.empty()) {
            continue;
        }
        // ADR-924: the routes the plan made are ordinary routes in the Modulation panel's Routes tab,
        // marked "[plan: <key>]", where they are edited; this is why each one is there.
        ImGui::Indent(ImGui::GetTextLineHeight() + 6.0f);
        ImGui::PushID(p.id.c_str());
        if (ImGui::TreeNode("##reactivity", "Reactivity: %s", reactivityHeading(p).c_str())) {
            std::string level;
            for (const ReactivityRow& row : reactivityRows(p, engine.modulator().routes())) {
                if (row.level != level) {
                    level = row.level;
                    ImGui::TextDisabled("%s", level.c_str());
                }
                ImGui::PushID(row.key.c_str());
                const bool asMade = row.state == "as made";
                ImGui::TextWrapped("%s  %s", row.key.c_str(), row.what.c_str());
                if (ImGui::IsItemHovered() && !row.reason.empty()) {
                    ImGui::SetTooltip("%s", row.reason.c_str());
                }
                if (!asMade) {
                    ImGui::SameLine();
                    ImGui::TextColored(kWarn, "(%s)", row.state.c_str());
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        ImGui::Unindent(ImGui::GetTextLineHeight() + 6.0f);
    }
}

void DirectorPanel::drawSetPieces(app::Engine& engine) {
    const auto& plans = engine.directingPlans();
    const bool any = std::any_of(plans.begin(), plans.end(), [](const directing::Plan& p) { return !p.setPieces.empty(); });
    if (!any) {
        setPieceRows_.clear();
        return;
    }
    const std::uint64_t state = edits != nullptr ? edits->history().stateId() : 0;
    if (state != setPieceState_ || plans.size() != setPiecePlans_) {
        setPieceRows_.clear();
        const directing::SceneFacts facts = app::sceneFactsFor(engine);
        for (const directing::Plan& p : plans) {
            for (SetPieceRow& row : setPieceRows(p, facts)) {
                setPieceRows_.push_back(std::move(row));
            }
        }
        setPieceState_ = state;
        setPiecePlans_ = plans.size();
    }
    heading("UFO set pieces");
    ImGui::TextWrapped("Each change here is a new revision of its plan, applied as one undo. The Parameters panel's "
                       "staging > setpiece/<name> sliders fine-tune what the plan made.");
    std::optional<std::pair<std::size_t, SetPieceEdit>> pending;
    for (std::size_t i = 0; i < setPieceRows_.size(); ++i) {
        SetPieceRow& row = setPieceRows_[i];
        ImGui::PushID(static_cast<int>(i));
        const bool errors = !row.lines.empty() && !row.seconds;
        markIcon(row.state != "as made" ? ItemMark::Warning
                 : errors                ? ItemMark::Blocked
                 : !row.lines.empty()    ? ItemMark::Warning
                                         : ItemMark::Ok);
        const bool open = ImGui::TreeNode("##setpiece", "%s  -  %s", row.key.c_str(), row.when.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s\n%s\nplan: %s", row.what.c_str(), row.where.c_str(), row.how.c_str(),
                              row.planTitle.c_str());
        }
        if (open) {
            ImGui::TextWrapped("%s", row.what.c_str());
            ImGui::TextWrapped("where: %s", row.where.c_str());
            ImGui::TextWrapped("%s", row.how.c_str());
            if (row.state != "as made") {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
                ImGui::TextWrapped("%s: %s", row.state.c_str(), row.whyNot.c_str());
                ImGui::PopStyleColor();
            }
            for (const std::string& line : row.lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
                ImGui::TextWrapped("! %s", line.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::BeginDisabled(!row.editable || edits == nullptr);
            SetPieceEdit edit;
            bool changed = false;
            // What kind of event: the template.
            static constexpr const char* kTemplates[] = {"abduction", "survey", "flyby"};
            if (ImGui::BeginCombo("event", row.templateName.c_str())) {
                for (const char* t : kTemplates) {
                    if (ImGui::Selectable(t, row.templateName == t) && row.templateName != t) {
                        edit.templateName = t;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            // When: the moment the time places, and the time.
            if (ImGui::BeginCombo("placed moment", row.moment.c_str())) {
                for (const std::string& m : row.moments) {
                    if (ImGui::Selectable(m.c_str(), row.moment == m) && row.moment != m) {
                        edit.moment = m;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            double seconds = row.seconds.value_or(0.0);
            if (ImGui::InputDouble("at (s)", &seconds, 0.1, 1.0, "%.3f")) {
                row.seconds = seconds;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.seconds = row.seconds.value_or(0.0);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("the plan wrote %s; an edit here writes seconds", row.timeText.c_str());
            }
            // Where.
            if (row.point) {
                float xz[2] = {row.x, row.z};
                if (ImGui::DragFloat2("over x, z (m)", xz, 0.5f)) {
                    row.x = xz[0];
                    row.z = xz[1];
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    edit.point = std::make_pair(row.x, row.z);
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("placed %s: change that in the plan", row.where.c_str());
            }
            // Variation.
            const bool flyby = row.templateName == "flyby";
            if (row.templateName == "abduction") {
                if (row.namedAnimals) {
                    ImGui::TextDisabled("lifts the animals the plan names (%d)", row.animals);
                } else {
                    ImGui::SliderInt("animals lifted", &row.animals, 1, 3);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        edit.slots.emplace_back("animals", static_cast<float>(row.animals));
                        changed = true;
                    }
                }
            }
            ImGui::SliderFloat(flyby ? "flies toward (deg, 0 = north)" : "comes in from (deg, 0 = north)", &row.bearing,
                               0.0f, 360.0f, "%.0f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.slots.emplace_back(flyby ? "pathBearing" : "approachBearing", row.bearing);
                changed = true;
            }
            ImGui::SliderFloat(flyby ? "height above the ground (m)" : "hover height above the ground (m)", &row.height,
                               flyby ? 5.0f : 4.0f, flyby ? 1000.0f : 120.0f, "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.slots.emplace_back(flyby ? "altitude" : "hoverHeight", row.height);
                changed = true;
            }
            if (!flyby) {
                bool coloured = row.beamColor.has_value();
                if (ImGui::Checkbox("coloured beam", &coloured)) {
                    edit.beamColor = coloured ? std::optional<std::array<float, 3>>(std::array<float, 3>{1.0f, 0.3f, 0.2f})
                                              : std::optional<std::array<float, 3>>();
                    changed = true;
                }
                if (row.beamColor) {
                    ImGui::SameLine();
                    float rgb[3] = {(*row.beamColor)[0], (*row.beamColor)[1], (*row.beamColor)[2]};
                    if (ImGui::ColorEdit3("beam colour", rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                        row.beamColor = std::array<float, 3>{rgb[0], rgb[1], rgb[2]};
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        edit.beamColor = row.beamColor;
                        changed = true;
                    }
                }
            }
            float framing = row.framingMetres.value_or(0.0f);
            if (ImGui::SliderFloat("meant to be seen from (m, 0 = not said)", &framing, 0.0f, 1000.0f, "%.0f")) {
                row.framingMetres = framing > 0.0f ? std::optional<float>(framing) : std::nullopt;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit.framingMetres = framing;
                changed = true;
            }
            ImGui::EndDisabled();
            if (!row.editable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !row.whyNot.empty()) {
                ImGui::SetTooltip("%s", row.whyNot.c_str());
            }
            if (changed) {
                pending = std::make_pair(i, std::move(edit));
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    // Applied after the loop: an apply moves the history, which rebuilds the rows being drawn.
    if (pending && edits != nullptr) {
        const SetPieceRow& row = setPieceRows_[pending->first];
        const auto plan = std::find_if(plans.begin(), plans.end(), [&](const directing::Plan& p) { return p.id == row.plan; });
        if (plan == plans.end()) {
            setPieceStatus_ = "the set piece's plan is no longer in the project";
        } else if (auto compiled = compileSetPieceEdit(*plan, row.key, pending->second, app::sceneFactsFor(engine));
                   !compiled) {
            setPieceStatus_ = fmt::format("{}: {}", setPieceEditLabel(row.key, pending->second), compiled.error().message);
        } else {
            const std::string label = setPieceEditLabel(row.key, pending->second);
            setPieceStatus_ = app::applyCompilation(engine, edits->history(), *compiled, label)
                                  ? label + " (revision " + std::to_string(compiled->plan.revision) + ", one undo)"
                                  : label + ": could not be installed";
        }
        setPieceState_ = ~std::uint64_t{0};
    }
    if (!setPieceStatus_.empty()) {
        ImGui::TextDisabled("%s", setPieceStatus_.c_str());
    }
}

void DirectorPanel::draw(app::Engine& engine) {
    if (plane == nullptr) {
        ImGui::TextWrapped("The Director needs the AI assistant, which is not available in this session.");
        drawProjectPlans(engine);
        drawSetPieces(engine);
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
    ps.followUp = !followUp.empty();
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
    // ADR-770: a follow-up, and what to do with it -- revise this plan, or ask again from the top.
    {
        std::string buffer = followUp;
        buffer.resize(std::max<std::size_t>(buffer.size() + 256, 512), '\0');
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##director-followup", "what to change, e.g. \"hold the chase lower\"",
                                     buffer.data(), buffer.size())) {
            followUp = std::string(buffer.c_str());
        }
    }
    if (button("Modify", actions.modify, buttons_.modify)) {
        if (actions.revertPreviewFirst) {
            (void)endPreview(engine);
        }
        if (plane->modifyCurrentTask(followUp) != nullptr) {
            status_ = "modifying: the revision will wait for your approval";
            followUp.clear();
        } else {
            status_ = "could not modify the proposal";
        }
    }
    ImGui::SameLine();
    if (button("Regenerate", actions.regenerate, buttons_.regenerate)) {
        if (actions.revertPreviewFirst) {
            (void)endPreview(engine);
        }
        status_ = plane->regenerateCurrentTask() != nullptr ? "asking again from the original request"
                                                            : "could not regenerate";
    }
    // The next row: what makes or shows something from the proposal, rather than decides it. Its
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
                    if (!s->second.framing.empty()) {
                        // Under the still, wrapped: a narrow dock must not squeeze it into a column.
                        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
                        ImGui::TextWrapped("! %s", s->second.framing.c_str());
                        ImGui::PopStyleColor();
                    }
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
    drawProjectPlans(engine);
    drawSetPieces(engine);
}

} // namespace avgen::ui
