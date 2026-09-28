// Which asset files does a test run actually open? A DYLD interposer for open() and fopen() that logs
// every successful read-open under an assets/ directory or ~/Desktop, or of a media file, to the file
// named by $OPENTRACE_OUT. It is how tools/ci/test-assets.list was measured (docs/qa-pass/ci.md).
//
//   clang -dynamiclib -O2 -o /tmp/opentrace.dylib tools/ci/opentrace.c
//   tools/gpu-lock.sh env DYLD_INSERT_LIBRARIES=/tmp/opentrace.dylib OPENTRACE_OUT=/tmp/gpu-open.txt \
//       ./build/release/tests/avgen_render_tests
//
// `env` must come LAST, right before the test binary: macOS strips DYLD_* variables whenever a
// SIP-protected binary (/bin/bash, /usr/bin/nice, ...) is executed, so `env ... nice ./bin` traces
// nothing. Loaders canonicalise symlinks, so a worktree's opens can appear under the primary checkout.
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int interesting(const char* p) {
    if (!p) return 0;
    if (strstr(p, "/assets/") || strstr(p, "Desktop/") ) return 1;
    const char* e = strrchr(p, '.');
    return e && (!strcmp(e, ".mp3") || !strcmp(e, ".wav") || !strcmp(e, ".glb") || !strcmp(e, ".gltf") || !strcmp(e, ".hdr") || !strcmp(e, ".exr"));
}
static void note(const char* p) {
    if (!interesting(p)) return;
    const char* out = getenv("OPENTRACE_OUT");
    if (!out) return;
    pthread_mutex_lock(&mu);
    int fd = ((int(*)(const char*, int, ...))dlsym(RTLD_NEXT, "open"))(out, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd >= 0) { write(fd, p, strlen(p)); write(fd, "\n", 1); close(fd); }
    pthread_mutex_unlock(&mu);
}
int my_open(const char* p, int flags, ...) {
    mode_t m = 0; if (flags & O_CREAT) { va_list a; va_start(a, flags); m = (mode_t)va_arg(a, int); va_end(a); }
    int r = open(p, flags, m);
    if (r >= 0 && !(flags & (O_WRONLY | O_RDWR))) note(p);
    return r;
}
FILE* my_fopen(const char* p, const char* mode) {
    FILE* f = fopen(p, mode);
    if (f && mode && mode[0] == 'r') note(p);
    return f;
}
typedef struct { const void* n; const void* o; } interpose_t;
__attribute__((used)) static const interpose_t interposers[] __attribute__((section("__DATA,__interpose"))) = {
    { (const void*)my_open, (const void*)open },
    { (const void*)my_fopen, (const void*)fopen },
};
