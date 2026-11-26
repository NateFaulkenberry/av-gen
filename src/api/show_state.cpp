#include "api/show_state.hpp"

#include <algorithm>

namespace avgen::api {

const char* showReasonName(ShowReason reason) {
    switch (reason) {
    case ShowReason::Transport: return "transport";
    case ShowReason::LiveInput: return "liveInput";
    case ShowReason::Output: return "output";
    case ShowReason::Share: return "share";
    }
    return "transport";
}

std::string ShowState::reasonsText() const {
    std::string out;
    for (const ShowReason r : reasons) {
        out += (out.empty() ? "" : ", ");
        out += showReasonName(r);
    }
    return out.empty() ? std::string("none") : out;
}

nlohmann::json ShowState::reasonsJson() const {
    nlohmann::json out = nlohmann::json::array();
    for (const ShowReason r : reasons) {
        out.push_back(showReasonName(r));
    }
    return out;
}

ShowState showStateFor(const ShowFacts& f) {
    ShowState s;
    if (f.transportPlaying) {
        s.reasons.push_back(ShowReason::Transport);
    }
    if (f.liveInput) {
        s.reasons.push_back(ShowReason::LiveInput);
    }
    if (f.outputsOpen > 0) {
        s.reasons.push_back(ShowReason::Output);
    }
    if (f.shareOpen) {
        s.reasons.push_back(ShowReason::Share);
    }
    s.active = !s.reasons.empty();
    // Only a file that plays to its end ends the show at a known time: a loop never ends, live input has no end, and
    // under live input the position is frames captured, not a place in a file.
    if (f.transportPlaying && f.hasAudio && !f.looping && !f.liveInput && f.durationSeconds > 0.0) {
        s.remainingSeconds = std::max(f.durationSeconds - f.positionSeconds, 0.0);
    }
    return s;
}

nlohmann::json ShowLockState::toJson() const {
    nlohmann::json out{{"held", held},
                       {"contended", contended},
                       {"path", path.empty() ? nlohmann::json(nullptr) : nlohmann::json(path)},
                       {"attempts", attempts},
                       {"lastAttemptMs", lastAttemptMs},
                       {"maxAttemptMs", maxAttemptMs}};
    if (contended || holderPid != 0 || !holder.empty()) {
        out["holder"] = nlohmann::json{{"pid", holderPid}, {"name", holder}};
    } else {
        out["holder"] = nullptr;
    }
    return out;
}

} // namespace avgen::api
