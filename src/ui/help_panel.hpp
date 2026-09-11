#pragma once

// The Help panel (§2, §3, §28) and the contextual-help entry point (§11).
//
// A dedicated native panel, following the editor's own conventions: it is a row in the same panel
// registry every other panel is in, it docks like the others, and it is drawn from the same
// `drawPanels` loop. Not a browser, not a web view -- §48 is explicit, and so is the rest of this
// application.
//
// The panel knows nothing about documentation content. It draws whatever `help::HelpDatabase` has
// loaded, which is a directory of markdown files. Adding, rewriting or reorganising a topic never
// touches this file -- which is the requirement §6 actually states.

#include "help/database.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {

class HelpPanel {
public:
    HelpPanel();
    ~HelpPanel();
    HelpPanel(const HelpPanel&) = delete;
    HelpPanel& operator=(const HelpPanel&) = delete;

    // Loads content the first time it is called, from the directories `help::HelpDatabase` looks
    // in. Safe to call every frame.
    void ensureLoaded();

    void draw();

    // §11: go straight to a topic. Opens the panel if it is closed. An unknown id is searched for
    // instead of being silently ignored, because the caller had a reason to ask.
    void open(std::string_view documentId);

    [[nodiscard]] const help::HelpDatabase& database() const { return db_; }
    [[nodiscard]] bool loaded() const { return loaded_; }

    // Set by ControlPanel so the panel can open itself when a contextual link is followed. Null is
    // fine; the link then navigates without opening anything.
    bool* openFlag = nullptr;

private:
    void drawToolbar();
    void drawSidebar();
    void drawArticle();
    void drawDocument(const help::HelpDocument& doc);
    void drawSearchResults();
    void drawShortcutTable();

    // AVGEN_HELP_SELFTEST=1: drive the panel from inside its own draw, one step per frame, so the
    // parts a mouse would reach get exercised in a run rather than only in a unit test. See the
    // implementation for what it covers and what it cannot.
    void stepSelfTest();

    void navigate(std::string_view documentId, bool recordHistory = true);
    void goBack();
    void goForward();
    void runSearch();
    void registerPanelFeatures();

    help::HelpDatabase db_;
    bool loaded_ = false;

    std::string current_;                // the topic being shown; empty means the home page
    std::vector<std::string> history_;   // every topic visited, oldest first
    std::size_t historyPos_ = 0;         // where in it we are; history_.size() means the home page
    std::vector<std::string> recent_;    // most recent first, deduplicated, capped

    char search_[192] = {};
    std::vector<help::SearchResult> results_;
    std::string pendingFocus_;           // an anchor to scroll to on the next frame
    bool focusSearchBox_ = false;
    std::string status_;

    int selfTestStep_ = -1; // -1 until ensureLoaded decides whether the self-test is on
    int selfTestFailures_ = 0;
};

// §11's reusable mechanism, reachable from anywhere in the UI without being handed a pointer:
//
//     if (ui::helpLink("Learn more", "rendering/emission-and-bloom")) { ... }
//
// Backed by whichever HelpPanel drew most recently; a no-op when there is none, which is the case
// in a headless run and in the tests.
void helpOpen(std::string_view documentId);

// A small "?" affordance that opens a topic. Place it beside a setting. Returns true if clicked.
bool helpLink(const char* label, std::string_view documentId);

// §12: a tooltip that says what a control does in one line, and offers the topic that says how and
// when. Short by design -- the paragraph belongs in the topic, not in the tooltip.
void helpTooltip(const char* oneLine, std::string_view documentId);

} // namespace avgen::ui
