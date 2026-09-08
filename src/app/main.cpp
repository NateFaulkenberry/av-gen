#include "core/log.hpp"
#include "core/time.hpp"

int main() {
    avgen::log::init(avgen::log::Level::Info);
    avgen::log::info("avgen skeleton: logging and clock online");
    avgen::FixedStepClock clock(60.0);
    clock.tick();
    const auto t = clock.tick();
    avgen::log::info("frame {} at {:.4f}s (dt {:.4f})", t.frameIndex, t.renderTime, t.deltaTime);
    return 0;
}
