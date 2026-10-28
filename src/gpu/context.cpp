#include "gpu/context.hpp"

#include "core/log.hpp"
#include "gpu/blob_cache.hpp"
#include "gpu/surface.hpp"

#include <sys/sysctl.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

namespace avgen::gpu {

namespace {

std::string osBuild() {
    char buf[64] = {};
    std::size_t size = sizeof(buf);
    if (::sysctlbyname("kern.osversion", buf, &size, nullptr, 0) != 0) {
        return "unknown";
    }
    return std::string(buf);
}

const char* backendName(wgpu::BackendType type) {
    switch (type) {
    case wgpu::BackendType::Metal: return "Metal";
    case wgpu::BackendType::Vulkan: return "Vulkan";
    case wgpu::BackendType::D3D12: return "D3D12";
    case wgpu::BackendType::D3D11: return "D3D11";
    case wgpu::BackendType::OpenGL: return "OpenGL";
    case wgpu::BackendType::OpenGLES: return "OpenGLES";
    case wgpu::BackendType::WebGPU: return "WebGPU";
    case wgpu::BackendType::Null: return "Null";
    default: return "Unknown";
    }
}

const char* adapterTypeName(wgpu::AdapterType type) {
    switch (type) {
    case wgpu::AdapterType::DiscreteGPU: return "discrete";
    case wgpu::AdapterType::IntegratedGPU: return "integrated";
    case wgpu::AdapterType::CPU: return "cpu";
    default: return "unknown";
    }
}

const char* errorTypeName(wgpu::ErrorType type) {
    switch (type) {
    case wgpu::ErrorType::Validation: return "validation";
    case wgpu::ErrorType::OutOfMemory: return "out-of-memory";
    case wgpu::ErrorType::Internal: return "internal";
    case wgpu::ErrorType::Unknown: return "unknown";
    default: return "error";
    }
}

} // namespace

std::string Context::toString(wgpu::StringView view) {
    if (view.data == nullptr) {
        return {};
    }
    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }
    return std::string(view.data, view.length);
}

Context::~Context() {
    primarySurface_.reset(); // unconfigures before the device goes away
    if (blobCache_) {
        const BlobCacheStats s = blobCache_->stats();
        log::info("pipeline cache: {} hit(s) ({:.1f} MB), {} miss(es), {} stored ({:.1f} MB), {} discarded as damaged, "
                  "{} store failure(s)",
                  s.loadHits, static_cast<double>(s.bytesLoaded) / 1.0e6, s.loadMisses, s.stores,
                  static_cast<double>(s.bytesStored) / 1.0e6, s.corrupt, s.storeFailures);
    }
}

Result<std::unique_ptr<Context>> Context::create(const ContextDesc& desc) {
    std::unique_ptr<Context> ctx(new Context());

    // ---- instance ----
    const std::array<wgpu::InstanceFeatureName, 1> instanceFeatures = {wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = instanceFeatures.size();
    instanceDesc.requiredFeatures = instanceFeatures.data();
    ctx->instance_ = wgpu::CreateInstance(&instanceDesc);
    if (!ctx->instance_) {
        return fail("wgpuCreateInstance failed (TimedWaitAny unsupported?)");
    }

    // ---- primary surface (optional); created before the adapter so it can be compatible ----
    wgpu::Surface rawSurface;
    if (desc.metalLayer != nullptr) {
        wgpu::SurfaceSourceMetalLayer metalSource{};
        metalSource.layer = desc.metalLayer;
        wgpu::SurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &metalSource;
        surfaceDesc.label = "avgen-surface";
        rawSurface = ctx->instance_.CreateSurface(&surfaceDesc);
        if (!rawSurface) {
            return fail("failed to create a WebGPU surface from the Metal layer");
        }
    }

    // ---- adapter ----
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference =
        desc.preferHighPerformance ? wgpu::PowerPreference::HighPerformance : wgpu::PowerPreference::LowPower;
    adapterOptions.compatibleSurface = rawSurface;
#if defined(__APPLE__)
    adapterOptions.backendType = wgpu::BackendType::Metal;
#endif
    std::string adapterError;
    auto adapterFuture = ctx->instance_.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                ctx->adapter_ = std::move(adapter);
            } else {
                adapterError = toString(message);
            }
        });
    if (ctx->instance_.WaitAny(adapterFuture, 5'000'000'000ull) != wgpu::WaitStatus::Success || !ctx->adapter_) {
        return fail("no suitable GPU adapter: {}", adapterError.empty() ? "timeout" : adapterError);
    }

    wgpu::AdapterInfo info{};
    ctx->adapter_.GetInfo(&info);
    ctx->caps_.adapterName = toString(info.device);
    ctx->caps_.vendor = toString(info.vendor);
    ctx->caps_.backendName = backendName(info.backendType);
    ctx->caps_.adapterType = adapterTypeName(info.adapterType);
    ctx->caps_.timestampQuery = desc.requestTimestamps && ctx->adapter_.HasFeature(wgpu::FeatureName::TimestampQuery);
    // Dawn's Metal backend needs both for a wrapped IOSurface: the import itself and the shared
    // event it hands back from EndAccess (share/syphon_share.mm waits on it GPU-side).
    ctx->caps_.sharedTextureIOSurface = desc.requestSharedTextures &&
        ctx->adapter_.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface) &&
        ctx->adapter_.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent);

    // What the adapter will admit to, for the record rather than for the renderer. Three of these
    // decide whether a whole class of per-pixel optimisation is even expressible here, and the
    // question has been answered from documentation twice and from the driver zero times:
    // `TransientAttachments` is Metal's memoryless storage mode (an attachment that never reaches
    // system memory), `FramebufferFetch` and the `PixelLocalStorage` pair are the two ways a
    // fragment shader can read what is already in the tile. Debug level: it is a condition of a
    // measurement, not news.
    {
        struct NamedFeature {
            wgpu::FeatureName feature;
            const char* name;
        };
        static constexpr NamedFeature kInteresting[] = {
            {wgpu::FeatureName::TransientAttachments, "TransientAttachments"},
            {wgpu::FeatureName::FramebufferFetch, "FramebufferFetch"},
            {wgpu::FeatureName::PixelLocalStorageCoherent, "PixelLocalStorageCoherent"},
            {wgpu::FeatureName::PixelLocalStorageNonCoherent, "PixelLocalStorageNonCoherent"},
            {wgpu::FeatureName::MSAARenderToSingleSampled, "MSAARenderToSingleSampled"},
            {wgpu::FeatureName::DualSourceBlending, "DualSourceBlending"},
            {wgpu::FeatureName::Subgroups, "Subgroups"},
            {wgpu::FeatureName::ShaderF16, "ShaderF16"},
        };
        std::string present;
        std::string absent;
        for (const auto& f : kInteresting) {
            std::string& into = ctx->adapter_.HasFeature(f.feature) ? present : absent;
            into += into.empty() ? f.name : std::string(", ") + f.name;
        }
        log::debug("adapter tile/bandwidth features present: {}", present.empty() ? "(none)" : present);
        log::debug("adapter tile/bandwidth features absent:  {}", absent.empty() ? "(none)" : absent);
    }

    // Request the adapter's full limits so nothing is capped at WebGPU defaults (ADR-001).
    wgpu::Limits adapterLimits{};
    ctx->adapter_.GetLimits(&adapterLimits);
    adapterLimits.nextInChain = nullptr;

    // ---- device ----
    ctx->caps_.rg11b10Renderable = ctx->adapter_.HasFeature(wgpu::FeatureName::RG11B10UfloatRenderable);

    std::vector<wgpu::FeatureName> features;
    if (ctx->caps_.rg11b10Renderable) {
        features.push_back(wgpu::FeatureName::RG11B10UfloatRenderable);
    }
    if (ctx->caps_.timestampQuery) {
        features.push_back(wgpu::FeatureName::TimestampQuery);
    }
    if (ctx->caps_.sharedTextureIOSurface) {
        features.push_back(wgpu::FeatureName::SharedTextureMemoryIOSurface);
        features.push_back(wgpu::FeatureName::SharedFenceMTLSharedEvent);
    }
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.label = desc.label.c_str();
    // AVGEN_DAWN_TOGGLES=a,b,...: Dawn device toggles to enable, for investigation only (e.g. dump_shaders, which
    // logs the MSL each pipeline compiles at debug level). Unset, only the always-disabled toggles below apply.
    std::vector<std::string> toggleNames;
    std::vector<const char*> toggles;
    wgpu::DawnTogglesDescriptor togglesDesc{};
    // ADR-1600: Dawn quantises timestamp queries to 2^16 ns by default (`timestamp_quantization`, a browser
    // anti-fingerprinting measure), which put every pass time on a 65.5 us grid and made any pass under one
    // quantum read 0 or 0.066 ms. A native application has nothing to hide from itself, so it is always off.
    static constexpr const char* kDisabledToggles[] = {"timestamp_quantization"};
    togglesDesc.disabledToggleCount = std::size(kDisabledToggles);
    togglesDesc.disabledToggles = kDisabledToggles;
    deviceDesc.nextInChain = &togglesDesc;
    if (const char* env = std::getenv("AVGEN_DAWN_TOGGLES"); env != nullptr && env[0] != '\0') {
        std::string list = env;
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t comma = std::min(list.find(',', start), list.size());
            if (comma > start) {
                toggleNames.push_back(list.substr(start, comma - start));
            }
            start = comma + 1;
        }
        for (const std::string& t : toggleNames) {
            toggles.push_back(t.c_str());
        }
        togglesDesc.enabledToggleCount = toggles.size();
        togglesDesc.enabledToggles = toggles.data();
        log::info("Dawn toggles enabled from AVGEN_DAWN_TOGGLES: {}", list);
    }
    // ADR-1811: Dawn's persistent blob cache, so a new build does not start its shader warm-up from nothing.
    wgpu::DawnCacheDeviceDescriptor cacheDesc{};
    std::string cacheIsolation;
    if (!desc.pipelineCacheDir.empty() && ctx->caps_.backendName != std::string("Null")) {
        const std::string adapterKey = toString(info.vendor) + " " + toString(info.architecture) + " " +
                                       toString(info.device) + " " + toString(info.description) + " " +
                                       ctx->caps_.backendName;
        const auto dir = desc.pipelineCacheDir /
                         BlobCache::directoryKey(AVGEN_DAWN_COMMIT, ctx->caps_.adapterName, osBuild());
        ctx->blobCache_ = std::make_unique<BlobCache>(dir);
        cacheIsolation = adapterKey;
        cacheDesc.isolationKey = wgpu::StringView(cacheIsolation.data(), cacheIsolation.size());
        // The C++ wrapper's Set*Callback helpers do not compile for these two callbacks in this Dawn pin (no
        // CArgConverter for them), so the C fields are filled through the wrapper's own layout-identical view of
        // itself (Dawn static_asserts the layouts match). The cache travels as userdata; it outlives the device.
        auto& c = const_cast<WGPUDawnCacheDeviceDescriptor&>(static_cast<const WGPUDawnCacheDeviceDescriptor&>(cacheDesc));
        c.dawnLoadCacheDataCallbackInfo.callback = [](size_t keySize, const uint8_t* key, size_t valueSize,
                                                      uint8_t* value, void* self, void*) -> size_t {
            return static_cast<BlobCache*>(self)->load(key, keySize, value, valueSize);
        };
        c.dawnLoadCacheDataCallbackInfo.userdata1 = ctx->blobCache_.get();
        c.dawnStoreCacheDataCallbackInfo.callback = [](size_t keySize, const uint8_t* key, size_t valueSize,
                                                       const uint8_t* value, void* self, void*) {
            static_cast<BlobCache*>(self)->store(key, keySize, value, valueSize);
        };
        c.dawnStoreCacheDataCallbackInfo.userdata1 = ctx->blobCache_.get();
        togglesDesc.nextInChain = &cacheDesc;
        log::info("pipeline cache: {}", dir.string());
    }
    deviceDesc.requiredFeatureCount = features.size();
    deviceDesc.requiredFeatures = features.data();
    deviceDesc.requiredLimits = &adapterLimits;
    // Repeatable callbacks must be capture-less; the context pointer travels as userdata. The
    // Context lives on the heap behind the unique_ptr, so its address is stable.
    Context* raw = ctx.get();
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, Context* self) {
            self->onError(type, message);
        },
        raw);
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message, Context* self) {
            if (reason != wgpu::DeviceLostReason::Destroyed) {
                self->deviceLost_ = true;
                log::error("GPU device lost: {}", toString(message));
            }
        },
        raw);

    std::string deviceError;
    auto deviceFuture = ctx->adapter_.RequestDevice(
        &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                ctx->device_ = std::move(device);
            } else {
                deviceError = toString(message);
            }
        });
    if (ctx->instance_.WaitAny(deviceFuture, 5'000'000'000ull) != wgpu::WaitStatus::Success || !ctx->device_) {
        return fail("failed to create GPU device: {}", deviceError.empty() ? "timeout" : deviceError);
    }
    ctx->queue_ = ctx->device_.GetQueue();
    ctx->device_.GetLimits(&ctx->caps_.limits);
    ctx->caps_.limits.nextInChain = nullptr;

    ctx->device_.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
        const auto text = toString(message);
        switch (type) {
        case wgpu::LoggingType::Error: log::error("[wgpu] {}", text); break;
        case wgpu::LoggingType::Warning: log::warn("[wgpu] {}", text); break;
        default: log::debug("[wgpu] {}", text); break;
        }
    });

    // ---- primary surface format ----
    if (rawSurface) {
        ctx->primarySurface_ = std::unique_ptr<Surface>(new Surface(*ctx, std::move(rawSurface)));
        if (auto r = ctx->primarySurface_->chooseFormat(); !r) {
            return std::unexpected(r.error());
        }
    }

    log::info("GPU: {} ({}, {}) via {}; timestamps={}; sharedTextures={}; maxColorAttachments={}, maxBufferSize={} MB",
              ctx->caps_.adapterName, ctx->caps_.vendor, ctx->caps_.adapterType, ctx->caps_.backendName,
              ctx->caps_.timestampQuery, ctx->caps_.sharedTextureIOSurface, ctx->caps_.limits.maxColorAttachments,
              ctx->caps_.limits.maxBufferSize / (1024 * 1024));
    return ctx;
}

void Context::onError(wgpu::ErrorType type, wgpu::StringView message) {
    ++errorCount_;
    lastError_ = toString(message);
    log::error("[wgpu {}] {}", errorTypeName(type), lastError_);
}

wgpu::TextureFormat Context::surfaceFormat() const {
    return primarySurface_ ? primarySurface_->format() : wgpu::TextureFormat::Undefined;
}

std::uint32_t Context::surfaceWidth() const { return primarySurface_ ? primarySurface_->width() : 0; }

std::uint32_t Context::surfaceHeight() const { return primarySurface_ ? primarySurface_->height() : 0; }

void Context::configureSurface(std::uint32_t width, std::uint32_t height) {
    if (primarySurface_) {
        primarySurface_->configure(width, height);
    }
}

Result<wgpu::TextureView> Context::acquireSurfaceView() {
    if (!primarySurface_) {
        return fail("no configured surface");
    }
    return primarySurface_->acquire();
}

void Context::present() {
    if (primarySurface_) {
        primarySurface_->present();
    }
}

void Context::processEvents() { instance_.ProcessEvents(); }

bool Context::waitFor(wgpu::Future future, std::uint64_t timeoutNs) {
    return instance_.WaitAny(future, timeoutNs) == wgpu::WaitStatus::Success;
}

void Context::waitForQueue() {
    auto future = queue_.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                             [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
    waitFor(future);
}

} // namespace avgen::gpu
