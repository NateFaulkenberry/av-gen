// Event routing (input regression): the shared SDL queue is drained once per frame by the window
// that routes events to the UI. OutputManager::pumpEvents must not drain it again with no
// handler, or the events that handler has not seen yet are lost and the UI stops responding to
// the mouse while still redrawing.

#include "app/output_manager.hpp"

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {
// A user event stands in for the mouse events the real queue carries.
SDL_Event makeEvent(std::uint32_t type) {
    SDL_Event event{};
    event.type = type;
    event.user.timestamp = SDL_GetTicksNS();
    return event;
}
bool queueHasEvent(std::uint32_t type) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("OutputManager only drains the SDL queue when asked to", "[gpu][input]") {
    REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
    const std::uint32_t probe = SDL_RegisterEvents(1);
    REQUIRE(probe != 0);
    app::OutputManager outputs;

    SECTION("pumpQueue false leaves queued events for the primary window's handler") {
        SDL_Event event = makeEvent(probe);
        REQUIRE(SDL_PushEvent(&event));
        outputs.pumpEvents(/*pumpQueue*/ false);
        CHECK(queueHasEvent(probe)); // still there: the UI's handler will see it
    }

    SECTION("pumpQueue true drains the queue, as the headless paths need") {
        SDL_Event event = makeEvent(probe);
        REQUIRE(SDL_PushEvent(&event));
        outputs.pumpEvents(/*pumpQueue*/ true);
        CHECK_FALSE(queueHasEvent(probe)); // consumed by the pump
    }

    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}
