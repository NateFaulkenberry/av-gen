#pragma once

// The transport bar (ADR-102): the play controls, the time, the loop and the rate.
//
// One widget, drawn in two places -- full across the top of the Sequence panel, where the timeline
// is, and compact in the Control panel where the old ad-hoc Play/Stop buttons were. Two *views*, not
// two implementations: every button here goes through `app::Engine`'s transport calls, which are the
// same calls the keyboard, the OSC map and the AI tools make, so a button cannot come to mean
// something different from the shortcut for it.
//
// It owns nothing but which time format to show. Everything else is read from one
// `TransportSnapshot` taken at the top of the frame, so the bar cannot ask three questions and get
// answers from two different moments.
//
// The glyphs are drawn rather than typed: the UI's font is ImGui's default, which is ASCII only, and
// a transport labelled "Play" "Stop" "|<" is not a transport anybody recognises at a glance.

#include "app/engine.hpp"

namespace avgen::ui {

class TransportBar {
public:
    // `compact` drops the loop controls, the rate and the readouts and keeps the play controls and
    // the time, for a narrow panel.
    void draw(app::Engine& engine, bool compact = false);

    // Which of the four time formats the displays are showing. Shared between both views because it
    // is a preference about reading time, not about a panel.
    enum class TimeFormat { Clock, Timecode, Frames, Bars };
    [[nodiscard]] TimeFormat timeFormat() const { return format_; }
    // The position, in whichever format is showing. Used by the status bar so it cannot disagree
    // with the bar about what second it is.
    [[nodiscard]] std::string format(const app::TransportSnapshot& snapshot, double seconds) const;

private:
    void drawTransportButtons(app::Engine& engine, const app::TransportSnapshot& snapshot);
    void drawTimeDisplay(app::Engine& engine, const app::TransportSnapshot& snapshot);
    void drawLoopControls(app::Engine& engine, const app::TransportSnapshot& snapshot);

    TimeFormat format_ = TimeFormat::Clock;
};

} // namespace avgen::ui
