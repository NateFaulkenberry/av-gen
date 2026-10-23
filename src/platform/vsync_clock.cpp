#include "platform/vsync_clock.hpp"

#include "core/log.hpp"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>
#include <mach/mach_time.h>
#endif

namespace avgen::platform {

#if defined(__APPLE__)
// CVDisplayLink is deprecated from macOS 15 in favour of CADisplayLink, which needs an NSView and delivers on the
// run loop the main thread pumps -- exactly the thread whose lateness the pacer is correcting. The link's own thread
// is what this needs, so the deprecated API is used on purpose.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
namespace {

std::uint64_t ticksToNs(std::uint64_t ticks) {
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    return ticks * timebase.numer / timebase.denom;
}

CVReturn onVsync(CVDisplayLinkRef link, const CVTimeStamp* now, const CVTimeStamp* /*output*/, CVOptionFlags,
                 CVOptionFlags*, void* context) {
    auto* clock = static_cast<VsyncClock*>(context);
    std::uint64_t periodNs = 0;
    const double period = CVDisplayLinkGetActualOutputVideoRefreshPeriod(link);
    if (period > 0.0) {
        periodNs = static_cast<std::uint64_t>(period * 1e9);
    } else if (now->videoTimeScale > 0) {
        periodNs = static_cast<std::uint64_t>(static_cast<double>(now->videoRefreshPeriod) /
                                              static_cast<double>(now->videoTimeScale) * 1e9);
    }
    clock->tick(ticksToNs(now->hostTime), periodNs);
    return kCVReturnSuccess;
}

} // namespace

std::unique_ptr<VsyncClock> VsyncClock::forDisplayAt(int x, int y) {
    CGDirectDisplayID display = 0;
    std::uint32_t count = 0;
    if (CGGetDisplaysWithPoint(CGPointMake(x, y), 1, &display, &count) != kCGErrorSuccess || count == 0) {
        display = CGMainDisplayID();
    }
    std::unique_ptr<VsyncClock> clock(new VsyncClock());
    CVDisplayLinkRef link = nullptr;
    if (CVDisplayLinkCreateWithCGDisplay(display, &link) != kCVReturnSuccess || link == nullptr) {
        log::warn("vsync clock: no display link for display {}", display);
        return nullptr;
    }
    CVDisplayLinkSetOutputCallback(link, &onVsync, clock.get());
    if (CVDisplayLinkStart(link) != kCVReturnSuccess) {
        CVDisplayLinkRelease(link);
        log::warn("vsync clock: the display link for display {} did not start", display);
        return nullptr;
    }
    clock->link_ = link;
    clock->displayId_ = display;
    log::info("vsync clock: display {} (ADR-1609)", display);
    return clock;
}

VsyncClock::~VsyncClock() {
    if (link_ != nullptr) {
        auto* link = static_cast<CVDisplayLinkRef>(link_);
        CVDisplayLinkStop(link); // returns once the callback thread has stopped calling into this object
        CVDisplayLinkRelease(link);
    }
}
#pragma clang diagnostic pop
#else
std::unique_ptr<VsyncClock> VsyncClock::forDisplayAt(int, int) { return nullptr; }
VsyncClock::~VsyncClock() = default;
#endif

} // namespace avgen::platform
