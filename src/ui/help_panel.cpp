#include "ui/help_panel.hpp"

#include "core/log.hpp"
#include "help/markdown.hpp"
#include "ui/editor_layout.hpp"

#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>

namespace avgen::ui {
namespace {

// The panel that drew most recently, for helpOpen(). A pointer rather than a singleton: the panel
// is owned by ControlPanel like every other panel, and this only exists so that a contextual link
// in a settings widget does not have to be plumbed a reference through four layers of drawing code.
HelpPanel* g_activePanel = nullptr;

constexpr ImVec4 kHeadingColor{0.86f, 0.90f, 1.00f, 1.00f};
constexpr ImVec4 kStrongColor{1.00f, 0.98f, 0.90f, 1.00f};
constexpr ImVec4 kEmphasisColor{0.82f, 0.86f, 0.92f, 1.00f};
constexpr ImVec4 kCodeColor{0.62f, 0.92f, 0.86f, 1.00f};
constexpr ImVec4 kLinkColor{0.48f, 0.74f, 1.00f, 1.00f};
constexpr ImVec4 kMutedColor{0.62f, 0.66f, 0.72f, 1.00f};
constexpr ImVec4 kTipColor{0.45f, 0.85f, 0.60f, 1.00f};
constexpr ImVec4 kWarningColor{1.00f, 0.75f, 0.38f, 1.00f};
constexpr ImVec4 kGapColor{1.00f, 0.66f, 0.45f, 1.00f};

ImVec4 statusColor(help::HelpStatus status) {
    switch (status) {
    case help::HelpStatus::Stable:
        return kMutedColor;
    case help::HelpStatus::Partial:
        return kWarningColor;
    case help::HelpStatus::NotYetDocumented:
        return kGapColor;
    }
    return kMutedColor;
}

ImVec4 spanColor(const help::InlineSpan& span) {
    switch (span.style) {
    case help::InlineSpan::Style::Code:
        return kCodeColor;
    case help::InlineSpan::Style::Strong:
        return kStrongColor;
    case help::InlineSpan::Style::Emphasis:
        return kEmphasisColor;
    case help::InlineSpan::Style::Link:
        return span.internal ? kLinkColor : kMutedColor;
    case help::InlineSpan::Style::Text:
        break;
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

// One word plus whatever whitespace follows it. Splitting here rather than on spaces alone keeps a
// run of spaces with the word before it, so wrapping never leaves a stray space at the start of a
// line.
std::vector<std::string> wordsOf(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = i;
        while (end < text.size() && text[end] != ' ') {
            ++end;
        }
        while (end < text.size() && text[end] == ' ') {
            ++end;
        }
        out.push_back(text.substr(i, end - i));
        i = end;
    }
    return out;
}

std::string trimRight(std::string s) {
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

// Styled runs, laid out word by word with manual wrapping.
//
// ImGui has no rich text and one font, so "bold" here is a brighter colour and "code" a tinted one.
// That is a real limitation and it is the right trade: the alternative is a second font atlas and a
// markup renderer, for a panel whose job is to be readable. Links are the part that has to work,
// and they do -- a Text item still hit-tests, so hover and click are available without a widget.
//
// Returns the id of a link that was clicked this frame, or empty.
std::string drawSpans(const std::vector<help::InlineSpan>& spans, float wrapWidth) {
    std::string clicked;
    const float startX = ImGui::GetCursorPosX();
    float lineWidth = 0.0f;
    bool firstOnLine = true;

    for (const help::InlineSpan& span : spans) {
        const ImVec4 color = spanColor(span);
        for (const std::string& word : wordsOf(span.text)) {
            const std::string drawn = word;
            const float advance = ImGui::CalcTextSize(drawn.c_str()).x;
            if (!firstOnLine && lineWidth + advance > wrapWidth) {
                ImGui::SetCursorPosX(startX);
                lineWidth = 0.0f;
                firstOnLine = true;
            } else if (!firstOnLine) {
                ImGui::SameLine(0.0f, 0.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(drawn.c_str());
            ImGui::PopStyleColor();
            if (span.style == help::InlineSpan::Style::Link && span.internal) {
                // Underline only the word, not the space after it, or a link at the end of a
                // sentence trails a stray rule into the gap.
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                const float underline = max.y - ImGui::GetStyle().FramePadding.y * 0.5f;
                const float inked = ImGui::CalcTextSize(trimRight(drawn).c_str()).x;
                ImGui::GetWindowDrawList()->AddLine({min.x, underline}, {min.x + inked, underline},
                                                    ImGui::GetColorU32(kLinkColor), 1.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", span.target.c_str());
                }
                if (ImGui::IsItemClicked()) {
                    clicked = span.target;
                }
            }
            lineWidth += advance;
            firstOnLine = false;
        }
    }
    if (firstOnLine) {
        ImGui::NewLine(); // nothing was drawn; keep the cursor moving
    }
    return clicked;
}

} // namespace

void helpOpen(std::string_view documentId) {
    if (g_activePanel != nullptr) {
        g_activePanel->open(documentId);
    }
}

bool helpLink(const char* label, std::string_view documentId) {
    ImGui::PushStyleColor(ImGuiCol_Text, kLinkColor);
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Help: %.*s", static_cast<int>(documentId.size()), documentId.data());
    }
    if (pressed) {
        helpOpen(documentId);
    }
    return pressed;
}

void helpTooltip(const char* oneLine, std::string_view documentId) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(oneLine);
    ImGui::PushStyleColor(ImGuiCol_Text, kLinkColor);
    ImGui::Text("Learn more: %.*s", static_cast<int>(documentId.size()), documentId.data());
    ImGui::PopStyleColor();
    ImGui::EndTooltip();
}

HelpPanel::HelpPanel() = default;

HelpPanel::~HelpPanel() {
    if (g_activePanel == this) {
        g_activePanel = nullptr;
    }
}

void HelpPanel::registerPanelFeatures() {
    // §5 says to extend the panel registry rather than duplicate it, so the panel rows are derived
    // from it here. docs/help/features.json supplies only an id and a Help topic for each; the
    // name, the summary and the region come from the registry that already owns them, which means
    // adding a panel there makes it visible to Help, to search and to the AI with no second edit.
    for (const EditorPanel& panel : editorPanels()) {
        help::HelpFeature feature;
        feature.id = "panel." + help::slugify(panel.id);
        feature.name = std::string(panel.label);
        feature.kind = help::FeatureKind::Panel;
        feature.summary = std::string(panel.hint);
        feature.menuPath = "View > " + std::string(panel.label);
        feature.availability = std::string("docked ") + dockRegionName(panel.region);
        db_.addFeature(std::move(feature));
    }
}

void HelpPanel::ensureLoaded() {
    if (loaded_) {
        return;
    }
    loaded_ = true;

    std::filesystem::path exe;
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        exe = std::filesystem::path(base) / "avgen";
    }
    const auto dirs = help::HelpDatabase::searchDirs(exe);
    const help::HelpLoadReport report = db_.loadFirstAvailable(dirs);
    registerPanelFeatures();

    if (report.documents == 0) {
        // Loud, because this is the failure that looks exactly like "nobody wrote any Help". The
        // panel itself lists the directories it tried; this is for whoever reads the log.
        log::warn("help: no content found; looked in {} director(y/ies)", report.searched.size());
        for (const auto& dir : report.searched) {
            log::warn("help:   {}", dir.string());
        }
    } else {
        log::info("help: {} topic(s), {} feature(s), {} shortcut(s) from {}", report.documents,
                  db_.featureTable().features().size(), report.shortcuts, report.loadedFrom.string());
    }
    for (const std::string& error : report.errors) {
        log::warn("help: {}", error);
    }
    if (std::getenv("AVGEN_HELP_SELFTEST") != nullptr && !db_.empty()) {
        selfTestStep_ = 0;
    }
}

void HelpPanel::open(std::string_view documentId) {
    ensureLoaded();
    if (openFlag != nullptr) {
        *openFlag = true;
    }
    if (db_.get(documentId) != nullptr) {
        navigate(documentId);
        return;
    }
    // §34 in miniature: rather than showing a blank page for a topic that does not exist, say so
    // and search for it, so a stale contextual link degrades into a useful result list.
    status_ = "No topic '" + std::string(documentId) + "'. Showing the closest matches.";
    const std::size_t n = std::min(documentId.size(), sizeof(search_) - 1);
    std::copy_n(documentId.data(), n, search_);
    search_[n] = '\0';
    runSearch();
    navigate({});
}

void HelpPanel::navigate(std::string_view documentId, bool recordHistory) {
    current_ = std::string(documentId);
    if (recordHistory) {
        history_.resize(historyPos_);
        history_.push_back(current_);
        historyPos_ = history_.size();
    }
    if (!current_.empty()) {
        std::erase(recent_, current_);
        recent_.insert(recent_.begin(), current_);
        if (recent_.size() > 8) {
            recent_.resize(8);
        }
    }
}

void HelpPanel::goBack() {
    if (historyPos_ <= 1) {
        return;
    }
    --historyPos_;
    current_ = history_[historyPos_ - 1];
}

void HelpPanel::goForward() {
    if (historyPos_ >= history_.size()) {
        return;
    }
    ++historyPos_;
    current_ = history_[historyPos_ - 1];
}

void HelpPanel::runSearch() {
    help::SearchOptions options;
    options.limit = 25;
    results_ = db_.search(search_, options);
}

// AVGEN_HELP_SELFTEST=1 -- a scripted walk through the panel, one step per frame.
//
// This exists because of a specific failure this codebase has had seven times: a system that is
// built, tested and wired into nothing. A unit test over HelpDatabase proves the content loads; it
// does not prove the panel draws it. So the self-test navigates to **every** topic in turn, which
// puts every heading, list, table, code block and callout through the real ImGui renderer inside a
// real frame -- where an unbalanced Begin/End or a table with the wrong column count asserts.
//
// What it covers: loading, the category tree, navigation, history, search, the article renderer for
// every block kind in the shipped content, and the contextual `open()` entry point.
//
// What it cannot cover: the mouse hit-test on an inline link. Clicking one needs a real pointer
// over a real item rectangle. The link *targets* are checked instead -- by the validator, which
// reports any that names no topic -- so what is unproven here is ImGui's hit-testing, not the
// links.
void HelpPanel::stepSelfTest() {
    const auto ids = db_.documents();
    const int total = static_cast<int>(ids.size());
    const int step = selfTestStep_++;

    const auto expect = [this](bool condition, const char* what) {
        if (!condition) {
            ++selfTestFailures_;
            log::error("help selftest: {}", what);
        }
    };

    if (step < total) {
        navigate(ids[static_cast<std::size_t>(step)].id);
        expect(db_.get(current_) != nullptr, "navigated to a topic that is not loaded");
        return;
    }

    switch (step - total) {
    case 0: {
        const char* query = "how do i make something glow";
        std::snprintf(search_, sizeof(search_), "%s", query);
        runSearch();
        expect(!results_.empty(), "search for a phrase in the shipped content returned nothing");
        break;
    }
    case 1:
        expect(!results_.empty(), "search results vanished between frames");
        if (!results_.empty()) {
            navigate(results_.front().documentId);
            expect(db_.get(current_) != nullptr, "top search result is not a loaded topic");
        }
        break;
    case 2:
        search_[0] = '\0';
        results_.clear();
        goBack();
        expect(!history_.empty(), "history was emptied by going back");
        break;
    case 3:
        goForward();
        break;
    case 4:
        open("rendering/emission-and-bloom");
        expect(current_ == "rendering/emission-and-bloom", "open() did not navigate to the topic");
        break;
    case 5:
        open("no/such/topic");
        expect(current_.empty(), "open() of a missing topic did not fall back to the home page");
        expect(!status_.empty(), "open() of a missing topic said nothing about it");
        break;
    case 6:
        status_.clear();
        search_[0] = '\0';
        results_.clear();
        navigate({});
        log::info("help selftest: {} topic(s) drawn, {} search, history and contextual step(s); {}",
                  total, 7, selfTestFailures_ == 0 ? "PASS" : "FAIL");
        break;
    default:
        break;
    }
}

void HelpPanel::draw() {
    g_activePanel = this;
    ensureLoaded();
    if (selfTestStep_ >= 0) {
        stepSelfTest();
    }

    drawToolbar();
    ImGui::Separator();

    if (db_.empty()) {
        ImGui::TextColored(kWarningColor, "No Help content was found.");
        ImGui::TextWrapped("Help reads markdown topics from a directory. Set AVGEN_HELP_DIR, or "
                           "keep docs/help beside the source tree. Directories tried:");
        for (const auto& dir : db_.report().searched) {
            ImGui::BulletText("%s", dir.string().c_str());
        }
        return;
    }

    // A fixed sidebar rather than a splitter: the panel docks into a column that is already narrow,
    // and a splitter inside a splitter is a way to lose the article entirely.
    const float sidebar = std::min(240.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    if (ImGui::BeginChild("help-sidebar", ImVec2(sidebar, 0.0f), ImGuiChildFlags_Borders)) {
        drawSidebar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("help-article", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        drawArticle();
    }
    ImGui::EndChild();
}

void HelpPanel::drawToolbar() {
    ImGui::BeginDisabled(historyPos_ <= 1);
    if (ImGui::Button("<")) {
        goBack();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("back");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(historyPos_ >= history_.size());
    if (ImGui::Button(">")) {
        goForward();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("forward");
    }
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        navigate({});
        status_.clear();
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputTextWithHint("##help-search", "search the documentation",
                                 search_, sizeof(search_))) {
        runSearch();
        status_.clear();
    }
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter) && !results_.empty()) {
        navigate(results_.front().documentId); // Enter opens the best match (§49: usable without a mouse)
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(search_[0] == '\0');
    if (ImGui::Button("Clear")) {
        search_[0] = '\0';
        results_.clear();
    }
    ImGui::EndDisabled();

    if (!status_.empty()) {
        ImGui::TextColored(kWarningColor, "%s", status_.c_str());
    }
}

void HelpPanel::drawSidebar() {
    if (search_[0] != '\0') {
        drawSearchResults();
        return;
    }

    if (!recent_.empty()) {
        if (ImGui::CollapsingHeader("Recently viewed")) {
            for (const std::string& id : recent_) {
                const help::HelpDocument* doc = db_.get(id);
                if (doc == nullptr) {
                    continue;
                }
                if (ImGui::Selectable(doc->title.c_str(), doc->id == current_)) {
                    navigate(doc->id);
                }
            }
        }
        ImGui::Separator();
    }

    for (const help::HelpCategory& category : db_.categories()) {
        // Getting Started open, the rest closed: a tree that is entirely expanded is a list, and a
        // list of forty-six is not navigation.
        const ImGuiTreeNodeFlags flags =
            category.name == "Getting Started" ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        if (!ImGui::CollapsingHeader(category.name.c_str(), flags)) {
            continue;
        }
        ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
        for (const help::HelpDocument* doc : category.documents) {
            ImGui::PushID(doc->id.c_str());
            if (doc->status != help::HelpStatus::Stable) {
                ImGui::PushStyleColor(ImGuiCol_Text, statusColor(doc->status));
            }
            if (ImGui::Selectable(doc->title.c_str(), doc->id == current_)) {
                navigate(doc->id);
            }
            if (doc->status != help::HelpStatus::Stable) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && !doc->summary.empty()) {
                ImGui::SetTooltip("%s", doc->summary.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
    }
}

void HelpPanel::drawSearchResults() {
    ImGui::TextColored(kMutedColor, "%zu result(s)", results_.size());
    ImGui::Separator();
    if (results_.empty()) {
        ImGui::TextWrapped("Nothing matched. Try fewer words, or a term you would expect to see in "
                           "the interface.");
        return;
    }
    for (const help::SearchResult& result : results_) {
        ImGui::PushID(result.documentId.c_str());
        if (result.status != help::HelpStatus::Stable) {
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor(result.status));
        }
        if (ImGui::Selectable(result.title.c_str(), result.documentId == current_)) {
            navigate(result.documentId);
        }
        if (result.status != help::HelpStatus::Stable) {
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kMutedColor);
        ImGui::TextWrapped("%s", result.category.c_str());
        if (!result.summary.empty()) {
            ImGui::TextWrapped("%s", result.summary.c_str());
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !result.excerpt.empty()) {
            ImGui::SetTooltip("%s", result.excerpt.c_str());
        }
        // §7: results say which terms matched, so a result never appears by magic.
        if (!result.matchedTerms.empty()) {
            std::string terms;
            for (const std::string& term : result.matchedTerms) {
                if (!terms.empty()) {
                    terms += ", ";
                }
                terms += term;
            }
            ImGui::TextColored(kCodeColor, "%s", terms.c_str());
        }
        ImGui::Separator();
        ImGui::PopID();
    }
}

void HelpPanel::drawArticle() {
    if (current_.empty()) {
        ImGui::TextColored(kHeadingColor, "AV Gen Help");
        ImGui::TextWrapped("%zu topics across %zu categories. Pick one on the left, or search.",
                           db_.documents().size(), db_.categories().size());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(kHeadingColor, "Start here");
        for (const char* id : {"start/welcome", "start/how-it-works", "start/interface",
                               "modulation/recipes", "troubleshooting/index"}) {
            if (const help::HelpDocument* doc = db_.get(id); doc != nullptr) {
                if (ImGui::Selectable(doc->title.c_str())) {
                    navigate(doc->id);
                }
                ImGui::TextColored(kMutedColor, "  %s", doc->summary.c_str());
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        drawShortcutTable();
        return;
    }

    const help::HelpDocument* doc = db_.get(current_);
    if (doc == nullptr) {
        ImGui::TextColored(kWarningColor, "Topic '%s' is not loaded.", current_.c_str());
        return;
    }
    drawDocument(*doc);
}

void HelpPanel::drawShortcutTable() {
    const auto shortcuts = db_.featureTable().shortcuts();
    if (shortcuts.empty()) {
        return;
    }
    ImGui::TextColored(kHeadingColor, "Keyboard shortcuts");
    if (ImGui::BeginTable("help-shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("does");
        for (const help::HelpShortcut& shortcut : shortcuts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kCodeColor, "%s", shortcut.keys.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", shortcut.description.c_str());
        }
        ImGui::EndTable();
    }
    if (const help::HelpDocument* doc = db_.get("reference/keyboard-shortcuts"); doc != nullptr) {
        if (ImGui::SmallButton("The full shortcut reference")) {
            navigate(doc->id);
        }
    }
}

void HelpPanel::drawDocument(const help::HelpDocument& doc) {
    ImGui::TextColored(kHeadingColor, "%s", doc.title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kMutedColor, "  %s", doc.category.c_str());
    if (doc.status != help::HelpStatus::Stable) {
        ImGui::TextColored(statusColor(doc.status), "%s",
                           doc.status == help::HelpStatus::Partial
                               ? "This topic is partial: it says where it stops."
                               : "This area is not documented yet. Nothing below describes "
                                 "behaviour that has not been checked.");
    }
    if (!doc.summary.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kMutedColor);
        ImGui::TextWrapped("%s", doc.summary.c_str());
        ImGui::PopStyleColor();
    }
    // §33: "copy topic ID", so a topic can be quoted in a bug report or handed to the AI.
    if (ImGui::SmallButton("Copy topic id")) {
        ImGui::SetClipboardText(doc.id.c_str());
        status_ = "Copied '" + doc.id + "'";
    }
    ImGui::Separator();
    ImGui::Spacing();

    std::string followed;
    const float wrap = std::max(ImGui::GetContentRegionAvail().x - 8.0f, 120.0f);
    int blockId = 0;

    for (const help::HelpBlock& block : doc.body) {
        ImGui::PushID(blockId++);
        switch (block.kind) {
        case help::BlockKind::Heading: {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, kHeadingColor);
            ImGui::TextWrapped("%s", block.text.c_str());
            ImGui::PopStyleColor();
            if (block.level <= 2) {
                ImGui::Separator();
            }
            break;
        }
        case help::BlockKind::Paragraph: {
            const std::string clicked = drawSpans(block.spans, wrap);
            if (!clicked.empty()) {
                followed = clicked;
            }
            ImGui::Spacing();
            break;
        }
        case help::BlockKind::Bullet:
        case help::BlockKind::Numbered: {
            const float indent = ImGui::GetStyle().IndentSpacing * (1.0f + static_cast<float>(block.level));
            ImGui::Indent(indent);
            ImGui::Bullet();
            ImGui::SameLine(0.0f, 0.0f);
            const std::string clicked = drawSpans(block.spans, wrap - indent - 20.0f);
            if (!clicked.empty()) {
                followed = clicked;
            }
            ImGui::Unindent(indent);
            break;
        }
        case help::BlockKind::Code: {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
            const float lines = 1.0f + static_cast<float>(std::count(block.text.begin(), block.text.end(), '\n'));
            const ImVec2 size(0.0f, lines * ImGui::GetTextLineHeightWithSpacing() +
                                        ImGui::GetStyle().FramePadding.y * 2.0f);
            // Wide code scrolls inside its own box rather than widening the article.
            if (ImGui::BeginChild("code", size, ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                ImGui::PushStyleColor(ImGuiCol_Text, kCodeColor);
                ImGui::TextUnformatted(block.text.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
            break;
        }
        case help::BlockKind::Table: {
            if (block.rows.empty()) {
                break;
            }
            const int columns = static_cast<int>(block.rows.front().cells.size());
            if (columns <= 0) {
                break;
            }
            if (ImGui::BeginTable("t", columns,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
                for (std::size_t r = 0; r < block.rows.size(); ++r) {
                    ImGui::TableNextRow(r == 0 ? ImGuiTableRowFlags_Headers : 0);
                    // Clamped to the column count the header declared. A row with more cells than
                    // the header is a typo in a markdown table, and ImGui asserts on the extra
                    // TableNextColumn -- a content edit must not be able to abort the editor.
                    const std::size_t cells = std::min(block.rows[r].cells.size(),
                                                       static_cast<std::size_t>(columns));
                    for (std::size_t c = 0; c < cells; ++c) {
                        const std::vector<help::InlineSpan>& cell = block.rows[r].cells[c];
                        ImGui::TableNextColumn();
                        const float cellWrap = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
                        const std::string clicked = drawSpans(cell, cellWrap);
                        if (!clicked.empty()) {
                            followed = clicked;
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            break;
        }
        case help::BlockKind::Callout: {
            const ImVec4 accent = block.callout == help::CalloutKind::Warning ? kWarningColor
                                  : block.callout == help::CalloutKind::Tip   ? kTipColor
                                                                             : kMutedColor;
            const char* label = block.callout == help::CalloutKind::Warning ? "Warning"
                                : block.callout == help::CalloutKind::Tip   ? "Tip"
                                                                            : "Note";
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::Indent();
            const std::string clicked = drawSpans(block.spans, wrap - ImGui::GetStyle().IndentSpacing);
            if (!clicked.empty()) {
                followed = clicked;
            }
            ImGui::Unindent();
            ImGui::Spacing();
            break;
        }
        case help::BlockKind::Image: {
            // §31: images are in the content model and no screenshots ship. A topic that names one
            // says so rather than drawing a placeholder that could be mistaken for the real thing.
            ImGui::TextColored(kMutedColor, "[image: %s]", block.text.c_str());
            break;
        }
        case help::BlockKind::Rule:
            ImGui::Separator();
            break;
        }
        ImGui::PopID();
    }

    // Related topics (§28), in both directions.
    const std::vector<const help::HelpDocument*> related = db_.related(doc.id);
    if (!related.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(kHeadingColor, "Related");
        for (const help::HelpDocument* other : related) {
            ImGui::PushID(other->id.c_str());
            if (ImGui::Selectable(other->title.c_str())) {
                followed = other->id;
            }
            if (ImGui::IsItemHovered() && !other->summary.empty()) {
                ImGui::SetTooltip("%s", other->summary.c_str());
            }
            ImGui::PopID();
        }
    }

    if (!followed.empty()) {
        navigate(followed);
    }
}

} // namespace avgen::ui
