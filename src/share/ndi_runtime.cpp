#include "share/ndi_runtime.hpp"

#include "core/log.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <mutex>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace avgen::share::ndi {

namespace {

std::vector<std::string> candidatePaths() {
    std::vector<std::string> paths;
    if (const char* env = std::getenv("AVGEN_NDI_LIB"); env != nullptr && *env != '\0') {
        paths.emplace_back(env);
    }
    for (const char* var : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"}) {
        if (const char* env = std::getenv(var); env != nullptr && *env != '\0') {
            paths.push_back(fmt::format("{}/libndi.dylib", env));
        }
    }
#if defined(__APPLE__)
    // NDI Runtime / NDI Tools installer, the SDK's own tree, and the conventional symlink.
    paths.emplace_back("/usr/local/lib/libndi.dylib");
    paths.emplace_back("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib");
    paths.emplace_back("/Library/Application Support/NDI/lib/libndi.dylib");
    paths.emplace_back("libndi.dylib"); // dyld search path
#elif defined(__linux__)
    paths.emplace_back("/usr/local/lib/libndi.so");
    paths.emplace_back("libndi.so.6");
    paths.emplace_back("libndi.so.5");
    paths.emplace_back("libndi.so");
#endif
    return paths;
}

Runtime loadRuntime() {
    Runtime rt;
#if defined(_WIN32)
    rt.error = "runtime loading is not implemented on Windows";
    return rt;
#else
    void* handle = nullptr;
    for (const auto& path : candidatePaths()) {
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
            rt.path = path;
            break;
        }
    }
    if (handle == nullptr) {
        rt.error = "libndi not found (set AVGEN_NDI_LIB or install the NDI Runtime)";
        return rt;
    }
    auto resolve = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(handle, name));
        if (fn == nullptr && rt.error.empty()) {
            rt.error = fmt::format("{} lacks {}", rt.path, name);
        }
    };
    resolve(rt.initialize, "NDIlib_initialize");
    resolve(rt.destroy, "NDIlib_destroy");
    resolve(rt.version, "NDIlib_version");
    resolve(rt.sendCreate, "NDIlib_send_create");
    resolve(rt.sendDestroy, "NDIlib_send_destroy");
    resolve(rt.sendVideoV2, "NDIlib_send_send_video_v2");
    resolve(rt.sendVideoAsyncV2, "NDIlib_send_send_video_async_v2");
    resolve(rt.sendGetNoConnections, "NDIlib_send_get_no_connections");
    if (!rt.error.empty()) {
        rt = Runtime{};
        rt.error = "incomplete libndi (" + rt.error + ")";
        return rt;
    }
    if (!rt.initialize()) {
        const std::string path = rt.path;
        rt = Runtime{};
        rt.error = fmt::format("NDIlib_initialize failed for {} (unsupported CPU?)", path);
        return rt;
    }
    if (const char* v = rt.version(); v != nullptr) {
        rt.versionText = v;
    }
    log::info("NDI runtime loaded: {} ({})", rt.path, rt.versionText);
    // Deliberately never dlclose'd or NDIlib_destroy'ed: the runtime stays for the process.
    return rt;
#endif
}

} // namespace

const Runtime& Runtime::get() {
    static const Runtime runtime = loadRuntime();
    return runtime;
}

std::string searchDescription() {
    std::string text;
    for (const auto& path : candidatePaths()) {
        if (!text.empty()) {
            text += ", ";
        }
        text += path;
    }
    return text;
}

} // namespace avgen::share::ndi
