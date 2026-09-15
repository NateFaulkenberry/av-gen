#pragma once

// The keyboard bindings this application actually has, named once.
//
// Why a table rather than a literal at each menu item: **a menu that advertises a shortcut the
// application does not have is worse than a menu that advertises none.** `Cmd+S` was printed beside
// "Save Project" while nothing was bound to it, because the key handling had no modifier-aware
// branch -- the menu was not lying on purpose, it was lying because the two facts lived in
// different files and only one of them changed. The context menus this pass adds multiply the
// number of places a shortcut is printed by about six, so the two facts are now one fact.
//
// Every string below was read off `Application::handleEditorShortcut`,
// `Application::handleTransportShortcut` or the File-menu branch of `Application::handleInputEvent`
// in the same pass that wrote this file. **Adding a name here does not create a binding.** If you
// want a shortcut that does not exist, bind it first; a constant here is a claim about the
// keyboard, and `tests/unit/test_ui_logic.cpp` pins the ones that are only claims.
//
// The `Cmd` spelling is the macOS one because this is a macOS application (ADR-002); the handlers
// accept Control as well, so the binding works on a keyboard without a Command key, but the label
// names the one a person here is holding.

namespace avgen::ui::shortcut {

// ---- editing (Application::handleEditorShortcut, dispatched through app::EditSystem) -----------
inline constexpr const char* kUndo = "Cmd+Z";
inline constexpr const char* kRedo = "Cmd+Shift+Z";
inline constexpr const char* kCut = "Cmd+X";
inline constexpr const char* kCopy = "Cmd+C";
inline constexpr const char* kPaste = "Cmd+V";
inline constexpr const char* kDuplicate = "Cmd+D";
inline constexpr const char* kSelectAll = "Cmd+A";
inline constexpr const char* kSelectNone = "Cmd+Shift+A";
inline constexpr const char* kGroup = "Cmd+G";
inline constexpr const char* kUngroup = "Cmd+Shift+G";
// Both keys are bound and both reach EditAction::Delete. The label names Delete because that is
// what the key is called on the keyboard this ships for.
inline constexpr const char* kDelete = "Delete";

// ---- the world editor's tools ------------------------------------------------------------------
inline constexpr const char* kFrameSelection = "F";
inline constexpr const char* kSelectTool = "Q";
inline constexpr const char* kPlaceTool = "B";
inline constexpr const char* kMoveTool = "W";
inline constexpr const char* kRotateTool = "E";
inline constexpr const char* kScaleTool = "R";
inline constexpr const char* kToggleLocalSpace = "X";

// ---- the transport (Application::handleTransportShortcut) ---------------------------------------
inline constexpr const char* kPlayPause = "Space";
inline constexpr const char* kGoToStart = "Return";
inline constexpr const char* kGoToEnd = "End";
inline constexpr const char* kStepFrameBack = "Left";
inline constexpr const char* kStepFrameForward = "Right";
inline constexpr const char* kStepBeatBack = "Shift+Left";
inline constexpr const char* kStepBeatForward = "Shift+Right";
inline constexpr const char* kPreviousMarker = "Up";
inline constexpr const char* kNextMarker = "Down";
inline constexpr const char* kToggleLoop = "L";

// ---- files (the File-menu branch of Application::handleInputEvent) -------------------------------
inline constexpr const char* kSaveProject = "Cmd+S";
inline constexpr const char* kOpenAudio = "O";
inline constexpr const char* kOpenScene = "S";
inline constexpr const char* kOpenEnvironment = "E";

// ---- deliberately absent -------------------------------------------------------------------------
//
// There is **no binding** for rename, split, mute, solo, lock, add track, or reveal-in-finder, and
// no constant is provided for them. A menu item for one of those shows no shortcut column at all,
// which is the truthful presentation: the action exists, the key does not.

} // namespace avgen::ui::shortcut
