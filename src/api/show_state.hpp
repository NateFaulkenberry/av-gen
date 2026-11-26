#pragma once

// "Is a show running?" as a structured host state, with reasons (ADR-2167, Engine API stage S10; 05-live-safety.md
// G1).
//
// Before this the question was one boolean, `Engine::isPlaying()`, and it was wrong in both directions for a live
// performance: a live-input show never sets the transport playing, and a paused frame on a projector or a Syphon/NDI
// feed is still a picture in front of an audience (the parked clock keeps the world moving, ADR-2154). The host now
// answers with the facts that hold, from a closed set:
//
//   transport   the transport is playing a file
//   liveInput   an audio input drives the visuals (a capture device, live Sonic input)
//   output      an output or projection window is open
//   share       the frame is published over Syphon or NDI
//
// `active` is true when any of them holds. One meaning drives everything a show changes: the live policy (Session),
// the machine GPU lock Studio holds while it lasts, and the GPU jobs that may not start meanwhile (EngineHost).
// Transport alone counts: an owner who mirrors the editor's display onto a projector has no output window open,
// and the safe reading of "the transport plays" is that somebody may be watching.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::api {

enum class ShowReason : std::uint8_t { Transport, LiveInput, Output, Share };
[[nodiscard]] const char* showReasonName(ShowReason reason);

struct ShowState {
    bool active = false;
    std::vector<ShowReason> reasons;
    // When the show will stop, when the host knows: a file playing with no loop and no live input.
    std::optional<double> remainingSeconds;

    // "transport, output": for logs and messages.
    [[nodiscard]] std::string reasonsText() const;
    [[nodiscard]] nlohmann::json reasonsJson() const;
};

// The facts Studio reads each frame; all of them already exist in Application and Engine.
struct ShowFacts {
    bool transportPlaying = false;
    bool liveInput = false;   // Engine::hasLiveInput() or live Sonic input running
    std::size_t outputsOpen = 0; // OutputManager::openCount(): output windows and the projection
    bool shareOpen = false;   // TextureShare::isOpen()
    bool hasAudio = false;
    bool looping = false;
    double durationSeconds = 0.0;
    double positionSeconds = 0.0;
};

// The pure rule, so it can be tested without an application.
[[nodiscard]] ShowState showStateFor(const ShowFacts& facts);

// The machine GPU lock as a show holds it (EngineHost::updateShow).
struct ShowLockState {
    bool showActive = false;
    bool held = false;      // this process holds the lock (or an ancestor does: run under tools/gpu-lock.sh)
    bool contended = false; // the show is running while another process holds the lock
    long holderPid = 0;     // who held it at the last refused attempt
    std::string holder;
    std::string path;
    std::uint64_t attempts = 0;     // tryAcquire calls since the show started
    double lastAttemptMs = 0.0;     // what the last attempt cost on the frame
    double maxAttemptMs = 0.0;      // the dearest attempt since the show started
    [[nodiscard]] nlohmann::json toJson() const;
};

} // namespace avgen::api
