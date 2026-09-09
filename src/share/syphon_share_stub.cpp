// Syphon exists only on macOS; other platforms report it unavailable.
#include "share/share_backend.hpp"

namespace avgen::share {

bool syphonAvailable() { return false; }
std::string syphonDescribe() { return "not available (macOS only)"; }
std::unique_ptr<ShareBackend> createSyphonBackend() { return nullptr; }

} // namespace avgen::share
