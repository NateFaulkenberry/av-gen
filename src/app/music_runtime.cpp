#include "app/music_runtime.hpp"

// MusicRuntime is defined in its header. That began as a build constraint -- `avgen_tests` names the
// individual app sources it compiles and this file was not on the list, so an out-of-line definition
// would have left the test binary unable to link an Engine. The file is on the list now, so the
// constraint is gone and the definitions can move down here whenever somebody wants the compile-time
// back; nothing depends on them being inline.
//
// This translation unit still earns its place: it compiles the header on its own at least once,
// which is the same include-hygiene check tests/unit/test_headers.cpp performs for public headers.
